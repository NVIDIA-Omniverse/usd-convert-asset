---
name: omniverse-asset-to-usd-contributing
description: Use when an NVIDIA maintainer asks to modify, test, document, package, or review code changes for the usd-convert-asset repository, CLI, Python package, wheel staging, or smoke tests. External contributions and pull requests are not accepted.
version: "0.1.0"
license: Apache-2.0 AND CC-BY-4.0
metadata:
  author: "NVIDIA Corporation"
  tags: [omniverse, openusd, converter, contributing, packaging]
tools: [Read, Edit, Shell]
---

# Maintainer Development - Omniverse Asset to USD

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0 -->

## Purpose

Guide NVIDIA maintainer code, packaging, documentation, and test changes for the
`usd-convert-asset` repository without mixing maintainer workflows into the
end-user conversion skill.

## Tested Wheel Version

This skill was tested against `usd-convert-asset==1.1.0` using the Python 3.12 wheel.

This repository does not accept external contributions or pull requests. Do not
present this module as an external contribution workflow. See the repository
`CONTRIBUTING.md` for the full contribution policy and issue-reporting guidance.

## When to Use

Use this module when an NVIDIA maintainer asks to change converter
implementation, CLI arguments, package metadata, wheel staging, smoke tests,
build docs, release notes, or maintainer workflow.

For installing or building the package, load `../omniverse-asset-to-usd-getting-started/SKILL.md`. For running conversions or choosing CLI flags, load `../omniverse-asset-to-usd-asset-conversion/SKILL.md`.

## Read First

Before editing, identify the change type and read the local implementation:

| Change type | Read first |
|-------------|------------|
| CLI behavior | `source/python/usd_convert_asset/cli.py` |
| USDZ helper | `source/python/usd_convert_asset/usdz.py` |
| Module entry point | `source/python/usd_convert_asset/__main__.py` |
| Package metadata | `source/python/templates/pyproject.toml` |
| Wheel staging | `source/python/premake5.lua` and `source/python/hatch_build.py` |
| Smoke tests | `source/python/tests/test_pip_wheel_smoke.py` |
| Build and install docs | `README.md`, `source/python/README.md`, `CONTRIBUTING.md` |

Keep CLI examples, README snippets, and smoke tests aligned when flags, entry points, install flow, package metadata, or staging behavior change.

## Hard Rules

1. Do not import native bindings from `usd_convert_asset.__init__`; package import should stay lightweight.
2. Keep both entry points working when CLI behavior changes: `usd-convert-asset` and `python -m usd_convert_asset`.
3. Preserve public CLI flag names and meanings when documentation can resolve an agent-selection ambiguity. Do not remove, rename, or replace a public flag without an approved breaking-change plan and a major-version increment.
4. For source-build or wheel-staging changes, account for platform-specific native runtime libraries and plugin resources.
5. Do not commit machine-local Autodesk FBX SDK paths or accept Autodesk license terms on behalf of the user.

## Verification

Use the smallest verification that covers the changed surface:

| Change | Verification |
|--------|--------------|
| CLI argument parsing or conversion flow | `usd-convert-asset --help` and a small asset conversion |
| `python -m usd_convert_asset` entry point | `python -m usd_convert_asset --help` |
| Wheel metadata or native runtime staging | Build a wheel from the staged `_build/<platform>/<config>/pip/usd_convert_asset` tree, install it into a clean venv, then import `usd_convert_asset` and `asset_converter_native_bindings` |
| Smoke test changes | `python -m unittest discover -s source/python/tests -p "test_pip_wheel_smoke.py" -v` after installing the wheel |
| Build or maintainer docs | Check changed commands against `README.md`, `source/python/README.md`, and `CONTRIBUTING.md` |

## Output

When done, summarize changed files, relevant verification, and any build/test steps that could not be run locally.
