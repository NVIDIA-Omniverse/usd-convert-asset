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
| `-o` / `--output` | required | Output USD file path: `.usd`, `.usda`, or `.usdc`. |
| `--ignore-materials` | false | Do not export materials. |
| `--ignore-animation` | false | Do not export animation. |
| `--single-mesh` | false | Export a single mesh USD file. |
| `--smooth-normals` | false | Generate smooth normals for meshes. |
| `--ignore-cameras` | false | Do not export cameras. |
| `--ignore-lights` | false | Do not export lights. |
| `--preview-surface` | false | Export USD Preview Surface materials. Flavor-sensitive `AUTOREMOVE` option. |
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
