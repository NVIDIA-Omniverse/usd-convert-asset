# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
__all__ = [
    "__version__",
    "get_version",
]

__version__ = "9.0.10"


def get_version():
    """Return package version."""
    return __version__
