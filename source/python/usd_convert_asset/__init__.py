# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
try:
    from ._version import __version__, get_version
except ImportError:
    __version__ = "0.0.0"

    def get_version():
        """Return package version."""
        return __version__


__all__ = [
    "__version__",
    "get_version",
]
