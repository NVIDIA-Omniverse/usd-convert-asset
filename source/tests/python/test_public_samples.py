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
            validate_output = True

            # loading gltf would fail without a .bin file
            if "MissingBin" in file_path:
                print(f"Skipping public assimp sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # gltf importer can't load file with wrong scene type
            if "sceneWrongType.gltf" in file_path:
                print(f"Skipping public assimp sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # intentionally malformed accessors / bufferViews
            if "IncorrectVertexArrays" in file_path:
                print(f"Skipping public assimp sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # these malformed robustness fixtures cannot produce valid geometry extents
            malformed_geometry_samples = ("AllIndicesOutOfRange", "/wrongTypes/badArray.gltf")
            if any(sample in file_path for sample in malformed_geometry_samples):
                validate_output = False

            # KHR_draco_mesh_compression is not supported, so these valid compressed meshes import as empty geometry
            if "/draco/2CylinderEngine" in file_path:
                validate_output = False

            # glTF modes 0-3 are points or lines. The indexed variants use modes 0-3 again.
            # The models contain valid geometry, but the importer supports only triangle modes.
            unsupported_primitive_modes = tuple(
                f"Mesh_PrimitiveMode_{index:02}.gltf" for index in (0, 1, 2, 3, 7, 8, 9, 10)
            )
            if any(sample in file_path for sample in unsupported_primitive_modes):
                validate_output = False

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
                # Non-finite attributes leave no valid geometry, so the importer rejects this fixture.
                if "BoxWithInfinites" in file_path:
                    self.assertEqual(status, assetconverter.OmniConverterStatus.INCOMPLETE_IMPORT_FORMAT)
                    continue
                self.assertEqual(status, assetconverter.OmniConverterStatus.OK)
                print(f"Validating public assimp sample output: {format_path_for_log(output_path)}", flush=True)
                self.assertIsValidUsd(output_path, validate_geometry=validate_output)

    async def test_gltf(self):
        """Test gltf sample models from https://github.com/KhronosGroup/glTF-Sample-Models"""
        # recursively find all test files inside the test folder and attempt to convert all of them
        test_files_folder = omni.repo.man.resolve_tokens("${root}/_build/target-deps/gltf_sample_models/2.0")
        extensions = (".gltf", ".glb")
        matching_files = find_files_with_extensions(test_files_folder, extensions)

        for file_path in matching_files:
            output_path = self.get_output_path(file_path)
            validate_output = True

            # models with meshopt compression aren't supported at the moment
            if "glTF-Meshopt" in file_path:
                print(f"Skipping public glTF sample: {format_path_for_log(file_path)}", flush=True)
                continue

            # KHR_draco_mesh_compression and KHR_mesh_quantization are not supported.
            # These valid extension variants therefore import as empty geometry.
            if "/glTF-Draco/" in file_path or "/glTF-Quantized/" in file_path:
                validate_output = False

            # These models test valid point and line modes, but the importer supports only triangle modes.
            if "/MeshPrimitiveModes/" in file_path or "/PrimitiveModeNormalsTest/" in file_path:
                validate_output = False

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
                self.assertIsValidUsd(output_path, validate_geometry=validate_output)
