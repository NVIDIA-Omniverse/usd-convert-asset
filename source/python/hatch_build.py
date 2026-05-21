# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
from __future__ import annotations

import shutil
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomHook(BuildHookInterface):
    def initialize(self, version: str, build_data: dict) -> None:
        build_data["infer_tag"] = True
        build_data["pure_python"] = False

        self._sync_native_bindings()

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
