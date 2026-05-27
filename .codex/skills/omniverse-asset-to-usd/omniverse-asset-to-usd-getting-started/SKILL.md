---
name: omniverse-asset-to-usd-getting-started
description: Use when a user asks to install, verify, build from source, troubleshoot installation, or smoke-test the usd-convert-asset Python package.
version: "0.1.0"
license: Apache-2.0 AND CC-BY-4.0
metadata:
  author: "NVIDIA Corporation"
  tags: [omniverse, openusd, python, install, packaging]
tools: [Read, Shell]
---

# Getting Started - Omniverse Asset to USD

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

## Purpose

Install and verify `usd-convert-asset` in the intended Python environment, using the published wheel by default and the source-build fallback only when necessary.

## When to Use

Use this module when the task asks to install, verify, build, or troubleshoot the `usd-convert-asset` package before running conversions. Stop applying once the package is installed in the intended Python environment and the smoke tests pass.

For actual conversions, CLI flag selection, format validation, or conversion troubleshooting, load `../omniverse-asset-to-usd-asset-conversion/SKILL.md`.

## Hard Rules

1. Prefer installing the published `usd-convert-asset` package into a clean Python environment.
2. Build from source only when the published package is unavailable for the target environment, or when the user needs a local source checkout build.
3. For source builds on Windows x86_64 or Linux x86_64, developers must download Autodesk FBX SDK `2020.3.7`, stage it locally into the layout expected by `premake5.lua`, and point `deps/target-deps.packman.xml` at that staged folder.
4. Do not accept Autodesk FBX SDK license terms, run a silent Autodesk installer, or commit a user-specific FBX SDK path on the user's behalf.
5. When installing a locally built wheel, type the exact wheel filename from `dist`; do not rely on shell wildcards.

## Prerequisites

- Python: `>=3.10,<3.13`
- Distribution name: `usd-convert-asset`
- Import package: `usd_convert_asset`
- Console script: `usd-convert-asset`
- Module entry point: `python -m usd_convert_asset`
- Native runtime module: `asset_converter_native_bindings`

## Install From PyPI

Use a clean virtual environment when possible. Confirm the active interpreter before installing or validating the package.

Linux:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install usd-convert-asset
```

Windows PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install usd-convert-asset
```

## Smoke Test

Verify the active Python environment and both CLI entry points:

```bash
python -m pip show usd-convert-asset
usd-convert-asset --help
python -m usd_convert_asset --help
python -c "import usd_convert_asset; print(usd_convert_asset.get_version())"
```

If `usd-convert-asset` is not on `PATH`, use `python -m usd_convert_asset --help` to confirm whether the package is installed in the active interpreter.

## Source Build Fallback

For source builds, FBX SDK staging, platform-specific build commands, or local wheel installation, read `references/source-build.md`.

## Next

After the package is installed and the smoke test passes, move to `../omniverse-asset-to-usd-asset-conversion/SKILL.md` for supported formats, CLI usage, flag selection, conversion verification, and conversion troubleshooting.

## Limitations

- This module stops at installation, source-build fallback, and smoke-test verification.
- It does not choose conversion flags or troubleshoot converted USD output; use the asset-conversion module for that work.
- Autodesk FBX SDK setup requires the user to download the SDK and accept Autodesk license terms themselves.

## Troubleshooting

For install failures, missing entry points, native import errors, or FBX SDK setup problems, read `references/troubleshooting.md`.
