---
name: usd-convert-asset-python-package
description: Work on the usd-convert-asset pip-installable Python package, CLI, wheel staging, native binding packaging, and smoke tests. Use when changing source/python/usd_convert_asset, source/python packaging files, pip wheels, the usd-convert-asset CLI, asset_converter_native_bindings packaging, or Python package CI.
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# usd-convert-asset

A pip-installable Python package and CLI that converts 3D asset files into Pixar USD formats for use in Omniverse and other USD-based pipelines.

---

## What This Converter Does

Runs the native Omniverse asset converter through Python bindings. The CLI reads supported 3D asset inputs, writes USD outputs, and exposes common conversion controls for materials, animation, mesh handling, FBX up-axis conversion, stage up-axis overrides, texture embedding, and progress/debug logging.

Package identity:

- Distribution name: `usd-convert-asset`
- Import package: `usd_convert_asset`
- Console script: `usd-convert-asset`
- Module entry point: `python -m usd_convert_asset`
- Native runtime module: `asset_converter_native_bindings`

---

## Input Formats

| Format | Notes |
|--------|-------|
| `.fbx` | FBX import with optional Y-up/Z-up conversion, pivot handling, scale baking, rotation filtering, and bone filtering. |
| `.obj` | Mesh conversion with material, normal, mesh merge, and stage up-axis controls. |
| `.gltf` / `.glb` | glTF conversion with optional embedded data export and flavor-sensitive MDL extension support. |
| Other native converter inputs | Support depends on the staged native `asset_converter_native_bindings` build and its linked importer libraries. |

## Output Formats

| Extension | Format |
|-----------|--------|
| `.usd` | Generic USD |
| `.usda` | ASCII USD |
| `.usdc` | Binary USD crate |
| `.usdz` | Zip-packaged USD |

---

## Installation

Install into a clean Python environment. The package supports Python `>=3.10,<3.13` and includes platform-specific native runtime libraries.

```bash
python -m pip install usd-convert-asset
```

```powershell
python -m pip install usd-convert-asset
```

Verify the install:

```bash
python -m pip show usd-convert-asset
usd-convert-asset --help
python -m usd_convert_asset --help
```

---

## CLI Usage

```bash
# Basic conversion
usd-convert-asset -i scene.fbx -o scene.usda

# Run through the Python module entry point
python -m usd_convert_asset -i scene.obj -o scene.usdz

# Print conversion progress
usd-convert-asset -i scene.fbx -o scene.usdc --progress

# Embed textures for portable USDZ output
usd-convert-asset -i scene.fbx -o scene.usdz --embed-textures

# Convert FBX up-axis and force the output stage to Y-up
usd-convert-asset -i scene.fbx -o scene.usda --fbx-y-up --stage-up-y

# Export geometry without materials or animation
usd-convert-asset -i scene.fbx -o scene.usda --ignore-materials --ignore-animation
```

All CLI flags:

| Flag | Default | Description |
|------|---------|-------------|
| `-i` / `--input` | required | Input asset file path. |
| `-o` / `--output` | required | Output USD file path: `.usd`, `.usda`, `.usdc`, or `.usdz`. |
| `--ignore-materials` | false | Do not export materials. |
| `--ignore-animation` | false | Do not export animation. |
| `--single-mesh` | false | Export a single mesh USD file. |
| `--smooth-normals` | false | Generate smooth normals for meshes. |
| `--ignore-cameras` | false | Do not export cameras. |
| `--ignore-lights` | false | Do not export lights. |
| `--preview-surface` | false | Export USD Preview Surface materials. Flavor-sensitive `AUTOREMOVE` option. |
| `--embed-textures` | false | Embed textures in exported assets. |
| `--fbx-y-up` | false | Convert imported FBX stage to Y-up. |
| `--fbx-z-up` | false | Convert imported FBX stage to Z-up. |
| `--keep-all-materials` | false | Keep materials not referenced by meshes. |
| `--merge-all-meshes` | false | Merge meshes into one mesh. |
| `--double-precision-xform` | false | Use double precision for USD transform ops. |
| `--ignore-pivots` | false | Do not import pivots from source assets. |
| `--disable-instancing` | false | Disable scene instancing in USD export. |
| `--export-hidden-props` | false | Export hidden props. |
| `--baking-scales` | false | Bake FBX scales into mesh data. |
| `--ignore-flip-rotations` | false | Filter animation flip rotations. |
| `--ignore-unbound-bones` | false | Filter unbound FBX bones. |
| `--export-embedded-gltf` | false | Export embedded glTF data. |
| `--import-gltf-mdl-extension` | false | Import glTF `NV_materials_mdl` extension data. Flavor-sensitive `AUTOREMOVE` option. |
| `--stage-up-y` | false | Override output stage up-axis to Y. Mutually exclusive with `--stage-up-z`. |
| `--stage-up-z` | false | Override output stage up-axis to Z. Mutually exclusive with `--stage-up-y`. |
| `--progress` | false | Print conversion progress to stderr. |
| `--debug` | false | Print detailed CLI/native logs, enable `faulthandler`, and set `OMNI_ASSET_CONVERTER_DEBUG=1`. |

---

## Python API

This package currently exposes version metadata only:

```python
import usd_convert_asset

print(usd_convert_asset.__version__)
print(usd_convert_asset.get_version())
```

Use the CLI for conversions. Keep `usd_convert_asset.__init__` lightweight and avoid importing native bindings at package import time.

---

## Working on This Package

1. Identify the change type: CLI behavior, package metadata, native runtime staging, wheel build, smoke tests, or Python package CI.
2. Read the local implementation before editing:
   - CLI behavior: `source/python/usd_convert_asset/cli.py`
   - Package metadata: `source/python/templates/pyproject.toml`
   - Wheel staging: `source/python/premake5.lua` and `source/python/hatch_build.py`
   - Smoke tests: `source/python/tests/test_pip_wheel_smoke.py`
   - Build and install docs: `README.md`, `source/python/README.md`, `CONTRIBUTING.md`
3. Keep CLI examples, README snippets, and smoke tests aligned when flags, entry points, or install flow change.

## Verification

Use the smallest verification that covers the changed surface:

| Change | Verification |
|--------|--------------|
| CLI argument parsing or conversion flow | `usd-convert-asset --help` and a small asset conversion |
| `python -m usd_convert_asset` entry point | `python -m usd_convert_asset --help` |
| Wheel metadata or native runtime staging | Build a wheel from the staged `_build/<platform>/<config>/pip/usd_convert_asset` tree, install it into a clean venv, then import `usd_convert_asset` and `asset_converter_native_bindings` |
| Smoke test changes | `python -m unittest discover -s source/python/tests -p "test_pip_wheel_smoke.py" -v` after installing the wheel |

Canonical platform build commands live in `README.md`, `source/python/README.md`, and `CONTRIBUTING.md`; keep copied commands in this skill consistent with those files.

## Codebase Map

| File | Role |
|------|------|
| `source/python/usd_convert_asset/cli.py` | `usd-convert-asset` CLI, argument parsing, native conversion flow, progress/debug logging |
| `source/python/usd_convert_asset/__main__.py` | `python -m usd_convert_asset` entry point |
| `source/python/usd_convert_asset/__init__.py` | Version exports; keep native imports out of this file |
| `source/python/templates/pyproject.toml` | Wheel metadata, package entry point, build target configuration |
| `source/python/premake5.lua` | Stages Python package files, native bindings, USD runtime libraries, plugin resources |
| `source/python/hatch_build.py` | Custom hatch build behavior for staged wheel contents |
| `source/python/tests/test_pip_wheel_smoke.py` | Installed-wheel smoke tests for metadata, imports, CLI help, and small conversion |
| `source/python/README.md` | Package-specific build, install, and test workflow |

## Common Pitfalls

- `usd-convert-asset` is the distribution and CLI name; `usd_convert_asset` is the Python import name.
- The wheel contains platform-specific native runtime libraries, so build tags and artifacts matter.
- Preserve the mutually exclusive `--stage-up-y` and `--stage-up-z` behavior.
