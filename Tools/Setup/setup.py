"""Haruka Engine Setup script (E3, runs on embedded Python 3.15.2)."""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

SETUP_DIR = Path(__file__).resolve().parent
REPO_ROOT = SETUP_DIR.parent.parent
if str(SETUP_DIR) not in sys.path:
    sys.path.insert(0, str(SETUP_DIR))

import engine_registry  # noqa: E402


def parse_version_tuple(v: str) -> tuple[int, ...]:
    return tuple(int(p) for p in v.split(".") if p.isdigit())


def read_cmake_project_info(cmakelists: Path) -> tuple[str, str]:
    text = cmakelists.read_text(encoding="utf-8")
    m_min = re.search(r"cmake_minimum_required\s*\(\s*VERSION\s+([0-9]+(?:\.[0-9]+)*)", text)
    m_ver = re.search(r"project\s*\(\s*HarukaEngine\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)", text)
    min_cmake = m_min.group(1) if m_min else "3.21"
    eng_ver = m_ver.group(1) if m_ver else "0.1.0"
    return min_cmake, eng_ver


def find_vs_generator() -> tuple[str, str]:
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise RuntimeError(
            "Visual Studio Installer (vswhere.exe) not found. Install Visual Studio 2022 or 2026 with 'Desktop development with C++'."
        )

    def query(prerelease: bool) -> list[dict]:
        cmd = [
            str(vswhere),
            "-latest",
            "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-format", "json",
        ]
        if prerelease:
            cmd.append("-prerelease")
        res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if res.returncode != 0 or not res.stdout.strip():
            return []
        try:
            return json.loads(res.stdout)
        except json.JSONDecodeError:
            return []

    instances = query(prerelease=False) or query(prerelease=True)
    if not instances:
        raise RuntimeError(
            "MSVC C++ toolset not found via vswhere. Install Visual Studio 2022/2026 with 'Desktop development with C++'."
        )

    inst = instances[0]
    ver = inst.get("installationVersion", "")
    major = int(ver.split(".")[0]) if ver and ver.split(".")[0].isdigit() else 0
    if major >= 18:
        gen = "Visual Studio 18 2026"
    elif major == 17:
        gen = "Visual Studio 17 2022"
    else:
        raise RuntimeError(
            f"Unsupported Visual Studio version '{ver}'. Install Visual Studio 2022 (v17) or 2026 (v18)."
        )
    return gen, f"{inst.get('displayName', 'Visual Studio')} ({ver})"


def check_cmake(min_version: str) -> str:
    cmake_exe = shutil.which("cmake")
    if not cmake_exe:
        raise RuntimeError(
            f"CMake not found in PATH. Install CMake {min_version}+ from https://cmake.org/download/ and add it to PATH."
        )
    res = subprocess.run([cmake_exe, "--version"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    m = re.search(r"cmake version\s+([0-9]+(?:\.[0-9]+)*)", res.stdout)
    if not m:
        raise RuntimeError(f"Could not parse 'cmake --version' output: {res.stdout.strip()}")
    found = m.group(1)
    if parse_version_tuple(found) < parse_version_tuple(min_version):
        raise RuntimeError(
            f"CMake {found} is too old (minimum required: {min_version}). Upgrade CMake from https://cmake.org/download/."
        )
    return found


def check_git() -> str:
    git_exe = shutil.which("git")
    if not git_exe:
        raise RuntimeError("Git not found in PATH. Install Git for Windows from https://git-scm.com/download/win.")
    res = subprocess.run([git_exe, "--version"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return res.stdout.strip()


def check_vulkan_sdk_optional() -> str | None:
    env_sdk = os.environ.get("VULKAN_SDK", "").strip()
    if env_sdk and Path(env_sdk).is_dir():
        return env_sdk
    base = Path("C:/VulkanSDK")
    if base.is_dir():
        dirs = sorted([d for d in base.iterdir() if d.is_dir()], reverse=True)
        if dirs:
            return str(dirs[0])
    return None


def check_requirements(req_file: Path) -> None:
    if not req_file.exists():
        print("[Setup] Packages: none (requirements.txt not present).")
        return
    pkgs = [
        line.strip()
        for line in req_file.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]
    if not pkgs:
        print("[Setup] Packages: none (requirements.txt is empty).")
        return
    print(f"[Setup] Installing {len(pkgs)} Python package(s)...")
    subprocess.run(
        [sys.executable, "-m", "pip", "install", "--only-binary", ":all:", "-r", str(req_file)],
        check=True,
    )


def run_step(cmd: list[str], stage: str, fix_hint: str) -> None:
    res = subprocess.run(cmd)
    if res.returncode != 0:
        raise RuntimeError(f"Step '{stage}' failed (exit code {res.returncode}). {fix_hint}")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8")

    ap = argparse.ArgumentParser(description="Haruka Engine Setup")
    ap.add_argument("--unattended", action="store_true", help="Non-interactive mode (no prompts)")
    ap.add_argument("--build-dir", default=str(REPO_ROOT / "build"), help="Engine build directory (default: build)")
    ap.add_argument("--engines-ini", default=None, help="Override Engines.ini path (for testing)")
    ap.add_argument("--skip-build", action="store_true", help="Skip CMake configure/build; only check tools and register")
    ap.add_argument("--associate", action="store_true", help="Register .hproject file association for current user (HKCU)")
    args = ap.parse_args()

    t0 = time.monotonic()
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (REPO_ROOT / build_dir).resolve()

    try:
        # 1. Python packages
        check_requirements(SETUP_DIR / "requirements.txt")

        # 2. Prerequisite tools
        min_cmake, eng_ver = read_cmake_project_info(REPO_ROOT / "CMakeLists.txt")
        vs_gen, vs_desc = find_vs_generator()
        cmake_ver = check_cmake(min_cmake)
        git_ver = check_git()
        vk_sdk = check_vulkan_sdk_optional()

        print(f"[Setup] Visual Studio : {vs_desc} -> generator '{vs_gen}'")
        print(f"[Setup] CMake         : {cmake_ver} (min {min_cmake})")
        print(f"[Setup] Git           : {git_ver}")
        if vk_sdk:
            print(f"[Setup] Vulkan SDK    : {vk_sdk} (optional, for validation layers)")
        else:
            print("[Setup] Vulkan SDK    : not installed (optional; install LunarG Vulkan SDK if you need validation layers in Debug)")

        # 3. Configure & Build (Debug + Release)
        if args.skip_build:
            print("[Setup] Skipping configure and build (--skip-build).")
        else:
            cache_file = build_dir / "CMakeCache.txt"
            if cache_file.exists():
                print(f"[Setup] CMake already configured at {build_dir} (skipping configure).")
            else:
                print(f"[Setup] Configuring CMake in {build_dir}...")
                run_step(
                    ["cmake", "-S", str(REPO_ROOT), "-B", str(build_dir), "-G", vs_gen, "-A", "x64"],
                    "CMake configure",
                    "Check CMake/network output above and re-run Setup.bat.",
                )

            print("[Setup] Building Debug...")
            run_step(
                ["cmake", "--build", str(build_dir), "--config", "Debug"],
                "Build Debug",
                "Inspect compiler/linker errors above and re-run Setup.bat.",
            )

            print("[Setup] Building Release...")
            run_step(
                ["cmake", "--build", str(build_dir), "--config", "Release", "--parallel"],
                "Build Release",
                "Inspect compiler/linker errors above and re-run Setup.bat.",
            )

        # 4. Register engine
        config_cmake = build_dir / "HarukaConfig.cmake"
        if not config_cmake.exists():
            raise RuntimeError(
                f"Step 'Engine registration' failed: '{config_cmake}' not found. Run Setup.bat without --skip-build first."
            )

        ini_path = engine_registry.default_engines_ini_path(args.engines_ini)
        entries, pruned = engine_registry.register_engine(
            ini_path=ini_path,
            name="haruka",
            version=eng_ver,
            engine_dir=REPO_ROOT,
            build_dir=build_dir,
        )
        elapsed = time.monotonic() - t0
        print(
            f"[Setup] Registered haruka {eng_ver} -> {ini_path} "
            f"(entries={len(entries)}, pruned={pruned}) in {elapsed:.1f}s"
        )

        if args.associate:
            launcher_exe = build_dir / "Release" / "HarukaLauncher.exe"
            if not launcher_exe.exists():
                launcher_exe = build_dir / "Debug" / "HarukaLauncher.exe"
            if launcher_exe.exists():
                run_step(
                    [str(launcher_exe), "associate"],
                    "File association",
                    "Run HarukaLauncher.exe associate manually.",
                )
            else:
                print("[Setup] Warning: HarukaLauncher.exe not found; skipping --associate.")
        return 0
    except Exception as exc:
        sys.stderr.write(f"[Setup] ERROR: {exc}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
