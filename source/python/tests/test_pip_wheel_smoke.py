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

import importlib.metadata
import subprocess
import sys
import tempfile
import unittest
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

    def test_cli_module_help(self):
        result = self._run_cli("--help")

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Convert 3D assets to USD", result.stdout)
        self.assertIn("--input", result.stdout)
        self.assertIn("--output", result.stdout)

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


if __name__ == "__main__":
    unittest.main()
