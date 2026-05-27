<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Source Build Reference

Use this reference only when the published `usd-convert-asset` package is unavailable for the target environment, or when the user needs a local checkout build.

## Build Preparation

1. Inspect the local OS, architecture, and build configuration.
2. If the target architecture is ARM64/aarch64, skip FBX SDK setup because Autodesk FBX SDK is not supported for that target; FBX conversion uses Assimp instead.
3. For Windows x86_64 only, determine whether the machine uses Visual Studio 2019 or Visual Studio 2022, and check `repo.toml` for `repo_build.msbuild.vs_version`.
4. For Windows x86_64 and Linux x86_64 source builds, find the Autodesk FBX SDK download instructions from `https://aps.autodesk.com/developer/overview/fbx-sdk` and locate FBX SDK `2020.3.7`.
5. Ask the user to download/run the Autodesk installer and review and accept Autodesk license terms themselves.
6. Stage the installed SDK into the layout that `premake5.lua` expects:
   - Windows: `include` and `lib/x64/release`
   - Linux x86_64: `include` and `lib/release`
7. Update `deps/target-deps.packman.xml` locally so the `fbxsdk` dependency points at the staged folder. Do not point at the raw Autodesk installer root unless it already has the normalized layout.

Example local FBX SDK override:

```xml
<dependency name="fbxsdk" linkPath="../_build/target-deps/fbxsdk">
    <source path="C:/path/to/staged/fbxsdk-packman-layout"/>
</dependency>
```

Use forward slashes in the XML path. Treat this as a machine-local override.

## Build Commands

Windows x86_64:

```powershell
.\repo.bat build --config release
.\repo.bat uv -- build _build\windows-x86_64\release\pip\usd_convert_asset -o dist
```

Linux x86_64:

```bash
./repo.sh build --config release
./repo.sh uv -- build _build/linux-x86_64/release/pip/usd_convert_asset -o dist
```

Linux aarch64:

```bash
./repo.sh build --config release
./repo.sh uv -- build _build/linux-aarch64/release/pip/usd_convert_asset -o dist
```

Canonical platform build commands live in `README.md`, `source/python/README.md`, and `CONTRIBUTING.md`; keep copied commands in this reference consistent with those files.

## Install A Built Wheel

Windows:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
Get-ChildItem dist\*.whl
python -m pip install dist\REPLACE_WITH_EXACT_WHEEL_FILENAME.whl
usd-convert-asset --help
python -c "import usd_convert_asset, asset_converter_native_bindings; print(usd_convert_asset.get_version())"
```

Linux:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
ls dist/*.whl
python -m pip install dist/REPLACE_WITH_EXACT_WHEEL_FILENAME.whl
usd-convert-asset --help
python -c "import usd_convert_asset, asset_converter_native_bindings; print(usd_convert_asset.get_version())"
```
