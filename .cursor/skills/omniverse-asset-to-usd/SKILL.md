---
name: omniverse-asset-to-usd
description: Convert supported 3D asset files (.fbx, .obj, .gltf, .glb, .stl, .ply) into Pixar USD artifacts with the usd-convert-asset Python package and CLI. Use when invoking, documenting, troubleshooting, or updating asset-to-USD conversion workflows.
version: "0.1.0"
author: NVIDIA Omniverse
tags:
  - omniverse
  - usd
  - python
  - packaging
  - cli
tools:
  - Read
  - Edit
  - Shell
compatibility: Requires Python >=3.10,<3.13 for package use. Wheel build and smoke-test workflows require this repo's build tooling, staged native runtime libraries, and Windows/Linux platform-specific artifacts.
---

# Omniverse Asset to USD

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

A pip-installable Python package and CLI that converts 3D asset files into Pixar USD formats for use in Omniverse and other USD-based pipelines.

---

## When to Use

Use this skill when a user asks to:

- Convert `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, or `.ply` assets to `.usd`, `.usda`, `.usdc`, or `.usdz`.
- Choose `usd-convert-asset` CLI flags for materials, animation, mesh handling, FBX up-axis conversion, stage up-axis, texture embedding, progress, or debug logging.
- Explain or troubleshoot the converter's CLI, Python package install, native runtime bindings, supported formats, or output behavior.
- Update or verify package behavior for `usd-convert-asset` conversion workflows.

Do not use this skill for Gaussian Splat, URDF, CAD-native, or unrelated USD workflows unless `usd-convert-asset` is the relevant converter.

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
| `.gltf` | Text glTF conversion with optional embedded data export and flavor-sensitive MDL extension support. |
| `.glb` | Binary glTF conversion with optional embedded data export and flavor-sensitive MDL extension support. |
| `.stl` | Assimp-backed triangle mesh import; case-insensitive extension handling is covered by tests. |
| `.ply` | Assimp-backed mesh and point-cloud import, including vertex colors and optional normals. |

Material, texture, and metadata sidecars such as `.mtl`, `.mdl`, image files, `.bin`, `.xml`, and `.mcx` support conversions but are not standalone CLI input assets.

## Output Formats

| Extension | Format |
|-----------|--------|
| `.usd` | Generic USD |
| `.usda` | ASCII USD |
| `.usdc` | Binary USD crate |
| `.usdz` | Zip-packaged USD |

---

## Prerequisites

Use a clean Python environment with Python `>=3.10,<3.13`. The package includes platform-specific native runtime libraries, so install a wheel built for the current OS and architecture.

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

For source-tree work, build or stage the wheel before testing package changes. Canonical platform build commands live in `README.md`, `source/python/README.md`, and `CONTRIBUTING.md`; keep copied commands in this skill consistent with those files.

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

## Instructions

1. Confirm whether the user wants to run a conversion, inspect converter behavior, update code, or get usage guidance.
2. Validate source and destination formats before invoking the tool: input must be `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, or `.ply`; output must be `.usd`, `.usda`, `.usdc`, or `.usdz`.
3. Choose CLI flags from the user's intent for materials, animation, mesh handling, FBX up-axis conversion, stage up-axis, texture embedding, progress, or debug logging.
4. For code changes, identify the change type: CLI behavior, package metadata, native runtime staging, wheel build, smoke tests, or Python package CI.
5. Read the local implementation before editing:
   - CLI behavior: `source/python/usd_convert_asset/cli.py`
   - Package metadata: `source/python/templates/pyproject.toml`
   - Wheel staging: `source/python/premake5.lua` and `source/python/hatch_build.py`
   - Smoke tests: `source/python/tests/test_pip_wheel_smoke.py`
   - Build and install docs: `README.md`, `source/python/README.md`, `CONTRIBUTING.md`
6. Keep CLI examples, README snippets, and smoke tests aligned when flags, entry points, or install flow change.
7. Return a concise summary of command used or changed files, user-facing behavior, and verification performed.

## Output Format

For a completed conversion, return:

```markdown
Converted `<input>` to `<output>` with `usd-convert-asset`.

Command:
`usd-convert-asset -i <input> -o <output> [options]`

Notes:
- <materials/animation/mesh/up-axis/texture choices, if any>
- <warnings or validation results, if any>
```

For usage guidance, troubleshooting, or code changes, return a short explanation with the exact CLI command, package entry point, code location, changed files, or verification result the user needs.

## Verification

Use the smallest verification that covers the changed surface:

| Change | Verification |
|--------|--------------|
| CLI argument parsing or conversion flow | `usd-convert-asset --help` and a small asset conversion |
| `python -m usd_convert_asset` entry point | `python -m usd_convert_asset --help` |
| Wheel metadata or native runtime staging | Build a wheel from the staged `_build/<platform>/<config>/pip/usd_convert_asset` tree, install it into a clean venv, then import `usd_convert_asset` and `asset_converter_native_bindings` |
| Smoke test changes | `python -m unittest discover -s source/python/tests -p "test_pip_wheel_smoke.py" -v` after installing the wheel |

## Limitations

- Supported standalone CLI inputs are `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, and `.ply`. Sidecars such as `.mtl`, `.mdl`, image files, `.bin`, `.xml`, and `.mcx` are supporting files, not standalone conversion inputs.
- This skill does not cover Gaussian Splat, URDF, CAD-native, or unrelated USD workflows unless `usd-convert-asset` is the relevant converter.
- High-fidelity Gaussian Splat conversion from `.ply` is not provided here; use a Gaussian Splat-specific converter (e.g. usd-convert-gsplat) for that workflow.
- The Python package currently exposes version metadata only. Use the CLI or `python -m usd_convert_asset` for conversions.
- Wheel builds and smoke tests depend on staged, platform-specific native runtime libraries. Build tags, artifact locations, and OS/architecture must match.
- `--stage-up-y` and `--stage-up-z` are mutually exclusive.

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

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `usd-convert-asset` command not found | Package is not installed in the active Python environment, or console scripts are not on `PATH`. | Run `python -m pip show usd-convert-asset`, activate the intended environment, or use `python -m usd_convert_asset --help`. |
| `asset_converter_native_bindings` import fails | Wheel was not built or installed with the staged native runtime libraries for the current platform. | Rebuild the staged wheel for the current OS/architecture, install it into a clean environment, then import `asset_converter_native_bindings`. |
| Input file is rejected | File extension is unsupported or is a sidecar rather than a standalone asset. | Use `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, or `.ply` as the CLI input and keep sidecars next to the source asset. |
| Output file is rejected | Output extension is not a supported USD artifact. | Use `.usd`, `.usda`, `.usdc`, or `.usdz`. |
| Converted scene has wrong up-axis or orientation | Source asset up-axis or FBX import settings differ from desired stage orientation. | Use `--fbx-y-up`, `--fbx-z-up`, `--stage-up-y`, or `--stage-up-z` as appropriate. |
| Materials or textures are missing | Material export flags, texture embedding, or sidecar placement do not match the source asset. | Check `--ignore-materials`, `--preview-surface`, `--embed-textures`, and source sidecar paths. |

## References

- `README.md` — repository build, install, usage, support, and release notes.
- `source/python/README.md` — Python package build, install, and test workflow.
- `CONTRIBUTING.md` — repository contribution and verification workflow.
- `source/python/usd_convert_asset/cli.py` — CLI argument parsing and native conversion flow.
- `source/python/tests/test_pip_wheel_smoke.py` — installed-wheel smoke tests.
