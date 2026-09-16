# Contributing

Thank you for your interest in `usd-convert-asset`.

This repository contains the native C++ conversion libraries, Python bindings,
USD plugin resources, and documentation for `usd-convert-asset`.

## Contribution Policy

This project is not currently accepting external contributions or pull requests.
Unsolicited pull requests will be closed. Because external contributions are not
accepted, no Contributor License Agreement (CLA) or Developer Certificate of
Origin (DCO) process applies.

NVIDIA maintainers may continue to update this repository through NVIDIA's
internal development and review processes. A future change to the external
contribution policy will be documented in this file and in `README.md`.

## Reporting Issues

Use issues for bug reports, feature requests, and documentation problems.
Include enough detail for maintainers to reproduce or understand the issue:

- Package version or commit.
- Operating system, architecture, and Python version.
- Input format, output format, and whether the issue affects the native library,
  Python package, or CLI.
- Exact command or API call used.
- Error output, logs, or a short description of the unexpected behavior.

Do not report security vulnerabilities through public issues. See `SECURITY.md`
for private disclosure instructions.

## Development Setup

From a clean checkout, build the repository first. This fetches/configures
dependencies, builds the native converter, generates Python package metadata,
and stages the wheel source tree under `_build`.

Windows:

```powershell
.\repo.bat build --config release
```

Linux:

```bash
./repo.sh build --config release
```

Developer source builds require Autodesk FBX SDK `2020.3.7` on Windows x86_64
and Linux x86_64. Download it from
`https://aps.autodesk.com/developer/overview/fbx-sdk`, review and accept
Autodesk's license terms yourself, stage it into the layout expected by
`premake5.lua` (`include` plus `lib/x64/release` on Windows or `lib/release` on
Linux x86_64), then edit `deps/target-deps.packman.xml` locally and point the
`fbxsdk` dependency at that staged folder. Do not commit local SDK paths.

Linux aarch64 source builds do not use Autodesk FBX SDK because Autodesk does
not support that target. FBX conversion uses Assimp on Linux aarch64.

## Building the Python Package

After the repo build completes, build the staged Python package with `uv`.

Windows:

```powershell
.\repo.bat uv -- build _build\windows-x86_64\release\pip\usd_convert_asset -o dist
```

Linux x86_64:

```bash
./repo.sh uv -- build _build/linux-x86_64/release/pip/usd_convert_asset -o dist
```

Linux aarch64:

```bash
./repo.sh uv -- build _build/linux-aarch64/release/pip/usd_convert_asset -o dist
```

The wheel is written to `dist/`.

## Testing

Run the tests that apply to your change.

Windows:

```powershell
.\repo.bat test --config release
```

Linux:

```bash
./repo.sh test --config release
```

At minimum, smoke test the CLI when changing conversion, packaging, or entry
point behavior:

```bash
python -m venv .venv
source .venv/bin/activate  # Windows PowerShell: .\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
python -m pip install dist/usd_convert_asset-<version>-<python>-<abi>-<platform>.whl
usd-convert-asset --help
usd-convert-asset -i path/to/input.fbx -o path/to/output.usda
python -m usd_convert_asset -i path/to/input.obj -o path/to/output.usdc
```

To test the staged package tree before building a wheel, install the generated
package directory instead:

```bash
python -m pip install _build/<platform>/<config>/pip/usd_convert_asset
```

Use small source-control-friendly sample assets when adding or changing
conversion behavior.

## Coding Guidelines

- Follow the existing code style in the files you edit.
- Keep changes narrowly scoped and avoid unrelated formatting churn.
- Add comments only where they clarify non-obvious conversion, packaging, or USD
  behavior.
- Preserve third-party license headers and notices in copied third-party source.
- Keep generated build output out of source control unless the repository
  explicitly tracks that artifact.

## License

This project is licensed under the Apache License, Version 2.0 and the Creative
Commons Attribution 4.0 International Public License. See `LICENSE` for details.
