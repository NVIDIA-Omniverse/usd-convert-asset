---
name: omniverse-asset-to-usd
description: Use when a user asks to install, build, troubleshoot, or run usd-convert-asset to convert supported 3D assets to Pixar USD, or when an NVIDIA maintainer asks to modify, package, or document the repository.
version: "0.1.0"
license: Apache-2.0 AND CC-BY-4.0
metadata:
  author: "NVIDIA Corporation"
  tags: [omniverse, openusd, converter, cli, assets]
tools: [Read, Edit, Shell]
---

# Omniverse Asset to USD

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

`usd-convert-asset` is a pip-installable Python package and CLI for converting supported 3D assets into Pixar USD formats for Omniverse and other USD-based pipelines.

## Tested Wheel Version

This skill was tested against `usd-convert-asset==1.1.0` using the Python 3.12 wheel.

## Route First

Load the narrowest module that matches the user's task:

- For installation, package verification, source builds, wheel packaging, or first-run smoke tests, read `omniverse-asset-to-usd-getting-started/SKILL.md`.
- For asset conversion, supported formats, CLI flag selection, conversion verification, or conversion troubleshooting, read `omniverse-asset-to-usd-asset-conversion/SKILL.md`.
- For NVIDIA maintainer code changes, packaging changes, docs updates, or smoke tests, read `omniverse-asset-to-usd-contributing/SKILL.md`. This repository does not accept external contributions or pull requests.

## Use This Skill For

- Install, verify, build, or troubleshoot the `usd-convert-asset` package.
- Convert `.fbx`, `.obj`, `.gltf`, `.glb`, `.stl`, or `.ply` assets to `.usd`, `.usda`, `.usdc`, or `.usdz`.
- Choose CLI flags for materials, animation, mesh handling, FBX up-axis conversion, stage up-axis, progress, or debug logging.
- Help NVIDIA maintainers update code, docs, tests, packaging, or release behavior for asset-to-USD conversion workflows.

Do not use this skill for Gaussian Splat, URDF, CAD-native, or unrelated USD workflows unless `usd-convert-asset` is the relevant converter. For Gaussian Splat `.ply` conversion, redirect the user to `usd-convert-gsplat` and name that tool explicitly.

## Core Constraints

1. If the user requests Gaussian Splat `.ply` conversion, redirect the user to `usd-convert-gsplat` and name that tool explicitly. Do not create a substitute converter unless the user explicitly requests a workaround.
2. Prefer the published `usd-convert-asset` Python wheel for end-user conversion tasks.
3. Build from source only when the published package is unavailable for the target environment, or when an NVIDIA maintainer is changing this repository.
4. Validate source and destination formats before invoking the converter. Supported outputs are `.usd`, `.usda`, `.usdc`, and `.usdz`. If the user asks for any unsupported input or output format, state that it is unsupported and offer the supported formats; do not construct the unsupported deliverable with other tools unless the user explicitly asks for a workaround.
5. Confirm every conversion flag through the installed command's `--help` output. Follow the format restrictions in the conversion module. If a requested flag is unavailable or inapplicable, explain the limitation and offer a supported alternative if one exists. If no supported flag provides the requested capability, state that limitation. Do not substitute another flag or mechanism unless the user explicitly requests a workaround.
6. For one FBX import, pass at most one of `--fbx-y-up` and `--fbx-z-up`. For one output stage, pass at most one of `--stage-up-y` and `--stage-up-z`. If the user requests both axes for either scope, state that the request is unsupported and ask the user to choose one axis for each affected scope. Do not pass both flags or select an axis for the user. One FBX flag and one stage flag may appear together because they control different scopes.
7. Report conversion success only after a zero exit code and a new or updated non-empty output file.
8. Claim a requested effect only after output inspection confirms it. Otherwise, report the flag without claiming its expected effect.
9. Keep CLI examples, README snippets, and smoke tests aligned when flags, entry points, install flow, or package metadata change.
10. Treat the package's bundled OpenUSD runtime as process-local. If a workflow uses another OpenUSD or `usd-core` installation, run `usd-convert-asset` in a separate process from a dedicated virtual environment, or invoke only its CLI, to avoid `pxr` and native-library conflicts.
