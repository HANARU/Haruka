"""Create a standalone Haruka project outside the engine repository (P4, 2026-11-19).

Usage:
    python -I new_project.py <folder> <name> [--company X] [--package-id Y]
"""
import argparse
import re
import sys
from pathlib import Path


def sanitize_ident(text: str, fallback: str = "app") -> str:
    s = re.sub(r"[^a-z0-9]", "", text.lower())
    if not s or not s[0].isalpha():
        s = fallback + s
    return s


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8")

    ap = argparse.ArgumentParser(description="Create a new Haruka project")
    ap.add_argument("folder", help="Target project folder (must not already exist)")
    ap.add_argument("name", help="Project name (C++/CMake identifier, e.g. MyGame)")
    ap.add_argument("--company", default="Haruka", help="Company/studio name (default: Haruka)")
    ap.add_argument("--package-id", default=None, help="Android package ID (default: com.<company>.<name>)")
    args = ap.parse_args()

    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", args.name):
        sys.stderr.write(f"error: project name must be a valid identifier ([A-Za-z_][A-Za-z0-9_]*): {args.name}\n")
        return 1

    target = Path(args.folder)
    if target.exists():
        sys.stderr.write(f"error: target folder already exists (refusing to overwrite): {target}\n")
        return 1

    company = args.company.strip() or "Haruka"
    package_id = args.package_id.strip() if args.package_id else f"com.{sanitize_ident(company, 'haruka')}.{sanitize_ident(args.name, 'game')}"

    (target / "Config").mkdir(parents=True, exist_ok=False)
    (target / "Content" / "scenes").mkdir(parents=True, exist_ok=True)
    (target / "Content" / "models").mkdir(parents=True, exist_ok=True)
    (target / "Content" / "prefabs").mkdir(parents=True, exist_ok=True)
    (target / "Content" / "audio").mkdir(parents=True, exist_ok=True)
    (target / "Source").mkdir(parents=True, exist_ok=True)

    (target / f"{args.name}.hproject").write_text(
        "# Haruka project descriptor\n"
        f"name = {args.name}\n"
        f"company = {company}\n"
        f"packageId = {package_id}\n"
        "engineVersion = 0.1.0\n",
        encoding="utf-8",
        newline="\n",
    )

    (target / "Config" / "project.ini").write_text(
        "# Haruka project settings\n"
        "startScene = /Game/scenes/Start.hscene\n"
        "xr.height = tracked\n"
        "xr.eyeHeight = 1.7\n"
        "xr.startInVr = false\n"
        "render.ao = true\n",
        encoding="utf-8",
        newline="\n",
    )

    (target / "Content" / "scenes" / "Start.hscene").write_text(
        "# Haruka Scene v5\n"
        "ACTOR:Sun\n"
        "POS:0 0 0\n"
        "ROT:-50 0 0\n"
        "QUAT:-0.42261826 0 0 0.90630779\n"
        "SCALE:1 1 1\n"
        "ACTIVE:1\n"
        "COMPONENT:LightComponent\n"
        "enabled=1\n"
        "type=directional\n"
        "color=1 1 1\n"
        "intensity=3\n"
        "attenuationRadius=10\n"
        "innerConeAngle=15\n"
        "outerConeAngle=30\n"
        "useTemperature=0\n"
        "temperatureK=6500\n"
        "castShadows=1\n"
        "END_COMPONENT\n"
        "END_ACTOR\n"
        "ACTOR:Ground\n"
        "POS:0 -0.1 0\n"
        "ROT:0 0 0\n"
        "QUAT:0 0 0 1\n"
        "SCALE:20 0.2 20\n"
        "ACTIVE:1\n"
        "COMPONENT:StaticMeshComponent\n"
        "enabled=1\n"
        "visible=1\n"
        "transparent=0\n"
        "prim=box|0x0|0|0.0000\n"
        "primMaterial=color B4B4B4FF 0 0.6\n"
        "END_COMPONENT\n"
        "END_ACTOR\n"
        "ACTOR:MainCamera\n"
        "POS:0 1.6 3\n"
        "ROT:0 0 0\n"
        "QUAT:0 0 0 1\n"
        "SCALE:1 1 1\n"
        "ACTIVE:1\n"
        "COMPONENT:CameraComponent\n"
        "enabled=1\n"
        "fovY=75\n"
        "zNear=0.05\n"
        "zFar=1000\n"
        "primary=1\n"
        "END_COMPONENT\n"
        "END_ACTOR\n",
        encoding="utf-8",
        newline="\n",
    )

    (target / "Source" / f"{args.name}GameModule.cpp").write_text(
        '#include "runtime/IGameModule.hpp"\n\n'
        f"class {args.name}GameModule final : public IGameModule {{\n"
        "public:\n"
        "    void onStart(EngineContext& /*ctx*/) override {}\n"
        "};\n\n"
        "std::unique_ptr<IGameModule> createGameModule() {\n"
        f"    return std::make_unique<{args.name}GameModule>();\n"
        "}\n",
        encoding="utf-8",
        newline="\n",
    )

    (target / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.21)\n"
        f"project({args.name} LANGUAGES C CXX)\n\n"
        "set(CMAKE_CXX_STANDARD 20)\n"
        "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n"
        'set(HARUKA_ENGINE_DIR "" CACHE PATH "Path to the Haruka engine repository")\n'
        'if(NOT HARUKA_ENGINE_DIR OR NOT EXISTS "${HARUKA_ENGINE_DIR}/CMakeLists.txt")\n'
        '    message(FATAL_ERROR "Set -DHARUKA_ENGINE_DIR=<path to Haruka> to an existing Haruka engine checkout")\n'
        "endif()\n\n"
        'add_subdirectory("${HARUKA_ENGINE_DIR}" haruka)\n\n'
        "haruka_add_project(\n"
        f"    NAME        {args.name}\n"
        "    PROJECT_DIR ${CMAKE_CURRENT_SOURCE_DIR}\n"
        f"    SOURCES     ${{CMAKE_CURRENT_SOURCE_DIR}}/Source/{args.name}GameModule.cpp\n"
        ")\n",
        encoding="utf-8",
        newline="\n",
    )

    (target / ".gitignore").write_text(
        "Saved/\n"
        "build/\n"
        "out/\n",
        encoding="utf-8",
        newline="\n",
    )

    print(f"Created Haruka project '{args.name}' at {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
