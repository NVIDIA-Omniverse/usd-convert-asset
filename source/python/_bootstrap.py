# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Guard against loading a second OpenUSD (pxr) copy into the process."""

from __future__ import annotations

import logging
import sys
from pathlib import Path

_logger = logging.getLogger(__name__)


def check_pxr_conflict(*bundled_pxr_dirs: str) -> None:
    """Warn when a different OpenUSD (``pxr``) copy was already imported.

    Host environments (Kit, CI packman USD, usd-core) often import ``pxr`` before
    this package. Raising would break those workflows. When a foreign ``pxr`` is
    already in ``sys.modules``, Python keeps that copy instead of reloading the
    bundled build. Log a warning so mixed-USD setups are visible.
    """
    module = sys.modules.get("pxr")
    if module is None:
        return

    foreign_locations = _module_locations(module)
    if not foreign_locations:
        return

    bundled = {Path(bundled_pxr_dir).resolve() for bundled_pxr_dir in bundled_pxr_dirs}
    if any(location in bundled for location in foreign_locations):
        return

    origin = foreign_locations[0]
    bundled_display = ", ".join(str(path) for path in sorted(bundled)) or "<unknown>"
    _logger.warning(
        "A different OpenUSD (pxr) build was already imported from %s before "
        "usd-convert-asset loaded. The host pxr bindings will remain in use rather "
        "than reloading the bundled OpenUSD at %s. Mixing USD builds in one process "
        "can cause ABI and singleton conflicts; prefer importing usd_convert_asset "
        "first, or run the converter in a separate process/venv when mixing "
        "unrelated USD builds.",
        origin,
        bundled_display,
    )


def _module_locations(module: object) -> tuple[Path, ...]:
    """Best-effort resolved directories a module was loaded from."""
    file_attr = getattr(module, "__file__", None)
    if file_attr:
        return (Path(file_attr).resolve().parent,)
    # Namespace packages expose no __file__ but carry __path__ search dirs.
    return tuple(Path(entry).resolve() for entry in getattr(module, "__path__", []))
