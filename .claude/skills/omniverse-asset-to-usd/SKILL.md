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

Do not use this skill for Gaussian Splat, URDF, CAD-native, or unrelated USD workflows unless `usd-convert-asset` is the relevant converter.

## Core Constraints

1. Prefer the published `usd-convert-asset` Python wheel for end-user conversion tasks.
2. Build from source only when the published package is unavailable for the target environment, or when an NVIDIA maintainer is changing this repository.
3. Validate source and destination formats before invoking the converter. Supported outputs are `.usd`, `.usda`, `.usdc`, and `.usdz`. If the user asks for any unsupported input or output format, state that it is unsupported and offer the supported formats; do not construct the unsupported deliverable with other tools unless the user explicitly asks for a workaround.
4. Keep CLI examples, README snippets, and smoke tests aligned when flags, entry points, install flow, or package metadata change.
5. Treat the package's bundled OpenUSD runtime as process-local. If a workflow uses another OpenUSD or `usd-core` installation, run `usd-convert-asset` in a separate process from a dedicated virtual environment, or invoke only its CLI, to avoid `pxr` and native-library conflicts.
