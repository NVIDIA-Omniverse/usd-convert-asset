-- Shared build scripts from repo_build package
repo_build = require("omni/repo/build")
repo_build.root = os.getcwd()

newoption {
    trigger     = "usd-flavor",
    description = "Specify the usd flavor",
    default = "usd"
}

newoption {
    trigger     = "usd-ver",
    description = "Specify the usd version",
    default = "25.11"
}

newoption {
    trigger     = "python-ver",
    description = "Specify the python version",
    allowed = {
        { "3.10", "Python 3.10" },
        { "3.11", "Python 3.11" },
        { "3.12", "Python 3.12" },
    },
    default = "3.12"
}

-- these variables are used in repo_build. Without them we'd get invalid option errors during build
USD_FLAVOR = _OPTIONS["usd-flavor"]
USD_VERSION = _OPTIONS["usd-ver"]
PYTHON_VERSION = _OPTIONS["python-ver"]

-- strip out the '.' from PYTHON_VERSION.
PY_VER = string.gsub(PYTHON_VERSION, "%.", "")

-- Pull in new Premake options from repo_build-1.0.0
repo_build.setup_options()
-- Set variables so repo_kit_tools does not set default values for MSVC and WINSDK
MSVC_VERSION = _OPTIONS["visual-cxx-version"]
WINSDK_VERSION = _OPTIONS["winsdk-version"]

target_build_dir = target_build_dir or repo_build.target_dir()
target_bin_dir = target_bin_dir or target_build_dir.."/bin"
target_lib_dir = target_lib_dir or target_build_dir.."/lib"
target_python_dir = target_python_dir or target_build_dir.."/python"
target_deps = target_deps or repo_build.target_deps_dir()

-- Override the target lib dir so that everything is in the bin
target_lib_dir = target_build_dir.."/bin"

function copy_to_file(filePath, newPath)
    local filePathAbs = path.getabsolute(filePath)
    local targetPathAbs = path.getabsolute(newPath)
    local dir = targetPathAbs:match("(.*[\\/])")
    if os.target() == "windows" then
        if dir ~= "" then
            --dir = dir:gsub('/', '\\')
            postbuildcommands { "{MKDIR} \""..dir.."\"" }
        end
        -- Using {COPY} on Windows adds an IF EXIST with an extra backslash which doesn't work
        filePathAbs = filePathAbs:gsub('/', '\\')
        targetPathAbs = targetPathAbs:gsub('/', '\\')
        postbuildcommands { "copy /Y \""..filePathAbs.."\" \""..targetPathAbs.."\"" }
    else
        if dir ~= "" then
            postbuildcommands { "{MKDIR} "..dir }
        end
        postbuildcommands { "{COPY} "..filePathAbs.." "..targetPathAbs }
    end
end

function write_text_to_file(filePath, content)
    local filePathAbs = path.getabsolute(filePath)
    local dir = filePathAbs:match("(.*[\\/])")
    content = content:gsub("\n$", "")

    local function quote_posix(value)
        return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
    end

    local function quote_powershell(value)
        return "'" .. tostring(value):gsub("'", "''") .. "'"
    end

    if os.target() == "windows" then
        filePathAbs = filePathAbs:gsub('/', '\\')
        local command = ""
        if dir and dir ~= "" then
            dir = dir:gsub('/', '\\')
            command = "New-Item -ItemType Directory -Force -Path "..quote_powershell(dir).." | Out-Null; "
        end
        command = command.."Set-Content -LiteralPath "..quote_powershell(filePathAbs).." -Value "..quote_powershell(content).." -Encoding ASCII"
        postbuildcommands { "powershell -NoProfile -ExecutionPolicy Bypass -Command \""..command.."\"" }
    else
        if not dir or dir == "" then
            dir = "."
        end
        postbuildcommands {
            "mkdir -p "..quote_posix(dir).." && printf '%s\\n' "..quote_posix(content).." > "..quote_posix(filePathAbs)
        }
    end
end

function version_compare(version, target)
    local function parts(value)
        local result = {}
        for part in tostring(value):gmatch("%d+") do
            table.insert(result, tonumber(part))
        end
        if #result == 0 then
            error("Invalid version: " .. tostring(value))
        end
        return result
    end

    local lhs = parts(version)
    local rhs = parts(target)
    local count = math.max(#lhs, #rhs)
    for index = 1, count do
        local lhs_part = lhs[index] or 0
        local rhs_part = rhs[index] or 0
        if lhs_part ~= rhs_part then
            return lhs_part < rhs_part and -1 or 1
        end
    end
    return 0
end

function version_at_least(version, target)
    return version_compare(version, target) >= 0
end

function version_greater_than(version, target)
    return version_compare(version, target) > 0
end

function use_draco()
    -- For USD 24.05/25.02 flavors we use draco from usd build library for linux-aarch64. Otherwise we get draco clashes which result in invalid pointers
    -- For USD 25.11 flavor, stock build doesn't come with draco so we would need to include it ourselves
    filter {"system:linux", "platforms:x86_64" }
        if version_at_least(USD_VERSION, "25.11") then
            externalincludedirs { target_deps.."/draco/%{cfg.buildcfg}/include"}
            libdirs { target_deps.."/draco/%{cfg.buildcfg}/lib"}
        end
        links { "draco" }
    filter {"system:linux", "platforms:aarch64" }
        if version_at_least(USD_VERSION, "25.11") then
            externalincludedirs { target_deps.."/draco/%{cfg.buildcfg}/include"}
            libdirs { target_deps.."/draco/%{cfg.buildcfg}/lib"}
            links { "draco" }
        end
    filter { "system:windows" }
        externalincludedirs { target_deps.."/draco/%{cfg.buildcfg}/include"}
        libdirs { target_deps.."/draco/%{cfg.buildcfg}/lib"}
        libdirs { target_deps.."/draco/%{cfg.buildcfg}/bin"}
        links { "draco" }
    filter {}
end

function use_assimp()
    includedirs {target_deps.."/assimp/include"}
    filter { "system:windows" }
        libdirs {target_deps.."/assimp/lib"}
        links { "assimp-vc142-mt" }
    filter { "system:linux" }
        libdirs {target_deps.."/assimp/lib"}
        links { "assimp" }
    filter {}
end

function use_fbxsdk()
    defines {"FBXSDK_SHARED"}
    includedirs {target_deps.."/fbxsdk/include"}
    filter { "system:windows" }
        libdirs {target_deps.."/fbxsdk/lib/x64/release"}
        links {"libfbxsdk"}
    filter {"system:linux", "platforms:x86_64" }
        libdirs {target_deps.."/fbxsdk/lib/release"}
        libdirs {target_deps.."/libxml2/release/lib"}
        links {"fbxsdk", "xml2"}
    filter {}
end

function use_iray()
    includedirs {target_deps.."/iray/include"}
    filter { "system:windows" }
        libdirs {target_deps.."/iray/nt-x86-64/lib"}
    filter {}
end

function use_tinyxml2()
    includedirs {target_deps.."/tinyxml2/include"}
    libdirs {target_deps.."/tinyxml2/lib"}
    links {"tinyxml2"}
end

function use_python()
    python_folder = target_deps.."/python"

    filter { "system:windows" }
        externalincludedirs { python_folder.."/include" }
        syslibdirs { python_folder.."/libs" }
    filter { "system:linux" }
        externalincludedirs { python_folder.."/include/python"..PYTHON_VERSION }
        syslibdirs { python_folder.."/lib" }
        links { "python"..PYTHON_VERSION }
    filter {}
end

function use_pybind()
    externalincludedirs { target_deps.."/pybind11/include" }
end


function use_usd(usd_libs)
    -- Suppress deprecated tbb/atomic.h and tbb/task.h warnings from OpenUSD
    defines { "TBB_SUPPRESS_DEPRECATED_MESSAGES" }

    externalincludedirs { target_deps.."/usd/%{cfg.buildcfg}/include" }
    syslibdirs { target_deps.."/usd/%{cfg.buildcfg}/lib" }

    usd_lib_prefix = ""
    if _OPTIONS["linux-x86_64-cxx11-abi"] or (os.target() == "windows" and version_at_least(USD_VERSION, "25.11")) then
        usd_lib_prefix = "usd_"
    end

    for _, lib in ipairs(usd_libs) do
        links { usd_lib_prefix..lib }
    end

    if version_greater_than(PYTHON_VERSION, "3.11") then
        links { "usd_python" }
    end

    -- Link TBB libraries on Linux (USD depends on TBB)
    filter { "system:linux", "configurations:debug" }
        links { "tbb_debug" }
    filter { "system:linux", "configurations:release" }
        links { "tbb" }
    filter {}
end

function use_doctest()
    externalincludedirs { target_deps.."/doctest/include" }
    filter { "system:windows" }
        disablewarnings {
            "4805", -- '==': unsafe mix of type 'const bool' and type 'const R' in operation
        }
    filter {}
end


function __library(options)
    -- check options
    if type(options.library_name) ~= "string" then
        error("`library_name` must be specified")
    end

    local library_name = options.library_name
    local headers = options.headers or {}
    local sources = options.sources or {}

    language "C++"
    defines { library_name.."_EXPORTS" }
    location (repo_build.workspace_dir().."/"..library_name)
    includedirs { "include" }
    libdirs { target_lib_dir }
    files { headers, sources }
    filter { "system:windows" }
        if os.isfile("version.rc") then
            files{ "version.rc" }
        end
    filter {}
    targetdir(target_lib_dir)
    targetname(library_name)
end

-- Create a C++ shared library project
-- @param library_name: The base file name for the compiled binary target
-- @param headers: A list of header files to add to the project
-- @param sources: A list of source files to add to the project
function shared_library(options)
    kind "SharedLib"
    __library(options)
end

-- Create a C++ executable ConsoleApp project
-- @param name: The base file name for the compiled binary target
-- @param headers: A list of header files to add to the project
-- @param sources: A list of source files to add to the project
function executable(options)
    -- check options
    if type(options.name) ~= "string" then
        error("`name` must be specified")
    end

    local name = options.name
    local sources = options.sources or {}
    local headers = options.headers or {}

    kind "ConsoleApp"
    language "C++"
    location (repo_build.workspace_dir().."/"..name)
    includedirs { "include" }
    libdirs { target_lib_dir }
    files { headers, sources }
    filter { "system:windows" }
        if os.isfile("version.rc") then
            files{ "version.rc" }
        end
    filter {}
    targetdir(target_bin_dir)
    targetname(name)
end

function python_module(options)
    -- check options
    if type(options.module_name) ~= "string" and type(options.bindings_module_name) ~= "string" then
        error("One of `module_name` or `bindings_module_name` must be specified")
    end

    bindings_module_name = options.bindings_module_name
    python_module_name = options.module_name or bindings_module_name:gsub("_", ".")
    module_dir = python_module_name:gsub("%.", "/")
    python_sources = options.python_sources or {}
    bindings_sources = options.bindings_sources or {}

    target_bindings_dir = target_python_dir.."/"..module_dir

    repo_build.prebuild_copy({ python_sources, target_bindings_dir })
    write_text_to_file(target_bindings_dir.."/_build_info.txt", USD_VERSION.."\n")

    if bindings_sources then

        use_pybind()

        defines { "MODULE_NAME="..bindings_module_name }
        includedirs { "include" }
        libdirs { target_lib_dir }
        files { python_sources, bindings_sources }
        targetdir(target_bindings_dir)

        repo_build.define_bindings_python("_"..bindings_module_name, target_deps.."/python", PYTHON_VERSION)

        -- its unclear why repo_build adds "-Wl,--no-undefined", but we don't want it
        -- or we'd have to explicitly link every upstream dependency
        removelinkoptions { "-Wl,--no-undefined" }

        -- this causes a compiliation error in the tbb headers... its doing something for pybind11, but
        -- its not clear what we're losing by removing this, nor how to avoid the tbb issue otherwise.
        removedefines {"_DEBUG"}

    end

end



workspace "usd_convert_asset"
    repo_build.setup_workspace({
        windows_x86_64_enabled = true,
        linux_x86_64_enabled = true,
        linux_aarch64_enabled = true,
        macos_universal_enabled = false,
        copy_windows_debug_libs = false,
        allow_undefined_symbols_linux = true,
        extra_warnings = true,
        security_hardening = false,
        fix_cpp_version = true,

        -- enable modern gcc warnings
        linux_gcc7_warnings = false
    })

    repo_build.enable_vstudio_sourcelink()
    repo_build.remove_vstudio_jmc()

    exceptionhandling "On"
    rtti "On"

    filter { "configurations:debug" }
        defines { "TBB_USE_DEBUG=1" }
    filter {}

    filter { "system:windows" }
        defines { "NOMINMAX" }
        -- Force MSVC to treat source and execution charsets as UTF-8 so Unicode literals work correctly
        buildoptions { "/utf-8" }
    filter { "system:linux" }
        buildoptions { "-fvisibility=hidden", "-fdiagnostics-color", "-Wno-deprecated", "-Wconversion", "-finput-charset=UTF-8", "-fexec-charset=UTF-8" }
    filter {}

    flags { "ShadowedVariables" }

    filter {"system:linux"}
        buildoptions {
            "-Wno-conversion",
            "-Wno-attributes",
            "-Wno-cpp"
        }
    filter {"system:windows"}
        disablewarnings { "4267", "4244", "4003", "4100", "4127", "4305", "4996", "4251", "4275", "4201", "4099", "4273" }
    filter {}

library_headers = {
    "source/library/*.h",
    "source/library/common/common.h",
    "source/library/common/cubicSpline.h",
    "source/library/common/curveTessellation.h",
    "source/library/thirdparty/**.h",
    "source/library/exporters/assimp/*.h",
    "source/library/exporters/gltf/*.h",
    "source/library/exporters/usd/*.h",
    "source/library/exporters/*.h",
    "source/library/importers/assimp/*.h",
    "source/library/importers/gltf/*.h",
    "source/library/importers/obj/*.h",
    "source/library/importers/usd/*.h",
    "source/library/importers/*.h",
    "source/library/utils/*.h"
}
library_sources = {
    "source/library/*.cpp",
    "source/library/common/common.cpp",
    "source/library/thirdparty/**.cpp",
    "source/library/exporters/assimp/*.cpp",
    "source/library/exporters/gltf/*.cpp",
    "source/library/exporters/usd/*.cpp",
    "source/library/exporters/*.cpp",
    "source/library/importers/assimp/*.cpp",
    "source/library/importers/gltf/*.cpp",
    "source/library/importers/obj/*.cpp",
    "source/library/importers/usd/*.cpp",
    "source/library/importers/*.cpp",
    "source/library/utils/*.cpp"
}

fbx_headers = {
    "source/library/common/custom_fbx_io.h",
    "source/library/common/fbx_common.h",
    "source/library/importers/fbx/*.h",
    "source/library/exporters/fbx/*.h",
}

fbx_sources = {
    "source/library/common/custom_fbx_io.cpp",
    "source/library/common/fbx_common.cpp",
    "source/library/importers/fbx/*.cpp",
    "source/library/exporters/fbx/*.cpp",
}

project "converter_library"
    use_usd ({"kind", "arch", "gf", "plug", "tf", "pcp", "vt", "ar", "sdf", "usd", "usdGeom", "usdLux", "usdShade", "usdSkel", "usdUtils"})
    filter {"system:windows" }
        use_fbxsdk()
    filter {"system:linux", "platforms:x86_64" }
        use_fbxsdk()
    filter {}
    use_draco()
    use_assimp()
    use_tinyxml2()
    use_iray()
    use_python()

    -- to do: address the warnings if possible
    filter { "system:windows" }
        disablewarnings {"4245", "4324", "4456", "4189", "4505", "4457", "4389", "4458", "4702", "4310", "4459", "4251", "4275", "4201", "4099"}
    filter { "system:linux" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'", "-Wl,-shared" }
        buildoptions {
            "-Wno-error=undef",
            "-Wno-deprecated",
            "-Wno-error=deprecated-declarations",
            "-Wno-unused-function",
            "-Wno-unused-parameter",
            "-Wno-unknown-pragmas",
            "-Wno-sign-compare",
            "-Wno-reorder",
            "-Wno-maybe-uninitialized",
            "-Wno-unused-variable",
            "-Wno-implicit-fallthrough",
            "-Wno-shadow",
            "-Wno-array-bounds",
            "-Wno-parentheses",
            "-Wno-type-limits",
            "-Wno-switch",
            "-Wno-unused-but-set-variable",
            "-Wno-deprecated-copy",
            "-Wno-class-memaccess",
            "-Wno-missing-field-initializers"
        }
        links { "stdc++fs" }
    filter {}

    -- Enable std::experimental::filesystem for now
    defines { "_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING"}

    files { "include/*.h" }

    -- we do not support fbx conversion on aarch64 platform
    filter {"system:windows"}
        files { fbx_headers, fbx_sources }
    filter {"system:linux", "platforms:x86_64" }
        files { fbx_headers, fbx_sources }
    filter {}

    shared_library{
        library_name = "usd_convert_asset",
        headers = library_headers,
        sources = library_sources,
    }

project "test_executable"
    use_usd ({"kind", "arch", "gf", "plug", "tf", "pcp", "vt", "ar", "sdf", "usd", "usdGeom", "usdLux", "usdShade", "usdSkel", "usdUtils"})
    filter {"system:windows" }
        use_fbxsdk()
    filter {"system:linux", "platforms:x86_64" }
        use_fbxsdk()
    filter {}
    use_assimp()
    use_tinyxml2()
    use_python()
    use_draco()

    -- Enable std::experimental::filesystem for now
    defines { "_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING"}

    filter {"system:linux" }
        linkoptions { "-Wl,--rpath -Wl,\\$$ORIGIN" }
        buildoptions { "-Wno-unused-parameter" }
        links { "stdc++fs" }
    filter {}

    links { "converter_library" }

    executable{
        name = "test_executable",
        headers = { "include/*.h" },
        sources = { "source/tests/app/main.cpp" },
    }

project "test_suite"
    use_doctest()
    use_usd ({"kind", "arch", "gf", "plug", "tf", "pcp", "vt", "ar", "sdf", "usd", "usdGeom", "usdLux", "usdShade", "usdSkel", "usdUtils"})
    filter {"system:windows" }
        use_fbxsdk()
    filter {"system:linux", "platforms:x86_64" }
        use_fbxsdk()
    filter {}
    use_draco()
    use_assimp()
    use_tinyxml2()
    use_iray()
    use_python()

    -- Enable std::experimental::filesystem for now
    defines { "_SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING"}

    -- to do: address the warnings if possible
    filter { "system:windows" }
        disablewarnings {"4245", "4324", "4456", "4189", "4505", "4457", "4389", "4458", "4702", "4310", "4459"}
    filter { "system:linux" }
        linkoptions {
            "-Wl,--rpath",
            "-Wl,\\$$ORIGIN"
        }
        if _OPTIONS["enable-gcov"] then
            buildoptions { "--coverage" }
            linkoptions { "--coverage" }
        end

        buildoptions {
            "-Wno-error=undef",
            "-Wno-deprecated",
            "-Wno-error=deprecated-declarations",
            "-Wno-unused-function",
            "-Wno-class-memaccess",
            "-Wno-deprecated-copy",
            "-Wno-unused-parameter",
            "-Wno-unknown-pragmas",
            "-Wno-sign-compare",
            "-Wno-reorder",
            "-Wno-maybe-uninitialized",
            "-Wno-unused-variable",
            "-Wno-implicit-fallthrough",
            "-Wno-shadow",
            "-Wno-array-bounds",
            "-Wno-parentheses",
            "-Wno-type-limits",
            "-Wno-switch",
            "-Wno-unused-but-set-variable",
            "-Wno-missing-field-initializers"
        }
        links { "stdc++fs", "dl", "pthread" }
        if PYTHON_VERSION == "3.11" or PYTHON_VERSION == "3.10" then
            links { "boost_python"..PY_VER }
        end
    filter {}

    includedirs { "include/*.h" }

    files { "source/tests/cpp/**.cpp" }

    -- we do not support fbx conversion on aarch64 platform
    filter {"system:windows"}
        files { fbx_headers, fbx_sources }
    filter {"system:linux", "platforms:x86_64" }
        files { fbx_headers, fbx_sources }
    filter {}

    executable({
        name = "test_suite",
        headers = library_headers,
        sources = library_sources,
    })

project "usd_convert_asset_bindings"

    dependson { "converter_library" }
    use_pybind()
    use_python()

    filter { "system:linux" }
        if _OPTIONS["enable-gcov"] then
            buildoptions { "--coverage" }
            linkoptions { "--coverage" }
        end
        buildoptions { "-Wno-unused-parameter" }
        links { "stdc++fs" }
    filter {}

    links { "converter_library" }

    python_module{
        bindings_module_name = "assetconverter",
        module_name = "asset_converter_native_bindings",
        bindings_sources = "source/python/bindings/omniverse_binding.cpp",
        python_sources = "source/python/*.py",
    }

    -- Copy required libs to asset_converter_native_bindings Python module.
    local bindings_libs_dir = target_build_dir.."/python/asset_converter_native_bindings/libs"
    repo_build.prebuild_copy (
        {
            -- windows dll in bin
            {target_deps.."/assimp/bin/${lib_prefix}assimp*${lib_ext}*", bindings_libs_dir},
            -- linux-x86_64 .so in lib64
            {target_deps.."/assimp/lib64/${lib_prefix}assimp*${lib_ext}*", bindings_libs_dir},
            -- linux-aarch64 .so in lib
            {target_deps.."/assimp/lib/${lib_prefix}assimp*${lib_ext}*", bindings_libs_dir},
            {target_deps.."/libxml2/$config/lib/${lib_prefix}xml2*${lib_ext}*", bindings_libs_dir},

            -- fbxsdk on Windows
            {target_deps.."/fbxsdk/lib/x64/$config/*${lib_ext}", bindings_libs_dir},
            -- fbxsdk on Linux
            {target_deps.."/fbxsdk/lib/$config/*${lib_ext}", bindings_libs_dir},

            -- tinyxml2 on Windows
            {target_deps.."/tinyxml2/bin/*${lib_ext}*", bindings_libs_dir },
            -- tinyxml2 on Linux
            {target_deps.."/tinyxml2/lib/*${lib_ext}*", bindings_libs_dir },
        }
    )

    -- USD 24.05/25.02 linux-aarch64 already carries Draco through USD. Packaging the external Draco can
    -- load duplicate global Draco symbols and cause intermittent invalid-pointer crashes.
    filter { "system:windows" }
        repo_build.prebuild_copy({
            {target_deps.."/draco/$config/bin/${lib_prefix}draco*${lib_ext}*", bindings_libs_dir},
            {target_deps.."/draco/$config/lib/${lib_prefix}draco*${lib_ext}*", bindings_libs_dir},
        })
    filter { "system:linux", "platforms:x86_64" }
        repo_build.prebuild_copy({
            {target_deps.."/draco/$config/bin/${lib_prefix}draco*${lib_ext}*", bindings_libs_dir},
            {target_deps.."/draco/$config/lib/${lib_prefix}draco*${lib_ext}*", bindings_libs_dir},
        })
    filter { "system:linux", "platforms:aarch64" }
        if version_at_least(USD_VERSION, "25.11") then
            repo_build.prebuild_copy({
                {target_deps.."/draco/$config/bin/${lib_prefix}draco*${lib_ext}*", bindings_libs_dir},
                {target_deps.."/draco/$config/lib/${lib_prefix}draco*${lib_ext}*", bindings_libs_dir},
            })
        end
    filter {}

    filter { "system:windows" }
        copy_to_file(target_build_dir.."/bin/usd_convert_asset.dll", target_build_dir.."/python/asset_converter_native_bindings/libs/usd_convert_asset.dll")
    filter { "system:linux" }
        copy_to_file(target_build_dir.."/bin/libusd_convert_asset.so", target_build_dir.."/python/asset_converter_native_bindings/libs/libusd_convert_asset.so")
    filter {}

project "usd_convert_asset_usd_plugin"

    dependson { "converter_library" }
    use_usd ({"arch", "sdf", "tf", "usd"})
    use_pybind()
    use_python()

    filter {"system:linux" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'", "-Wl,-shared" }
        buildoptions { "-Wno-unused-parameter" }
        links { "stdc++fs" }
    filter { "system:windows" }
        -- to do: address the warnings if possible
        disablewarnings {"4245"}

        buildoptions { "/Zc:inline-" }
    filter {}

    links { "converter_library" }

    shared_library{
        library_name = "usd_convert_asset_usd_plugin",
        headers = { "include/**.h",
                    "source/usd_plugins/**.h"
                },
        sources = { "source/usd_plugins/**.cpp"},
    }

    filter { "system:windows" }
        copy_to_file("source/usd_plugins/plugInfo_windows-x86_64.json", target_build_dir.."/python/asset_converter_native_bindings/libs/resources/plugInfo.json")
        copy_to_file(target_build_dir.."/bin/usd_convert_asset_usd_plugin.dll", target_build_dir.."/python/asset_converter_native_bindings/libs/usd_convert_asset_usd_plugin.dll")
    filter { "system:linux" }
        copy_to_file("source/usd_plugins/plugInfo_linux-x86_64.json", target_build_dir.."/python/asset_converter_native_bindings/libs/resources/plugInfo.json")
        copy_to_file(target_build_dir.."/bin/libusd_convert_asset_usd_plugin.so", target_build_dir.."/python/asset_converter_native_bindings/libs/libusd_convert_asset_usd_plugin.so")
    filter {}

-- Create pip package staging layout
include("source/python/premake5.lua")

-- Copy some dlls to run tests locally from executable
--[=====[
repo_build.prebuild_copy (
    {
        {target_deps.."/usd/%{cfg.buildcfg}/lib/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
        {target_deps.."/usd/%{cfg.buildcfg}/bin/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
        {target_deps.."/python/lib/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
        {target_deps.."/python/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},

        {target_deps.."/usd/%{cfg.buildcfg}/lib/usd", target_build_dir.."/bin/usd"},
        {target_deps.."/fbxsdk/lib/x64/%{cfg.buildcfg}/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
        {target_deps.."/assimp/bin/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
        {target_deps.."/tinyxml2/bin/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
        {target_deps.."/draco/%{cfg.buildcfg}/bin/${lib_prefix}*${lib_ext}*", target_build_dir.."/bin"},
    }
)
--]=====]
