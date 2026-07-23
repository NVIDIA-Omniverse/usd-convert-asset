# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
from pathlib import Path

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd, stdcpp_library
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.env import VirtualBuildEnv
from conan.tools.files import (
    collect_libs,
    copy,
    get,
    patch,
    replace_in_file,
    rmdir,
    save,
)
from conan.tools.microsoft import is_msvc, is_msvc_static_runtime
from conan.tools.scm import Version

required_conan_version = ">=2.0"


class AssimpConan(ConanFile):
    name = "assimp"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/assimp/assimp"
    description = (
        "A library to import and export various 3d-model-formats including "
        "scene-post-processing to generate missing render data."
    )
    topics = ("assimp", "3d", "game development", "3mf", "collada")
    license = "BSD-3-Clause"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "conan_deps.cmake", "patches/*.patch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "double_precision": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
        "double_precision": False,
    }

    _format_option_map = {
        "with_3d": ("ASSIMP_BUILD_3D_IMPORTER", "5.0.0"),
        "with_3ds": ("ASSIMP_BUILD_3DS_IMPORTER", "5.0.0"),
        "with_3ds_exporter": ("ASSIMP_BUILD_3DS_EXPORTER", "5.0.0"),
        "with_3mf": ("ASSIMP_BUILD_3MF_IMPORTER", "5.0.0"),
        "with_3mf_exporter": ("ASSIMP_BUILD_3MF_EXPORTER", "5.0.0"),
        "with_ac": ("ASSIMP_BUILD_AC_IMPORTER", "5.0.0"),
        "with_amf": ("ASSIMP_BUILD_AMF_IMPORTER", "5.0.0"),
        "with_ase": ("ASSIMP_BUILD_ASE_IMPORTER", "5.0.0"),
        "with_assbin": ("ASSIMP_BUILD_ASSBIN_IMPORTER", "5.0.0"),
        "with_assbin_exporter": ("ASSIMP_BUILD_ASSBIN_EXPORTER", "5.0.0"),
        "with_assxml_exporter": ("ASSIMP_BUILD_ASSXML_EXPORTER", "5.0.0"),
        "with_assjson_exporter": ("ASSIMP_BUILD_ASSJSON_EXPORTER", "5.0.0"),
        "with_b3d": ("ASSIMP_BUILD_B3D_IMPORTER", "5.0.0"),
        "with_blend": ("ASSIMP_BUILD_BLEND_IMPORTER", "5.0.0"),
        "with_bvh": ("ASSIMP_BUILD_BVH_IMPORTER", "5.0.0"),
        "with_ms3d": ("ASSIMP_BUILD_MS3D_IMPORTER", "5.0.0"),
        "with_cob": ("ASSIMP_BUILD_COB_IMPORTER", "5.0.0"),
        "with_collada": ("ASSIMP_BUILD_COLLADA_IMPORTER", "5.0.0"),
        "with_collada_exporter": ("ASSIMP_BUILD_COLLADA_EXPORTER", "5.0.0"),
        "with_csm": ("ASSIMP_BUILD_CSM_IMPORTER", "5.0.0"),
        "with_dxf": ("ASSIMP_BUILD_DXF_IMPORTER", "5.0.0"),
        "with_fbx": ("ASSIMP_BUILD_FBX_IMPORTER", "5.0.0"),
        "with_fbx_exporter": ("ASSIMP_BUILD_FBX_EXPORTER", "5.0.0"),
        "with_gltf": ("ASSIMP_BUILD_GLTF_IMPORTER", "5.0.0"),
        "with_gltf_exporter": ("ASSIMP_BUILD_GLTF_EXPORTER", "5.0.0"),
        "with_hmp": ("ASSIMP_BUILD_HMP_IMPORTER", "5.0.0"),
        "with_ifc": ("ASSIMP_BUILD_IFC_IMPORTER", "5.0.0"),
        "with_irr": ("ASSIMP_BUILD_IRR_IMPORTER", "5.0.0"),
        "with_irrmesh": ("ASSIMP_BUILD_IRRMESH_IMPORTER", "5.0.0"),
        "with_lwo": ("ASSIMP_BUILD_LWO_IMPORTER", "5.0.0"),
        "with_lws": ("ASSIMP_BUILD_LWS_IMPORTER", "5.0.0"),
        "with_md2": ("ASSIMP_BUILD_MD2_IMPORTER", "5.0.0"),
        "with_md3": ("ASSIMP_BUILD_MD3_IMPORTER", "5.0.0"),
        "with_md5": ("ASSIMP_BUILD_MD5_IMPORTER", "5.0.0"),
        "with_mdc": ("ASSIMP_BUILD_MDC_IMPORTER", "5.0.0"),
        "with_mdl": ("ASSIMP_BUILD_MDL_IMPORTER", "5.0.0"),
        "with_mmd": ("ASSIMP_BUILD_MMD_IMPORTER", "5.0.0"),
        "with_ndo": ("ASSIMP_BUILD_NDO_IMPORTER", "5.0.0"),
        "with_nff": ("ASSIMP_BUILD_NFF_IMPORTER", "5.0.0"),
        "with_obj": ("ASSIMP_BUILD_OBJ_IMPORTER", "5.0.0"),
        "with_obj_exporter": ("ASSIMP_BUILD_OBJ_EXPORTER", "5.0.0"),
        "with_off": ("ASSIMP_BUILD_OFF_IMPORTER", "5.0.0"),
        "with_ogre": ("ASSIMP_BUILD_OGRE_IMPORTER", "5.0.0"),
        "with_opengex": ("ASSIMP_BUILD_OPENGEX_IMPORTER", "5.0.0"),
        "with_opengex_exporter": ("ASSIMP_BUILD_OPENGEX_EXPORTER", "5.0.0"),
        "with_pbrt_exporter": ("ASSIMP_BUILD_PBRT_EXPORTER", "5.1.0"),
        "with_ply": ("ASSIMP_BUILD_PLY_IMPORTER", "5.0.0"),
        "with_ply_exporter": ("ASSIMP_BUILD_PLY_EXPORTER", "5.0.0"),
        "with_q3bsp": ("ASSIMP_BUILD_Q3BSP_IMPORTER", "5.0.0"),
        "with_q3d": ("ASSIMP_BUILD_Q3D_IMPORTER", "5.0.0"),
        "with_raw": ("ASSIMP_BUILD_RAW_IMPORTER", "5.0.0"),
        "with_sib": ("ASSIMP_BUILD_SIB_IMPORTER", "5.0.0"),
        "with_smd": ("ASSIMP_BUILD_SMD_IMPORTER", "5.0.0"),
        "with_step": ("ASSIMP_BUILD_STEP_IMPORTER", "5.0.0"),
        "with_step_exporter": ("ASSIMP_BUILD_STEP_EXPORTER", "5.0.0"),
        "with_stl": ("ASSIMP_BUILD_STL_IMPORTER", "5.0.0"),
        "with_stl_exporter": ("ASSIMP_BUILD_STL_EXPORTER", "5.0.0"),
        "with_terragen": ("ASSIMP_BUILD_TERRAGEN_IMPORTER", "5.0.0"),
        "with_x": ("ASSIMP_BUILD_X_IMPORTER", "5.0.0"),
        "with_x_exporter": ("ASSIMP_BUILD_X_EXPORTER", "5.0.0"),
        "with_x3d": ("ASSIMP_BUILD_X3D_IMPORTER", "5.0.0"),
        "with_x3d_exporter": ("ASSIMP_BUILD_X3D_EXPORTER", "5.0.0"),
        "with_xgl": ("ASSIMP_BUILD_XGL_IMPORTER", "5.0.0"),
        "with_m3d": ("ASSIMP_BUILD_M3D_IMPORTER", "5.1.0"),
        "with_m3d_exporter": ("ASSIMP_BUILD_M3D_EXPORTER", "5.1.0"),
        "with_iqm": ("ASSIMP_BUILD_IQM_IMPORTER", "5.2.0"),
    }
    options.update(dict.fromkeys(_format_option_map, [True, False]))
    default_options.update(dict.fromkeys(_format_option_map, True))

    short_paths = True

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    @property
    def _depends_on_kuba_zip(self):
        return self.options.with_3mf_exporter

    @property
    def _depends_on_poly2tri(self):
        return self.options.with_blend or self.options.with_ifc

    @property
    def _depends_on_rapidjson(self):
        return self.options.with_gltf or self.options.with_gltf_exporter

    @property
    def _depends_on_draco(self):
        return self.options.with_gltf or self.options.with_gltf_exporter

    @property
    def _depends_on_clipper(self):
        return self.options.with_ifc

    @property
    def _depends_on_stb(self):
        return self.options.with_m3d or self.options.with_m3d_exporter or self.options.with_pbrt_exporter

    @property
    def _depends_on_openddlparser(self):
        return self.options.with_opengex

    @property
    def _depends_on_earcut(self):
        return Version(self.version) >= "6.0.0"

    def requirements(self):
        self.requires("minizip/1.2.13")
        self.requires("pugixml/1.14")
        self.requires("utfcpp/4.0.1")
        self.requires("zlib/[>=1.3.2 <2]")
        if self._depends_on_kuba_zip:
            self.requires("kuba-zip/0.3.0")
        if self._depends_on_poly2tri:
            self.requires("poly2tri/cci.20130502")
        if self._depends_on_rapidjson:
            self.requires("rapidjson/cci.20230929")
        if self._depends_on_draco:
            self.requires("draco/1.5.6")
        if self._depends_on_clipper:
            self.requires("clipper/6.4.2")
        if self._depends_on_stb:
            self.requires("stb/cci.20230920")
        if self._depends_on_openddlparser:
            self.requires("openddl-parser/0.5.1")
        if self._depends_on_earcut:
            self.requires("earcut/2.2.4")

    def validate(self):
        if self.settings.compiler.cppstd:
            check_min_cppstd(self, 17)
        minimum_versions = {
            "gcc": "7",
            "clang": "6",
            "apple-clang": "10",
            "msvc": "191",
            "Visual Studio": "15",
        }
        minimum_version = minimum_versions.get(str(self.settings.compiler), False)
        if minimum_version and Version(self.settings.compiler.version) < minimum_version:
            raise ConanInvalidConfiguration(f"{self.ref} requires C++17, which your compiler does not support.")

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.22 <4]")

    def source(self):
        get(
            self,
            url="https://github.com/assimp/assimp/archive/refs/tags/v6.0.2.tar.gz",
            sha256="d1822d9a19c9205d6e8bc533bf897174ddb360ce504680f294170cc1d6319751",
            strip_root=True,
        )
        copy(self, "conan_deps.cmake", self.export_sources_folder, self.source_folder)

    def generate(self):
        tc = CMakeToolchain(self)
        if self.settings.os == "Linux":
            tc.extra_cxxflags.append("-fstack-protector-all")
        tc.variables["ASSIMP_ANDROID_JNIIOSYSTEM"] = False
        tc.variables["ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT"] = False
        tc.variables["ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT"] = False
        tc.variables["ASSIMP_BUILD_ASSIMP_TOOLS"] = False
        tc.variables["ASSIMP_BUILD_DOCS"] = False
        tc.variables["ASSIMP_BUILD_DRACO"] = False
        tc.variables["ASSIMP_BUILD_FRAMEWORK"] = False
        tc.variables["ASSIMP_BUILD_MINIZIP"] = False
        tc.variables["ASSIMP_BUILD_SAMPLES"] = False
        tc.variables["ASSIMP_BUILD_TESTS"] = False
        tc.variables["ASSIMP_BUILD_ZLIB"] = False
        tc.variables["ASSIMP_DOUBLE_PRECISION"] = self.options.double_precision
        tc.variables["ASSIMP_HUNTER_ENABLED"] = False
        tc.variables["ASSIMP_IGNORE_GIT_HASH"] = True
        tc.variables["ASSIMP_INJECT_DEBUG_POSTFIX"] = False
        tc.variables["ASSIMP_INSTALL"] = True
        tc.variables["ASSIMP_INSTALL_PDB"] = False
        tc.variables["ASSIMP_NO_EXPORT"] = False
        tc.variables["ASSIMP_OPT_BUILD_PACKAGES"] = False
        tc.variables["ASSIMP_RAPIDJSON_NO_MEMBER_ITERATOR"] = False
        tc.variables["ASSIMP_UBSAN"] = False
        tc.variables["ASSIMP_WARNINGS_AS_ERRORS"] = False
        tc.variables["USE_STATIC_CRT"] = is_msvc_static_runtime(self)
        tc.variables["ASSIMP_BUILD_USE_CCACHE"] = False

        for option, (definition, _) in self._format_option_map.items():
            tc.variables[definition] = self.options[option]
        if self.settings.os == "Windows":
            tc.preprocessor_definitions["NOMINMAX"] = 1

        tc.cache_variables["CMAKE_PROJECT_Assimp_INCLUDE"] = "conan_deps.cmake"
        tc.cache_variables["WITH_CLIPPER"] = self._depends_on_clipper
        tc.cache_variables["WITH_DRACO"] = self._depends_on_draco
        tc.cache_variables["WITH_EARCUT"] = self._depends_on_earcut
        tc.cache_variables["WITH_KUBAZIP"] = self._depends_on_kuba_zip
        tc.cache_variables["WITH_OPENDDL"] = self._depends_on_openddlparser
        tc.cache_variables["WITH_POLY2TRI"] = self._depends_on_poly2tri
        tc.cache_variables["WITH_RAPIDJSON"] = self._depends_on_rapidjson
        tc.cache_variables["WITH_STB"] = self._depends_on_stb
        tc.generate()

        cd = CMakeDeps(self)
        cd.set_property("rapidjson", "cmake_target_name", "rapidjson::rapidjson")
        cd.set_property("utfcpp", "cmake_target_name", "utf8cpp::utf8cpp")
        cd.generate()

        VirtualBuildEnv(self).generate()

    def _patch_sources(self):
        patch(self, patch_file=Path(self.export_sources_folder) / "patches" / "6.0.2-0001-apply-internal-fixes.patch")
        source_root = Path(self.source_folder)

        for pattern in [
            "-fPIC",
            "-g ",
            "SET(CMAKE_POSITION_INDEPENDENT_CODE ON)",
            'SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_DEBUG /Zi /Od")',
            'SET(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG:FULL /PDBALTPATH:%_PDB% /OPT:REF /OPT:ICF")',
        ]:
            replace_in_file(self, source_root / "CMakeLists.txt", pattern, "")

        for pattern in ["-Werror", "/WX"]:
            replace_in_file(self, source_root / "CMakeLists.txt", pattern, "")
            replace_in_file(self, source_root / "code" / "CMakeLists.txt", pattern, "")

        for contrib_dir in source_root.joinpath("contrib").iterdir():
            if contrib_dir.is_dir() and contrib_dir.name != "Open3DGC":
                rmdir(self, contrib_dir)

        code_cmakelists = source_root / "code" / "CMakeLists.txt"
        content = code_cmakelists.read_text(encoding="utf-8")
        for vendored_lib in [
            "unzip_compile",
            "Poly2Tri",
            "Clipper",
            "openddl_parser",
            "ziplib",
            "Pugixml",
            "stb",
        ]:
            content = content.replace("${%s_SRCS}" % vendored_lib, "")
        code_cmakelists.write_text(content, encoding="utf-8")

        for contrib_header, include in [
            (Path("clipper") / "clipper.hpp", "polyclipping/clipper.hpp"),
            (Path("earcut-hpp") / "earcut.hpp", "mapbox/earcut.hpp"),
            (Path("poly2tri") / "poly2tri" / "poly2tri.h", "poly2tri/poly2tri.h"),
            (Path("stb") / "stb_image.h", "stb_image.h"),
            (Path("utf8cpp") / "source" / "utf8.h", "utf8.h"),
            (Path("zip") / "src" / "zip.h", "zip/zip.h"),
        ]:
            save(self, source_root / "contrib" / contrib_header, f"#include <{include}>\n")
        rmdir(self, source_root / "contrib" / "utf8cpp")

        replace_in_file(
            self,
            source_root / "CMakeLists.txt",
            "use_pkgconfig(UNZIP minizip)",
            "set(UNZIP_FOUND TRUE)",
        )
        replace_in_file(self, source_root / "CMakeLists.txt", "INSTALL( TARGETS zlib", "set(_ #")

    def build(self):
        self._patch_sources()
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=Path(self.package_folder) / "licenses")
        cmake = CMake(self)
        cmake.install()
        rmdir(self, Path(self.package_folder) / "lib" / "cmake")
        rmdir(self, Path(self.package_folder) / "lib" / "pkgconfig")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "assimp")
        self.cpp_info.set_property("cmake_target_name", "assimp::assimp")
        self.cpp_info.set_property("pkg_config_name", "assimp")
        self.cpp_info.libs = collect_libs(self)
        if is_msvc(self) and self.options.shared:
            self.cpp_info.defines.append("ASSIMP_DLL")
        if self.settings.os in ["Linux", "FreeBSD"]:
            self.cpp_info.system_libs = ["rt", "m", "pthread"]
        if not self.options.shared:
            libcxx = stdcpp_library(self)
            if libcxx:
                self.cpp_info.system_libs.append(libcxx)
