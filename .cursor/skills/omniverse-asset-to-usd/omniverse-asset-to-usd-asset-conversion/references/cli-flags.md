<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# CLI Flags Reference

Prefer `usd-convert-asset --help` for current CLI syntax. Use this reference when the user needs offline flag selection guidance or examples.

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
| `--single-props-file` | false | Write repeated mesh prototypes to one props USD file instead of separate mesh USD files. Does not merge meshes. |
| `--smooth-normals` | false | Generate smooth normals for meshes imported through Assimp only. Has no effect on native OBJ, glTF/GLB, USD, or FBX importers. |
| `--ignore-cameras` | false | Do not export cameras. |
| `--ignore-lights` | false | Do not export lights. |
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

Linux aarch64 imports FBX through Assimp because Autodesk FBX SDK is unavailable
for that target. FBX SDK-specific options apply only on Windows x86_64 and Linux
x86_64.
