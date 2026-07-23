<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Format Support Reference

Use this reference when validating source and destination extensions or explaining format-specific conversion behavior.

## Standalone Input Assets

| Format | Notes |
|--------|-------|
| `.fbx` | FBX import with optional Y-up/Z-up conversion, pivot handling, scale baking, rotation filtering, and bone filtering. |
| `.obj` | Mesh conversion with material, normal, mesh merge, and stage up-axis controls. |
| `.gltf` | Text glTF conversion with optional embedded data export and flavor-sensitive MDL extension support. |
| `.glb` | Binary glTF conversion with optional embedded data export and flavor-sensitive MDL extension support. |
| `.stl` | Assimp-backed triangle mesh import; case-insensitive extension handling is covered by tests. |
| `.ply` | Assimp-backed mesh and point-cloud import, including vertex colors and optional normals. |

## Output USD Artifacts

| Extension | Format |
|-----------|--------|
| `.usd` | Binary USD crate by default; honors OpenUSD's `USD_DEFAULT_FILE_FORMAT` override (for example, `usda` selects ASCII) |
| `.usda` | ASCII USD |
| `.usdc` | Binary USD crate |

Material, texture, and metadata sidecars such as `.mtl`, `.mdl`, image files, `.bin`, `.xml`, and `.mcx` support conversions but are not standalone CLI input assets.
