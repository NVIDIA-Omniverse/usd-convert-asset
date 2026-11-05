<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# CLI Flags Reference

Confirm every selected flag through the installed command's `usd-convert-asset --help` output. Use this reference to check format applicability and selection examples.

Do not pass a flag that the installed command does not list. If a requested flag is unavailable or inapplicable, explain the limitation and offer a supported alternative if one exists. If no supported flag provides the requested capability, state that limitation. Do not substitute another flag or mechanism unless the user explicitly requests a workaround.

Use each flag only with supported input formats. Do not claim effects from an inapplicable flag.

When materials are exported, the converter authors USD Preview Surface materials for all supported USD output formats.

## Common Choices

```bash
# Convert FBX up-axis and force the output stage to Y-up
usd-convert-asset -i scene.fbx -o scene.usda --fbx-y-up --stage-up-y

# Export geometry without materials or animation
usd-convert-asset -i scene.fbx -o scene.usda --ignore-materials --ignore-animation
```

## Flag Matrix

| Flag | Default | Description |
|------|---------|-------------|
| `-i` / `--input` | required | Input asset file path. |
| `-o` / `--output` | required | Output USD file path: `.usd`, `.usda`, `.usdc`, or `.usdz`. |
| `--ignore-materials` | false | Do not export materials. |
| `--ignore-animation` | false | Do not export animation. |
| `--single-props-file` | false | Store all mesh data in the exported scene instead of separate mesh files. This disables USD scene instancing and does not merge meshes. It also places copied texture sidecars under `materials/textures/` instead of `textures/`. |
| `--smooth-normals` | false | Generate smooth normals for meshes without authored normals across all importers. Authored normals take precedence. |
| `--ignore-cameras` | false | Do not export cameras. |
| `--ignore-lights` | false | Do not export lights. |
| `--fbx-y-up` | false | Convert the imported FBX stage to Y-up. Do not combine with `--fbx-z-up`. |
| `--fbx-z-up` | false | Convert the imported FBX stage to Z-up. Do not combine with `--fbx-y-up`. |
| `--keep-all-materials` | false | Keep materials not referenced by meshes. |
| `--merge-all-meshes` | false | Merge eligible meshes into one mesh. Point clouds remain unmerged. |
| `--double-precision-xform` | false | Use double precision for USD transform ops. |
| `--ignore-pivots` | false | Do not import pivots from source assets. |
| `--disable-instancing` | false | Disable scene instancing in USD export. |
| `--export-hidden-props` | false | Export hidden props. |
| `--baking-scales` | false | Bake FBX scales into mesh data. |
| `--ignore-flip-rotations` | false | Filter animation flip rotations. |
| `--ignore-unbound-bones` | false | Filter unbound FBX bones. |
| `--export-embedded-gltf` | false | Export embedded glTF data. |
| `--stage-up-y` | false | Override the output stage up-axis to Y. Do not combine with `--stage-up-z`. |
| `--stage-up-z` | false | Override the output stage up-axis to Z. Do not combine with `--stage-up-y`. |
| `--progress` | false | Print conversion progress to stderr. |
| `--debug` | false | Print detailed CLI/native logs, enable `faulthandler`, and set `OMNI_ASSET_CONVERTER_DEBUG=1`. |

## Texture Sidecar Layout

When `--single-props-file` is set, copied texture sidecars use
`materials/textures/`. Without this flag, copied texture sidecars use
`textures/`. Account for this layout difference when collecting output files.

## Up-Axis Rules

- For one FBX import, pass at most one of `--fbx-y-up` and `--fbx-z-up`.
- For one output stage, pass at most one of `--stage-up-y` and `--stage-up-z`.
- If the user requests both axes for either scope, state that the request is unsupported. Ask the user to choose one axis for each affected scope.
- Do not pass both flags for one scope. Do not select an axis for the user.
- One FBX flag and one stage flag may appear together because they control different scopes.

The FBX flags change FBX import orientation. They do not select the output stage up-axis.

Linux aarch64 imports FBX through Assimp because Autodesk FBX SDK is unavailable
for that target. FBX SDK-specific options apply only on Windows x86_64 and Linux
x86_64.
