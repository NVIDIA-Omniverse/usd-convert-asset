# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Check that USD conversion preserves meshes and rigid skeleton bindings.

USD-to-USD conversion reads the source into the converter's internal scene and
writes a new USD file; it does not simply copy the source. These small fixtures
isolate mesh loss and binding errors without introducing another format's
limitations. Assimp FBX import/export and Linux ARM64 behavior require separate
coverage; these checks do not establish full skin or animation support there.
"""

import platform
import re
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

import asset_converter_native_bindings as converter
from pxr import Gf, Usd, UsdGeom, UsdSkel, Vt


class TestSceneFidelity(unittest.IsolatedAsyncioTestCase):
    async def convert_fixture(self, name, mesh_count):
        source = Path(__file__).resolve().parents[3] / "data/tests/usda" / name
        with tempfile.TemporaryDirectory() as output_dir:
            destination = Path(output_dir) / "converted.usda"
            try:
                async with converter.OmniAssetConverter(str(source), str(destination)) as task:
                    self.assertEqual(task.get_status(), converter.OmniConverterStatus.OK, task.get_detailed_error())
                stage = Usd.Stage.Open(str(destination))
                self.assertTrue(stage)
            finally:
                converter.OmniAssetConverter.shutdown()
        meshes = [UsdGeom.Mesh(prim) for prim in stage.Traverse() if prim.IsA(UsdGeom.Mesh)]
        self.assertEqual(len(meshes), mesh_count)
        for mesh in meshes:
            self.assertEqual(len(mesh.GetPointsAttr().Get()), 3)
            self.assertEqual(list(mesh.GetFaceVertexCountsAttr().Get()), [3])
        return stage, meshes

    async def test_unbound_mesh_inside_skeleton_group_survives(self):
        """Traversing a skeleton group must also keep unbound meshes.

        The fixture's triangle belongs to the group but is not attached to a
        joint. Skipping the group's ordinary children would lose this mesh.
        """
        await self.convert_fixture("skeleton_unbound_mesh.usda", 1)

    async def test_rigid_binding_and_unbound_sibling_survive(self):
        """A constant joint influence is a valid rigid binding.

        One joint assignment applies to the entire triangle. Conversion must
        expand it to all three vertices, retain the binding, and keep the
        unbound sibling without duplicating either mesh.
        """
        stage, meshes = await self.convert_fixture("skeleton_rigid_binding.usda", 2)
        bindings = [UsdSkel.BindingAPI(mesh.GetPrim()) for mesh in meshes]
        bindings = [binding for binding in bindings if binding.GetSkeletonRel().GetTargets()]
        self.assertEqual(len(bindings), 1)
        self.assertEqual(list(bindings[0].GetJointIndicesPrimvar().ComputeFlattened()), [0, 0, 0])
        self.assertEqual(list(bindings[0].GetJointWeightsPrimvar().ComputeFlattened()), [1, 1, 1])

    async def test_transformed_skeleton_static_placement(self):
        """Assimp static export must place translated and rotated rigs only once.

        OpenUSD computes the expected rest positions, including a rig moved
        after binding, child-only influences, and blended joint weights.
        USD output is also checked to protect skeleton retention.
        """
        source = Path(__file__).resolve().parents[3] / "data/tests/usda/skeleton_transformed_bindings.usda"

        def world_points(stage):
            cache = UsdSkel.Cache()
            for prim in stage.Traverse():
                if prim.IsA(UsdSkel.Root):
                    cache.Populate(UsdSkel.Root(prim), Usd.PrimDefaultPredicate)
            result = []
            for prim in stage.Traverse():
                if not prim.IsA(UsdGeom.Mesh):
                    continue
                points = Vt.Vec3fArray(UsdGeom.Mesh(prim).GetPointsAttr().Get())
                skin = cache.GetSkinningQuery(prim)
                skeleton = UsdSkel.BindingAPI(prim).GetInheritedSkeleton()
                if skin and skeleton:
                    query = cache.GetSkelQuery(skeleton)
                    self.assertTrue(skin.ComputeSkinnedPoints(query.ComputeSkinningTransforms(), points))
                    transform = UsdGeom.Xformable(skeleton).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
                else:
                    transform = UsdGeom.Xformable(prim).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
                result.extend(tuple(transform.Transform(point)) for point in points)
            return sorted(result, key=lambda point: tuple(round(value, 5) for value in point))

        expected = world_points(Usd.Stage.Open(str(source)))
        self.assertEqual(len(expected), 27)
        with tempfile.TemporaryDirectory() as directory:
            for extension in ("obj", "usda"):
                with self.subTest(extension=extension):
                    destination = Path(directory) / ("converted." + extension)
                    try:
                        async with converter.OmniAssetConverter(str(source), str(destination)) as task:
                            self.assertEqual(
                                task.get_status(), converter.OmniConverterStatus.OK, task.get_detailed_error()
                            )
                        if extension == "obj":
                            actual = sorted(
                                tuple(map(float, line.split()[1:4]))
                                for line in destination.read_text().splitlines()
                                if line.startswith("v ")
                            )
                            actual.sort(key=lambda point: tuple(round(value, 5) for value in point))
                            normals = [
                                Gf.Vec3d(*map(float, line.split()[1:4]))
                                for line in destination.read_text().splitlines()
                                if line.startswith("vn ")
                            ]
                            self.assertTrue(any(Gf.IsClose(normal, Gf.Vec3d(1, 0, 0), 1e-5) for normal in normals))
                        else:
                            actual = world_points(Usd.Stage.Open(str(destination)))
                        self.assertEqual(len(actual), len(expected))
                        for a, b in zip(actual, expected):
                            self.assertTrue(Gf.IsClose(Gf.Vec3d(*a), Gf.Vec3d(*b), 1e-5), (a, b))
                    finally:
                        converter.OmniAssetConverter.shutdown()

    @unittest.skipIf(platform.machine().lower() in ("aarch64", "arm64"), "Autodesk FBX export only")
    async def test_fbx_animation_preserves_seconds(self):
        """All transform channels must keep source timing, independently of display FPS."""
        fixture = Path(__file__).resolve().parents[3] / "data/tests/usda/animated_transforms.usda"
        with tempfile.TemporaryDirectory() as directory:
            for rate in (24.0, 30.0, 60.0, 29.97):
                with self.subTest(time_codes_per_second=rate):
                    source = Path(directory) / "animation.usda"
                    stage = Usd.Stage.Open(str(fixture))
                    stage.GetRootLayer().Export(str(source))
                    stage = Usd.Stage.Open(str(source))
                    stage.SetTimeCodesPerSecond(rate)
                    stage.SetFramesPerSecond(24)
                    stage.GetRootLayer().Save()
                    output = Path(directory) / "animation.fbx"
                    async with converter.OmniAssetConverter(str(source), str(output)) as task:
                        self.assertEqual(task.get_status(), converter.OmniConverterStatus.OK, task.get_detailed_error())
                    # Read FBX binary int64 key arrays directly, avoiding a converter
                    # roundtrip that could hide a matching import/export timing error.
                    data = output.read_bytes()
                    curves = []
                    for match in re.finditer(b"KeyTime" + b"l", data):
                        offset = match.end()
                        count, encoding, length = struct.unpack_from("<III", data, offset)
                        payload = data[offset + 12 : offset + 12 + length]
                        if encoding == 1:
                            payload = zlib.decompress(payload)
                        else:
                            self.assertEqual(encoding, 0)
                        curves.append([tick / 46186158000 for tick in struct.unpack(f"<{count}q", payload)])
                    self.assertGreaterEqual(len(curves), 9)
                    for curve in curves:
                        self.assertEqual(len(curve), 3)
                        for index, seconds in enumerate(curve):
                            self.assertAlmostEqual(seconds, index / rate, delta=1e-7)
                    converter.OmniAssetConverter.shutdown()


if __name__ == "__main__":
    unittest.main()
