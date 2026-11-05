# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import sys

# The native bindings are built against CPython 3.12 and use the host interpreter at runtime.
if sys.version_info[:2] != (3, 12):
    raise ImportError(
        "usd-convert-asset only supports Python 3.12; "
        f"found Python {sys.version_info.major}.{sys.version_info.minor}."
    )

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
