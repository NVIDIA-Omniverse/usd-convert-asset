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
WINDOWS_MSVC_VERSION = "192"

g_repo_root = Path(__file__).resolve().parents[2]
LOCAL_RECIPES = ((g_repo_root / "tools" / "conan" / "recipes" / "assimp" / "5.x", "5.4.3"),)


def _build_type(config: str) -> str:
    return "Debug" if config.lower() == "debug" else "Release"


def _assimp_build_type(config: str) -> str:
    return "Debug" if config.lower() == "debug" else "RelWithDebInfo"


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


def _write_repo_profile() -> Path:
    profile_dir = g_repo_root / "_build" / "conan-home" / "profiles"
    profile_dir.mkdir(parents=True, exist_ok=True)

    lines = ["include(default)", ""]
    if os.name == "nt":
        lines.extend(
            [
                "[settings]",
                f"compiler.version={WINDOWS_MSVC_VERSION}",
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


def install_conan_deps(config: str):
    env = _subprocess_env()
    os.environ["CONAN_HOME"] = env["CONAN_HOME"]
    _ensure_conan(env)
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

    command = [
        "install",
        str(g_repo_root),
        "--build=missing",
        f"--profile:host={profile}",
        f"--profile:build={profile}",
        *settings,
        f"--output-folder={output_folder}",
        f"--deployer={deployer}",
        f"--deployer-folder={target_deps}",
    ]

    print(f"Installing Conan dependencies for {build_type}...")
    try:
        _run_conan(command)
    except FileNotFoundError as exc:
        raise RuntimeError(f"A required tool was not found during Conan dependency installation: {exc}") from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(f"Conan dependency installation failed with exit code {exc.returncode}.") from exc


def main():
    parser = argparse.ArgumentParser(
        description="Install Conan packages into the _build/target-deps layout used by Premake."
    )
    parser.add_argument("--config", default="release", choices=["debug", "release"])
    options = parser.parse_args()
    install_conan_deps(options.config)


if __name__ == "__main__":
    main()
