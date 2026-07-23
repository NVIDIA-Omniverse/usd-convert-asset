# Contributing

Thank you for your interest in contributing to `usd-convert-asset`.

This project accepts contributions through pull requests. Before opening a pull
request, please make sure your change is focused, documented where appropriate,
and tested for the behavior it affects.

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

## Pull Requests

Pull requests should be scoped to a single logical change. Include a clear
description of the problem being solved and the approach taken.

Before submitting a pull request:

- Run the repo build when your change affects native code, bindings, packaging,
  or staged runtime files.
- Run relevant tests or smoke tests.
- Update `README.md`, `source/python/README.md`, or other docs when behavior,
  options, packaging, or supported formats change.
- Keep public APIs backwards compatible unless the pull request intentionally
  proposes a breaking change.
- Do not commit machine-specific dependency paths, build output, or local
  virtual environments.

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
not support that target; FBX conversion uses Assimp instead.

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

## Signing Your Work

We require that all contributors sign off on their commits using the Developer
Certificate of Origin (DCO). This certifies that the contribution is your
original work, or that you have the right to submit it under this project's
license or a compatible license.

Contributions containing commits that are not signed off may not be accepted.
To sign off on a commit, use the `--signoff` or `-s` option:

```bash
git commit -s -m "Add conversion option"
```

This appends a line like this to your commit message:

```text
Signed-off-by: Your Name <your.email@example.com>
```

Full text of the DCO:

```text
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this license
document, but changing it is not allowed.

Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I have the right
to submit it under the open source license indicated in the file; or

(b) The contribution is based upon previous work that, to the best of my
knowledge, is covered under an appropriate open source license and I have the
right under that license to submit that work with modifications, whether created
in whole or in part by me, under the same open source license (unless I am
permitted to submit under a different license), as indicated in the file; or

(c) The contribution was provided directly to me by some other person who
certified (a), (b) or (c) and I have not modified it.

(d) I understand and agree that this project and the contribution are public and
that a record of the contribution (including all personal information I submit
with it, including my sign-off) is maintained indefinitely and may be
redistributed consistent with this project or the open source license(s)
involved.
```

## Coding Guidelines

- Follow the existing code style in the files you edit.
- Keep changes narrowly scoped and avoid unrelated formatting churn.
- Add comments only where they clarify non-obvious conversion, packaging, or USD
  behavior.
- Preserve third-party license headers and notices in copied third-party source.
- Keep generated build output out of source control unless the repository
  explicitly tracks that artifact.

## License

By contributing, you agree that your contributions will be licensed under the
Apache License, Version 2.0 and the Creative Commons Attribution 4.0
International Public License. See `LICENSE` for details.
