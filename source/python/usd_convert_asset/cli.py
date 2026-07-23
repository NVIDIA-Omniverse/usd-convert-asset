# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Command-line interface for usd-convert-asset"""

from __future__ import annotations

import argparse
import asyncio
import faulthandler
import os
import signal
import sys
import threading
from typing import Any, TextIO

_SUPPORTED_INPUT_EXTENSIONS = (".fbx", ".obj", ".gltf", ".glb", ".stl", ".ply")
_SUPPORTED_OUTPUT_EXTENSIONS = (".usd", ".usda", ".usdc")

# Windows STATUS_CONTROL_C_EXIT. POSIX uses conventional 128+SIGINT.
_INTERRUPT_EXIT_CODE = 0xC000013A if sys.platform == "win32" else 130

_interrupt_requested = False
_interrupt_lock = threading.Lock()
_installed_signal_handlers: dict[signal.Signals, Any] = {}
_console_ctrl_handler_ref: Any = None
_console_ctrl_handler_installed = False


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert 3D assets to USD with usd-convert-asset.")
    parser.add_argument("-i", "--input", required=True, help="Input asset file path.")
    parser.add_argument("-o", "--output", required=True, help="Output USD file path (.usd, .usda, or .usdc).")
    parser.add_argument("--ignore-materials", action="store_true", help="Do not export materials.")
    parser.add_argument("--ignore-animation", action="store_true", help="Do not export animation.")
    parser.add_argument("--single-mesh", action="store_true", help="Export a single mesh USD file.")
    parser.add_argument("--smooth-normals", action="store_true", help="Generate smooth normals for meshes.")
    parser.add_argument("--ignore-cameras", action="store_true", help="Do not export cameras.")
    parser.add_argument("--ignore-lights", action="store_true", help="Do not export lights.")
    parser.add_argument("--fbx-y-up", action="store_true", help="Convert imported FBX stage to Y-up.")
    parser.add_argument("--fbx-z-up", action="store_true", help="Convert imported FBX stage to Z-up.")
    parser.add_argument("--keep-all-materials", action="store_true", help="Keep materials not referenced by meshes.")
    parser.add_argument("--merge-all-meshes", action="store_true", help="Merge meshes into one mesh.")
    parser.add_argument(
        "--double-precision-xform",
        action="store_true",
        help="Use double precision for USD transform ops.",
    )
    parser.add_argument("--ignore-pivots", action="store_true", help="Do not import pivots from source assets.")
    parser.add_argument("--disable-instancing", action="store_true", help="Disable scene instancing in USD export.")
    parser.add_argument("--export-hidden-props", action="store_true", help="Export hidden props.")
    parser.add_argument("--baking-scales", action="store_true", help="Bake FBX scales into mesh data.")
    parser.add_argument("--ignore-flip-rotations", action="store_true", help="Filter animation flip rotations.")
    parser.add_argument("--ignore-unbound-bones", action="store_true", help="Filter unbound FBX bones.")
    parser.add_argument("--export-embedded-gltf", action="store_true", help="Export embedded glTF data.")
    stage_up_group = parser.add_mutually_exclusive_group()
    stage_up_group.add_argument("--stage-up-y", action="store_true", help="Override output stage up-axis to Y.")
    stage_up_group.add_argument("--stage-up-z", action="store_true", help="Override output stage up-axis to Z.")
    parser.add_argument("--progress", action="store_true", help="Print conversion progress to stderr.")
    parser.add_argument("--debug", action="store_true", help="Print detailed CLI and native conversion debug logs.")
    return parser.parse_args(argv)


def _validate_formats(args: argparse.Namespace) -> bool:
    input_extension = os.path.splitext(args.input)[1].lower()
    if input_extension not in _SUPPORTED_INPUT_EXTENSIONS:
        supported = ", ".join(_SUPPORTED_INPUT_EXTENSIONS)
        _safe_print(
            f"Error: unsupported input format '{input_extension or '(none)'}'; supported formats: {supported}",
            file=sys.stderr,
        )
        return False

    output_extension = os.path.splitext(args.output)[1].lower()
    if output_extension not in _SUPPORTED_OUTPUT_EXTENSIONS:
        supported = ", ".join(_SUPPORTED_OUTPUT_EXTENSIONS)
        _safe_print(
            f"Error: unsupported output format '{output_extension or '(none)'}'; supported formats: {supported}",
            file=sys.stderr,
        )
        return False

    return True


def _ply_header_property_names(path: str) -> set[str] | None:
    """Return PLY header property names, or None if the file is not a readable PLY header."""
    property_names: set[str] = set()
    try:
        with open(path, "rb") as handle:
            first_line = handle.readline()
            if first_line.strip().lower() != b"ply":
                return None

            for raw_line in handle:
                line = raw_line.decode("ascii", errors="ignore").strip()
                if not line:
                    continue
                lower = line.lower()
                if lower == "end_header":
                    return property_names
                tokens = lower.split()
                if len(tokens) >= 3 and tokens[0] == "property":
                    # property <type> <name>  OR  property list <count_type> <type> <name>
                    property_names.add(tokens[-1])
    except OSError:
        return None

    return None


def _is_gaussian_splat_ply(path: str) -> bool:
    """True when PLY header declares the standard 3DGS attribute signature."""
    # Assimp drops these; fail-fast so agents/users do not treat exit 0 as success.
    markers = frozenset({"f_dc_0", "opacity", "scale_0", "rot_0"})
    if os.path.splitext(path)[1].lower() != ".ply":
        return False
    property_names = _ply_header_property_names(path)
    if property_names is None:
        return False
    return markers.issubset(property_names)


def _encode_for_stream(text: str, stream: TextIO) -> str:
    """Make text printable on legacy Windows consoles (e.g. cp1252).

    Paths and native log lines can contain characters outside the console
    codepage. Encoding with backslashreplace avoids UnicodeEncodeError while
    keeping unencodable code points recoverable (e.g. \\u4f60\\u597d).
    """
    encoding = getattr(stream, "encoding", None) or "utf-8"
    return text.encode(encoding, errors="backslashreplace").decode(encoding)


def _safe_print(message: str, *, file: TextIO | None = None, flush: bool = False) -> None:
    stream = sys.stdout if file is None else file
    print(_encode_for_stream(message, stream), file=stream, flush=flush)


def _print_log(message: str) -> None:
    _safe_print(message, file=sys.stderr, flush=True)


def _print_progress(progress: int, total: int) -> None:
    _safe_print(f"Progress: {progress}/{total}", file=sys.stderr, flush=True)


def _print_debug(args: argparse.Namespace, message: str) -> None:
    if args.debug:
        _safe_print(f"Debug: {message}", file=sys.stderr, flush=True)


def _status_name(status: object) -> str:
    return getattr(status, "name", str(status))


def _request_interrupt() -> None:
    """Mark interrupt requested. Safe from console-ctrl / signal threads.

    Does not call converter.cancel(); that runs only on the event-loop thread via
    _watch_for_interrupt to avoid concurrent native future access.
    """
    global _interrupt_requested
    with _interrupt_lock:
        _interrupt_requested = True


def _install_posix_signal_handlers() -> None:
    def _handler(signum: int, _frame: object | None) -> None:
        _request_interrupt()

    for sig in (signal.SIGINT, signal.SIGTERM):
        if sig in _installed_signal_handlers:
            continue
        try:
            previous = signal.signal(sig, _handler)
        except (ValueError, OSError):
            continue
        _installed_signal_handlers[sig] = previous


def _install_windows_console_ctrl_handler() -> None:
    """Register SetConsoleCtrlHandler so CTRL_BREAK cancels instead of ExitProcess."""
    global _console_ctrl_handler_ref, _console_ctrl_handler_installed
    if _console_ctrl_handler_installed:
        return

    import ctypes
    from ctypes import wintypes

    CTRL_C_EVENT = 0
    CTRL_BREAK_EVENT = 1
    CTRL_CLOSE_EVENT = 2

    handler_routine = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.DWORD)

    @handler_routine
    def _console_ctrl_handler(ctrl_type: int) -> bool:
        if ctrl_type in (CTRL_C_EVENT, CTRL_BREAK_EVENT, CTRL_CLOSE_EVENT):
            _request_interrupt()
            return True
        return False

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    if not kernel32.SetConsoleCtrlHandler(_console_ctrl_handler, True):
        raise ctypes.WinError(ctypes.get_last_error())

    # Keep callback alive; ctypes does not hold a reference.
    _console_ctrl_handler_ref = _console_ctrl_handler
    _console_ctrl_handler_installed = True


def _install_interrupt_handlers() -> None:
    if sys.platform == "win32":
        try:
            _install_windows_console_ctrl_handler()
        except OSError as exc:
            _safe_print(f"Warning: failed to install console ctrl handler: {exc}", file=sys.stderr)
        # Still map SIGINT for terminals that deliver it without console-ctrl.
        _install_posix_signal_handlers()
    else:
        _install_posix_signal_handlers()


def _uninstall_interrupt_handlers() -> None:
    global _console_ctrl_handler_ref, _console_ctrl_handler_installed

    for sig, previous in list(_installed_signal_handlers.items()):
        try:
            signal.signal(sig, previous)
        except (ValueError, OSError):
            pass
    _installed_signal_handlers.clear()

    if sys.platform == "win32" and _console_ctrl_handler_installed and _console_ctrl_handler_ref is not None:
        import ctypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.SetConsoleCtrlHandler(_console_ctrl_handler_ref, False)
        _console_ctrl_handler_ref = None
        _console_ctrl_handler_installed = False


async def _watch_for_interrupt(converter: Any) -> None:
    """Poll interrupt flag on the event loop; cancel native future from this thread only."""
    while True:
        with _interrupt_lock:
            requested = _interrupt_requested
        if requested:
            try:
                converter.cancel()
            except Exception:
                pass
            return
        await asyncio.sleep(0.05)


async def _convert(args: argparse.Namespace) -> int:
    global _interrupt_requested

    if args.debug:
        faulthandler.enable()
        os.environ["OMNI_ASSET_CONVERTER_DEBUG"] = "1"

    _print_debug(args, "importing asset_converter_native_bindings")
    import asset_converter_native_bindings as assetconverter

    input_path = os.path.abspath(args.input)
    output_path = os.path.abspath(args.output)
    _print_debug(args, f"input path: {input_path}")
    _print_debug(args, f"output path: {output_path}")

    if not os.path.isfile(input_path):
        _safe_print(f"Error: input file not found: {input_path}", file=sys.stderr)
        return 1

    if _is_gaussian_splat_ply(input_path):
        _safe_print(
            "Error: gaussian-splat attributes detected and would be discarded; " "use a gsplat-specific converter",
            file=sys.stderr,
            flush=True,
        )
        return 1

    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    assetconverter.OmniAssetConverter.set_log_callback(_print_log)
    _print_debug(args, "native log callback registered")

    with _interrupt_lock:
        _interrupt_requested = False

    _install_interrupt_handlers()
    watch_task: asyncio.Task[None] | None = None
    try:
        _print_debug(args, "creating native conversion future")
        converter = assetconverter.OmniAssetConverter(
            input_path,
            output_path,
            progress_callback=_print_progress if args.progress else None,
            ignore_material=args.ignore_materials,
            ignore_animation=args.ignore_animation,
            single_mesh=args.single_mesh,
            smooth_normals=args.smooth_normals,
            ignore_cameras=args.ignore_cameras,
            ignore_lights=args.ignore_lights,
            convert_fbx_to_y_up=args.fbx_y_up,
            convert_fbx_to_z_up=args.fbx_z_up,
            keep_all_materials=args.keep_all_materials,
            merge_all_meshes=args.merge_all_meshes,
            use_double_precision_to_usd_transform_op=args.double_precision_xform,
            ignore_pivots=args.ignore_pivots,
            disable_instancing=args.disable_instancing,
            export_hidden_props=args.export_hidden_props,
            baking_scales=args.baking_scales,
            ignore_flip_rotations=args.ignore_flip_rotations,
            ignore_unbound_bones=args.ignore_unbound_bones,
            export_embedded_gltf=args.export_embedded_gltf,
            convert_stage_up_y=args.stage_up_y,
            convert_stage_up_z=args.stage_up_z,
        )

        watch_task = asyncio.create_task(_watch_for_interrupt(converter))
        async with converter:
            _print_debug(args, "native conversion future completed")
            with _interrupt_lock:
                interrupted = _interrupt_requested
            status = converter.get_status()

            if interrupted or status == assetconverter.OmniConverterStatus.CANCELLED:
                _safe_print("Interrupted.", file=sys.stderr, flush=True)
                return _INTERRUPT_EXIT_CODE

            if status == assetconverter.OmniConverterStatus.OK:
                _safe_print(f"Successfully converted: {output_path}")
                return 0

            _safe_print(f"Error: conversion failed with status {_status_name(status)}", file=sys.stderr)
            detailed_error = converter.get_detailed_error()
            if detailed_error:
                _safe_print(detailed_error, file=sys.stderr)
            return 1
    finally:
        if watch_task is not None:
            watch_task.cancel()
            try:
                await watch_task
            except asyncio.CancelledError:
                pass
        _uninstall_interrupt_handlers()
        try:
            assetconverter.OmniAssetConverter.shutdown()
        finally:
            assetconverter.OmniAssetConverter.set_log_callback(None)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    if not _validate_formats(args):
        return 1
    try:
        return asyncio.run(_convert(args))
    except KeyboardInterrupt:
        _request_interrupt()
        _safe_print("Interrupted.", file=sys.stderr, flush=True)
        return _INTERRUPT_EXIT_CODE


if __name__ == "__main__":
    raise SystemExit(main())
