# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Guard against loading a second OpenUSD (pxr) copy into the process."""

from __future__ import annotations

import os
import sys
import warnings
from pathlib import Path

_SUPPRESS_PXR_CONFLICT_WARNING_ENV = "SUPPRESS_PXR_CONFLICT_WARNING"


class PxrConflictWarning(RuntimeWarning):
    """Warn that a different OpenUSD build is already loaded."""


def check_pxr_conflict(*bundled_pxr_dirs: str) -> bool:
    """Warn when a different OpenUSD (``pxr``) copy was already imported.

    Host environments (Kit, CI packman USD, usd-core) often import ``pxr`` before
    this package. Python keeps a foreign ``pxr`` in ``sys.modules`` instead of
    reloading the bundled build. Treating every conflict as an error would break
    compatible workflows. Report the conflict through Python's warning system.
    Hosts can suppress or escalate it through standard warning filters. The
    function returns whether a conflict was found when execution continues.
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
        warnings.warn(
            f"A different OpenUSD (pxr) build was already imported from {origin} before "
            "usd-convert-asset loaded. The host pxr bindings will remain in use rather "
            f"than reloading the bundled OpenUSD at {bundled_display}. Mixing USD builds in one process "
            "can cause ABI and singleton conflicts. Do not use the converter in this "
            "process. Run it in a separate process and virtual environment that does "
            "not load an unrelated USD build.",
            PxrConflictWarning,
            stacklevel=2,
        )
    return True


def load_windows_converter_library(library_path: str, loader, *bundled_pxr_dirs: str) -> bool:
    """Load the converter DLL and add context for a possible OpenUSD conflict."""
    has_pxr_conflict = check_pxr_conflict(*bundled_pxr_dirs)
    try:
        loader(library_path)
    except OSError as error:
        if has_pxr_conflict and getattr(error, "winerror", None) == 127:
            error.add_note(
                "A different OpenUSD (pxr) build is loaded and may conflict with "
                "the bundled OpenUSD DLLs. Windows error 127 can also indicate a "
                "missing export in another dependency. Inspect the original loader "
                "error and its dependencies. If OpenUSD is the cause, run the "
                "converter in a separate process and virtual environment."
            )
        raise

    return has_pxr_conflict


def _module_locations(module: object) -> tuple[Path, ...]:
    """Best-effort resolved directories a module was loaded from."""
    file_attr = getattr(module, "__file__", None)
    if file_attr:
        return (Path(file_attr).resolve().parent,)
    # Namespace packages expose no __file__ but carry __path__ search dirs.
    return tuple(Path(entry).resolve() for entry in getattr(module, "__path__", []))
