<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Installation Troubleshooting

Use this reference for `usd-convert-asset` installation, source-build, wheel-installation, and first-run smoke-test failures.

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `usd-convert-asset` command not found | Package is not installed in the active Python environment, or console scripts are not on `PATH`. | Run `python -m pip show usd-convert-asset`, activate the intended environment, or use `python -m usd_convert_asset --help`. |
| `asset_converter_native_bindings` import fails | Wheel was not built or installed with the staged native runtime libraries for the current platform. | Rebuild the staged wheel for the current OS/architecture, install it into a clean environment, then import `asset_converter_native_bindings`. |
| FBX SDK dependency is missing during source build | Autodesk FBX SDK has not been staged locally, or the local override points at the wrong layout. | Download Autodesk FBX SDK `2020.3.7`, stage it into the expected `include` plus `lib/.../release` layout, update `deps/target-deps.packman.xml` locally, and rebuild. |
| Built wheel installs but conversion fails immediately | Runtime libraries, plugin resources, or build platform tags may not match the active environment. | Rebuild from the staged `_build/<platform>/<config>/pip/usd_convert_asset` tree for the current OS/architecture. |

If installation succeeds but converted USD output is wrong, switch to `../../omniverse-asset-to-usd-asset-conversion/SKILL.md`.
