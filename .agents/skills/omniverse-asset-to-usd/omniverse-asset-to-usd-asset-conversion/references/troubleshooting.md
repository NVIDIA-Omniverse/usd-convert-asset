<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Conversion Troubleshooting

Use this reference for conversion failures and output-quality issues after `usd-convert-asset` is installed.

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Input file is rejected | File extension is unsupported or is a sidecar rather than a standalone asset. | Use `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, or `.ply` as the CLI input and keep sidecars next to the source asset. |
| FBX output differs on Linux aarch64 | This target imports FBX through Assimp rather than Autodesk FBX SDK. | Use a Windows x86_64 or Linux x86_64 wheel/build when FBX SDK-specific behavior is required. |
| Some FBX NURBS curves are missing | Only valid static curves supported by the FBX SDK path can be represented safely as `UsdGeomNurbsCurves`; malformed curves are skipped, and animated control-point deformation is unsupported. | Review the conversion log for skipped curve names/counts and validate the source curve topology. |
| Output file is rejected | Output extension is not a supported USD artifact. | Use `.usd`, `.usda`, `.usdc`, or `.usdz`. For any unsupported output format, offer the supported formats instead; do not package or rewrite the result with other tools unless the user asks for a workaround. |
| Converted scene has wrong up-axis or orientation | Source asset up-axis or FBX import settings differ from desired stage orientation. | Use `--fbx-y-up`, `--fbx-z-up`, `--stage-up-y`, or `--stage-up-z` as appropriate. |
| Materials or textures are missing | Material export flags or sidecar placement do not match the source asset. | Check `--ignore-materials` and source sidecar paths. USDZ output enables USD Preview Surface automatically. |
| CLI crashes without useful context | Native runtime, plugin, or input asset issue needs more logs. | Re-run with `--debug` and, when useful, `--progress`. |

If the package cannot be imported or the command is missing, switch to `../../omniverse-asset-to-usd-getting-started/SKILL.md`.
