# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Unit tests for CLI interrupt / console-ctrl cancellation helpers (NVBUG 6493939)."""

from __future__ import annotations

import asyncio
import importlib.util
import os
import runpy
import signal
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

_CLI_PATH = Path(__file__).resolve().parents[1] / "usd_convert_asset" / "cli.py"


def _load_cli():
    spec = importlib.util.spec_from_file_location("usd_convert_asset_cli_under_test", _CLI_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class _FakeConverter:
    def __init__(self) -> None:
        self.cancelled = False
        self._future = object()

    def cancel(self) -> None:
        self.cancelled = True


class TestCliInterruptHelpers(unittest.TestCase):
    def setUp(self) -> None:
        self.cli = _load_cli()
        self.cli._interrupt_requested = False
        self.cli._success_committed = False

    def tearDown(self) -> None:
        self.cli._uninstall_interrupt_handlers()
        self.cli._interrupt_requested = False
        self.cli._success_committed = False

    def test_interrupt_exit_code_is_curated(self) -> None:
        if sys.platform == "win32":
            self.assertEqual(self.cli._INTERRUPT_EXIT_CODE, -1073741510)
        else:
            self.assertEqual(self.cli._INTERRUPT_EXIT_CODE, 130)

    @unittest.skipUnless(sys.platform == "win32", "Windows process exit code only")
    def test_interrupt_exit_code_reaches_subprocess_caller(self) -> None:
        result = subprocess.run(
            [sys.executable, "-c", f"raise SystemExit({self.cli._INTERRUPT_EXIT_CODE})"],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0xC000013A, result.stderr)

    def test_request_interrupt_sets_flag_only(self) -> None:
        converter = _FakeConverter()

        self.cli._request_interrupt()

        self.assertTrue(self.cli._interrupt_requested)
        self.assertFalse(converter.cancelled)

    def test_install_interrupt_handlers_is_idempotent(self) -> None:
        original_sigint = signal.getsignal(signal.SIGINT)
        original_sigterm = signal.getsignal(signal.SIGTERM)

        self.cli._install_interrupt_handlers()
        self.cli._install_interrupt_handlers()
        self.cli._uninstall_interrupt_handlers()

        # Double-install must still restore the pre-install handlers, not our own.
        self.assertIs(signal.getsignal(signal.SIGINT), original_sigint)
        self.assertIs(signal.getsignal(signal.SIGTERM), original_sigterm)

    @unittest.skipUnless(sys.platform == "win32", "Windows console-ctrl handler only")
    def test_windows_console_ctrl_handler_requests_interrupt(self) -> None:
        converter = _FakeConverter()
        self.cli._install_windows_console_ctrl_handler()

        handler = self.cli._console_ctrl_handler_ref
        self.assertIsNotNone(handler)
        # CTRL_BREAK_EVENT == 1
        self.assertTrue(handler(1))
        self.assertTrue(self.cli._interrupt_requested)
        # Cancel must not run on the console-ctrl thread.
        self.assertFalse(converter.cancelled)

    @unittest.skipUnless(sys.platform == "win32", "Windows console-ctrl handler only")
    def test_process_success_keeps_handler_until_immediate_exit(self) -> None:
        self.cli._install_interrupt_handlers()
        handler = self.cli._console_ctrl_handler_ref
        self.cli._success_committed = True

        self.cli._uninstall_interrupt_handlers(keep_windows_console_handler=True)
        self.assertTrue(self.cli._console_ctrl_handler_installed)
        self.assertIs(self.cli._console_ctrl_handler_ref, handler)
        self.assertTrue(handler(1))
        self.assertFalse(self.cli._interrupt_requested)

    def test_watch_for_interrupt_cancels_on_event_loop(self) -> None:
        converter = _FakeConverter()

        async def _run() -> None:
            watch = asyncio.create_task(self.cli._watch_for_interrupt(converter))
            self.cli._request_interrupt()
            await asyncio.wait_for(watch, timeout=1.0)

        asyncio.run(_run())
        self.assertTrue(converter.cancelled)

    def test_main_keyboard_interrupt_returns_curated_code(self) -> None:
        def _raise_interrupt(coro):
            coro.close()
            raise KeyboardInterrupt

        args = mock.Mock(input="scene.fbx", output="scene.usda")
        with mock.patch.object(self.cli, "_parse_args", return_value=args):
            with mock.patch.object(self.cli, "_validate_formats", return_value=True):
                with mock.patch.object(self.cli.asyncio, "run", side_effect=_raise_interrupt):
                    code = self.cli.main([])

        self.assertEqual(code, self.cli._INTERRUPT_EXIT_CODE)

    def test_main_preserves_host_console(self) -> None:
        with mock.patch.object(self.cli, "_main", return_value=0) as run:
            self.assertEqual(self.cli.main(["input.obj"]), 0)

        run.assert_called_once_with(["input.obj"], keep_windows_console_handler_after_success=False)

    @unittest.skipUnless(sys.platform == "win32", "Windows process exit only")
    def test_process_main_uses_immediate_exit(self) -> None:
        self.cli._success_committed = True
        with mock.patch.object(self.cli, "_main", return_value=0) as run:
            with mock.patch.object(self.cli.os, "_exit") as immediate_exit:
                self.assertEqual(self.cli.process_main(), 0)

        run.assert_called_once_with(None, keep_windows_console_handler_after_success=True)
        immediate_exit.assert_called_once_with(0)

    def test_python_module_entry_point_uses_process_main(self) -> None:
        with mock.patch("usd_convert_asset.cli.process_main", return_value=23) as process_main:
            with self.assertRaises(SystemExit) as raised:
                runpy.run_module("usd_convert_asset", run_name="__main__")

        self.assertEqual(raised.exception.code, 23)
        process_main.assert_called_once_with()


class TestOutputCommit(unittest.TestCase):
    def test_interrupted_output_is_not_installed(self):
        from usd_convert_asset import cli

        for suffix in (".usda", ".usdc", ".usdz"):
            for existing in (False, True):
                with self.subTest(suffix=suffix, existing=existing), tempfile.TemporaryDirectory() as directory:
                    source = Path(directory) / "input.obj"
                    source.write_text("input")
                    output = Path(directory) / ("output" + suffix)
                    if existing:
                        output.write_bytes(b"original")

                    class Converter:
                        def __init__(self, source, destination, **kwargs):
                            self.destination = Path(destination)

                        async def __aenter__(self):
                            self.destination.write_bytes(b"new layer")
                            if suffix != ".usdz":
                                cli._request_interrupt()
                            return self

                        async def __aexit__(self, *args):
                            pass

                        def get_status(self):
                            return "OK"

                        def cancel(self):
                            pass

                        @staticmethod
                        def set_log_callback(callback):
                            pass

                        @staticmethod
                        def shutdown():
                            pass

                    def package(layer, archive):
                        archive.write_bytes(b"new archive")
                        cli._request_interrupt()

                    native = types.SimpleNamespace(
                        OmniAssetConverter=Converter,
                        OmniConverterStatus=types.SimpleNamespace(OK="OK", CANCELLED="CANCELLED"),
                    )
                    with (
                        mock.patch.dict(sys.modules, {"asset_converter_native_bindings": native}),
                        mock.patch.object(cli.usdz, "package_usdz", side_effect=package),
                    ):
                        result = cli.main(["-i", str(source), "-o", str(output)])
                    self.assertEqual(result, cli._INTERRUPT_EXIT_CODE)
                    self.assertEqual(output.exists(), existing)
                    if existing:
                        self.assertEqual(output.read_bytes(), b"original")

    def test_commit_moves_sidecars_and_restores_on_failure(self):
        from usd_convert_asset import usdz

        for fail in (False, True):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "scene.usda"
                output.write_bytes(b"original layer")
                texture = Path(directory) / "textures/a.png"
                texture.parent.mkdir()
                texture.write_bytes(b"original texture")
                with usdz.temporary_staging_directory(output) as staging:
                    (staging / output.name).write_bytes(b"new layer")
                    (staging / "textures").mkdir()
                    (staging / "textures/a.png").write_bytes(b"new texture")
                    replace = usdz._replace_staged_file

                    def replace_file(source, target):
                        if fail and source == staging / output.name:
                            raise PermissionError("simulated output commit failure")
                        return replace(source, target)

                    with mock.patch.object(usdz, "_replace_staged_file", replace_file):
                        if fail:
                            with self.assertRaises(PermissionError):
                                usdz.commit_staged_files(staging, output)
                        else:
                            usdz.commit_staged_files(staging, output)
                self.assertEqual(output.read_bytes(), b"original layer" if fail else b"new layer")
                self.assertEqual(texture.read_bytes(), b"original texture" if fail else b"new texture")

    def test_staging_permission_error_is_not_retried(self):
        from usd_convert_asset import usdz

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "scene.usdz"
            mkdir = Path.mkdir
            attempts = []

            def create(path, *args, **kwargs):
                if path.name.startswith(".uca-"):
                    attempts.append(path)
                    raise PermissionError("denied")
                return mkdir(path, *args, **kwargs)

            with mock.patch.object(Path, "mkdir", create):
                with self.assertRaises(PermissionError):
                    with usdz.temporary_staging_directory(output):
                        self.fail("permission denial should prevent staging")
            self.assertEqual(len(attempts), 1)

    def test_readonly_destination_preserves_all_output_files(self):
        from usd_convert_asset import usdz

        for protected in ("scene.usda", "texture.png"):
            with self.subTest(protected=protected), tempfile.TemporaryDirectory() as directory:
                parent = Path(directory)
                output = parent / "scene.usda"
                texture = parent / "texture.png"
                output.write_bytes(b"old layer")
                texture.write_bytes(b"old texture")
                readonly = parent / protected
                readonly.chmod(0o444)
                before = readonly.stat()
                try:
                    with usdz.temporary_staging_directory(output) as staging:
                        (staging / output.name).write_bytes(b"new layer")
                        (staging / texture.name).write_bytes(b"new texture")
                        with self.assertRaisesRegex(PermissionError, "not writable"):
                            usdz.commit_staged_files(staging, output)
                    self.assertEqual(output.read_bytes(), b"old layer")
                    self.assertEqual(texture.read_bytes(), b"old texture")
                    self.assertEqual(readonly.stat().st_ino, before.st_ino)
                    self.assertEqual(readonly.stat().st_mode, before.st_mode)
                finally:
                    readonly.chmod(0o666)

    @unittest.skipUnless(os.name == "posix", "POSIX file permission modes")
    def test_replacement_keeps_existing_permissions(self):
        import stat

        from usd_convert_asset import usdz

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "scene.usda"
            output.write_bytes(b"old")
            output.chmod(0o600)
            with usdz.temporary_staging_directory(output) as staging:
                (staging / output.name).write_bytes(b"new")
                usdz.commit_staged_files(staging, output)
            self.assertEqual(output.read_bytes(), b"new")
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
