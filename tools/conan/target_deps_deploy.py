# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import os
import shutil

CONFIGURED_DEPENDENCIES = {
    "assimp": "assimp",
    "doctest": "doctest",
    "draco": os.path.join("draco", "{config}"),
    "libxml2": os.path.join("libxml2", "{config}"),
    "pybind11": "pybind11",
    "tinyxml2": "tinyxml2",
}


def _remove_tree(path: str):
    if os.path.isdir(path):
        shutil.rmtree(path)


def _copy_tree(src: str, dst: str):
    if not os.path.isdir(src):
        return
    os.makedirs(dst, exist_ok=True)
    shutil.copytree(src, dst, dirs_exist_ok=True)


def _copy_file(src: str, dst: str):
    if not os.path.isfile(src):
        return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def _metadata_prefix(conanfile) -> str:
    os_name = str(conanfile.settings.os).lower()
    arch = str(conanfile.settings.arch)
    return f"{os_name}-{arch}"


def _copy_dependency(dep, target_root: str, config: str, metadata_prefix: str):
    package_folder = getattr(dep, "package_folder", None)
    if not package_folder:
        return

    rel_target = CONFIGURED_DEPENDENCIES.get(dep.ref.name)
    if rel_target is None:
        return

    destination = os.path.join(target_root, rel_target.format(config=config))
    _remove_tree(destination)

    for folder in ("include", "lib", "lib64", "bin"):
        _copy_tree(os.path.join(package_folder, folder), os.path.join(destination, folder))

    _copy_tree(os.path.join(package_folder, "licenses"), os.path.join(destination, "PACKAGE-LICENSES"))
    _copy_file(
        os.path.join(package_folder, "conaninfo.txt"),
        os.path.join(destination, f"{metadata_prefix}-conaninfo.txt"),
    )
    _copy_file(
        os.path.join(package_folder, "conanmanifest.txt"),
        os.path.join(destination, f"{metadata_prefix}-conanmanifest.txt"),
    )


def _copy_runtime_libs(dep, runtime_root: str):
    package_folder = getattr(dep, "package_folder", None)
    if not package_folder:
        return

    for folder in ("bin", "lib", "lib64"):
        src = os.path.join(package_folder, folder)
        if os.path.isdir(src):
            _copy_tree(src, os.path.join(runtime_root, folder))


def deploy(graph, output_folder: str, **kwargs):
    conanfile = graph.root.conanfile
    config = str(conanfile.settings.build_type).lower()
    runtime_root = os.path.join(output_folder, "conan-runtime", config)
    metadata_prefix = _metadata_prefix(conanfile)

    _remove_tree(runtime_root)
    for _, dep in conanfile.dependencies.host.items():
        _copy_dependency(dep, output_folder, config, metadata_prefix)
        _copy_runtime_libs(dep, runtime_root)
