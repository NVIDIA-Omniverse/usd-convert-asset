# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
"""
This module contains bindings and helpers to usd_convert_asset interface. You can use it as follows:
    >>> def progress_callback(progress, total_steps):
    >>>     # pass
    >>>
    >>>
    >>> async with OmniAssetConverter(in_filename, out_filename, progress_callback) as converter:
    >>>     status = converter.get_status()
    >>>     if status == OmniConverterStatus.OK:
    >>>         pass # Handle success
    >>>     else:
    >>>         error_message = converter.get_detailed_error()
    >>>         pass # Handle failure
"""

import asyncio
import ctypes
import glob
import logging
import os
import platform
import sys
import traceback
from pathlib import Path

from ._bootstrap import check_pxr_conflict as _check_pxr_conflict

_module_dir = os.path.dirname(__file__)
_libs_dir = os.path.join(_module_dir, "libs")
_dll_directory_handles = []
_logger = logging.getLogger(__name__)

# Warn when a foreign OpenUSD (pxr) copy was imported before us. Python keeps the
# already-loaded pxr module instead of reloading our bundled build, but mixing USD
# versions in one process can still corrupt shared Tf/Plug singletons. Run this
# before preloading any USD-linked native library below. The bundled pxr can land
# under either python-path layout, so pass both candidates.
_check_pxr_conflict(
    os.path.join(_libs_dir, "python", "pxr"),
    os.path.join(_libs_dir, "lib", "python", "pxr"),
)


def _prepend_env_path(name, path):
    if not os.path.isdir(path):
        return

    existing = os.environ.get(name)
    paths = existing.split(os.pathsep) if existing else []
    if path not in paths:
        os.environ[name] = os.pathsep.join([path] + paths) if paths else path


def _prepend_sys_path(path):
    if os.path.isdir(path) and path not in sys.path:
        sys.path.insert(0, path)


def _register_bundled_usd_plugins():
    try:
        from pxr import Plug
    except ImportError:
        return

    resource_paths = [os.path.join(_libs_dir, "resources")]
    resource_paths.extend(path for path in os.environ.get("PXR_PLUGINPATH_NAME", "").split(os.pathsep) if path)

    registered_paths = set()
    for resource_path in resource_paths:
        normalized_path = os.path.normcase(os.path.normpath(resource_path))
        if normalized_path in registered_paths or not os.path.isdir(resource_path):
            continue

        Plug.Registry().RegisterPlugins(resource_path)
        registered_paths.add(normalized_path)


def _preload_bundled_libpython():
    libpython_name = f"libpython{sys.version_info.major}.{sys.version_info.minor}.so.1.0"
    libpython_path = os.path.join(_libs_dir, libpython_name)
    if os.path.isfile(libpython_path):
        ctypes.CDLL(libpython_path, mode=ctypes.RTLD_GLOBAL)


def _preload_bundled_draco():
    for libdraco_path in sorted(glob.glob(os.path.join(_libs_dir, "libdraco.so*"))):
        if os.path.isfile(libdraco_path):
            ctypes.CDLL(libdraco_path, mode=ctypes.RTLD_GLOBAL)
            return


def _preload_bundled_library(library_name):
    library_path = os.path.join(_libs_dir, library_name)
    if os.path.isfile(library_path):
        ctypes.CDLL(library_path, mode=ctypes.RTLD_GLOBAL)


_prepend_env_path("PXR_PLUGINPATH_NAME", os.path.join(_libs_dir, "resources"))
_prepend_env_path("PXR_PLUGINPATH_NAME", os.path.join(_libs_dir, "usd"))
_prepend_sys_path(os.path.join(_libs_dir, "lib", "python"))
_prepend_sys_path(os.path.join(_libs_dir, "python"))

if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
    _dll_directory_handles.append(os.add_dll_directory(_libs_dir))

# preload dep libs into the process
if sys.platform == "win32":
    _assimp_dlls = sorted(Path(_libs_dir).glob("assimp-vc*-mt.dll"))
    if not _assimp_dlls:
        raise FileNotFoundError(f"No assimp-vc*-mt.dll found in {_libs_dir}")
    ctypes.WinDLL(str(_assimp_dlls[-1]))
    ctypes.WinDLL(os.path.join(_libs_dir, "libfbxsdk.dll"))
    ctypes.WinDLL(os.path.join(_libs_dir, "usd_convert_asset.dll"))
elif sys.platform == "linux":
    _preload_bundled_libpython()
    _preload_bundled_draco()
    ctypes.CDLL(os.path.join(_libs_dir, "libassimp.so"))
    ctypes.CDLL(os.path.join(_libs_dir, "libxml2.so"), mode=ctypes.RTLD_GLOBAL)
    _preload_bundled_library("libusd_usdUI.so")
    # ctypes.CDLL(os.path.join(os.path.dirname(__file__), 'libs/libfbxsdk.so'))
    ctypes.CDLL(os.path.join(_libs_dir, "libusd_convert_asset.so"))

_register_bundled_usd_plugins()

from ._assetconverter import *

__all__ = ["OmniAssetConverter"]


class OmniAssetConverter:
    __future_progress_callbacks = {}
    __future_material_loaders = {}
    __read_callback = None
    __binary_write_callback = None
    __progress_callback_is_set = False
    __material_loader_is_set = False
    __fallback_material_loader = None

    def __init__(
        self,
        in_file,
        out_file,
        progress_callback=None,
        ignore_material=False,
        ignore_animation=False,
        single_mesh=False,
        smooth_normals=False,
        ignore_cameras=False,
        support_point_instancer=False,
        as_shapenet=False,
        use_meter_as_world_unit=False,
        create_world_as_default_root_prim=True,
        ignore_lights=False,
        embed_textures=False,
        material_loader=None,
        convert_fbx_to_y_up=False,
        convert_fbx_to_z_up=False,
        keep_all_materials=False,
        merge_all_meshes=False,
        use_double_precision_to_usd_transform_op=False,
        ignore_pivots=False,
        disable_instancing=False,
        export_hidden_props=False,
        baking_scales=False,
        ignore_flip_rotations=False,
        ignore_unbound_bones=False,
        export_embedded_gltf=False,
        convert_stage_up_default=False,
        convert_stage_up_y=False,
        convert_stage_up_z=False,
    ):
        """
        Constructor.

        Args:
            in_file (str): Asset file path to be converted.

            out_file (str): Output usd file.

            progress_callback: (Callable[int, int]): Progress callback of this task.
                                                     The first param is the progress, and second one is the total steps.

            ignore_animation (bool): Whether to export animation.

            ignore_material (bool): Whether to export materials.

            single_mesh (bool): Export Single props USD even there are native instancing in the imported assets.
                                By default, it will export separate USD files for instancing assets.

            smooth_normals (bool): Generate smooth normals for every mesh.

            ignore_cameras (bool): Whether to export camera.

            support_point_instancer (bool): DEPRECATED: Whether to use point instancer for mesh instances (deprecated).

            as_shapenet (bool): DEPRECATED: Input is expected to be a shapenet obj file.

            use_meter_as_world_unit (bool): Uses meter as world unit. By default, it's centimeter for usd.

            create_world_as_default_root_prim (bool): Whether to create /World as default root prim.

            ignore_cameras (bool): Whether to export light.

            embed_textures (bool): Whether to embed textures for export.

            material_loader (Callable[OmniConveterFuture, OmniConverterMaterialDescription): Material loader for this task.

            convert_fbx_to_y_up (bool): Whether to convert imported fbx stage to Maya Y-Up.

            convert_fbx_to_z_up (bool): Whether to convert imported fbx stage to Maya Z-Up.

            keep_all_materials (bool): Whether to keep all materials including those ones that are not referenced by any meshes.

            merge_all_meshes (bool): Whether to merge all meshes as a single one.

            use_double_precision_to_usd_transform_op (bool): Whether to use double precision for all USD transform op.
                                                             It's double3 for translate op, float3 for pivot, scale and rotation by default.

            ignore_pivots (bool): Don't import pivots from assets.

            disable_instancing (bool): Disables scene instancing for USD export. That the instanceable flag for all prims will always to false even native assets have instancing.

            export_hidden_props (bool): Export props that are hidden or not.

            baking_scales (bool): Baking scales into mesh for fbx import.

            ignore_flip_rotations (bool): Wheather to filter the animation's flip rotation value.

            ignore_unbound_bones (bool): Wheather to filter the unbound bones.

            export_embedded_gltf (bool): Export embedded gltf if true, else export separate gltf with bin file.

            convert_stage_up_default (bool): whether to use asset file's up-axis

            convert_stage_up_y (bool): whether to override asset file's up-axis to be y-axis

            convert_stage_up_z (bool): whether to override asset file's up-axis to be z-axis
        """

        self._in_file = in_file
        self._out_file = out_file
        self._status = OmniConverterStatus.IN_PROGRESS
        self._detailed_error = ""
        self._progress_callback = progress_callback
        self._material_loader = material_loader
        self._ignore_animation = ignore_animation
        self._ignore_material = ignore_material
        self._single_mesh = single_mesh
        self._smooth_normals = smooth_normals
        self._ignore_cameras = ignore_cameras
        self._support_point_instancer = support_point_instancer
        self._as_shapenet = as_shapenet
        self._use_meter_as_world_unit = use_meter_as_world_unit
        self._create_world_as_default_root_prim = create_world_as_default_root_prim
        self._ignore_lights = ignore_lights
        self._embed_textures = embed_textures
        self._convert_fbx_to_y_up = convert_fbx_to_y_up
        self._convert_fbx_to_z_up = convert_fbx_to_z_up
        self._keep_all_materials = keep_all_materials
        self._merge_all_meshes = merge_all_meshes
        self._use_double_precision_to_usd_transform_op = use_double_precision_to_usd_transform_op
        self._ignore_pivots = ignore_pivots
        self._disable_instancing = disable_instancing
        self._export_hidden_props = export_hidden_props
        self._baking_scales = baking_scales
        self._ignore_flip_rotations = ignore_flip_rotations
        self._ignore_unbound_bones = ignore_unbound_bones
        self._export_embedded_gltf = export_embedded_gltf
        self._convert_stage_up_default = convert_stage_up_default
        self._convert_stage_up_y = convert_stage_up_y
        self._convert_stage_up_z = convert_stage_up_z

        self._future = None
        if not OmniAssetConverter.__progress_callback_is_set:
            omniConverterSetProgressCallback(OmniAssetConverter._importer_progress_callback)
            OmniAssetConverter.__progress_callback_is_set = True

        if not OmniAssetConverter.__material_loader_is_set:
            omniConverterSetMaterialCallback(OmniAssetConverter._importer_material_loader)
            OmniAssetConverter.__material_loader_is_set = True

    @staticmethod
    def major_version() -> int:
        return OMNI_CONVERTER_MAJOR_VERSION

    @staticmethod
    def minor_version() -> int:
        return OMNI_CONVERTER_MINOR_VERSION

    @classmethod
    def set_cache_folder(cls, cache_folder):
        """Sets the cache store for USD conversion with USD plugin.

        Args:
            cache_folder (str): Location of cache folder on your system.
        """

        omniConverterSetCacheFolder(cache_folder)

    @classmethod
    def set_log_callback(cls, callback):
        """Sets log callback globally.

        Args:
            callback (Callable[str]): Log callback.
        """

        omniConverterSetLogCallback(callback)

    @classmethod
    def set_progress_callback(cls, callback):
        """Sets progress callback globally.
        This is used to monitor the asset convert progress.

        Args:
            callback (Callable[OmniConverterFuture, int, int]): Callback to be called with
            converting future, current progress, and total steps.
        """

        omniConverterSetProgressCallback(callback)

    @classmethod
    def set_file_callback(
        cls,
        mkdir_callback,
        binary_write_callback,
        file_exists_callback,
        read_callback,
        layer_write_callback=None,
        file_copy_callback=None,
    ):
        """Sets calbacks for file operations.
        This is used to override the file operations so it could
        be used to read asset from remote repository. By default,
        it will use fallback functions that support only to read
        from local disk.

        Args:
            mkdir_callback (Callable[str]): Function to create dir with path.
            binary_write_callback (Callable[str, Buffer]): Function to write binary with path and content.
            file_exists_callback (Callable[str]): Function to check file existence with path.
            read_callback (Callable[str] -> bytes): Function to read bytes from path.
            layer_write_callback (Callable[str, str]): Function to write layer content with target path and layer identifier.
            file_copy_callback (Callable[str, str]): Function to copy file to target path with target path and source path.
        """

        cls.__read_callback = read_callback
        cls.__binary_write_callback = binary_write_callback

        if read_callback:
            _internal_read_callback = cls._importer_read_callback
        else:
            _internal_read_callback = None

        if binary_write_callback:
            _internal_write_callback = cls._importer_write_callback
        else:
            _internal_write_callback = None

        omniConverterSetFileCallbacks(
            mkdir_callback,
            _internal_write_callback,
            file_exists_callback,
            _internal_read_callback,
            layer_write_callback,
            file_copy_callback,
        )

    @classmethod
    def set_material_loader(cls, material_loader):
        """Sets material loader to intercept material loading.
        This function is deprecated since material loader is
        moved to constructor to make it customized per task.
        You can still set material load with this function which
        will work as a global fallback one if no material loader
        is provided to the constructor.

        Args:
            material_loader (Callable[OmniConverterMaterialDescription]): Function that takes
            material description as param.
        """

        OmniAssetConverter.__fallback_material_loader = material_loader


    @classmethod
    def populate_all_materials(cls, asset_path):
        """Populates all material descriptions from assets.

        Args:
            asset_path (str): Asset path. Only FBX is supported currently.

        Returns:
            ([OmniConverterMaterialDescription]): Array of material descriptions.
        """

        return omniConverterPopulateMaterials(asset_path)

    @classmethod
    def _importer_write_callback(cls, path, blob):
        if not cls.__binary_write_callback:
            return False

        return cls.__binary_write_callback(path, memoryview(blob))

    @classmethod
    def _importer_read_callback(cls, path, blob):
        if not cls.__read_callback:
            return False

        file_bytes = bytes(cls.__read_callback(path))
        if file_bytes:
            blob.assign(file_bytes)
            return True
        else:
            return False

    @classmethod
    def _importer_progress_callback(cls, future, progress, total):
        callback = cls.__future_progress_callbacks.get(future, None)
        if callback:
            callback(progress, total)

    @classmethod
    def _importer_material_loader(cls, future, material_description):
        callback = cls.__future_material_loaders.get(future, None)
        if callback:
            return callback(material_description)
        elif OmniAssetConverter.__fallback_material_loader:
            return OmniAssetConverter.__fallback_material_loader(material_description)
        else:
            return None

    @classmethod
    def shutdown(cls):
        """Cleans up all setups. After this, all callbacks will be reset to fallback ones."""

        cls.__read_callback = None
        cls.set_file_callback(None, None, None, None, None, None)
        cls.set_log_callback(None)
        cls.set_progress_callback(None)
        cls.set_material_loader(None)
        cls.set_cache_folder("")
        omniConverterSetMaterialCallback(None)
        cls.__material_loader_is_set = False
        cls.__progress_callback_is_set = False
        cls.__future_progress_callbacks = {}
        cls.__future_material_loaders = {}
        cls.__fallback_material_loader = None

    def get_status(self):
        """Gets the status of this task. See `OmniConverterStatus`."""

        return self._status

    def get_detailed_error(self):
        """Gets the detailed error of this task if status is not OmniConverterStatus.OK"""

        return self._detailed_error

    def cancel(self):
        """Cancels this task."""

        if self._future:
            omniConverterCancelFuture(self._future)
            self._status = omniConverterCheckFutureStatus(self._future)
            self._detailed_error = omniConverterGetFutureDetailedError(self._future)

    async def __aenter__(self):
        flags = 0
        if self._ignore_animation:
            flags |= OMNI_CONVERTER_FLAGS_IGNORE_ANIMATION
        if self._ignore_material:
            flags |= OMNI_CONVERTER_FLAGS_IGNORE_MATERIALS
        if self._single_mesh:
            flags |= OMNI_CONVERTER_FLAGS_SINGLE_MESH_FILE
        if self._smooth_normals:
            flags |= OMNI_CONVERTER_FLAGS_GEN_SMOOTH_NORMALS
        if self._ignore_cameras:
            flags |= OMNI_CONVERTER_FLAGS_IGNORE_CAMERAS
        if self._support_point_instancer:
            flags |= OMNI_CONVERTER_FLAGS_SUPPORT_POINTER_INSTANCER
        if self._as_shapenet:
            flags |= OMNI_CONVERTER_FLAGS_EXPORT_AS_SHAPENET
        if self._use_meter_as_world_unit:
            flags |= OMNI_CONVERTER_FLAGS_USE_METER_PER_UNIT
        if self._create_world_as_default_root_prim:
            flags |= OMNI_CONVERTER_FLAGS_CREATE_WORLD_AS_DEFAULT_PRIM
        if self._ignore_lights:
            flags |= OMNI_CONVERTER_FLAGS_IGNORE_LIGHTS
        if self._embed_textures:
            flags |= OMNI_CONVERTER_FLAGS_EMBED_TEXTURES
        if self._convert_fbx_to_y_up:
            flags |= OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Y_UP
        if self._convert_fbx_to_z_up:
            flags |= OMNI_CONVERTER_FLAGS_FBX_CONVERT_TO_Z_UP
        if self._keep_all_materials:
            flags |= OMNI_CONVERTER_FLAGS_KEEP_ALL_MATERIALS
        if self._merge_all_meshes:
            flags |= OMNI_CONVERTER_FLAGS_MERGE_ALL_MESHES
        if self._use_double_precision_to_usd_transform_op:
            flags |= OMNI_CONVERTER_FLAGS_USE_DOUBLE_PRECISION_FOR_USD_TRANSFORM_OP
        if self._ignore_pivots:
            flags |= OMNI_CONVERTER_FLAGS_IGNORE_PIVOTS
        if self._disable_instancing:
            flags |= OMNI_CONVERTER_FLAGS_DISABLE_INSTANCING
        if self._export_hidden_props:
            flags |= OMNI_CONVERTER_FLAGS_EXPORT_HIDDEN_PROPS
        if self._baking_scales:
            flags |= OMNI_CONVERTER_FLAGS_FBX_BAKING_SCALES_INTO_MESH
        if self._ignore_flip_rotations:
            flags |= OMNI_CONVERTER_FLAGS_IGNORE_FLIP_ROTATION
        if self._ignore_unbound_bones:
            flags |= OMNI_CONVERTER_FLAGS_FBX_IGNORE_UNBOUND_BONES
        if self._export_embedded_gltf:
            flags |= OMNI_CONVERTER_FLAGS_EXPORT_EMBEDDED_GLTF
        if self._convert_stage_up_y:
            flags |= OMNI_CONVERTER_FLAGS_STAGE_UP_Y
        if self._convert_stage_up_z:
            flags |= OMNI_CONVERTER_FLAGS_STAGE_UP_Z

        try:
            self._future = omniConverterCreateAsset(self._in_file, self._out_file, flags)
            if self._progress_callback:
                OmniAssetConverter.__future_progress_callbacks[self._future] = self._progress_callback

            if self._material_loader:
                OmniAssetConverter.__future_material_loaders[self._future] = self._material_loader

            status = OmniConverterStatus.IN_PROGRESS
            while True:
                status = omniConverterCheckFutureStatus(self._future)
                if status == OmniConverterStatus.IN_PROGRESS:
                    await asyncio.sleep(0.1)
                else:
                    break

            self._status = status
            self._detailed_error = omniConverterGetFutureDetailedError(self._future)
        except Exception as e:
            traceback.print_exc()
            self._status = OmniConverterStatus.UNKNOWN
            self._detailed_error = f"Failed to convert {self._in_file} with error: str(e)."

        return self

    async def __aexit__(self, exc_type, exc, tb):
        OmniAssetConverter.__future_progress_callbacks.pop(self._future, None)
        OmniAssetConverter.__future_material_loaders.pop(self._future, None)
        omniConverterReleaseFuture(self._future)
        self._future = None

        # clean up registered callbacks
        omniConverterSetProgressCallback(None)
        OmniAssetConverter.__progress_callback_is_set = False

        omniConverterSetMaterialCallback(None)
        OmniAssetConverter.__material_loader_is_set = False
