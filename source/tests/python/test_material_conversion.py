# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Check material values and texture connections in actual conversion output.

The checked-in fixtures share a small triangle and texture across named materials.
Assertions inspect authored USD values; they do not compare rendered appearance.
"""

import inspect
import tempfile
import unittest
from pathlib import Path

import asset_converter_native_bindings as converter
from pxr import Gf, Usd, UsdShade

INTERNAL_MDL = "preview_surface" in inspect.signature(converter.OmniAssetConverter).parameters
FIXTURES = Path(__file__).resolve().parents[3] / "data/tests/materials"


class TestMaterialConversion(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.addCleanup(converter.OmniAssetConverter.shutdown)
        self.output = Path(self.directory.name)

    async def convert(self, source, name="converted.usda", preview=True):
        options = {"preview_surface": preview} if INTERNAL_MDL else {}
        destination = self.output / name
        async with converter.OmniAssetConverter(str(source), str(destination), **options) as task:
            self.assertEqual(task.get_status(), converter.OmniConverterStatus.OK, task.get_detailed_error())
        return destination

    def surfaces(self, stage, context=""):
        return {
            prim.GetName(): UsdShade.Material(prim).ComputeSurfaceSource(context)[0]
            for prim in stage.Traverse()
            if prim.IsA(UsdShade.Material)
        }

    async def test_transparency_and_refraction(self):
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "gltf_materials.gltf")))
        shaders = self.surfaces(stage)
        for name, attribute, expected in (
            ("blend", "opacity", 0.25),
            ("mask", "opacity", 0.6),
            ("mask", "opacityThreshold", 0.3),
            ("ior", "ior", 2.0),
        ):
            with self.subTest(material=name, attribute=attribute):
                self.assertAlmostEqual(shaders[name].GetInput(attribute).Get(), expected, places=5)
        self.assertFalse(shaders["opaque_texture"].GetInput("opacity").HasConnectedSource())
        texture = UsdShade.Shader(shaders["blend_texture"].GetInput("opacity").GetConnectedSource()[0].GetPrim())
        self.assertAlmostEqual(texture.GetInput("scale").Get()[3], 0.25)

    async def test_second_texture_coordinates(self):
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "gltf_materials.gltf")))
        surface = self.surfaces(stage)["second_uv"]
        texture = UsdShade.Shader(surface.GetInput("diffuseColor").GetConnectedSource()[0].GetPrim())
        self.assertTrue(texture.GetInput("st").HasConnectedSource())
        transform = UsdShade.Shader(texture.GetInput("st").GetConnectedSource()[0].GetPrim())
        self.assertEqual(transform.GetIdAttr().Get(), "UsdTransform2d")
        self.assertTrue(transform.GetInput("in").HasConnectedSource())
        reader = UsdShade.Shader(transform.GetInput("in").GetConnectedSource()[0].GetPrim())
        self.assertEqual(reader.GetIdAttr().Get(), "UsdPrimvarReader_float2")
        self.assertEqual(reader.GetInput("varname").Get(), "st_1")

    async def test_emission_factors_and_strength(self):
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "gltf_materials.gltf")))
        shaders = self.surfaces(stage)
        for strength in (0, 1, 4, 16, 32):
            with self.subTest(strength=strength):
                self.assertTrue(
                    Gf.IsClose(
                        shaders[f"emission_{strength}"].GetInput("emissiveColor").Get(),
                        Gf.Vec3f(strength, strength / 2, 0),
                        1e-5,
                    )
                )
        for name, expected in (
            ("texture_emission", (4, 2, 0, 1)),
            ("texture_zero", (0, 0, 0, 1)),
            ("texture_default", (0, 0, 0, 1)),
        ):
            with self.subTest(material=name):
                texture = UsdShade.Shader(shaders[name].GetInput("emissiveColor").GetConnectedSource()[0].GetPrim())
                self.assertTrue(Gf.IsClose(texture.GetInput("scale").Get(), Gf.Vec4f(*expected), 1e-5))

    async def test_authored_mdl_color(self):
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "mdl_colors.usda")))
        self.assertTrue(
            Gf.IsClose(self.surfaces(stage)["authored"].GetInput("diffuseColor").Get(), Gf.Vec3f(0.8, 0.4, 0.2), 1e-5)
        )

    async def test_mdl_tint_preserves_base_color(self):
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "mdl_colors.usda")))
        shaders = self.surfaces(stage)
        for name in ("tinted_constant", "tinted_base"):
            with self.subTest(material=name):
                self.assertTrue(Gf.IsClose(shaders[name].GetInput("diffuseColor").Get(), Gf.Vec3f(0.4, 0.1, 0.2), 1e-5))

    @unittest.skipUnless(INTERNAL_MDL, "Internal MDL material conversion")
    async def test_textured_mdl_preserves_base_color_and_tint(self):
        """A readable custom MDL color must still multiply the diffuse texture."""
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "mdl_colors.usda")))
        surface = self.surfaces(stage)["textured_base"]
        texture = UsdShade.Shader(surface.GetInput("diffuseColor").GetConnectedSource()[0].GetPrim())
        self.assertEqual(texture.GetIdAttr().Get(), "UsdUVTexture")
        self.assertTrue(Gf.IsClose(texture.GetInput("scale").Get(), Gf.Vec4f(0.4, 0.1, 0.2, 1), 1e-5))
        self.assertTrue(Path(texture.GetInput("file").Get().resolvedPath).is_file())

    @unittest.skipUnless(INTERNAL_MDL, "Internal MDL material conversion")
    async def test_emission_survives_mdl_roundtrip(self):
        legacy = await self.convert(FIXTURES / "gltf_materials.gltf", "legacy.usda", preview=False)
        stage = Usd.Stage.Open(str(await self.convert(legacy)))
        shaders = self.surfaces(stage)
        for strength in (0, 1, 4, 16, 32):
            with self.subTest(strength=strength):
                self.assertTrue(
                    Gf.IsClose(
                        shaders[f"emission_{strength}"].GetInput("emissiveColor").Get(),
                        Gf.Vec3f(strength, strength / 2, 0),
                        1e-5,
                    )
                )

    async def test_specular_glossiness_opacity(self):
        """Use diffuse alpha for this workflow, including its default of one."""
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "gltf_materials.gltf")))
        shaders = self.surfaces(stage)
        for name, expected in (
            ("specular_blend", 0.25),
            ("specular_mask", 0.6),
            ("specular_opaque", 1.0),
            ("specular_default", 1.0),
        ):
            with self.subTest(material=name):
                self.assertAlmostEqual(shaders[name].GetInput("opacity").Get(), expected, places=5)
        self.assertAlmostEqual(shaders["specular_mask"].GetInput("opacityThreshold").Get(), 0.3, places=5)
        texture = UsdShade.Shader(shaders["specular_texture"].GetInput("opacity").GetConnectedSource()[0].GetPrim())
        self.assertAlmostEqual(texture.GetInput("scale").Get()[3], 0.25)

    @unittest.skipUnless(INTERNAL_MDL, "Internal MDL material conversion")
    async def test_mdl_emissive_texture_factor(self):
        """An emission texture must retain its authored factor, including zero."""
        stage = Usd.Stage.Open(str(await self.convert(FIXTURES / "gltf_materials.gltf", preview=False)))
        shaders = self.surfaces(stage, "mdl")
        for name, expected in (
            ("texture_emission", (1, 0.5, 0)),
            ("texture_zero", (0, 0, 0)),
            ("texture_default", (0, 0, 0)),
        ):
            with self.subTest(material=name):
                self.assertTrue(Gf.IsClose(shaders[name].GetInput("emissive_factor").Get(), Gf.Vec3f(*expected), 1e-5))
