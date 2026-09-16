# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Guard against loading a second OpenUSD (pxr) copy into the process."""

from __future__ import annotations

import logging
import os
import sys
from pathlib import Path

_logger = logging.getLogger(__name__)
_SUPPRESS_PXR_CONFLICT_WARNING_ENV = "SUPPRESS_PXR_CONFLICT_WARNING"


def check_pxr_conflict(*bundled_pxr_dirs: str) -> bool:
    """Warn when a different OpenUSD (``pxr``) copy was already imported.

    Host environments (Kit, CI packman USD, usd-core) often import ``pxr`` before
    this package. Raising would break those workflows. When a foreign ``pxr`` is
    already in ``sys.modules``, Python keeps that copy instead of reloading the
    bundled build. Log a warning so mixed-USD setups are visible, and return
    whether a conflict was found.
    """
    module = sys.modules.get("pxr")
    if module is None:
        return False

    foreign_locations = _module_locations(module)
    if not foreign_locations:
        return False

    bundled = {Path(bundled_pxr_dir).resolve() for bundled_pxr_dir in bundled_pxr_dirs}
    if any(location in bundled for location in foreign_locations):
        return False

    origin = foreign_locations[0]
    bundled_display = ", ".join(str(path) for path in sorted(bundled)) or "<unknown>"
    suppress_warning = os.environ.get(_SUPPRESS_PXR_CONFLICT_WARNING_ENV, "").lower() in {"1", "true", "yes", "on"}
    if not suppress_warning:
        _logger.warning(
            "A different OpenUSD (pxr) build was already imported from %s before "
            "usd-convert-asset loaded. The host pxr bindings will remain in use rather "
            "than reloading the bundled OpenUSD at %s. Mixing USD builds in one process "
            "can cause ABI and singleton conflicts. Do not use the converter in this "
            "process. Run it in a separate process and virtual environment that does "
            "not load an unrelated USD build.",
            origin,
            bundled_display,
        )
    return True


def _module_locations(module: object) -> tuple[Path, ...]:
    """Best-effort resolved directories a module was loaded from."""
    file_attr = getattr(module, "__file__", None)
    if file_attr:
        return (Path(file_attr).resolve().parent,)
    # Namespace packages expose no __file__ but carry __path__ search dirs.
    return tuple(Path(entry).resolve() for entry in getattr(module, "__path__", []))
