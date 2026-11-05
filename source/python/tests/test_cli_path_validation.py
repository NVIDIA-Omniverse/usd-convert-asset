# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Unit tests for CLI output-path validation (no native runtime required)."""

from __future__ import annotations

import errno
import io
import os
import subprocess
import sys
import tempfile
import types
import unittest
from contextlib import nullcontext
from pathlib import Path
from unittest import mock

from usd_convert_asset import cli, usdz


class TestCliPathValidation(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows device names")
    def test_device_names_rejected_before_filesystem_access(self):
        names = (
            "PRN.usd",
            "AUX.usd",
            "COM1.usd",
            "LPT1.usd",
            "CON.usd",
            "NUL.usd",
            "CONIN$.usd",
            "CONOUT$.usd",
            "cOm9.usda",
            "LPT³.usdz",
            "COM¹.usd",
            "COM².usd",
            "PRN .usd",
            "AUX:stream",
        )
        for name in names:
            for output in (Path("output") / name, Path("output") / name / "scene.usd"):
                with (
                    self.subTest(output=output),
                    mock.patch.object(Path, "stat", side_effect=AssertionError("filesystem accessed")),
                    mock.patch.object(os, "open", side_effect=AssertionError("device opened")),
                ):
                    with self.assertRaisesRegex(OSError, "reserved Windows device name"):
                        usdz.preflight_output(output)

    @unittest.skipUnless(os.name == "nt", "Windows device names")
    def test_similar_valid_names_pass_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ("COM0.usd", "COM10.usd", "LPT10.usd", "PRN_model.usd", "AUXiliary.usd"):
                with self.subTest(name=name):
                    usdz.preflight_output(Path(directory) / name)
            self.assertEqual(list(Path(directory).iterdir()), [])

    @unittest.skipUnless(os.name == "nt", "Windows device names")
    def test_device_names_exit_cli_cleanly(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.obj"
            source.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
            env = dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parents[1]))
            for name in ("PRN", "AUX", "COM1", "LPT1"):
                for suffix in (".usd", ".usdz"):
                    with self.subTest(name=name, suffix=suffix):
                        result = subprocess.run(
                            [
                                sys.executable,
                                "-m",
                                "usd_convert_asset",
                                "-i",
                                str(source),
                                "-o",
                                str(Path(directory) / (name + suffix)),
                            ],
                            capture_output=True,
                            text=True,
                            timeout=5,
                            env=env,
                        )
                        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                        self.assertIn("reserved Windows device name", result.stderr)
                        self.assertNotIn("Traceback", result.stderr)
            self.assertEqual(list(Path(directory).iterdir()), [source])

    def test_non_windows_allows_device_names(self):
        output = Path("PRN.usd")
        with mock.patch.object(usdz.os, "name", "posix"):
            usdz._validate_output_path(output)

    def test_preflight_creates_missing_directories_and_preserves_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "nested" / "scene.usda"
            usdz.preflight_output(output)
            self.assertEqual(list(output.parent.iterdir()), [])
            output.write_bytes(b"original")
            before = output.stat()
            usdz.preflight_output(output)
            self.assertEqual(output.read_bytes(), b"original")
            self.assertEqual(output.stat().st_mtime_ns, before.st_mtime_ns)
            self.assertEqual(list(output.parent.iterdir()), [output])

    def test_preflight_failures_do_not_load_native_converter(self):
        for failure in ("readonly", "directory", "blocked_parent", "write_denied"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                base = Path(directory)
                source = base / "input.obj"
                source.write_text("v 0 0 0\n")
                output = base / "out.usda"
                if failure == "readonly":
                    output.write_bytes(b"original")
                    output.chmod(0o444)
                elif failure == "directory":
                    output.mkdir()
                elif failure == "blocked_parent":
                    blocked = base / "blocked"
                    blocked.write_bytes(b"original")
                    output = blocked / "out.usda"
                deny = mock.patch.object(usdz.os, "open", side_effect=PermissionError("denied"))
                stderr = io.StringIO()
                try:
                    with (
                        mock.patch.dict(sys.modules, {"asset_converter_native_bindings": None}),
                        mock.patch.object(cli.sys, "stderr", stderr),
                        deny if failure == "write_denied" else nullcontext(),
                    ):
                        self.assertEqual(cli.main(["-i", str(source), "-o", str(output)]), 1)
                    self.assertIn("cannot write output", stderr.getvalue())
                    self.assertNotIn("import", stderr.getvalue())
                    if failure == "readonly":
                        self.assertEqual(output.read_bytes(), b"original")
                    self.assertFalse(list(base.glob(".uca-*")))
                finally:
                    if failure == "readonly":
                        output.chmod(0o600)

    def test_preflight_cleans_probe_after_write_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "out.usda"
            original_fdopen = os.fdopen

            def fail_write(descriptor, mode):
                stream = original_fdopen(descriptor, mode)
                stream.close()
                raise OSError("disk full")

            with mock.patch.object(usdz.os, "fdopen", side_effect=fail_write):
                with self.assertRaisesRegex(OSError, "disk full"):
                    usdz.preflight_output(output)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_preflight_rejects_filesystem_limit_before_creating_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "nested" / "scene.usda"
            with mock.patch.object(usdz, "_native_path_limit", return_value=usdz._path_length(output)):
                with self.assertRaisesRegex(OSError, "filesystem path limit"):
                    usdz.preflight_output(output)
            self.assertFalse(output.parent.exists())

    def test_accepts_normal_paths(self):
        self.assertIsNone(cli._windows_trailing_space_or_dot_error(r"C:\out\file.usda"))
        self.assertIsNone(cli._windows_trailing_space_or_dot_error("out/nested/file.usda"))

    @unittest.skipUnless(os.name == "nt", "Windows path-component rules only")
    def test_rejects_trailing_space_or_dot_component(self):
        self.assertIsNotNone(cli._windows_trailing_space_or_dot_error(r"trail \out.usda"))
        self.assertIsNotNone(cli._windows_trailing_space_or_dot_error(r"trail.\out.usda"))

    @unittest.skipUnless(os.name != "nt", "non-Windows skips Windows-only validation")
    def test_non_windows_allows_trailing_space_component(self):
        self.assertIsNone(cli._windows_trailing_space_or_dot_error("trail /out.usda"))

    def test_windows_max_path_validation_uses_absolute_path_length(self):
        with (
            mock.patch.object(cli.os, "name", "nt"),
            mock.patch.object(cli.os.path, "abspath", side_effect=lambda path: path),
        ):
            self.assertIsNone(cli._windows_max_path_error("x" * 259))
            self.assertEqual(
                cli._windows_max_path_error("x" * 260),
                "Error: output path exceeds the native converter Windows limit of 259 characters (got 260)",
            )
            self.assertIn("input path", cli._windows_max_path_error("x" * 260, "input"))
            self.assertIsNotNone(cli._windows_max_path_error("x" * 258 + "\U0001f600"))

    def test_non_windows_allows_long_output_path(self):
        with mock.patch.object(cli.os, "name", "posix"):
            self.assertIsNone(cli._windows_max_path_error("x" * 261))

    @unittest.skipUnless(sys.platform == "win32", "Windows native path limit only")
    def test_main_rejects_long_output_before_loading_native_bindings(self):
        input_path = os.path.abspath("cube.obj")
        output_dir = os.path.join(os.path.dirname(input_path), "L")
        stem_length = 270 - len(output_dir) - len(os.sep) - len(".usd")
        output_path = os.path.join(output_dir, "z" * stem_length + ".usd")
        stderr = io.StringIO()

        self.assertEqual(len(output_path), 270)
        with mock.patch.dict(sys.modules, {"asset_converter_native_bindings": None}):
            with mock.patch.object(cli.sys, "stderr", stderr):
                exit_code = cli.main(["-i", input_path, "-o", output_path])

        self.assertEqual(exit_code, 1)
        error = stderr.getvalue()
        self.assertIn("Error: output path exceeds the native converter Windows limit", error)
        self.assertNotIn("Traceback", error)
        self.assertEqual(len(error.splitlines()), 1)

    @unittest.skipUnless(sys.platform == "win32", "Windows native path limit only")
    def test_rejects_long_input_before_loading_native_bindings(self):
        stderr = io.StringIO()
        with (
            mock.patch.dict(sys.modules, {"asset_converter_native_bindings": None}),
            mock.patch.object(cli.sys, "stderr", stderr),
        ):
            self.assertEqual(cli.main(["-i", "x" * 256 + ".obj", "-o", "out.usda"]), 1)
        self.assertIn("input path exceeds", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())

    @unittest.skipUnless(sys.platform == "win32", "Windows staging paths")
    def test_long_output_staging_and_packaging_remain_within_limit(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            for length in (232, 233, 242, 259):
                with self.subTest(length=length):
                    base = Path(temp_dir)
                    # Split the directory suffix so each component stays below NAME_MAX.
                    extra = length - len(str(base / "o.usdz")) - 2
                    parent = base / ("a" * (extra // 2)) / ("b" * (extra - extra // 2))
                    output = parent / "o.usdz"
                    self.assertEqual(len(str(output)), length)
                    parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(b"previous")
                    with usdz.temporary_staging_directory(output) as staging:
                        layer = usdz.staging_layer_path(staging, output)
                        self.assertLess(len(str(layer)) + 8, 260)
                        with usdz.temporary_staging_directory(staging / output.name) as packaging:
                            self.assertLess(len(str(packaging / output.name)) + 8, 260)
                        (staging / output.name).write_bytes(b"converted")
                        usdz.commit_staged_files(staging, output, archive=True)
                    self.assertEqual(output.read_bytes(), b"converted")
                    self.assertFalse(staging.exists())

    def test_packaging_error_preserves_underlying_cause_and_existing_output(self):
        sdf = types.SimpleNamespace(AssetPath=lambda path: path)
        utils = types.SimpleNamespace(CreateNewUsdzPackage=mock.Mock(side_effect=OSError("path too long")))
        pxr = types.ModuleType("pxr")
        pxr.Sdf, pxr.UsdUtils = sdf, utils
        with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(sys.modules, {"pxr": pxr}):
            output = Path(temp_dir) / "model.usdz"
            output.write_bytes(b"previous")
            with self.assertRaisesRegex(usdz.UsdzError, "path too long") as caught:
                usdz.package_usdz(Path(temp_dir) / "model.usdc", output)
            self.assertNotIn("--ignore-materials", str(caught.exception))
            self.assertEqual(output.read_bytes(), b"previous")

    def test_staging_reserves_space_for_native_temporary_files_on_posix(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / ("a" * 50) / ("b" * 50)
            output = parent / "o.usda"
            limit = len(str(output)) + 20
            with mock.patch.object(usdz, "_native_path_limit", return_value=limit):
                with usdz.temporary_staging_directory(output) as staging:
                    self.assertLess(len(str(staging / ("x" * 64))) + 32, limit)
                    self.assertEqual(staging.stat().st_dev, parent.stat().st_dev)
            self.assertFalse(staging.exists())

    def test_deep_output_uses_temp_directory_without_accessing_ancestors(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as fallback:
            parent = Path(directory) / ("a" * 100) / ("b" * 80)
            output = parent / "o.usda"
            parent.mkdir(parents=True)
            mkdir = Path.mkdir

            def create(path, *args, **kwargs):
                if path.name.startswith(".uca-"):
                    self.assertEqual(path.parent, Path(fallback))
                return mkdir(path, *args, **kwargs)

            with (
                mock.patch.object(usdz, "_native_path_limit", return_value=260),
                mock.patch.object(usdz.tempfile, "gettempdir", return_value=fallback),
                mock.patch.object(Path, "mkdir", create),
            ):
                with usdz.temporary_staging_directory(output) as staging:
                    self.assertEqual(staging.parent, Path(fallback))
                    root = usdz.staging_output_path(staging, output)
                    root.write_bytes(b"converted")
                    usdz.commit_staged_files(staging, output, root_path=root)
                self.assertEqual(output.read_bytes(), b"converted")
                self.assertEqual(list(Path(fallback).iterdir()), [])

    def test_long_filename_uses_short_working_name_and_short_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            filename_length = min(240, 259 - usdz._path_length(Path(directory)) - 1)
            output = Path(directory) / ("n" * (filename_length - 5) + ".usda")
            output.write_bytes(b"original")
            with mock.patch.object(usdz, "_native_path_limit", return_value=260):
                with usdz.temporary_staging_directory(output) as staging:
                    root = usdz.staging_output_path(staging, output)
                    self.assertEqual(root.name, "layer.usda")
                    self.assertLess(usdz._path_length(root) + 32, 260)
                    root.write_bytes(b"converted")
                    usdz.commit_staged_files(staging, output, root_path=root)
                self.assertEqual(output.read_bytes(), b"converted")
                self.assertEqual(list(Path(directory).iterdir()), [output])

    def test_cross_volume_install_and_rollback_use_destination_siblings(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as workspace:
            output = Path(directory) / "scene.usda"
            texture = Path(directory) / "texture.png"
            output.write_bytes(b"original layer")
            texture.write_bytes(b"original texture")
            staging = Path(workspace)
            root = staging / "working.usda"
            root.write_bytes(b"converted layer")
            (staging / texture.name).write_bytes(b"converted texture")
            replace = Path.replace

            def cross_volume(source, target):
                if source.is_relative_to(staging):
                    raise OSError(errno.EXDEV, "different volume")
                if target == output:
                    raise PermissionError("root commit denied")
                self.assertTrue(source.parent.samefile(target.absolute().parent))
                return replace(source, target)

            with mock.patch.object(Path, "replace", cross_volume):
                with self.assertRaisesRegex(PermissionError, "root commit denied"):
                    usdz.commit_staged_files(staging, output, root_path=root)
            self.assertEqual(output.read_bytes(), b"original layer")
            self.assertEqual(texture.read_bytes(), b"original texture")
            self.assertEqual(set(Path(directory).iterdir()), {output, texture})

    def test_copy_failure_preserves_output_and_cleans_sibling(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "scene.usda"
            output.write_bytes(b"original")
            source = Path(directory) / "working.usda"
            source.write_bytes(b"converted")
            with (
                mock.patch.object(Path, "replace", side_effect=OSError(errno.EXDEV, "different volume")),
                mock.patch.object(usdz.shutil, "copyfileobj", side_effect=OSError("copy failed")),
            ):
                with self.assertRaisesRegex(OSError, "copy failed"):
                    usdz._replace_staged_file(source, output)
            self.assertEqual(output.read_bytes(), b"original")
            self.assertEqual(set(Path(directory).iterdir()), {output, source})


if __name__ == "__main__":
    unittest.main()
