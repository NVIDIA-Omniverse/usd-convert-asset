# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#

from conan import ConanFile  # type: ignore[reportMissingImports]


class UsdConvertAssetDependencies(ConanFile):
    settings = "os", "arch", "compiler", "build_type"

    requires = (
        "assimp/6.0.2",
        "libxml2/2.14.5",
        "draco/1.5.6",
        "tinyxml2/11.0.0",
        "doctest/2.4.12",
        "pybind11/2.13.6",
        "tinygltf/2.9.7",
        "tinyobjloader/2.0.0-rc10",
    )

    def requirements(self):
        self.requires("stb/cci.20240531", override=True)

    default_options = {
        "assimp/*:shared": True,
        "libxml2/*:shared": True,
        "libxml2/*:iconv": False,
        "libxml2/*:programs": False,
        "libxml2/*:zlib": False,
        "draco/*:shared": True,
        "tinyxml2/*:shared": True,
    }
