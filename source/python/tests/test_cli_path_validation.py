# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Unit tests for CLI output-path validation (no native runtime required)."""

from __future__ import annotations

import os
import unittest

from usd_convert_asset.cli import _windows_trailing_space_or_dot_error


class TestCliPathValidation(unittest.TestCase):
    def test_accepts_normal_paths(self):
        self.assertIsNone(_windows_trailing_space_or_dot_error(r"C:\out\file.usda"))
        self.assertIsNone(_windows_trailing_space_or_dot_error("out/nested/file.usda"))

    @unittest.skipUnless(os.name == "nt", "Windows path-component rules only")
    def test_rejects_trailing_space_or_dot_component(self):
        self.assertIsNotNone(_windows_trailing_space_or_dot_error(r"trail \out.usda"))
        self.assertIsNotNone(_windows_trailing_space_or_dot_error(r"trail.\out.usda"))

    @unittest.skipUnless(os.name != "nt", "non-Windows skips Windows-only validation")
    def test_non_windows_allows_trailing_space_component(self):
        self.assertIsNone(_windows_trailing_space_or_dot_error("trail /out.usda"))


if __name__ == "__main__":
    unittest.main()
