# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import unittest
from unittest import mock

from asset_converter_native_bindings import _bootstrap


class TestWindowsConverterLibraryLoad(unittest.TestCase):
    _LIBRARY_PATH = r"C:\package\libs\usd_convert_asset.dll"
    _BUNDLED_PXR_DIRS = (r"C:\package\libs\python\pxr", r"C:\package\libs\lib\python\pxr")

    @staticmethod
    def _windows_error(winerror):
        error = OSError(f"Windows loader error {winerror}")
        error.winerror = winerror
        return error

    @mock.patch.object(_bootstrap, "check_pxr_conflict", return_value=False)
    def test_success_without_pxr_conflict(self, check_pxr_conflict):
        loader = mock.Mock()

        result = _bootstrap.load_windows_converter_library(self._LIBRARY_PATH, loader, *self._BUNDLED_PXR_DIRS)

        self.assertFalse(result)
        check_pxr_conflict.assert_called_once_with(*self._BUNDLED_PXR_DIRS)
        loader.assert_called_once_with(self._LIBRARY_PATH)

    @mock.patch.object(_bootstrap, "check_pxr_conflict", return_value=True)
    def test_success_with_pxr_conflict(self, check_pxr_conflict):
        loader = mock.Mock()

        result = _bootstrap.load_windows_converter_library(self._LIBRARY_PATH, loader, *self._BUNDLED_PXR_DIRS)

        self.assertTrue(result)
        check_pxr_conflict.assert_called_once_with(*self._BUNDLED_PXR_DIRS)
        loader.assert_called_once_with(self._LIBRARY_PATH)

    @mock.patch.object(_bootstrap, "check_pxr_conflict", return_value=True)
    def test_winerror_127_with_pxr_conflict_preserves_oserror_with_context(self, _):
        loader_error = self._windows_error(127)
        loader = mock.Mock(side_effect=loader_error)

        with self.assertRaises(OSError) as context:
            _bootstrap.load_windows_converter_library(self._LIBRARY_PATH, loader, *self._BUNDLED_PXR_DIRS)

        self.assertIs(context.exception, loader_error)
        self.assertEqual(len(context.exception.__notes__), 1)
        self.assertIn("may conflict", context.exception.__notes__[0])
        self.assertIn("another dependency", context.exception.__notes__[0])

    @mock.patch.object(_bootstrap, "check_pxr_conflict", return_value=False)
    def test_winerror_127_without_pxr_conflict_preserves_oserror(self, _):
        loader_error = self._windows_error(127)

        with self.assertRaises(OSError) as context:
            _bootstrap.load_windows_converter_library(
                self._LIBRARY_PATH, mock.Mock(side_effect=loader_error), *self._BUNDLED_PXR_DIRS
            )

        self.assertIs(context.exception, loader_error)

    @mock.patch.object(_bootstrap, "check_pxr_conflict", return_value=True)
    def test_other_loader_error_with_pxr_conflict_preserves_oserror(self, _):
        loader_error = self._windows_error(126)

        with self.assertRaises(OSError) as context:
            _bootstrap.load_windows_converter_library(
                self._LIBRARY_PATH, mock.Mock(side_effect=loader_error), *self._BUNDLED_PXR_DIRS
            )

        self.assertIs(context.exception, loader_error)


if __name__ == "__main__":
    unittest.main()
