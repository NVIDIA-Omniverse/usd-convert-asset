# usd-convert-asset

Pip-installable package for usd-convert-asset.

This package is staged from the existing repo build output. The native bindings,
runtime libraries, USD plugin resources, optional staged data, and generated
third-party license bundle are copied into a wheel source tree under `_build`.

## OpenUSD Runtime Compatibility

`usd-convert-asset` ships with and uses its own bundled OpenUSD runtime. Do not
mix it with another OpenUSD or `usd-core` installation in the same Python
process; competing `pxr` modules and native libraries can conflict.

If a workflow also uses other USD Python code, install the converter in a
dedicated virtual environment and run conversion in a separate process. Invoke
`usd-convert-asset` as a subprocess or use its CLI without importing both
runtimes into one process.

## Building From Source

Run the normal repo build first. This compiles `asset_converter_native_bindings`,
copies runtime libraries into `asset_converter_native_bindings/libs`, generates
`PACKAGE-LICENSES`, and stages the pip wheel source tree.

Windows:

```powershell
.\repo.bat build --config release
.\repo.bat uv -- build _build\windows-x86_64\release\pip\usd_convert_asset -o dist
```

Linux x86_64:

```bash
./repo.sh build --config release
./repo.sh uv -- build _build/linux-x86_64/release/pip/usd_convert_asset -o dist
```

Linux aarch64:

```bash
./repo.sh build --config release
./repo.sh uv -- build _build/linux-aarch64/release/pip/usd_convert_asset -o dist
```

The wheel is written to `dist/`:

```text
dist/usd_convert_asset-<version>-<python>-<abi>-<platform>.whl
```

Replace `<version>` with the version generated from `VERSION.md`. The wheel
tag is inferred from the build interpreter and platform because the package
contains native runtime libraries.

## Testing From Source

Use a clean virtual environment and install the staged wheel or generated pip
package directory. The package depends on native libraries staged by the repo
build, so install from `dist/` or `_build/.../pip` rather than from
`source/python` directly.

Windows:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install dist\usd_convert_asset-<version>-<python>-<abi>-<platform>.whl
usd-convert-asset -i path\to\input.fbx -o path\to\output.usda
.\.venv\Scripts\python.exe -m usd_convert_asset -i path\to\input.obj -o path\to\output.usdc
```

Linux:

```bash
python -m venv .venv
source .venv/bin/activate
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install dist/usd_convert_asset-<version>-<python>-<abi>-<platform>.whl
usd-convert-asset -i path/to/input.fbx -o path/to/output.usda
.venv/bin/python -m usd_convert_asset -i path/to/input.obj -o path/to/output.usdc
```

To test the staged tree before building a wheel, replace the wheel install with
the generated pip package directory:

```bash
.venv/bin/python -m pip install _build/<platform>/<config>/pip/usd_convert_asset
```

Use the matching `_build/<platform>/<config>/pip/usd_convert_asset` path
for the platform and configuration you built. Replace the input paths with
existing source assets and the output paths with writable locations. Run
`usd-convert-asset --help` for conversion flags such as `--single-mesh`,
`--ignore-materials`, `--fbx-y-up`, and `--progress`.

## License

This package is licensed under the Apache License, Version 2.0 and the Creative
Commons Attribution 4.0 International Public License. See `LICENSE`.

Third-party notices and generated dependency license attributions are included
in `PACKAGE-LICENSES`.

## Uninstalling

```bash
.venv/bin/python -m pip uninstall usd-convert-asset
```
