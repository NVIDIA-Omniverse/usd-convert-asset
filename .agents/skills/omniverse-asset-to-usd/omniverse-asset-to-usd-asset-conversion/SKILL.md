---
name: omniverse-asset-to-usd-asset-conversion
description: Use when a user asks to convert FBX, OBJ, glTF, GLB, STL, or PLY assets to USD/USDA/USDC/USDZ with usd-convert-asset, choose CLI flags, validate formats, or troubleshoot conversion output.
version: "0.1.0"
license: Apache-2.0 AND CC-BY-4.0
metadata:
  author: "NVIDIA Corporation"
  tags: [omniverse, openusd, converter, cli, assets]
tools: [Read, Edit, Shell]
---
<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Asset Conversion - Omniverse Asset to USD

## Purpose

Run supported 3D asset conversions through `usd-convert-asset`, choose conversion flags from user intent, verify input/output formats, and troubleshoot conversion results.

## When to Use

Use this module when a user asks to convert an asset, choose `usd-convert-asset` CLI flags, troubleshoot conversion output, or inspect supported conversion behavior.

For installation, source builds, wheel packaging, or first-run smoke tests, load `../omniverse-asset-to-usd-getting-started/SKILL.md`.

## Prerequisites

- `usd-convert-asset` must be installed in the active Python environment.
- The source asset must exist and use a supported standalone input extension.
- The output path must use `.usd`, `.usda`, `.usdc`, or `.usdz`.
- Sidecar files such as textures, `.mtl`, `.bin`, `.xml`, or `.mcx` should remain next to the source asset when the format requires them.

## Supported Formats

Supported standalone input assets are `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, and `.ply`. Supported USD output artifacts are `.usd`, `.usda`, `.usdc`, and `.usdz`.

For format-specific notes and sidecar handling, read `references/format-support.md`.

## Instructions

1. Confirm the source asset exists and has a supported input extension.
2. Confirm the requested output path has a supported USD extension.
3. Choose CLI flags from the user's intent for materials, animation, mesh handling, FBX up-axis conversion, stage up-axis, texture embedding, progress, or debug logging.
4. Run `usd-convert-asset` from the Python environment where the package is installed.
5. Return the exact command, output path, and any warnings or validation results.

## Examples

```bash
# Basic conversion
usd-convert-asset -i scene.fbx -o scene.usda

# Run through the Python module entry point
python -m usd_convert_asset -i scene.obj -o scene.usdz

# Print conversion progress
usd-convert-asset -i scene.fbx -o scene.usdc --progress

# Embed textures for portable USDZ output
usd-convert-asset -i scene.fbx -o scene.usdz --embed-textures
```

## CLI Flags

Prefer `usd-convert-asset --help` for current CLI flag details. If the task requires an offline flag matrix or examples for selecting flags, read `references/cli-flags.md`.

## Python API

This package currently exposes version metadata only:

```python
import usd_convert_asset

print(usd_convert_asset.__version__)
print(usd_convert_asset.get_version())
```

Use the CLI or `python -m usd_convert_asset` for conversions. Keep `usd_convert_asset.__init__` lightweight and avoid importing native bindings at package import time.

## Verification

Use the smallest verification that covers the changed surface:

| Change | Verification |
|--------|--------------|
| Conversion command or conversion flow | `usd-convert-asset --help` and a small asset conversion |
| `python -m usd_convert_asset` entry point | `python -m usd_convert_asset --help` |

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

For usage guidance or troubleshooting, return a short explanation with the exact CLI command, package entry point, output path, warnings, or verification result the user needs.

## Limitations

- Supported standalone CLI inputs are `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, and `.ply`.
- Sidecars such as `.mtl`, `.mdl`, image files, `.bin`, `.xml`, and `.mcx` are supporting files, not standalone conversion inputs.
- This skill does not cover Gaussian Splat, URDF, CAD-native, or unrelated USD workflows unless `usd-convert-asset` is the relevant converter.
- High-fidelity Gaussian Splat conversion from `.ply` is not provided here; use a Gaussian Splat-specific converter, such as `usd-convert-gsplat`, for that workflow.
- The Python package currently exposes version metadata only.
- Wheel builds and smoke tests depend on staged, platform-specific native runtime libraries.
- `--stage-up-y` and `--stage-up-z` are mutually exclusive.

## Troubleshooting

For rejected files, wrong orientation, missing materials/textures, or crashes without useful context, read `references/troubleshooting.md`.
