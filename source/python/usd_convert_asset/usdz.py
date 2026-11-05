# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Helpers for packaging USD layers as USDZ archives.

This module is intentionally free of CLI and converter imports so other
projects can reuse the staging and packaging flow.
"""

from __future__ import annotations

import errno
import json
import os
import shutil
import stat
import tempfile
import uuid
import warnings
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

WINDOWS_MAX_PATH = 260


def _path_length(path: Path) -> int:
    text = str(path)
    return len(text.encode("utf-16-le")) // 2 if os.name == "nt" else len(os.fsencode(text))


def _native_path_limit(directory: Path) -> int:
    if os.name == "nt":
        return WINDOWS_MAX_PATH
    limit = os.pathconf(directory, "PC_PATH_MAX")
    return limit if limit > 0 else 4096


def _check_output_writable(target: Path) -> None:
    if not target.exists():
        return
    if not target.is_file():
        raise IsADirectoryError(f"Output file is a directory: {target}")
    # Replacing a directory entry can succeed on POSIX even when its file is
    # read-only. Preserve the converter's existing write-protection behavior.
    if not target.stat().st_mode & (stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH) or not os.access(target, os.W_OK):
        raise PermissionError(f"Output file is not writable: {target}")


def _validate_output_path(output_path: Path) -> None:
    if os.name != "nt":
        return
    # Reject devices before stat/open: even names with extensions can address
    # printer or serial devices and block indefinitely on some Windows hosts.
    for component in output_path.parts:
        name = component.split(".", 1)[0].split(":", 1)[0].rstrip(" .").upper()
        if name in {"CON", "CONIN$", "CONOUT$", "PRN", "AUX", "NUL"} or (
            len(name) == 4 and name[:3] in {"COM", "LPT"} and name[3] in "123456789¹²³"
        ):
            raise OSError(f"Output path contains a reserved Windows device name: {output_path}")


def preflight_output(output_path: Path) -> None:
    """Check known output constraints without modifying an existing output."""
    _validate_output_path(output_path)
    output_path = output_path.absolute()
    ancestor = output_path.parent
    while not ancestor.exists():
        ancestor = ancestor.parent
    limit = _native_path_limit(ancestor)
    if _path_length(output_path) >= limit:
        raise OSError(f"Output path exceeds the filesystem path limit of {limit - 1}: {output_path}")
    _check_output_writable(output_path)
    if output_path.exists():
        # Ask the OS to check write access (including ACLs), without truncating.
        descriptor = os.open(output_path, os.O_WRONLY)
        os.close(descriptor)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    # Keep the probe short enough that it does not reduce supported output lengths.
    name_length = min(12, limit - _path_length(output_path.parent) - 2)
    for _ in range(16):
        probe = output_path.parent / uuid.uuid4().hex[:name_length]
        try:
            descriptor = os.open(probe, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            continue
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(b"0")
        finally:
            probe.unlink()
        return
    raise FileExistsError(f"Cannot allocate output write probe in {output_path.parent}")


class UsdzError(RuntimeError):
    """Raised when USDZ packaging fails."""


class OutputRestoreError(UsdzError):
    """Existing output could not be fully restored; retain backups and recovery records."""


def is_usdz_path(path: Path) -> bool:
    """Return True when the path uses a USDZ extension."""
    return path.suffix.lower() == ".usdz"


def staging_layer_path(staging_dir: Path, usdz_path: Path) -> Path:
    """Return the layer path to author inside a USDZ staging directory.

    Keep the requested stem when it fits; long filenames use a short working
    name. Packaging independently preserves the requested archive root name.
    """
    return staging_output_path(staging_dir, usdz_path.with_suffix(".usdc"))


def staging_output_path(staging_dir: Path, output_path: Path) -> Path:
    """Keep working filenames within the native writer's temporary-path budget."""
    path = staging_dir / output_path.name
    if _path_length(path) + 32 >= _native_path_limit(staging_dir):
        path = staging_dir / ("layer" + output_path.suffix)
    return path


def _create_staging_directory(output_parent: Path) -> Path:
    def fits(parent: Path) -> bool:
        return _path_length(parent / (".uca-" + "x" * 12)) + 1 + 96 < _native_path_limit(parent)

    fallback = not fits(output_parent)
    if fallback:
        temporary_parent = Path(tempfile.gettempdir()).absolute()
        candidates = (temporary_parent, *temporary_parent.parents)
    else:
        candidates = (output_parent,)
    denied = None
    for parent in candidates:
        if not fits(parent):
            continue
        for _ in range(16):
            staging_dir = parent / f".uca-{uuid.uuid4().hex[:12]}"
            try:
                staging_dir.mkdir(mode=0o700)
            except FileExistsError:
                continue
            except PermissionError as error:
                if not fallback:
                    raise
                denied = error
                break
            return staging_dir
        else:
            raise FileExistsError(f"Cannot allocate staging directory in {parent}")
    if denied is not None:
        raise denied
    raise UsdzError(f"Insufficient path space for staging: {output_parent}")


@contextmanager
def temporary_staging_directory(usdz_path: Path) -> Iterator[Path]:
    """Create a unique staging directory in a writable location.

    The directory and any authored sidecars are removed when the context exits.
    """
    parent = usdz_path.absolute().parent
    parent.mkdir(parents=True, exist_ok=True)
    # Prefer the output directory, then TEMP or its shorter writable ancestors.
    # Check permissions by creating the directory: os.access can miss Windows ACLs.
    staging_dir = _create_staging_directory(parent)
    preserve = False
    try:
        yield staging_dir
    except OutputRestoreError:
        preserve = True
        raise
    finally:
        if not preserve and staging_dir.exists():
            try:
                shutil.rmtree(staging_dir)
            except OSError as error:
                warnings.warn(
                    f"Failed to remove USDZ staging directory '{staging_dir}': {error}",
                    RuntimeWarning,
                    stacklevel=2,
                )


def package_usdz(layer_path: Path, usdz_path: Path, *, root_layer_name: str | None = None) -> None:
    """Package a USD layer and its dependencies into a USDZ archive.

    The packaged root layer uses ``root_layer_name`` when supplied, otherwise
    ``usdz_path.stem`` plus ``.usdc``. This keeps short working filenames out
    of the archive's public root-layer name.

    Raises:
        UsdzError: If packaging fails or the output archive is missing.
    """
    from pxr import Sdf, UsdUtils

    _check_output_writable(usdz_path)
    package_root_layer_name = root_layer_name or f"{usdz_path.stem}.usdc"
    preserved_message = (
        f"The existing USDZ was preserved and was not updated: {usdz_path}." if usdz_path.is_file() else ""
    )
    with temporary_staging_directory(usdz_path) as temporary_dir:
        temporary_usdz_path = staging_output_path(temporary_dir, usdz_path)
        try:
            created = bool(
                UsdUtils.CreateNewUsdzPackage(
                    Sdf.AssetPath(layer_path.as_posix()),
                    temporary_usdz_path.as_posix(),
                    package_root_layer_name,
                )
            )
        except Exception as error:
            raise UsdzError(f"{_packaging_error_message(usdz_path, preserved_message)} Cause: {error}") from error

        if not created or not temporary_usdz_path.is_file():
            raise UsdzError(_packaging_error_message(usdz_path, preserved_message))

        try:
            _check_output_writable(usdz_path)
            if usdz_path.exists():
                temporary_usdz_path.chmod(stat.S_IMODE(usdz_path.stat().st_mode))
            _replace_staged_file(temporary_usdz_path, usdz_path)
        except OSError as error:
            raise UsdzError(
                f"USDZ package was created but could not replace the output: {usdz_path}.{preserved_message}"
            ) from error


def _packaging_error_message(usdz_path: Path, preserved_message: str) -> str:
    return (
        f"failed to create USDZ package: {usdz_path}. "
        "Check the OpenUSD diagnostics for file access, path limits, or missing dependencies. "
        f"{preserved_message}"
    )


def _replace_staged_file(source: Path, target: Path) -> None:
    """Replace atomically, copying beside the destination when volumes differ."""
    try:
        source.replace(target)
        return
    except OSError as error:
        if error.errno != errno.EXDEV:
            raise
    # Python's Windows file APIs support extended paths; native USD writers do not.
    # Prefix the sibling path so this does not consume the destination's budget.
    parent = target.absolute().parent
    if os.name == "nt":
        text = str(parent)
        parent = Path("\\\\?\\UNC\\" + text[2:] if text.startswith("\\\\") else "\\\\?\\" + text)
    name_length = 32 if os.name == "nt" else min(12, _native_path_limit(parent) - _path_length(parent) - 2)
    for _ in range(16):
        temporary = parent / uuid.uuid4().hex[:name_length]
        try:
            stream = temporary.open("xb")
        except FileExistsError:
            continue
        try:
            with stream, source.open("rb") as incoming:
                shutil.copyfileobj(incoming, stream)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
        break
    else:
        raise FileExistsError(f"Cannot allocate output replacement in {parent}")
    try:
        shutil.copystat(source, temporary)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def commit_staged_files(
    staging_dir: Path, output_path: Path, *, archive: bool = False, root_path: Path | None = None
) -> None:
    """Install a completed conversion, restoring existing files if installation fails.

    Callers serialize this operation with their cancellation/success decision.
    The root layer is installed last so its supporting files are already present.
    Retained backups have companion JSON records with backup names and absolute
    destination paths for manual recovery if restoration fails.
    """
    root = root_path or staging_dir / output_path.name
    files = [root] if archive else [p for p in staging_dir.rglob("*") if p.is_file() and p != root] + [root]
    if not root.is_file():
        raise FileNotFoundError(f"Conversion did not create {root}")
    # Validate every destination before replacing any sidecars or the root layer.
    for source in files:
        target = output_path if source == root else output_path.parent / source.relative_to(staging_dir)
        if os.name == "nt" and _path_length(target.absolute()) >= WINDOWS_MAX_PATH:
            raise UsdzError(
                f"Output file exceeds the Windows path limit of {WINDOWS_MAX_PATH - 1} characters: {target}"
            )
        _check_output_writable(target)
    backups = staging_dir / ".previous"
    installed = []
    saved = []
    try:
        for source in files:
            relative = source.relative_to(staging_dir)
            target = output_path if source == root else output_path.parent / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            _check_output_writable(target)
            if target.exists():
                if not target.is_file():
                    raise IsADirectoryError(f"Output file is a directory: {target}")
                backup = backups / uuid.uuid4().hex
                backup.parent.mkdir(parents=True, exist_ok=True)
                # Persist each destination before overwriting it. Separate records
                # keep earlier mappings intact if a later metadata write fails.
                backup.with_suffix(".json").write_text(
                    json.dumps({"backup": backup.name, "target": str(target.absolute())}, indent=2) + "\n",
                    encoding="utf-8",
                )
                shutil.copy2(target, backup)
                saved.append((backup, target))
                source.chmod(stat.S_IMODE(target.stat().st_mode))
            _replace_staged_file(source, target)
            installed.append(target)
    except OSError:
        restoration_errors = []
        for target in reversed(installed):
            try:
                target.unlink()
            except OSError as error:
                restoration_errors.append(error)
        for backup, target in reversed(saved):
            if target not in installed:
                continue
            try:
                _replace_staged_file(backup, target)
            except OSError as error:
                restoration_errors.append(error)
        if restoration_errors:
            raise OutputRestoreError(
                f"Could not restore all previous output files. Backups and destination metadata retained in {backups}"
            ) from restoration_errors[0]
        raise
