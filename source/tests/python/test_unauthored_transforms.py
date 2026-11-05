# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""Validate USD import defaults through USD-to-GLB conversion, using OpenUSD as the oracle."""

import json
import struct
import tempfile
import unittest
from pathlib import Path

import asset_converter_native_bindings as converter
from pxr import Gf, Sdf, Usd, UsdGeom


class TestUnauthoredTransforms(unittest.IsolatedAsyncioTestCase):
    def read_glb(self, path):
        """Read the JSON and binary chunks produced by the GLB exporter."""
        data = path.read_bytes()
        size, kind = struct.unpack_from("<II", data, 12)
        self.assertEqual(kind, 0x4E4F534A, "Expected GLB JSON chunk")
        return json.loads(data[20 : 20 + size]), data[28 + size :]

    @staticmethod
    def accessor(doc, binary, index):
        """Decode scalar, position, or quaternion samples from a GLB accessor."""
        accessor = doc["accessors"][index]
        view = doc["bufferViews"][accessor["bufferView"]]
        count = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[accessor["type"]]
        fmt = {5126: "f", 5123: "H", 5125: "I"}[accessor["componentType"]]
        size = struct.calcsize("<" + fmt * count)
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return [
            struct.unpack_from("<" + fmt * count, binary, offset + i * view.get("byteStride", size))
            for i in range(accessor["count"])
        ]

    def glb_points(self, doc, binary, seconds=None):
        """Evaluate exported world positions at static or exact sampled times."""
        values = {}
        if seconds is not None:
            self.assertTrue(doc.get("animations"), "Expected exported animation")
            for animation in doc["animations"]:
                for channel in animation["channels"]:
                    sampler = animation["samplers"][channel["sampler"]]
                    self.assertEqual(sampler.get("interpolation", "LINEAR"), "LINEAR")
                    times = [v[0] for v in self.accessor(doc, binary, sampler["input"])]
                    samples = self.accessor(doc, binary, sampler["output"])
                    # These checks use exact authored frames, avoiding a second interpolation implementation.
                    index = min(range(len(times)), key=lambda i: abs(times[i] - seconds))
                    self.assertAlmostEqual(times[index], seconds, places=5)
                    target = channel["target"]
                    if target["path"] == "rotation":
                        self.assertAlmostEqual(sum(v * v for v in samples[index]), 1.0, places=5)
                    values[target["node"], target["path"]] = samples[index]
        points = []

        def visit(index, parent):
            """Accumulate node transforms and collect world-space mesh vertices."""
            node = doc["nodes"][index]
            if "matrix" in node:
                matrix = Gf.Matrix4d(*node["matrix"])
            else:

                def value(key, default):
                    """Prefer an animation sample over the static node transform."""
                    return values.get((index, key), node.get(key, default))

                t = Gf.Matrix4d().SetTranslate(Gf.Vec3d(*value("translation", (0, 0, 0))))
                s = Gf.Matrix4d().SetScale(Gf.Vec3d(*value("scale", (1, 1, 1))))
                q = value("rotation", (0, 0, 0, 1))
                r = Gf.Matrix4d().SetRotate(Gf.Quatd(q[3], Gf.Vec3d(*q[:3])))
                matrix = s * r * t
            world = matrix * parent
            if "mesh" in node:
                for primitive in doc["meshes"][node["mesh"]]["primitives"]:
                    points.extend(
                        tuple(world.Transform(Gf.Vec3d(*p)))
                        for p in self.accessor(doc, binary, primitive["attributes"]["POSITION"])
                    )
            for child in node.get("children", []):
                visit(child, world)

        for index in doc["scenes"][doc.get("scene", 0)]["nodes"]:
            visit(index, Gf.Matrix4d(1))
        return points

    @staticmethod
    def add_ops(xform, values, precision, order="XYZ"):
        """Declare translate, rotate, and scale operations and author supplied values."""
        ops = [
            xform.AddTranslateOp(precision),
            getattr(xform, "AddRotate" + order + "Op")(precision),
            xform.AddScaleOp(precision),
        ]
        for op, value in zip(ops, values):
            if value is not None:
                op.Set(value)
        return ops

    async def check_scene(
        self,
        values=((2, 3, 4), (20, 30, 40), (2, 2, 2)),
        precision=UsdGeom.XformOp.PrecisionFloat,
        order="XYZ",
        parent=False,
        blocked=False,
        animated=False,
        check_trs=False,
        sampled_op=None,
    ):
        """Compare a converted synthetic scene against OpenUSD evaluation."""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.usda"
            stage = Usd.Stage.CreateNew(str(source))
            UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
            UsdGeom.SetStageMetersPerUnit(stage, 1.0)
            root = UsdGeom.Xform.Define(stage, "/Root")
            stage.SetDefaultPrim(root.GetPrim())
            obj = UsdGeom.Xform.Define(stage, "/Root/Obj")
            mesh = UsdGeom.Mesh.Define(stage, "/Root/Obj/Mesh")
            mesh.CreatePointsAttr([(0, 0, 0), (1, 0, 0), (0, 1, 1)])
            mesh.CreateFaceVertexCountsAttr([3])
            mesh.CreateFaceVertexIndicesAttr([0, 1, 2])
            if parent:
                self.add_ops(root, ((4, 5, 6), (20, 30, 40), (2, 3, 4)), precision)
            layer = Sdf.Layer.CreateNew(str(Path(directory) / "weak.usda")) if blocked else stage.GetRootLayer()
            if blocked:
                stage.GetRootLayer().subLayerPaths.append(layer.identifier)
            with Usd.EditContext(stage, layer):
                if sampled_op:
                    if sampled_op in ("rotateX", "rotateY", "rotateZ"):
                        ops = [getattr(obj, "AddR" + sampled_op[1:] + "Op")(precision)]
                    else:
                        ops = [obj.AddOrientOp(precision) if sampled_op == "orient" else obj.AddTransformOp()]
                else:
                    ops = self.add_ops(obj, (None, None, None) if animated else values, precision, order)
            if blocked:
                layer.Save()
                for op in ops:
                    op.GetAttr().Block()
                    self.assertIsNone(op.Get())
            if animated:
                stage.SetStartTimeCode(0)
                stage.SetEndTimeCode(24)
                stage.SetTimeCodesPerSecond(24)
                for step, frame in enumerate((0, 12, 24)):
                    if sampled_op:
                        if sampled_op in ("rotateX", "rotateY", "rotateZ"):
                            value = 30 * (step + 1)
                        else:
                            # Unit quaternions exactly representable even in half precision.
                            quat = Gf.Quatd(0, Gf.Vec3d(0, 1, 0)) if step == 0 else Gf.Quatd(0.5, Gf.Vec3d(0.5))
                            rotation = Gf.Rotation(quat)
                            value = rotation.GetQuat() if sampled_op == "orient" else Gf.Matrix4d().SetRotate(rotation)
                            if sampled_op == "transform":
                                value = Gf.Matrix4d().SetScale(2) * value
                                value.SetTranslateOnly(Gf.Vec3d(2, 3, 4))
                            if sampled_op == "orient":
                                quat_type = {
                                    UsdGeom.XformOp.PrecisionHalf: Gf.Quath,
                                    UsdGeom.XformOp.PrecisionFloat: Gf.Quatf,
                                    UsdGeom.XformOp.PrecisionDouble: Gf.Quatd,
                                }[precision]
                                value = quat_type(value)
                        ops[0].Set(Sdf.ValueBlock() if frame == 12 else value, frame)
                    else:
                        for op, value in zip(
                            ops, ((step, 2 * step, 3 * step), (10 * step, 20 * step, 30 * step), (1 + step,) * 3)
                        ):
                            op.Set(value, frame)
                if sampled_op:
                    self.assertIsNone(ops[0].Get(12))
                for op in ops:
                    self.assertIsNone(op.Get())
            stage.GetRootLayer().Save()
            destination = Path(directory) / "converted.glb"
            async with converter.OmniAssetConverter(str(source), str(destination)) as task:
                self.assertEqual(task.get_status(), converter.OmniConverterStatus.OK, task.get_detailed_error())
            doc, binary = self.read_glb(destination)
            if check_trs:
                node = next(node for node in doc["nodes"] if node.get("name") == "Obj")
                self.assertEqual(node.get("translation", [0, 0, 0]), list(values[0] or (0, 0, 0)))
                self.assertEqual(node.get("scale", [1, 1, 1]), list(values[2] or (1, 1, 1)))
            for frame in [0, 12, 24] if animated else [Usd.TimeCode.Default()]:
                matrix = mesh.ComputeLocalToWorldTransform(frame)
                expected = [tuple(matrix.Transform(Gf.Vec3d(p))) for p in mesh.GetPointsAttr().Get()]
                actual = self.glb_points(doc, binary, frame / 24 if animated else None)
                self.assertEqual(len(actual), len(expected))
                # Match without relying on exporter vertex ordering, including degenerate scales.
                for point in expected:
                    nearest = min(range(len(actual)), key=lambda i: sum((a - b) ** 2 for a, b in zip(actual[i], point)))
                    found = actual.pop(nearest)
                    for a, b in zip(found, point):
                        self.assertAlmostEqual(a, b, delta=1e-4)

    async def test_transform_edge_cases(self):
        """Exercise unset, authored, inherited, and blocked transform values."""
        cases = [("parent", dict(parent=True, values=(None, None, None))), ("blocked", dict(blocked=True))]
        for precision in (
            UsdGeom.XformOp.PrecisionHalf,
            UsdGeom.XformOp.PrecisionFloat,
            UsdGeom.XformOp.PrecisionDouble,
        ):
            for authored in (False, True):
                values = ((2, 3, 4), None, (2, 2, 2)) if authored else (None, None, None)
                cases.append(("precision", dict(precision=precision, values=values, check_trs=True)))
            cases.append(("animation", dict(precision=precision, animated=True)))
        for precision in (
            UsdGeom.XformOp.PrecisionHalf,
            UsdGeom.XformOp.PrecisionFloat,
            UsdGeom.XformOp.PrecisionDouble,
        ):
            cases.append(("blocked orient sample", dict(precision=precision, animated=True, sampled_op="orient")))
            for axis in ("X", "Y", "Z"):
                cases.append(
                    ("blocked scalar rotation", dict(precision=precision, animated=True, sampled_op="rotate" + axis))
                )
        cases.append(("blocked matrix sample", dict(animated=True, sampled_op="transform")))
        for missing in range(3):
            values = [(2, 3, 4), (20, 30, 40), (2, 3, 4)]
            values[missing] = None
            cases.append(("missing operation", dict(values=values)))
        for order in ("XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX"):
            for rotation in (None, (20, 30, 40)):
                cases.append(("rotation", dict(order=order, values=((2, 3, 4), rotation, (2, 2, 2)))))
        for scale in ((0, 0, 0), (0, 2, 3), (-1, 2, 3), (2, 3, 4)):
            cases.append(("scale", dict(values=((2, 3, 4), (20, 30, 40), scale))))
        for name, options in cases:
            with self.subTest(case=name, **options):
                await self.check_scene(**options)


if __name__ == "__main__":
    unittest.main()
