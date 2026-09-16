# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Helpers for packaging USD layers as USDZ archives.

This module is intentionally free of CLI and converter imports so other
projects can reuse the staging and packaging flow.
"""

from __future__ import annotations

import shutil
import tempfile
import warnings
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path


class UsdzError(RuntimeError):
    """Raised when USDZ packaging fails."""


def is_usdz_path(path: Path) -> bool:
    """Return True when the path uses a USDZ extension."""
    return path.suffix.lower() == ".usdz"


def staging_layer_path(staging_dir: Path, usdz_path: Path) -> Path:
    """Return the layer path to author inside a USDZ staging directory.

    The layer uses the USDZ stem so converters that derive the default prim
    from the output file stem keep the user-facing name.
    """
    return staging_dir / f"{usdz_path.stem}.usdc"


@contextmanager
def temporary_staging_directory(usdz_path: Path) -> Iterator[Path]:
    """Create a unique staging directory next to the USDZ output path.

    The directory and any authored sidecars are removed when the context exits.
    """
    parent = usdz_path.parent
    if not parent.parts:
        parent = Path.cwd()
    parent.mkdir(parents=True, exist_ok=True)

    staging_dir = Path(
        tempfile.mkdtemp(
            prefix=f".{usdz_path.name}.staging-",
            dir=parent,
        )
    )
    try:
        yield staging_dir
    finally:
        if staging_dir.exists():
            try:
                shutil.rmtree(staging_dir)
            except OSError as error:
                warnings.warn(
                    f"Failed to remove USDZ staging directory '{staging_dir}': {error}",
                    RuntimeWarning,
                    stacklevel=2,
                )


def package_usdz(layer_path: Path, usdz_path: Path) -> None:
    """Package a USD layer and its dependencies into a USDZ archive.

    The packaged root layer is named after ``usdz_path.stem`` (for example
    ``model.usdc`` inside ``model.usdz``), not after any temporary staging
    filename.

    Raises:
        UsdzError: If packaging fails or the output archive is missing.
    """
    from pxr import Sdf, UsdUtils

    package_root_layer_name = f"{usdz_path.stem}.usdc"
    preserved_message = (
        f"The existing USDZ was preserved and was not updated: {usdz_path}." if usdz_path.is_file() else ""
    )
    with tempfile.TemporaryDirectory(
        prefix=f".{usdz_path.name}.packaging-",
        dir=usdz_path.parent,
    ) as temporary_dir:
        temporary_usdz_path = Path(temporary_dir) / usdz_path.name
        try:
            created = bool(
                UsdUtils.CreateNewUsdzPackage(
                    Sdf.AssetPath(layer_path.as_posix()),
                    temporary_usdz_path.as_posix(),
                    package_root_layer_name,
                )
            )
        except Exception as error:
            raise UsdzError(_packaging_error_message(usdz_path, preserved_message)) from error

        if not created or not temporary_usdz_path.is_file():
            raise UsdzError(_packaging_error_message(usdz_path, preserved_message))

        try:
            temporary_usdz_path.replace(usdz_path)
        except OSError as error:
            raise UsdzError(
                f"USDZ package was created but could not replace the output: {usdz_path}.{preserved_message}"
            ) from error


def _packaging_error_message(usdz_path: Path, preserved_message: str) -> str:
    return (
        f"failed to create USDZ package: {usdz_path}. "
        "Unresolved material or texture dependencies often cause this. "
        f"Check texture sidecars or try --ignore-materials.{preserved_message}"
    )
