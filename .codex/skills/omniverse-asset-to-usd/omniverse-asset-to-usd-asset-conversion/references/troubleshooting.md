<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

# Conversion Troubleshooting

Use this reference for conversion failures and output-quality issues after `usd-convert-asset` is installed.

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Input file is rejected | File extension is unsupported or is a sidecar rather than a standalone asset. | Use `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, or `.ply` as the CLI input and keep sidecars next to the source asset. |
| Output file is rejected | Output extension is not a supported USD artifact. | Use `.usd`, `.usda`, or `.usdc`. |
| Converted scene has wrong up-axis or orientation | Source asset up-axis or FBX import settings differ from desired stage orientation. | Use `--fbx-y-up`, `--fbx-z-up`, `--stage-up-y`, or `--stage-up-z` as appropriate. |
| Materials or textures are missing | Material export flags or sidecar placement do not match the source asset. | Check `--ignore-materials`, `--preview-surface`, and source sidecar paths. |
| CLI crashes without useful context | Native runtime, plugin, or input asset issue needs more logs. | Re-run with `--debug` and, when useful, `--progress`. |

If the package cannot be imported or the command is missing, switch to `../../omniverse-asset-to-usd-getting-started/SKILL.md`.
