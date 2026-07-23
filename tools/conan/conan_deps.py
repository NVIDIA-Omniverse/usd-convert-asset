# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 AND CC-BY-4.0
#
import argparse
import importlib
import os
import subprocess
import sys
from pathlib import Path

CONAN_VERSION = "2.28.1"
CONAN_CENTER_REMOTE_URL = "https://center2.conan.io"
DEFAULT_CONAN_BUILD_JOBS = "4"

g_repo_root = Path(__file__).resolve().parents[2]
LOCAL_RECIPES = ((g_repo_root / "tools" / "conan" / "recipes" / "assimp" / "5.x", "6.0.2"),)


def _build_type(config: str) -> str:
    return "Debug" if config.lower() == "debug" else "Release"


def _assimp_build_type(config: str) -> str:
    # Release (not RelWithDebInfo) so the toolchain does not retain debug info that can
    # embed build-host/path strings (e.g. internal hostnames) into the shipped .so files.
    return "Debug" if config.lower() == "debug" else "Release"


def _conan_build_jobs() -> str:
    build_jobs = os.environ.get("CONAN_BUILD_JOBS", DEFAULT_CONAN_BUILD_JOBS)
    try:
        if int(build_jobs) < 1:
            raise ValueError
    except ValueError as exc:
        raise RuntimeError("CONAN_BUILD_JOBS must be a positive integer.") from exc
    return build_jobs


def _subprocess_env() -> dict[str, str]:
    env = os.environ.copy()
    conan_home = g_repo_root / "_build" / "conan-home"
    pip_prebundle = g_repo_root / "_build" / "target-deps" / "pip_prebundle"

    env["CONAN_HOME"] = str(conan_home)
    env["PYTHONPATH"] = os.pathsep.join([str(pip_prebundle), *(path for path in [env.get("PYTHONPATH", "")] if path)])
    return env


def _run(command: list[str], env: dict[str, str], check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=g_repo_root, env=env, check=check)


def _pip_prebundle() -> Path:
    return g_repo_root / "_build" / "target-deps" / "pip_prebundle"


def _add_pip_prebundle_to_sys_path():
    pip_prebundle = str(_pip_prebundle())
    if pip_prebundle not in sys.path:
        sys.path.insert(0, pip_prebundle)


def _ensure_conan(env: dict[str, str]):
    _add_pip_prebundle_to_sys_path()
    try:
        importlib.import_module("conan.cli.cli")
        return
    except ModuleNotFoundError:
        pass

    pip_prebundle = _pip_prebundle()
    pip_prebundle.mkdir(parents=True, exist_ok=True)
    _run(
        [
            sys.executable,
            "-m",
            "pip",
            "install",
            "--upgrade",
            "--target",
            str(pip_prebundle),
            f"conan=={CONAN_VERSION}",
        ],
        env=env,
    )
    importlib.invalidate_caches()
    _add_pip_prebundle_to_sys_path()
    importlib.import_module("conan.cli.cli")


def _run_conan(command: list[str]):
    from conan.cli.cli import main as conan_entry  # type: ignore[reportMissingImports]

    try:
        conan_entry(command)
    except SystemExit as exc:
        if exc.code not in (None, 0):
            raise subprocess.CalledProcessError(exc.code, ["conan", *command]) from exc


def _ensure_profile(env: dict[str, str]):
    _run_conan(["profile", "detect", "--force"])


def _ensure_conan_center_remote():
    _run_conan(["remote", "add", "conancenter", CONAN_CENTER_REMOTE_URL, "--force"])


def _detect_conan_msvc_version_from_host_deps() -> str | None:
    """Detect the Visual Studio version from the host dependencies.
    This is used to set the compiler.version setting in the Conan profile.

    14.29... 192 (VS2019 / MSVC 19.2x)
    14.3x... 193 (VS2022 / MSVC 19.3x)
    14.4x... 194 (newer VS2022 toolsets / MSVC 19.4x)
    14.5x... 195 (VS18 / VS2026 / MSVC 19.5x)
    """
    version_file = (
        g_repo_root
        / "_build"
        / "host-deps"
        / "msvc"
        / "VC"
        / "Auxiliary"
        / "Build"
        / "Microsoft.VCToolsVersion.default.txt"
    )
    if not version_file.is_file():
        return None

    version = version_file.read_text(encoding="utf-8").strip()
    parts = version.split(".")
    if len(parts) < 2 or parts[0] != "14":
        return None

    try:
        minor = int(parts[1])
    except ValueError:
        return None
    if minor < 20:
        raise RuntimeError("Unsupported MSVC toolset detected. Visual Studio 2019 or 2022 is required.")
    if minor < 30:
        return "192"
    if minor < 40:
        return "193"
    if minor < 50:
        return "194"
    else:
        raise RuntimeError(
            "Unsupported Visual Studio 18 / VS2026 toolchain detected by Conan "
            "(compiler.version=195). Install/select VS2019 or VS2022 and set "
            'repo_build.msbuild.vs_version = "vs2022" (or "vs2019") in repo.toml.'
        )


def _write_repo_profile() -> Path:
    profile_dir = g_repo_root / "_build" / "conan-home" / "profiles"
    profile_dir.mkdir(parents=True, exist_ok=True)

    lines = ["include(default)", ""]
    if os.name == "nt":
        msvc_version = _detect_conan_msvc_version_from_host_deps()
        if msvc_version is None:
            raise RuntimeError("Failed to detect Visual Studio version from host dependencies.")

        lines.extend(
            [
                "[settings]",
                f"compiler.version={msvc_version}",
                "",
            ]
        )

    lines.extend(
        [
            "[tool_requires]",
            "*: cmake/[>=3.22 <4]",
            "",
        ]
    )

    profile = profile_dir / "repo"
    profile.write_text(
        "\n".join(lines),
        encoding="utf-8",
    )
    return profile


def _export_local_recipes():
    for recipe_path, version in LOCAL_RECIPES:
        if not recipe_path.is_dir():
            raise RuntimeError(f"Local Conan recipe not found: {recipe_path}")
        _run_conan(["export", str(recipe_path), f"--version={version}"])


def install_conan_deps(config: str, deploy_dependencies: list[str] | None = None):
    env = _subprocess_env()
    os.environ["CONAN_HOME"] = env["CONAN_HOME"]
    _ensure_conan(env)
    _ensure_conan_center_remote()
    _ensure_profile(env)

    profile = _write_repo_profile()
    _export_local_recipes()

    build_type = _build_type(config)
    settings = [
        f"--settings=build_type={build_type}",
        f"--settings=assimp/*:build_type={_assimp_build_type(config)}",
        "--settings=compiler.cppstd=17",
    ]
    if os.name == "nt":
        settings.append(f"--settings=compiler.runtime_type={build_type}")

    deployer = g_repo_root / "tools" / "conan" / "target_deps_deploy.py"
    target_deps = g_repo_root / "_build" / "target-deps"
    output_folder = g_repo_root / "_build" / "conan"

    build_args = ["--build=missing"]
    if os.name != "nt":
        build_args.append("--build=draco/*")

    command = [
        "install",
        str(g_repo_root),
        *build_args,
        f"--profile:host={profile}",
        f"--profile:build={profile}",
        f"--conf=tools.build:jobs={_conan_build_jobs()}",
        *settings,
        f"--output-folder={output_folder}",
        f"--deployer={deployer}",
        f"--deployer-folder={target_deps}",
    ]

    old_deploy_dependencies = os.environ.get("CONAN_DEPLOY_DEPENDENCIES")
    if deploy_dependencies:
        os.environ["CONAN_DEPLOY_DEPENDENCIES"] = ",".join(deploy_dependencies)
    else:
        os.environ.pop("CONAN_DEPLOY_DEPENDENCIES", None)

    print(f"Installing Conan dependencies for {build_type}...")
    try:
        _run_conan(command)
    except FileNotFoundError as exc:
        raise RuntimeError(f"A required tool was not found during Conan dependency installation: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Conan dependency installation failed with exit code {exc.returncode}.") from exc
    finally:
        if old_deploy_dependencies is None:
            os.environ.pop("CONAN_DEPLOY_DEPENDENCIES", None)
        else:
            os.environ["CONAN_DEPLOY_DEPENDENCIES"] = old_deploy_dependencies


def main():
    parser = argparse.ArgumentParser(
        description="Install Conan packages into the _build/target-deps layout used by Premake."
    )
    parser.add_argument("--config", default="release", choices=["debug", "release"])
    parser.add_argument("--deploy-dependency", action="append", dest="deploy_dependencies")
    options = parser.parse_args()
    install_conan_deps(options.config, options.deploy_dependencies)


if __name__ == "__main__":
    main()
