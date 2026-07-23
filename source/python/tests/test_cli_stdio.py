# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""CLI stdio encoding tests (NVBug 6471021 / OMPE-101983)."""

from __future__ import annotations

import io
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

from usd_convert_asset import cli

_CJK_PATH = r"out dir éñ/cube 你好.usda"
_CJK_PATH_MESSAGE = f"Successfully converted: {_CJK_PATH}"


class TestCliFormatValidation(unittest.TestCase):

    def test_main_rejects_unsupported_input_before_loading_native_bindings(self) -> None:
        stderr = io.StringIO()

        with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": None}):
            with mock.patch.object(sys, "stderr", stderr):
                exit_code = cli.main(["-i", "scene.7z", "-o", "scene.usda"])

        self.assertEqual(exit_code, 1)
        self.assertIn("unsupported input format '.7z'", stderr.getvalue())
        self.assertIn(".fbx, .obj, .gltf, .glb, .stl, .ply", stderr.getvalue())

    def test_main_rejects_usdz_output_before_loading_native_bindings(self) -> None:
        stderr = io.StringIO()

        with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": None}):
            with mock.patch.object(sys, "stderr", stderr):
                exit_code = cli.main(["-i", "scene.obj", "-o", "scene.usdz"])

        self.assertEqual(exit_code, 1)
        self.assertIn("unsupported output format '.usdz'", stderr.getvalue())
        self.assertIn(".usd, .usda, .usdc", stderr.getvalue())

    def test_main_rejects_paths_without_extensions(self) -> None:
        stderr = io.StringIO()

        with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": None}):
            with mock.patch.object(sys, "stderr", stderr):
                exit_code = cli.main(["-i", "scene", "-o", "output.usda"])

        self.assertEqual(exit_code, 1)
        self.assertIn("unsupported input format '(none)'", stderr.getvalue())


_GSPLAT_PLY_HEADER = """ply
format ascii 1.0
element vertex 1
property float x
property float y
property float z
property float f_dc_0
property float f_dc_1
property float f_dc_2
property float opacity
property float scale_0
property float scale_1
property float scale_2
property float rot_0
property float rot_1
property float rot_2
property float rot_3
end_header
0 0 0 0 0 0 1 0 0 0 1 0 0 0
"""

_PLAIN_PLY_HEADER = """ply
format ascii 1.0
element vertex 1
property float x
property float y
property float z
end_header
0 0 0
"""


class TestCliGaussianSplatPly(unittest.TestCase):

    def _run_with_fake_converter(self, input_path: Path, output_path: Path) -> tuple[int, str, str]:
        ok_status = types.SimpleNamespace(name="OK")
        cancelled_status = types.SimpleNamespace(name="CANCELLED")

        class _FakeOmniAssetConverter:
            def __init__(self, *args, **kwargs) -> None:
                self._status = ok_status

            def get_status(self):
                return self._status

            def get_detailed_error(self) -> str:
                return ""

            def cancel(self) -> None:
                self._status = cancelled_status

            async def __aenter__(self):
                return self

            async def __aexit__(self, exc_type, exc, tb) -> bool:
                return False

            @staticmethod
            def set_log_callback(callback) -> None:
                return None

            @staticmethod
            def shutdown() -> None:
                return None

        fake_module = types.ModuleType("asset_converter_native_bindings")
        fake_module.OmniAssetConverter = _FakeOmniAssetConverter
        fake_module.OmniConverterStatus = types.SimpleNamespace(OK=ok_status, CANCELLED=cancelled_status)

        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": fake_module}):
            with mock.patch.object(sys, "stdout", stdout), mock.patch.object(sys, "stderr", stderr):
                exit_code = cli.main(["-i", str(input_path), "-o", str(output_path)])
        return exit_code, stdout.getvalue(), stderr.getvalue()

    def test_rejects_gaussian_splat_ply(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "gsplat.ply"
            input_path.write_text(_GSPLAT_PLY_HEADER, encoding="utf-8")
            output_path = Path(temp_dir) / "out.usda"

            exit_code, stdout, stderr = self._run_with_fake_converter(input_path, output_path)

        self.assertEqual(exit_code, 1)
        self.assertIn("Error: gaussian-splat attributes detected and would be discarded", stderr)
        self.assertNotIn("Successfully converted", stdout)
        self.assertNotIn("usd-convert-gsplat", stderr)

    def test_no_error_for_plain_point_cloud_ply(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "points.ply"
            input_path.write_text(_PLAIN_PLY_HEADER, encoding="utf-8")
            output_path = Path(temp_dir) / "out.usda"

            exit_code, stdout, stderr = self._run_with_fake_converter(input_path, output_path)

        self.assertEqual(exit_code, 0)
        self.assertNotIn("gaussian-splat", stderr)
        self.assertIn("Successfully converted:", stdout)


class TestCliStdio(unittest.TestCase):

    def test_main_returns_zero_when_success_print_has_non_ascii_path(self) -> None:
        ok_status = types.SimpleNamespace(name="OK")
        cancelled_status = types.SimpleNamespace(name="CANCELLED")

        class _FakeOmniAssetConverter:
            def __init__(self, *args, **kwargs) -> None:
                self._status = ok_status

            def get_status(self):
                return self._status

            def get_detailed_error(self) -> str:
                return ""

            def cancel(self) -> None:
                self._status = cancelled_status

            async def __aenter__(self):
                return self

            async def __aexit__(self, exc_type, exc, tb) -> bool:
                return False

            @staticmethod
            def set_log_callback(callback) -> None:
                return None

            @staticmethod
            def shutdown() -> None:
                return None

        fake_module = types.ModuleType("asset_converter_native_bindings")
        fake_module.OmniAssetConverter = _FakeOmniAssetConverter
        fake_module.OmniConverterStatus = types.SimpleNamespace(OK=ok_status, CANCELLED=cancelled_status)

        stdout_buffer = io.BytesIO()
        stderr_buffer = io.BytesIO()
        stdout = io.TextIOWrapper(stdout_buffer, encoding="cp1252", errors="strict", newline="\n")
        stderr = io.TextIOWrapper(stderr_buffer, encoding="cp1252", errors="strict", newline="\n")

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "cube.OBJ"
            input_path.write_text("# stub\n", encoding="utf-8")
            output_path = Path(temp_dir) / "out dir éñ" / "cube 你好.USDA"

            with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": fake_module}):
                with mock.patch.object(sys, "stdout", stdout), mock.patch.object(sys, "stderr", stderr):
                    exit_code = cli.main(["-i", str(input_path), "-o", str(output_path)])
                    stdout.flush()
                    stderr.flush()

            self.assertEqual(exit_code, 0)
            self.assertTrue(output_path.parent.is_dir())

        self.assertIn(b"Successfully converted:", stdout_buffer.getvalue())
        self.assertIn(b"\\u4f60\\u597d", stdout_buffer.getvalue())

    def test_main_safely_prints_non_ascii_stderr_messages(self) -> None:
        failed_status = types.SimpleNamespace(name="失败")
        cancelled_status = types.SimpleNamespace(name="CANCELLED")

        class _FakeOmniAssetConverter:
            log_callback = None

            def __init__(self, *args, progress_callback=None, **kwargs) -> None:
                self._progress_callback = progress_callback
                self._status = failed_status

            def get_status(self):
                return self._status

            def get_detailed_error(self) -> str:
                return "详细错误"

            def cancel(self) -> None:
                self._status = cancelled_status

            async def __aenter__(self):
                _FakeOmniAssetConverter.log_callback("原生日志")
                self._progress_callback(1, 2)
                return self

            async def __aexit__(self, exc_type, exc, tb) -> bool:
                return False

            @classmethod
            def set_log_callback(cls, callback) -> None:
                cls.log_callback = callback

            @staticmethod
            def shutdown() -> None:
                return None

        fake_module = types.ModuleType("asset_converter_native_bindings")
        fake_module.OmniAssetConverter = _FakeOmniAssetConverter
        fake_module.OmniConverterStatus = types.SimpleNamespace(
            OK=types.SimpleNamespace(name="OK"),
            CANCELLED=cancelled_status,
        )

        stderr_buffer = io.BytesIO()
        stderr = io.TextIOWrapper(stderr_buffer, encoding="cp1252", errors="strict", newline="\n")

        with tempfile.TemporaryDirectory() as temp_dir:
            input_path = Path(temp_dir) / "cube.obj"
            input_path.write_text("# stub\n", encoding="utf-8")
            output_path = Path(temp_dir) / "cube.usda"

            with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": fake_module}):
                with mock.patch.object(sys, "stderr", stderr):
                    cli._print_debug(types.SimpleNamespace(debug=True), "调试")
                    exit_code = cli.main(["-i", str(input_path), "-o", str(output_path), "--progress"])
                    stderr.flush()

        self.assertEqual(exit_code, 1)
        output = stderr_buffer.getvalue()
        self.assertIn(b"Debug: \\u8c03\\u8bd5", output)
        self.assertIn(b"\\u539f\\u751f\\u65e5\\u5fd7", output)
        self.assertIn(b"Progress: 1/2", output)
        self.assertIn(b"status \\u5931\\u8d25", output)
        self.assertIn(b"\\u8be6\\u7ec6\\u9519\\u8bef", output)


if __name__ == "__main__":
    unittest.main()
