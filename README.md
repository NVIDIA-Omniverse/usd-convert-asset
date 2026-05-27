# usd-convert-asset

usd-convert-asset builds native C++ conversion libraries, Python
bindings, and USD plugin resources for converting source assets to USD.

## Overview

usd-convert-asset provides native and Python entry points for converting
3D asset files into USD, USDA, USDC, or USDZ outputs. It is intended for tools
and pipelines that need scriptable asset ingestion into OpenUSD-based workflows.

Key repository areas:

- `source/library` - native asset converter implementation.
- `source/python` - Python bindings and command-line package.
- `deps` - dependency manifests.
- `data/tests` - test assets and expected output fixtures.

## Getting Started

Clone the repository, fetch/configure dependencies, run a release build, run
tests, then build and install the Python package.

Windows:

```powershell
git clone <repo-url>
cd omniverse-asset-converter
.\repo.bat build --config release
.\repo.bat test --config release
.\repo.bat uv -- build _build\<platform>\release\pip\usd_convert_asset -o dist
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install dist\usd_convert_asset-*.whl
```

Linux:

```bash
git clone <repo-url>
cd omniverse-asset-converter
./repo.sh build --config release
./repo.sh test --config release
./repo.sh uv -- build _build/<platform>/release/pip/usd_convert_asset -o dist
python -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install dist/usd_convert_asset-*.whl
```

Replace `<platform>` with the build platform:

- Windows x86_64: `windows-x86_64`
- Linux x86_64: `linux-x86_64`
- Linux aarch64: `linux-aarch64`

More Python package details live in `source/python/README.md`.

## Requirements

- OS/Arch: Windows 10 or Ubuntu 22.04.
- Compiler: MSVC with Visual Studio 2019+ Build Tools on Windows.
- Build tools: Premake, Windows SDK on Windows.
- Runtime: Python 3.12 for Python package builds.
- SDKs: FBX SDK (2020.3) for Windows x86_64 and Linux x86_64 source builds.
  Developers must download it from Autodesk and stage it locally.
  Tested with FBX SDK 2020.3.7.
- GPU/Drivers: No GPU requirement for core conversion.

On Windows, `repo.toml` must match the Visual Studio version installed on your
machine. Set `repo_build.msbuild.vs_version` to your local version before
building, for example `vs2019` for Visual Studio 2019 or `vs2022` for Visual
Studio 2022. You may also leave it empty to use the latest installed version.

Windows has a 260-character path length limit. Enable long paths in Registry
settings if needed.

### Configure FBX SDK

For Windows x86_64 and Linux x86_64 source builds, the repository can build
against Autodesk FBX SDK `2020.3.7`. Developers must download FBX SDK
from `https://aps.autodesk.com/developer/overview/fbx-sdk`, review and accept
Autodesk's license terms themselves, stage the installed SDK into the layout
that `premake5.lua` expects, then edit
`deps/target-deps.packman.xml` locally and replace the `fbxsdk` package entries
with a local source path to the staged folder:

- Windows: `include` and `lib/x64/release`
- Linux x86_64: `include` and `lib/release`

For Linux aarch64, do not download or configure FBX SDK. Autodesk FBX SDK does
not support this target, so FBX conversion uses Assimp instead.

Treat this as a local-only dependency override. Do not commit machine-specific
paths in `deps/target-deps.packman.xml`.

```xml
<dependency name="fbxsdk" linkPath="../_build/target-deps/fbxsdk">
    <source path="C:/path/to/staged/fbxsdk-packman-layout"/>
</dependency>
```

## Usage

After building and installing the Python package, convert an input asset from the
command line. `usd-convert-asset` is the installed console script; `python -m
usd_convert_asset` runs the same CLI through the Python module.

```bash
usd-convert-asset -i path/to/input.fbx -o path/to/output.usda --progress
```

```bash
python -m usd_convert_asset -i path/to/input.obj -o path/to/output.usdz
```

Common conversion options include `--single-mesh`, `--ignore-materials`,
`--ignore-animation`, `--preview-surface`, `--embed-textures`, `--fbx-y-up`,
`--fbx-z-up`, `--stage-up-y`, and `--stage-up-z`.

Common examples:

```bash
# Choose output format by extension: .usd, .usda, .usdc, or .usdz.
usd-convert-asset -i path/to/input.fbx -o path/to/output.usdc

# Embed textures for portable USDZ output.
usd-convert-asset -i path/to/input.fbx -o path/to/output.usdz --embed-textures

# Convert FBX up-axis and force the output stage to Y-up.
usd-convert-asset -i path/to/input.fbx -o path/to/output.usda --fbx-y-up --stage-up-y

# Export geometry without materials.
usd-convert-asset -i path/to/input.obj -o path/to/output.usda --ignore-materials

# Batch conversion from a shell script.
for asset in assets/*.fbx; do
    usd-convert-asset -i "$asset" -o "converted/$(basename "${asset%.*}").usda"
done
```

- More examples/tutorials: `source/python/README.md`
- API reference: `include/usd_convert_asset.h`

## Releases

- Releases/Changelog: `CHANGELOG.md`

## Contribution Guidelines

Use GitHub pull requests for code changes and include build or test coverage
appropriate for the change.

Before opening a pull request, run a release build and the relevant tests.

Windows:

```powershell
.\repo.bat build --config release
.\repo.bat test --config release
```

Linux:

```bash
./repo.sh build --config release
./repo.sh test --config release
```

The default test suite covers Python unit tests and the C++ Catch2 test binary.
For Python package changes, also build and install the wheel from `dist/` and run
a CLI smoke conversion with a small asset.

### Governance & Maintainers

This project is maintained by NVIDIA. Project governance, maintainers, and
triage policy are managed by the repository owners.

### Security

- Vulnerability disclosure: `SECURITY.md`
- Do not file public issues for security reports.

### Support

- Level: Community support through repository issues.
- How to get help: Open a GitHub issue with environment details, input asset
  type, conversion command, and error output.
- Include: OS/architecture, converter version or commit SHA, FBX SDK version,
  input format, full conversion command, logs or stack traces, and a small
  reproduction asset when it can be shared.

## Community

Use repository issues and pull requests for project communication.

## References

- OpenUSD: https://openusd.org/
- NVIDIA Omniverse: https://www.nvidia.com/en-us/omniverse/
- Python package notes: `source/python/README.md`

## License

This project is licensed under the Apache License, Version 2.0 and the Creative
Commons Attribution 4.0 International Public License. See `LICENSE` for details.

Third-party notices and license attributions are listed in
`Third_Party_Notices.md`.
