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

The Linux aarch64 build converts FBX through Assimp rather than Autodesk FBX SDK.
This fallback does not provide the same fidelity as the Autodesk path. In particular,
FBX skin bindings and lights are not preserved on import, and animation and skin
bindings are not preserved on export. Validate these workflows on the target platform.

More Python package details live in `source/python/README.md`.

### OpenUSD Runtime Compatibility

`usd-convert-asset` ships with and uses its own bundled OpenUSD runtime. Do not
mix it with another OpenUSD or `usd-core` installation in the same Python
process; competing `pxr` modules and native libraries can conflict.

For workflows that also run other USD Python code, isolate conversion in a
separate process and environment. Use a dedicated virtual environment and
invoke `usd-convert-asset` as a subprocess or through its CLI instead of
importing both runtimes into one process.

## Requirements

- OS/Arch: Windows 10 or Ubuntu 22.04.
- Compiler: MSVC with Visual Studio 2019 or 2022 Build Tools on Windows.
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
not support this target. FBX conversion uses Assimp on Linux aarch64.

Treat this as a local-only dependency override. Do not commit machine-specific
paths in `deps/target-deps.packman.xml`.

```xml
<dependency name="fbxsdk" linkPath="../_build/target-deps/fbxsdk">
    <source path="C:/path/to/staged/fbxsdk-packman-layout"/>
</dependency>
```

### FBX NURBS Curves

On Windows x86_64 and Linux x86_64 builds that use Autodesk FBX SDK,
valid static open, closed, periodic, and rational FBX NURBS curves are
preserved in USD output as `UsdGeomNurbsCurves`. Curves with invalid order,
control-point, knot, or parameter-range data that cannot be mapped safely to
OpenUSD are skipped, and the conversion log reports their names and counts.

Linux aarch64 does not provide this NURBS curve support. This feature preserves
static curve geometry; it does not add support for animated control-point
deformation.

## Usage

After building and installing the Python package, convert an input asset from the
command line. `usd-convert-asset` is the installed console script; `python -m
usd_convert_asset` runs the same CLI through the Python module.

```bash
usd-convert-asset -i path/to/input.fbx -o path/to/output.usda --progress
```

```bash
python -m usd_convert_asset -i path/to/input.obj -o path/to/output.usdc
```

Common conversion options include `--single-props-file`, `--ignore-materials`,
`--ignore-animation`, `--fbx-y-up`, `--fbx-z-up`, `--stage-up-y`, and
`--stage-up-z`.

`--single-props-file` stores mesh data in the exported scene and disables USD
scene instancing. It does not merge meshes. It also places copied texture
sidecars under `materials/textures/` instead of `textures/`.

Common examples:

```bash
# Choose output format by extension: .usd, .usda, .usdc, or .usdz.
usd-convert-asset -i path/to/input.fbx -o path/to/output.usdc

# Convert FBX up-axis and force the output stage to Y-up.
usd-convert-asset -i path/to/input.fbx -o path/to/output.usda --fbx-y-up --stage-up-y

# Export geometry without materials.
usd-convert-asset -i path/to/input.obj -o path/to/output.usda --ignore-materials

# Batch conversion from a shell script.
for asset in assets/*.fbx; do
    usd-convert-asset -i "$asset" -o "converted/$(basename "${asset%.*}").usda"
done
```

When materials are exported, the converter authors USD Preview Surface materials for all supported USD output formats.
This changes the default material model from the MDL output used in 1.1. Full MDL
shader evaluation and glTF MDL extensions are no longer supported. Any available
base-color fallback is only an approximation; textures and more complex MDL
behavior may not survive conversion. For predictable results, supply a USD
Preview Surface material alongside MDL.

UsdPreviewSurface inputs use the types defined by the UsdPreviewSurface specification: `color3f` for color inputs and `normal3f` for the normal. Scripts that create or compare texture-connected inputs previously typed as `float3` must use these types; the values are unchanged.
Use `--ignore-materials` to create a geometry-only USDZ package.

- More examples/tutorials: `source/python/README.md`
- API reference: `include/usd_convert_asset.h`

## Releases

- Releases/Changelog: `CHANGELOG.md`

## Contribution Policy

This project is not currently accepting external contributions or pull requests.
See `CONTRIBUTING.md` for the contribution policy and issue-reporting guidance.

## Governance & Maintainers

This project is maintained by NVIDIA. Project governance, maintainers, and
triage policy are managed by the repository owners.

## Security

- Vulnerability disclosure: `SECURITY.md`
- Do not file public issues for security reports.

## Support

- Level: Maintained, with community support through repository issues.
- How to get help: Open a GitHub issue with environment details, input asset
  type, conversion command, and error output.
- Include: OS/architecture, converter version or commit SHA, FBX SDK version,
  input format, full conversion command, logs or stack traces, and a small
  reproduction asset when it can be shared.

## Community

Use repository issues for project communication. External pull requests are not
accepted.

## References

- OpenUSD: https://openusd.org/
- NVIDIA Omniverse: https://www.nvidia.com/en-us/omniverse/
- Python package notes: `source/python/README.md`

## License

This project is licensed under the Apache License, Version 2.0 and the Creative
Commons Attribution 4.0 International Public License. See `LICENSE` for details.

Third-party notices and license attributions are listed in
`Third_Party_Notices.md`.
