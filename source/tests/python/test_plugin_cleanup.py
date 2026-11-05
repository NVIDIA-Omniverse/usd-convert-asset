# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import base64
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import omni.repo.man


def python_command(script, *args):
    """Build a child command with the test runner's native library search paths."""
    if os.name == "nt":
        config = omni.repo.man.get_tool_config()["repo_test"]
        paths = config["suites"]["python"].get("library_paths", config["library_paths"])
        directories = [omni.repo.man.resolve_tokens(path) for path in paths]
        directories = [path for path in directories if os.path.isdir(path)]
        script = "import os\n" f"_dll_directories = [os.add_dll_directory(path) for path in {directories!r}]\n" + script
    return [sys.executable, "-c", script, *args]


class TestPluginCleanup(unittest.TestCase):
    @staticmethod
    def _write_textured_obj(root, name):
        (root / f"{name}.png").write_bytes(
            base64.b64decode(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Z8XcAAAAASUVORK5CYII="
            )
        )
        (root / f"{name}.mtl").write_text(f"newmtl material\nKd 1 1 1\nmap_Kd {name}.png\n")
        source = root / f"{name}.obj"
        source.write_text(
            f"mtllib {name}.mtl\n"
            "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vt 0 0\nvt 1 0\nvt 0 1\n"
            "usemtl material\nf 1/1 2/2 3/3\n"
        )
        return source

    def _run_child(self, script, *args, temporary_root):
        env = dict(
            os.environ,
            TMPDIR=str(temporary_root),
            TMP=str(temporary_root),
            TEMP=str(temporary_root),
        )
        return subprocess.run(
            python_command(script, *[str(arg) for arg in args]),
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )

    def test_layer_content_edits_preserve_textures_and_reload(self):
        script = """
import gc, sys
import asset_converter_native_bindings
from pathlib import Path
from pxr import Sdf, Usd

temporary_root = Path(sys.argv[2])
stage = Usd.Stage.Open(sys.argv[1])
assert stage
layer = stage.GetRootLayer()
text = layer.ExportToString()
paths = []
for prim in stage.Traverse():
    for attribute in prim.GetAttributes():
        value = attribute.Get()
        if isinstance(value, Sdf.AssetPath) and value.path.endswith(".png"):
            paths.append(Path(layer.ComputeAbsolutePath(value.path)))
assert paths and all(path.is_file() for path in paths), paths
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == 1, directories

assert layer.ImportFromString(text)
assert all(path.is_file() for path in paths), paths
assert layer.Reload(force=True)
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 1

text = layer.ExportToString()
paths = list(temporary_root.rglob("*.png"))
assert paths
layer.Clear()
assert layer.empty
assert layer.ImportFromString(text)
assert all(path.is_file() for path in paths), paths
assert layer.Reload(force=True)

del stage, layer
gc.collect()
stage = Usd.Stage.Open(sys.argv[1])
assert stage
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 1
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            temporary_root = root / "tmp"
            temporary_root.mkdir()
            source = self._write_textured_obj(root, "textured")
            result = self._run_child(script, source, temporary_root, temporary_root=temporary_root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])

    def test_copied_layer_content_preserves_textures(self):
        script = """
import gc, sys
import asset_converter_native_bindings
from pathlib import Path
from pxr import Sdf, Usd

temporary_root = Path(sys.argv[3])
stage = Usd.Stage.Open(sys.argv[1])
assert stage
source_layer = stage.GetRootLayer()
copied_layer = Sdf.Layer.CreateAnonymous()
copied_layer.TransferContent(source_layer)
texture_paths = list(temporary_root.rglob("*.png"))
assert texture_paths and all(path.is_file() for path in texture_paths), texture_paths

del stage, source_layer
gc.collect()
other_stage = Usd.Stage.Open(sys.argv[2])
assert other_stage
assert all(path.is_file() for path in texture_paths), texture_paths
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 2

del copied_layer, other_stage
gc.collect()
stage = Usd.Stage.Open(sys.argv[1])
assert stage
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 1
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            temporary_root = root / "tmp"
            temporary_root.mkdir()
            sources = [self._write_textured_obj(root, name) for name in ("first", "second")]
            result = self._run_child(
                script,
                *sources,
                temporary_root,
                temporary_root=temporary_root,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])

    def test_copied_layer_relative_paths_preserve_textures(self):
        script = """
import gc, os, sys
import asset_converter_native_bindings
from pathlib import Path
from pxr import Sdf, Usd

temporary_root = Path(sys.argv[3])
stage = Usd.Stage.Open(sys.argv[1])
assert stage
source_layer = stage.GetRootLayer()
copied_path = Path(sys.argv[1]).parent / "copied.usda"
copied_layer = Sdf.Layer.CreateNew(str(copied_path))
for prim in source_layer.rootPrims:
    assert Sdf.CopySpec(source_layer, prim.path, copied_layer, prim.path)
copied_stage = Usd.Stage.Open(copied_layer)

texture_paths = list(temporary_root.rglob("*.png"))
assert texture_paths
relative_paths = []
for prim in copied_stage.Traverse():
    for attribute in prim.GetAttributes():
        value = attribute.Get()
        if isinstance(value, Sdf.AssetPath) and value.path.endswith(".png"):
            absolute_path = Path(source_layer.ComputeAbsolutePath(value.path))
            relative_path = Path(os.path.relpath(absolute_path, copied_path.parent)).as_posix()
            attribute.Set(Sdf.AssetPath(relative_path))
            relative_paths.append(relative_path)
assert relative_paths
assert all(Path(copied_layer.ComputeAbsolutePath(path)).is_file() for path in relative_paths)

del stage, source_layer, prim, attribute, value
gc.collect()
other_stage = Usd.Stage.Open(sys.argv[2])
assert other_stage
assert all(path.is_file() for path in texture_paths), texture_paths
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 2

del copied_stage, copied_layer, other_stage
gc.collect()
stage = Usd.Stage.Open(sys.argv[2])
assert stage
assert all(not path.exists() for path in texture_paths), texture_paths
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 1
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            temporary_root = root / "tmp"
            temporary_root.mkdir()
            sources = [self._write_textured_obj(root, name) for name in ("first", "second")]
            result = self._run_child(
                script,
                *sources,
                temporary_root,
                temporary_root=temporary_root,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])

    def test_prim_copies_preserve_texture_references(self):
        script = """
import gc, sys
import asset_converter_native_bindings
from pathlib import Path
from pxr import Sdf, Usd

temporary_root = Path(sys.argv[3])
mode = sys.argv[4]
stage = Usd.Stage.Open(sys.argv[1])
source_layer = stage.GetRootLayer()
copied_layer = Sdf.Layer.CreateAnonymous()
for prim in source_layer.rootPrims:
    assert Sdf.CopySpec(source_layer, prim.path, copied_layer, prim.path)
del prim
assert not copied_layer.customLayerData
copied_stage = Usd.Stage.Open(copied_layer)
texture_paths = list(temporary_root.rglob("*.png"))
assert texture_paths
for prim in copied_stage.Traverse():
    for attribute in prim.GetAttributes():
        value = attribute.Get()
        if isinstance(value, Sdf.AssetPath) and value.path.endswith(".png"):
            if mode == "sample":
                attribute.Clear()
                attribute.Set(value, 1.0)
            elif mode == "array":
                name = attribute.GetName()
                prim.RemoveProperty(name)
                prim.CreateAttribute(name, Sdf.ValueTypeNames.AssetArray).Set([value])
            elif mode == "dictionary":
                attribute.Clear()
                copied_layer.customLayerData = {"nested": {"texture": value}}
assert all(path.is_file() for path in texture_paths)

# Reload must preserve old textures referenced by copied prims.
assert source_layer.Reload(force=True)
assert all(path.is_file() for path in texture_paths), (mode, texture_paths)
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 2
del stage, source_layer
gc.collect()
other_stage = Usd.Stage.Open(sys.argv[2])
assert other_stage
assert all(path.is_file() for path in texture_paths), (mode, texture_paths)

del copied_stage, copied_layer, other_stage, prim, attribute
gc.collect()
stage = Usd.Stage.Open(sys.argv[2])
assert stage
assert all(not path.exists() for path in texture_paths), (mode, texture_paths)
assert len(list(temporary_root.glob("omniverse_asset_converter_*"))) == 1
"""
        for mode in ("default", "sample", "array", "dictionary"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                temporary_root = root / "tmp"
                temporary_root.mkdir()
                sources = [self._write_textured_obj(root, name) for name in ("first", "second")]
                result = self._run_child(script, *sources, temporary_root, mode, temporary_root=temporary_root)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])

    def test_reload_failure_and_process_exit_preserve_required_textures(self):
        script = """
import gc, os, sys
import asset_converter_native_bindings
from pathlib import Path
from pxr import Sdf, Usd

temporary_root = Path(sys.argv[2])
source = Path(sys.argv[1])

def texture_paths(stage):
    layer = stage.GetRootLayer()
    paths = []
    for prim in stage.Traverse():
        for attribute in prim.GetAttributes():
            value = attribute.Get()
            if isinstance(value, Sdf.AssetPath) and value.path and not value.path.endswith(".mdl"):
                paths.append(Path(layer.ComputeAbsolutePath(value.path)))
    return paths

stage = Usd.Stage.Open(sys.argv[1])
assert stage
layer = stage.GetRootLayer()
paths = texture_paths(stage)
assert paths and all(path.is_file() for path in paths), paths
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == 1, directories

held_texture = paths[0].open("rb")
assert layer.Reload(force=True)
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == (2 if os.name == "nt" else 1), directories
held_texture.close()

assert layer.Reload(force=True)
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == 1, directories
paths = texture_paths(stage)
assert paths and all(path.is_file() for path in paths), paths

source.write_text("")
assert not layer.Reload(force=True)
assert list(temporary_root.glob("omniverse_asset_converter_*")) == directories
assert all(path.is_file() for path in paths), paths
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            temporary_root = root / "tmp"
            temporary_root.mkdir()
            source = self._write_textured_obj(root, "textured")
            result = self._run_child(
                script,
                source,
                temporary_root,
                temporary_root=temporary_root,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])

    def test_concurrent_released_layers_are_reaped(self):
        script = """
import concurrent.futures, gc, sys, threading
import asset_converter_native_bindings
from pathlib import Path
from pxr import Usd

temporary_root = Path(sys.argv[3])
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
    stages = list(executor.map(Usd.Stage.Open, sys.argv[1:3]))
assert all(stages), stages
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == 2, directories

stages.clear()
gc.collect()
stage = Usd.Stage.Open(sys.argv[1])
assert stage
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == 1, directories
del stage
gc.collect()

barrier = threading.Barrier(2)
def open_same_layer(_):
    barrier.wait()
    return Usd.Stage.Open(sys.argv[1])
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
    stages = list(executor.map(open_same_layer, range(2)))
assert all(stages), stages
assert stages[0].GetRootLayer() == stages[1].GetRootLayer()
directories = list(temporary_root.glob("omniverse_asset_converter_*"))
assert len(directories) == 1, directories
assert list(directories[0].rglob("*.png"))
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            temporary_root = root / "tmp"
            temporary_root.mkdir()
            sources = [self._write_textured_obj(root, name) for name in ("first", "second")]
            result = self._run_child(
                script,
                *sources,
                temporary_root,
                temporary_root=temporary_root,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])

    def test_cached_plugin_output_does_not_become_temporary(self):
        script = """
import sys
import asset_converter_native_bindings as assetconverter
from pathlib import Path
from pxr import Usd

temporary_root = Path(sys.argv[2])
assetconverter.OmniAssetConverter.set_cache_folder(sys.argv[3])
stage = Usd.Stage.Open(sys.argv[1])
assert stage
assert not list(temporary_root.glob("omniverse_asset_converter_*"))
"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            temporary_root = root / "tmp"
            temporary_root.mkdir()
            cache = root / "cache"
            source = Path("data/tests/python_tests/cube.gltf").resolve()
            result = self._run_child(
                script,
                source,
                temporary_root,
                cache,
                temporary_root=temporary_root,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(list(temporary_root.glob("omniverse_asset_converter_*")), [])
            self.assertTrue(cache.is_dir())
            self.assertTrue(any(cache.iterdir()))
