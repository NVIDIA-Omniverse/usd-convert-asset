# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import os
import unittest

import asset_converter_native_bindings as assetconverter
import omni.asset_validator
import omni.repo.man
from pxr import Ar

__all__ = ["convert", "assetconverter", "AsyncTestCase"]


async def convert(input_path, output_path, material_loader):
    try:
        async with assetconverter.OmniAssetConverter(
            input_path,
            output_path,
            material_loader=material_loader,
            single_mesh=True,
            keep_all_materials=True,
            use_double_precision_to_usd_transform_op=True,
            ignore_pivots=True,
            disable_instancing=True,
        ) as task:
            return task.get_status()
    finally:
        assetconverter.OmniAssetConverter.shutdown()


def log_print(message):
    print(message)


class AsyncTestCase(unittest.IsolatedAsyncioTestCase):

    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        # Add the mdl libraries to the default resolver search path to avoid MaterialPathChecker errors and logged warnings
        search_paths = [
            omni.repo.man.resolve_tokens("${root}/_build/target-deps/omni_core_materials/Base"),
            omni.repo.man.resolve_tokens("${root}/_build/target-deps/omni_core_materials/mdl"),
        ]
        Ar.DefaultResolver.SetDefaultSearchPath(search_paths)

    def setUp(self):
        self.outputs_dir = omni.repo.man.resolve_tokens("${root}/_build/${platform}/${config}/tests/output")
        os.makedirs(self.outputs_dir, exist_ok=True)
        assetconverter.OmniAssetConverter.set_log_callback(log_print)

    def tearDown(self):
        # Calls this to de-initialize everything
        try:
            assetconverter.OmniAssetConverter.shutdown()
        finally:
            assetconverter.OmniAssetConverter.set_material_loader(None)
            assetconverter.OmniAssetConverter.set_cache_folder("")
            assetconverter.OmniAssetConverter.set_log_callback(None)

    def assertIsValidUsd(self, identifier):
        validation_engine = omni.asset_validator.ValidationEngine()
        # deregister MaterialPathChecker and MissingReferenceChecker to ignore issues when resolving OmPBR.mdl
        validation_engine.disable_rule(omni.asset_validator.MaterialPathChecker)
        validation_engine.disable_rule(omni.asset_validator.MissingReferenceChecker)
        validation_engine.disable_rule(omni.asset_validator.MaterialUsdPreviewSurfaceChecker)

        # likely will be addressed in scene optimizer
        validation_engine.disable_rule(omni.asset_validator.ManifoldChecker)
        validation_engine.disable_rule(omni.asset_validator.IndexedPrimvarChecker)
        validation_engine.disable_rule(omni.asset_validator.ZeroAreaFaceChecker)
        validation_engine.disable_rule(omni.asset_validator.WeldChecker)
        validation_engine.disable_rule(omni.asset_validator.UsdAsciiPerformanceChecker)

        # to investigate..failing for assimp robust tests
        validation_engine.disable_rule(omni.asset_validator.UnusedPrimvarChecker)
        validation_engine.disable_rule(omni.asset_validator.TypeChecker)
        validation_engine.disable_rule(omni.asset_validator.ExtentsChecker)
        validation_engine.disable_rule(omni.asset_validator.SubdivisionSchemeChecker)
        validation_engine.disable_rule(omni.asset_validator.ValidateTopologyChecker)
        validation_engine.disable_rule(omni.asset_validator.UnusedMeshTopologyChecker)

        # to investigate..failing for gltf robust tests
        validation_engine.disable_rule(omni.asset_validator.TextureChecker)

        # to investigate from upgrading to asset validator 1.9.2. Some importers may not be generating normals.
        validation_engine.disable_rule(omni.asset_validator.NormalsValidChecker)
        validation_engine.disable_rule(omni.asset_validator.NormalsExistChecker)
        validation_engine.disable_rule(omni.asset_validator.NormalsWindingsChecker)

        result = validation_engine.validate(identifier)
        self.assertEqual(result.asset, identifier)

        self.assertFalse(result.issues(), msg=result.issues())

    def get_output_path(self, input_path, override_base_name=None, output_extension=".usd"):
        if override_base_name:
            output_path = self.outputs_dir + "/" + override_base_name + output_extension
        else:
            filename = os.path.basename(input_path)
            base_name = os.path.splitext(filename)[0]
            output_path = self.outputs_dir + "/" + base_name + output_extension
        return output_path
