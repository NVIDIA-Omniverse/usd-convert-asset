# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""
Pip / wheel smoke tests for usd-convert-asset.

Install the package, then run from ``source/python``:

    python -m unittest tests.test_pip_wheel_smoke -v

Or:

    python -m pip install dist/usd_convert_asset-<version>-<python>-<abi>-<platform>.whl
    python -m unittest discover -s tests -p "test_pip_wheel_smoke.py" -v
"""

from __future__ import annotations

import base64
import importlib.metadata
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import skipUnless

_DISTRIBUTION_NAME = "usd-convert-asset"
_IMPORT_PACKAGE_NAME = "usd_convert_asset"
_REPO_ROOT = Path(__file__).resolve().parents[3]
_CUBE_FBX_PATH = _REPO_ROOT / "data" / "tests" / "python_tests" / "cube.fbx"


def _is_distribution_installed() -> bool:
    try:
        importlib.metadata.distribution(_DISTRIBUTION_NAME)
        return True
    except importlib.metadata.PackageNotFoundError:
        return False


def _is_runtime_usable() -> bool:
    try:
        __import__(_IMPORT_PACKAGE_NAME)
        __import__("asset_converter_native_bindings")
        return True
    except ImportError:
        return False


def _wheel_ready() -> bool:
    return _is_distribution_installed() and _is_runtime_usable()


_RUNTIME_MSG = "requires pip-installed usd_convert_asset wheel with native runtime libraries"


@skipUnless(_wheel_ready(), _RUNTIME_MSG)
class TestPipWheelSmoke(unittest.TestCase):
    def test_license_notices_are_owned_by_distribution(self):
        distribution = importlib.metadata.distribution(_DISTRIBUTION_NAME)
        files = [str(path).replace("\\", "/") for path in distribution.files or []]
        self.assertNotIn("LICENSE", files)
        self.assertFalse(any(path.startswith("PACKAGE-LICENSES/") for path in files))
        licenses = [path for path in files if ".dist-info/licenses/" in path]
        self.assertTrue(any(path.endswith("/licenses/LICENSE") for path in licenses))
        self.assertTrue(any("/licenses/PACKAGE-LICENSES/" in path for path in licenses))
        for path in licenses:
            self.assertTrue(distribution.locate_file(path).is_file(), path)

    @staticmethod
    def _triangle(path: Path) -> None:
        path.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="utf-8")

    @staticmethod
    def _output_at_length(base: Path, length: int, filename: str = "o.usda") -> Path:
        parent = base
        while len(str(parent / filename)) + 201 < length:
            parent /= "d" * 200
        remainder = length - len(str(parent / filename)) - 1
        return parent / ("d" * remainder) / filename if remainder > 0 else parent / filename

    @skipUnless(sys.platform == "linux", "Linux path limits")
    def test_cli_output_near_linux_path_limit(self):
        from pxr import Usd

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "triangle.obj"
            self._triangle(source)
            for filename in ("o.usda", "o.usdz"):
                output = self._output_at_length(base, 4090, filename)
                result = self._run_cli("-i", str(source), "-o", str(output))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(Usd.Stage.Open(str(output)))

    @skipUnless(os.name == "posix", "POSIX read-only files")
    def test_cli_preserves_readonly_output(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "triangle.obj"
            self._triangle(source)
            for suffix in (".usda", ".usdz"):
                output = base / ("out" + suffix)
                output.write_bytes(b"protected")
                output.chmod(0o444)
                before = output.stat()
                try:
                    result = self._run_cli("-i", str(source), "-o", str(output))
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("not writable", result.stderr)
                    self.assertEqual(output.read_bytes(), b"protected")
                    self.assertEqual(output.stat().st_mode, before.st_mode)
                    self.assertEqual(output.stat().st_ino, before.st_ino)
                    self.assertFalse(list(base.glob(".uca-*")))
                finally:
                    output.chmod(0o600)

    @skipUnless(sys.platform == "win32", "Windows native path limits")
    def test_cli_long_paths_with_textures_and_instances(self):
        from pxr import Usd, UsdUtils

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            obj = base / "textured.obj"
            obj.write_text(
                "mtllib material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\nusemtl material\nf 1/1 2/2 3/3\n",
                encoding="utf-8",
            )
            (base / "material.mtl").write_text("newmtl material\nKd 1 1 1\nmap_Kd checker.png\n")
            (base / "checker.png").write_bytes(
                base64.b64decode(
                    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aWQAAAABJRU5ErkJggg=="
                )
            )
            data = struct.pack("<9f3H", 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 2)
            gltf = base / "instanced.gltf"
            gltf.write_text(
                json.dumps(
                    {
                        "asset": {"version": "2.0"},
                        "scene": 0,
                        "scenes": [{"nodes": [0, 1]}],
                        "nodes": [
                            {"mesh": 0, "name": "First"},
                            {"mesh": 0, "name": "Second", "translation": [2, 0, 0]},
                        ],
                        "meshes": [{"name": "T", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
                        "buffers": [
                            {
                                "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode(),
                                "byteLength": len(data),
                            }
                        ],
                        "bufferViews": [
                            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
                            {"buffer": 0, "byteOffset": 36, "byteLength": 6},
                        ],
                        "accessors": [
                            {
                                "bufferView": 0,
                                "componentType": 5126,
                                "count": 3,
                                "type": "VEC3",
                                "min": [0, 0, 0],
                                "max": [1, 1, 0],
                            },
                            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
                        ],
                    }
                )
            )
            for source, lengths in ((obj, (230, 236, 242)), (gltf, (236, 242, 250))):
                for length in lengths:
                    with self.subTest(source=source.name, length=length):
                        output = self._output_at_length(base / source.stem, length)
                        result = self._run_cli("-i", str(source), "-o", str(output))
                        self.assertEqual(result.returncode, 0, result.stderr)
                        stage = None
                        layers = []
                        try:
                            stage = Usd.Stage.Open(str(output))
                            self.assertTrue(stage)
                            layers, assets, unresolved = UsdUtils.ComputeAllDependencies(str(output))
                            self.assertFalse(unresolved)
                            if source == obj:
                                self.assertTrue(any(str(asset).endswith("checker.png") for asset in assets))
                            else:
                                self.assertGreaterEqual(sum(prim.IsInstance() for prim in stage.Traverse()), 2)
                        finally:
                            # USD layers can keep Windows files open until their references are released.
                            layers.clear()
                            stage = None

            # The final sidecar must fit too, even when the root layer fits.
            document = json.loads(gltf.read_text())
            document["meshes"][0]["name"] = "Triangle"
            gltf.write_text(json.dumps(document))
            output = self._output_at_length(base / "long-sidecar", 250)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(b"original")
            result = self._run_cli("-i", str(gltf), "-o", str(output))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Windows path limit", result.stderr)
            self.assertEqual(output.read_bytes(), b"original")

    @skipUnless(sys.platform == "win32", "Windows long output filenames")
    def test_windows_long_output_filenames(self):
        from pxr import Usd

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "triangle.obj"
            self._triangle(source)
            for suffix in (".usda", ".usdc", ".usd", ".usdz"):
                for length in (244, 246, 247, 250, 255, 256, 259, 260):
                    filename_length = length - len(str(base)) - 1
                    output = base / ("n" * (filename_length - len(suffix)) + suffix)
                    self.assertEqual(len(str(output)), length)
                    for existing in (False, True):
                        with self.subTest(suffix=suffix, length=length, existing=existing):
                            if existing:
                                output.write_bytes(b"original")
                            before = set(base.iterdir()) - {output}
                            result = self._run_cli("-i", str(source), "-o", str(output))
                            if length == 260:
                                self.assertEqual(result.returncode, 1, result.stderr)
                                self.assertIn("Windows limit", result.stderr)
                                self.assertEqual(output.exists(), existing)
                                if existing:
                                    self.assertEqual(output.read_bytes(), b"original")
                            else:
                                self.assertEqual(result.returncode, 0, result.stderr)
                                # Read a short copy to verify authored data independently
                                # of the USD reader's own path-length limitations.
                                check = base / ("check" + suffix)
                                shutil.copyfile(output, check)
                                if suffix == ".usdz":
                                    with zipfile.ZipFile(check) as archive:
                                        self.assertEqual(archive.namelist()[0], output.stem + ".usdc")
                                stage = Usd.Stage.Open(str(check))
                                self.assertTrue(stage)
                                self.assertTrue(stage.GetDefaultPrim())
                                stage = None
                                check.unlink()
                            self.assertEqual(set(base.iterdir()) - {output}, before)
                            output.unlink(missing_ok=True)

    @skipUnless(sys.platform == "win32", "Windows deep output folders")
    def test_windows_deep_output_paths(self):
        from pxr import Usd

        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "triangle.obj"
            self._triangle(source)
            for suffix in (".usda", ".usdc", ".usd", ".usdz"):
                for length in (244, 247, 250, 255, 256, 259):
                    with self.subTest(suffix=suffix, length=length), tempfile.TemporaryDirectory(dir=base) as case:
                        # Isolate parent directories: different filename lengths
                        # can make one case's output folder an ancestor of another.
                        output = self._output_at_length(Path(case) / "deep", length, "o" + suffix)
                        result = self._run_cli("-i", str(source), "-o", str(output))
                        self.assertEqual(result.returncode, 0, result.stderr)
                        check = base / ("check" + suffix)
                        shutil.copyfile(output, check)
                        stage = Usd.Stage.Open(str(check))
                        self.assertTrue(stage)
                        self.assertTrue(stage.GetDefaultPrim())
                        stage = None
                        check.unlink()
                        self.assertEqual(list(output.parent.iterdir()), [output])
                        output.unlink()

    def _run_cli(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, "-m", _IMPORT_PACKAGE_NAME, *args],
            check=False,
            capture_output=True,
            text=True,
            timeout=120,
        )

    def test_distribution_metadata_name(self):
        distribution = importlib.metadata.distribution(_DISTRIBUTION_NAME)

        self.assertEqual(distribution.metadata["Name"].lower(), _DISTRIBUTION_NAME)

    def test_import_python_package(self):
        import usd_convert_asset as package

        self.assertTrue(package.__version__)
        self.assertTrue(callable(package.get_version))
        self.assertEqual(package.get_version(), package.__version__)

    def test_import_native_bindings(self):
        import asset_converter_native_bindings as native

        self.assertTrue(hasattr(native, "OmniAssetConverter"))
        self.assertIsInstance(native.OmniAssetConverter.major_version(), int)
        self.assertIsInstance(native.OmniAssetConverter.minor_version(), int)

    def test_wheel_includes_agent_skill(self):
        distribution = importlib.metadata.distribution(_DISTRIBUTION_NAME)
        skill_path = Path(distribution.locate_file("usd_convert_asset/skills/omniverse-asset-to-usd/SKILL.md"))

        self.assertTrue(skill_path.is_file(), msg=f"missing wheel skill artifact: {skill_path}")

    def test_cli_module_help(self):
        result = self._run_cli("--help")

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Convert 3D assets to USD", result.stdout)
        self.assertIn("--input", result.stdout)
        self.assertIn("--output", result.stdout)
        self.assertIn(".usdz", result.stdout)

    def test_cli_converts_cube_fbx_to_usd(self):
        if not _CUBE_FBX_PATH.is_file():
            self.skipTest(f"missing test fixture: {_CUBE_FBX_PATH}")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "cube.usd"
            result = self._run_cli("-i", str(_CUBE_FBX_PATH), "-o", str(output_path))
            self.assertEqual(result.returncode, 0, msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
            self.assertNotIn("ModuleNotFoundError: No module named 'pxr'", result.stderr)
            self.assertTrue(output_path.is_file())
            self.assertGreater(output_path.stat().st_size, 0)

    def test_cli_converts_cube_fbx_to_usdz(self):
        if not _CUBE_FBX_PATH.is_file():
            self.skipTest(f"missing test fixture: {_CUBE_FBX_PATH}")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "cube.usdz"
            result = self._run_cli("-i", str(_CUBE_FBX_PATH), "-o", str(output_path))
            self.assertEqual(result.returncode, 0, msg=f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
            self.assertTrue(output_path.is_file())
            self.assertGreater(output_path.stat().st_size, 0)
            self.assertFalse(list(output_path.parent.glob(".cube.usdz.staging-*")))
            self.assertEqual(output_path.read_bytes()[:2], b"PK")
            with zipfile.ZipFile(output_path) as archive:
                self.assertIn(f"{output_path.stem}.usdc", archive.namelist())

    def test_cli_rejects_unknown_output_extension(self):
        if not _CUBE_FBX_PATH.is_file():
            self.skipTest(f"missing test fixture: {_CUBE_FBX_PATH}")

        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "cube.obj"
            result = self._run_cli("-i", str(_CUBE_FBX_PATH), "-o", str(output_path))

        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertIn("unsupported output format '.obj'", result.stderr)


if __name__ == "__main__":
    unittest.main()
