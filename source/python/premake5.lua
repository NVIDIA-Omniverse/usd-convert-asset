project "usd_convert_asset[pip]"
    local build_dir = repo_build.target_dir()
    local pip_package_dir = build_dir.."/pip/usd_convert_asset"

    kind "Utility"
    dependson {
        "usd_convert_asset_bindings",
        "usd_convert_asset_usd_plugin",
    }

    local staged_files = {
        { "usd_convert_asset", pip_package_dir.."/usd_convert_asset" },
        { "README.md", pip_package_dir.."/README.md" },
        { "hatch_build.py", pip_package_dir.."/hatch_build.py" },
        { "pyproject.toml", pip_package_dir.."/pyproject.toml" },
        { repo_build.root.."/LICENSE", pip_package_dir.."/LICENSE" },
        { target_deps.."/usd/%{cfg.buildcfg}/bin/*${lib_ext}*", pip_package_dir.."/asset_converter_native_bindings/libs" },
        { target_deps.."/usd/%{cfg.buildcfg}/lib/*${lib_ext}*", pip_package_dir.."/asset_converter_native_bindings/libs" },
        { target_deps.."/usd/%{cfg.buildcfg}/lib/python", pip_package_dir.."/asset_converter_native_bindings/libs/lib/python" },
        { target_deps.."/usd/%{cfg.buildcfg}/python", pip_package_dir.."/asset_converter_native_bindings/libs/python" },
        { target_deps.."/usd/%{cfg.buildcfg}/lib/usd", pip_package_dir.."/asset_converter_native_bindings/libs/usd" },
        { "PACKAGE-LICENSES", pip_package_dir.."/PACKAGE-LICENSES" },
    }

    if os.target() == "linux" then
        table.insert(staged_files, { target_deps.."/python/lib/libpython*${lib_ext}*", pip_package_dir.."/asset_converter_native_bindings/libs" })
    end

    repo_build.prebuild_copy(staged_files)
