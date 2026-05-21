# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Command-line interface for usd-convert-asset"""

from __future__ import annotations

import argparse
import asyncio
import faulthandler
import os
import sys


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert 3D assets to USD with usd-convert-asset.")
    parser.add_argument("-i", "--input", required=True, help="Input asset file path.")
    parser.add_argument("-o", "--output", required=True, help="Output USD file path (.usd, .usda, .usdc, or .usdz).")
    parser.add_argument("--ignore-materials", action="store_true", help="Do not export materials.")
    parser.add_argument("--ignore-animation", action="store_true", help="Do not export animation.")
    parser.add_argument("--single-mesh", action="store_true", help="Export a single mesh USD file.")
    parser.add_argument("--smooth-normals", action="store_true", help="Generate smooth normals for meshes.")
    parser.add_argument("--ignore-cameras", action="store_true", help="Do not export cameras.")
    parser.add_argument("--ignore-lights", action="store_true", help="Do not export lights.")
    parser.add_argument("--embed-textures", action="store_true", help="Embed textures in exported assets.")
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


def _print_log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def _print_progress(progress: int, total: int) -> None:
    print(f"Progress: {progress}/{total}", file=sys.stderr, flush=True)


def _print_debug(args: argparse.Namespace, message: str) -> None:
    if args.debug:
        print(f"Debug: {message}", file=sys.stderr, flush=True)


def _status_name(status: object) -> str:
    return getattr(status, "name", str(status))


async def _convert(args: argparse.Namespace) -> int:
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
        print(f"Error: input file not found: {input_path}", file=sys.stderr)
        return 1

    output_dir = os.path.dirname(output_path)
    if output_dir:
        os.makedirs(output_dir, exist_ok=True)

    assetconverter.OmniAssetConverter.set_log_callback(_print_log)
    _print_debug(args, "native log callback registered")

    try:
        _print_debug(args, "creating native conversion future")
        async with assetconverter.OmniAssetConverter(
            input_path,
            output_path,
            progress_callback=_print_progress if args.progress else None,
            ignore_material=args.ignore_materials,
            ignore_animation=args.ignore_animation,
            single_mesh=args.single_mesh,
            smooth_normals=args.smooth_normals,
            ignore_cameras=args.ignore_cameras,
            ignore_lights=args.ignore_lights,
            embed_textures=args.embed_textures,
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
        ) as converter:
            _print_debug(args, "native conversion future completed")
            status = converter.get_status()
            if status == assetconverter.OmniConverterStatus.OK:
                print(f"Successfully converted: {output_path}")
                return 0

            print(f"Error: conversion failed with status {_status_name(status)}", file=sys.stderr)
            detailed_error = converter.get_detailed_error()
            if detailed_error:
                print(detailed_error, file=sys.stderr)
            return 1
    finally:
        try:
            assetconverter.OmniAssetConverter.shutdown()
        finally:
            assetconverter.OmniAssetConverter.set_log_callback(None)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    return asyncio.run(_convert(args))


if __name__ == "__main__":
    raise SystemExit(main())
