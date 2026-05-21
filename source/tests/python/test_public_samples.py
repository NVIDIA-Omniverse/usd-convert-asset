# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import os
import platform

import omni.repo.man
from async_test_case import AsyncTestCase, assetconverter, convert
from pxr import Gf, Usd


def find_files_with_extensions(folder, extensions):
    matching_files = []
    for subdir, _, files in os.walk(folder):
        for file in files:
            if file.lower().endswith(extensions):
                matching_files.append(os.path.join(subdir, file).replace("\\", "/"))
    return sorted(matching_files)


def format_path_for_log(path):
    return path.encode("ascii", errors="backslashreplace").decode("ascii")


class TestPublicSamples(AsyncTestCase):

    async def test_assimp(self):
        """Test assimp sample models from https://github.com/assimp/assimp/tree/master/test/models"""
        # recursively find all test files inside the test folder and attempt to convert all of them
        assimp_sample_formats = (
            ("FBX", (".fbx",)),
            ("OBJ", (".obj",)),
            # gltf/glb formats go through gltf importer code path which only supports gltf 2.0
            ("glTF2", (".gltf", ".glb")),
        )

        matching_files = []
        for folder_name, extensions in assimp_sample_formats:
            if folder_name == "FBX" and "aarch64" in platform.machine():
                continue

            test_files_folder = omni.repo.man.resolve_tokens(
                f"${{root}}/_build/target-deps/assimp_sample_models/{folder_name}"
            )
            matching_files += find_files_with_extensions(test_files_folder, extensions)

        for file_path in matching_files:
            output_path = self.get_output_path(file_path)

            # loading gltf would fail without a .bin file
            if "MissingBin" in file_path:
                print(f"Skipping public assimp sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # gltf importer can't load file with wrong scene type
            if "sceneWrongType.gltf" in file_path:
                print(f"Skipping public assimp sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # file has invalid header (unable to load in Blender as well)
            if "transparentTest.fbx" in file_path:
                print(f"Skipping public assimp sample: {format_path_for_log(file_path)}", flush=True)
                continue

            with self.subTest(file_path=file_path):
                print(
                    f"Converting public assimp sample: {format_path_for_log(file_path)} -> "
                    f"{format_path_for_log(output_path)}",
                    flush=True,
                )
                status = await convert(file_path, output_path, None)
                self.assertEqual(status, assetconverter.OmniConverterStatus.OK)
                print(f"Validating public assimp sample output: {format_path_for_log(output_path)}", flush=True)
                self.assertIsValidUsd(output_path)

    async def test_gltf(self):
        """Test gltf sample models from https://github.com/KhronosGroup/glTF-Sample-Models"""
        # recursively find all test files inside the test folder and attempt to convert all of them
        test_files_folder = omni.repo.man.resolve_tokens("${root}/_build/target-deps/gltf_sample_models/2.0")
        extensions = (".gltf", ".glb")
        matching_files = find_files_with_extensions(test_files_folder, extensions)

        for file_path in matching_files:
            output_path = self.get_output_path(file_path)

            # models with meshopt compression aren't supported at the moment
            if "glTF-Meshopt" in file_path:
                print(f"Skipping public glTF sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # remove unicode characters from output path so asset validator can handle it
            # otherwise compliance checker will fail
            if "Unicode" in file_path:
                output_path = self.get_output_path(file_path, "Unicode")

            with self.subTest(file_path=file_path):
                print(
                    f"Converting public glTF sample: {format_path_for_log(file_path)} -> "
                    f"{format_path_for_log(output_path)}",
                    flush=True,
                )
                status = await convert(file_path, output_path, None)
                self.assertEqual(status, assetconverter.OmniConverterStatus.OK)
                print(f"Validating public glTF sample output: {format_path_for_log(output_path)}", flush=True)
                self.assertIsValidUsd(output_path)
