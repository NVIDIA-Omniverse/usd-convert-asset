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

## Tested Wheel Version

This skill was tested against `usd-convert-asset==1.1.0` using the Python 3.12 wheel.

## When to Use

Use this module when a user asks to convert an asset, choose `usd-convert-asset` CLI flags, troubleshoot conversion output, or inspect supported conversion behavior.

For installation, source builds, wheel packaging, or first-run smoke tests, load `../omniverse-asset-to-usd-getting-started/SKILL.md`.

## Prerequisites

- `usd-convert-asset` must be installed in the active Python environment.
- `usd-convert-asset` uses its bundled OpenUSD runtime. If the surrounding workflow uses another OpenUSD or `usd-core` runtime, run conversion in a separate process from a dedicated virtual environment, or invoke only the converter CLI, to avoid `pxr` conflicts.
- The source asset must exist and use a supported standalone input extension.
- The output path must use `.usd`, `.usda`, `.usdc`, or `.usdz`.
- Sidecar files such as textures, `.mtl`, `.bin`, `.xml`, or `.mcx` should remain next to the source asset when the format requires them.

## Supported Formats

Supported standalone input assets are `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, and `.ply`. Supported USD output artifacts are `.usd`, `.usda`, `.usdc`, and `.usdz`.

For format-specific notes and sidecar handling, read `references/format-support.md`.

## Instructions

1. If the user requests Gaussian Splat `.ply` conversion, redirect the user to `usd-convert-gsplat` and name that tool explicitly. Do not create a substitute converter unless the user explicitly requests a workaround.
2. Confirm the source asset exists and has a supported input extension.
3. Confirm the requested output path has a supported USD extension (`.usd`, `.usda`, `.usdc`, or `.usdz`). If the user asks for any unsupported input or output format, state that it is unsupported and offer the supported formats. Do not construct the unsupported deliverable using other tools unless the user explicitly asks for a workaround.
4. Confirm each requested flag through the installed command's `usd-convert-asset --help` output. Check `references/cli-flags.md` for format-specific restrictions. Do not pass an unavailable or inapplicable flag. If a requested flag is unavailable or inapplicable, explain the limitation and offer a supported alternative if one exists. If no supported flag provides the requested capability, state that limitation. Do not substitute another flag or mechanism unless the user explicitly requests a workaround.
5. For one FBX import, pass at most one of `--fbx-y-up` and `--fbx-z-up`. For one output stage, pass at most one of `--stage-up-y` and `--stage-up-z`. If the user requests both axes for either scope, state that the request is unsupported and ask the user to choose one axis for each affected scope. Do not pass both flags or select an axis for the user. One FBX flag and one stage flag may appear together because they control different scopes.
6. Choose only the flags required by the user's intent. Do not add optional flags such as `--ignore-materials` unless the user requests that behavior.
7. Run `usd-convert-asset` from the Python environment where the package is installed.
8. Check the process exit code. Treat every nonzero exit code as a failed conversion.
9. After a zero exit code, confirm the output path is a non-empty regular file produced or updated by this command. A pre-existing file alone is not success evidence.
10. Report success only after the exit-code and output checks pass. Return the exact command, output path, and verification result.
11. Describe a requested effect only when output inspection confirms it. Otherwise, state which flag was passed and that its effect was not verified.

## Examples

```bash
# Basic conversion — no optional flags unless the user asked for them
usd-convert-asset -i scene.fbx -o scene.usda

# Run through the Python module entry point
python -m usd_convert_asset -i scene.obj -o scene.usdc

# Print conversion progress
usd-convert-asset -i scene.fbx -o scene.usdc --progress

# Package as USDZ
usd-convert-asset -i scene.fbx -o scene.usdz
```

## CLI Flags

Use `usd-convert-asset --help` to confirm current CLI syntax before passing a flag. Read `references/cli-flags.md` for format applicability and selection examples.

The `--single-props-file` flag places copied texture sidecars under
`materials/textures/` instead of `textures/`. Include this layout change when
you explain or verify the flag's effects.

When materials are exported, the converter authors USD Preview Surface materials for all supported USD output formats.

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

Use the success format only after the command returns zero and produces or updates a non-empty output file:

```markdown
Converted `<input>` to `<output>` with `usd-convert-asset`.

Command:
`usd-convert-asset -i <input> -o <output> [options]`

Verification:
- Exit code: 0
- Output: `<output>` was produced or updated and is non-empty
- Effects: <confirmed output effects, or "not inspected">
```

If the command returns nonzero, or the output check fails, use this failure format:

```markdown
Conversion failed for `<input>`.

Command:
`usd-convert-asset -i <input> -o <output> [options]`

Exit code: <exit code>
Error:
<relevant stderr or output-check failure>
```

Never use the success statement for a failed or unverified conversion. Do not claim an effect that output inspection did not confirm.

For usage guidance, return a short explanation with the exact command, output path, warning, or verification result.

## Limitations

- Supported standalone CLI inputs are `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, and `.ply`.
- Supported outputs are `.usd`, `.usda`, `.usdc`, and `.usdz`. Any other format (for example non-USD deliverables such as images) is unsupported; refuse it and offer a supported format rather than packaging or rendering with other tools unless the user explicitly asks for a workaround.
- When materials are exported, the converter authors USD Preview Surface materials for all supported USD output formats.
- For `.usdz`, the CLI packages a temporary `.usdc` layer with OpenUSD.
- Sidecars such as `.mtl`, `.mdl`, image files, `.bin`, `.xml`, and `.mcx` are supporting files, not standalone conversion inputs.
- Linux aarch64 converts FBX through Assimp rather than Autodesk FBX SDK.
- FBX NURBS curve preservation requires the Autodesk FBX SDK path used on Windows x86_64 and Linux x86_64. Valid static curves are preserved as `UsdGeomNurbsCurves`; invalid curves are skipped with a warning, animated control-point deformation is not covered, and Linux aarch64 does not provide this support.
- This skill does not cover Gaussian Splat, URDF, CAD-native, or unrelated USD workflows unless `usd-convert-asset` is the relevant converter.
- High-fidelity Gaussian Splat conversion from `.ply` is not provided here. Redirect the user to `usd-convert-gsplat` and name that tool explicitly.
- The Python package currently exposes version metadata only.
- Wheel builds and smoke tests depend on staged, platform-specific native runtime libraries.

## Troubleshooting

For rejected files, wrong orientation, missing materials/textures, or crashes without useful context, read `references/troubleshooting.md`.
