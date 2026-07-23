# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Unit tests for CLI interrupt / console-ctrl cancellation helpers (NVBUG 6493939)."""

from __future__ import annotations

import asyncio
import importlib.util
import signal
import sys
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

    def tearDown(self) -> None:
        self.cli._uninstall_interrupt_handlers()
        self.cli._interrupt_requested = False

    def test_interrupt_exit_code_is_curated(self) -> None:
        if sys.platform == "win32":
            self.assertEqual(self.cli._INTERRUPT_EXIT_CODE, 0xC000013A)
        else:
            self.assertEqual(self.cli._INTERRUPT_EXIT_CODE, 130)

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


if __name__ == "__main__":
    unittest.main()
