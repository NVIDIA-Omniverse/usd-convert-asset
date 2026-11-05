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

## Conversion Results and Limitations

A successful conversion does not guarantee that every source element was preserved.
Review conversion warnings as well as the result status and output file. For
supported partial-conversion cases, the converter skips damaged or unsupported
elements and keeps the content it can convert. For example, invalid vertex values
can cause a glTF primitive or affected OBJ/PLY geometry to be skipped while valid
geometry remains. Conversion fails when no valid geometry remains; supported PLY
point clouds can be preserved without faces. Other errors can still fail conversion.

A custom Python `material_loader` callback that raises an exception causes the
conversion to report failure instead of silently substituting fallback materials.
Check the conversion result and logs before using the output.

On Linux aarch64, FBX conversion uses Assimp rather than Autodesk FBX SDK. FBX
skin bindings and lights are not preserved on import; animation and skin bindings
are not preserved on export. Static bind-pose geometry can be retained without
preserving an editable skeleton or its animation. Warnings identify unsupported
content; validate the result on the target platform. Windows x86_64 and Linux
x86_64 use Autodesk FBX SDK and additionally support FBX NURBS curve preservation.

## Python Multiprocessing

On POSIX systems, do not fork while a conversion or its cleanup is active. After
using the converter, use the `spawn` or `forkserver` multiprocessing start method
to start further processes. Create converters within the worker process rather
than sharing active converter objects between processes.

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
usd-convert-asset -i path/to/input.fbx -o path/to/output.usdz
.venv/bin/python -m usd_convert_asset -i path/to/input.obj -o path/to/output.usdc
```

When materials are exported, the converter authors USD Preview Surface materials for all supported USD output formats.

UsdPreviewSurface inputs use the types defined by the UsdPreviewSurface specification: `color3f` for color inputs and `normal3f` for the normal. Scripts that create or compare texture-connected inputs previously typed as `float3` must use these types; the values are unchanged.
Use `--ignore-materials` to create a geometry-only USDZ package.

To test the staged tree before building a wheel, replace the wheel install with
the generated pip package directory:

```bash
.venv/bin/python -m pip install _build/<platform>/<config>/pip/usd_convert_asset
```

Use the matching `_build/<platform>/<config>/pip/usd_convert_asset` path
for the platform and configuration you built. Replace the input paths with
existing source assets and the output paths with writable locations. Run
`usd-convert-asset --help` for conversion flags such as `--single-props-file`,
`--ignore-materials`, `--fbx-y-up`, `--fbx-z-up`, and `--progress`.

## License

This package is licensed under the Apache License, Version 2.0 and the Creative
Commons Attribution 4.0 International Public License. See `LICENSE`.

Third-party notices and generated dependency license attributions are included
in `PACKAGE-LICENSES` in the source archive. Installed wheels store the main license and third-party notices under their own `.dist-info/licenses/` directory.

## Uninstalling

```bash
.venv/bin/python -m pip uninstall usd-convert-asset
```
