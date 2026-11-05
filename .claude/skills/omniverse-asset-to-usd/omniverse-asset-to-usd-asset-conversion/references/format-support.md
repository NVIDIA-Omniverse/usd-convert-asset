<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Format Support Reference

Use this reference when validating source and destination extensions or explaining format-specific conversion behavior.

## Standalone Input Assets

| Format | Notes |
|--------|-------|
| `.fbx` | Imported with Autodesk FBX SDK on Windows x86_64 and Linux x86_64, and with Assimp on Linux aarch64. FBX SDK builds support optional Y-up/Z-up conversion, pivot handling, scale baking, rotation filtering, bone filtering, and preservation of valid static NURBS curves as `UsdGeomNurbsCurves`. Animated control-point deformation is not supported. |
| `.obj` | Mesh conversion with material, normal, mesh merge, and stage up-axis controls. |
| `.gltf` | Text glTF conversion with optional embedded data export. |
| `.glb` | Binary glTF conversion with optional embedded data export. |
| `.stl` | Assimp-backed triangle mesh import; case-insensitive extension handling is covered by tests. |
| `.ply` | Assimp-backed mesh and point-cloud import, including vertex colors and optional normals. |

## Output USD Artifacts

| Extension | Format |
|-----------|--------|
| `.usd` | Binary USD crate by default; honors OpenUSD's `USD_DEFAULT_FILE_FORMAT` override (for example, `usda` selects ASCII) |
| `.usda` | ASCII USD |
| `.usdc` | Binary USD crate |
| `.usdz` | USDZ package. The CLI authors a temporary `.usdc` layer in an isolated staging directory, then packages it and its dependencies with OpenUSD. |

If a requested input or output format is unsupported, refuse the conversion and offer the supported formats listed in the relevant section above. Do not construct an unsupported format with other tools unless the user asks for a workaround.

Material, texture, and metadata sidecars such as `.mtl`, `.mdl`, image files, `.bin`, `.xml`, and `.mcx` support conversions but are not standalone CLI input assets.
