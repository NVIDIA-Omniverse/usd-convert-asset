# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
from __future__ import annotations

import platform
import re
import shutil
import sys
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface

GLIBC_VERSION_RE = re.compile(r"^(?P<major>\d+)[._](?P<minor>\d+)$")


class CustomHook(BuildHookInterface):
    def initialize(self, version: str, build_data: dict) -> None:
        build_data["pure_python"] = False
        # Only Linux wheels carry a manylinux tag, so only require/validate the glibc baseline
        # there. On Windows repo.tokens.abi is intentionally empty, which would otherwise fail
        # the _manylinux_platform() check even though the value is unused for win_* tags.
        manylinux_platform = self._manylinux_platform() if sys.platform == "linux" else ""
        build_data["tag"] = self._wheel_tag(manylinux_platform)

        self._sync_native_bindings()

    def _manylinux_platform(self) -> str:
        value = self.config.get("manylinux-platform")
        if not isinstance(value, str):
            raise RuntimeError("manylinux-platform must be configured from repo.tokens.abi in pyproject.toml")

        match = GLIBC_VERSION_RE.fullmatch(value)
        if match is None:
            raise RuntimeError(f"manylinux-platform must be a glibc version like '2.35'; got {value!r}")

        return f"{match.group('major')}_{match.group('minor')}"

    @staticmethod
    def _wheel_tag(manylinux_platform: str) -> str:
        # The native bindings are a pybind11 CPython extension built against a
        # specific interpreter (not the limited/stable ABI), so the wheel is
        # only valid for the exact CPython that built it, e.g. cp312-cp312.
        interpreter = f"cp{sys.version_info.major}{sys.version_info.minor}"

        # Derive the platform tag from the host OS and architecture the build runs
        # on. Normalize known machine aliases to canonical wheel arch names and
        # reject anything unknown so we never emit a non-importable platform tag.
        machine = platform.machine().lower()
        if sys.platform == "win32":
            arch = {"amd64": "amd64", "x86_64": "amd64", "arm64": "arm64", "aarch64": "arm64"}.get(machine)
            if arch is None:
                raise RuntimeError(f"Unsupported Windows architecture for wheel tagging: {machine}")
            plat = f"win_{arch}"
        elif sys.platform == "linux":
            arch = {"x86_64": "x86_64", "amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}.get(machine)
            if arch is None:
                raise RuntimeError(f"Unsupported Linux architecture for wheel tagging: {machine}")
            plat = f"manylinux_{manylinux_platform}_{arch}"
        else:
            raise RuntimeError(f"Unsupported platform for wheel tagging: {sys.platform}/{machine}")

        return f"{interpreter}-{interpreter}-{plat}"

    def _sync_native_bindings(self) -> None:
        package_root = Path(self.root)
        native_source_dir = package_root.parent.parent / "python" / "asset_converter_native_bindings"
        native_target_dir = package_root / "asset_converter_native_bindings"

        if not native_source_dir.is_dir():
            # uv may run this hook from an unpacked sdist. The original build
            # output is not there, so use the native bindings already bundled
            # in the sdist.
            if native_target_dir.is_dir():
                return
            raise FileNotFoundError(f"Native bindings source directory is missing: source:{native_source_dir}")

        self._copy_tree(native_source_dir, native_target_dir)

    def _copy_tree(self, source_dir: Path, target_dir: Path) -> None:
        for source_path in source_dir.rglob("*"):
            target_path = target_dir / source_path.relative_to(source_dir)

            if source_path.is_dir():
                target_path.mkdir(parents=True, exist_ok=True)
                continue

            target_path.parent.mkdir(parents=True, exist_ok=True)
            try:
                if target_path.exists() or target_path.is_symlink():
                    target_path.unlink()
                shutil.copy2(source_path, target_path, follow_symlinks=False)
            except PermissionError as exc:
                raise PermissionError(f"Could not update native binding file: {target_path}") from exc
