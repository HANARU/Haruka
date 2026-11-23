"""Haruka Engine Registry (Engines.ini) management (E3).

Default path: %LOCALAPPDATA%\\Haruka\\Engines.ini
Override:     --engines-ini=<path> or HARUKA_ENGINES_INI env var.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REQUIRED_KEYS = ("name", "version", "engineDir", "buildDir")
VERSION_RE = re.compile(r"^\d+(?:\.\d+)*$")


class RegistryFormatError(ValueError):
    """Raised when Engines.ini has invalid syntax or missing fields."""


@dataclass
class EngineEntry:
    name: str
    version: str
    engine_dir: str
    build_dir: str


def norm_path(p: str | Path) -> str:
    return Path(p).resolve().as_posix()


def norm_key(p: str) -> str:
    return os.path.normcase(norm_path(p))


def default_engines_ini_path(override: str | None = None) -> Path:
    if override:
        return Path(override)
    env_path = os.environ.get("HARUKA_ENGINES_INI", "").strip()
    if env_path:
        return Path(env_path)
    local_app = os.environ.get("LOCALAPPDATA", "").strip()
    if not local_app:
        raise RuntimeError("LOCALAPPDATA is not set and no --engines-ini / HARUKA_ENGINES_INI was provided.")
    return Path(local_app) / "Haruka" / "Engines.ini"


def parse_engines_ini(text: str, source_name: str = "Engines.ini") -> list[EngineEntry]:
    entries: list[EngineEntry] = []
    in_section = False
    cur: dict[str, str] = {}

    def flush() -> None:
        if not in_section:
            return
        for k in REQUIRED_KEYS:
            if not cur.get(k):
                raise RegistryFormatError(f"{source_name}: section missing required key '{k}'")
        if not VERSION_RE.match(cur["version"]):
            raise RegistryFormatError(f"{source_name}: invalid version '{cur['version']}'")
        entries.append(
            EngineEntry(
                name=cur["name"],
                version=cur["version"],
                engine_dir=Path(cur["engineDir"]).as_posix(),
                build_dir=Path(cur["buildDir"]).as_posix(),
            )
        )

    for line_no, raw in enumerate(text.splitlines(), start=1):
        t = raw.strip()
        if not t or t.startswith("#") or t.startswith(";"):
            continue
        if t.startswith("[") and t.endswith("]") and len(t) > 2:
            flush()
            in_section = True
            cur = {}
            continue
        if not in_section:
            raise RegistryFormatError(f"{source_name}:{line_no}: key-value outside [section]")
        if "=" not in t:
            raise RegistryFormatError(f"{source_name}:{line_no}: expected 'key = value'")
        k, v = t.split("=", 1)
        k = k.strip()
        v = v.strip()
        if k not in REQUIRED_KEYS:
            raise RegistryFormatError(f"{source_name}:{line_no}: unknown key '{k}'")
        cur[k] = v

    flush()
    return entries


def load_engines_ini(ini_path: Path) -> list[EngineEntry]:
    if not ini_path.exists():
        return []
    text = ini_path.read_text(encoding="utf-8")
    return parse_engines_ini(text, str(ini_path))


def write_engines_ini(ini_path: Path, entries: list[EngineEntry]) -> None:
    ini_path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# Haruka Engine Registry (UTF-8)"]
    for idx, e in enumerate(entries):
        lines.append(f"[engine.{idx}]")
        lines.append(f"name = {e.name}")
        lines.append(f"version = {e.version}")
        lines.append(f"engineDir = {Path(e.engine_dir).as_posix()}")
        lines.append(f"buildDir = {Path(e.build_dir).as_posix()}")
    content = "\n".join(lines) + "\n"
    tmp = ini_path.with_suffix(".ini.tmp")
    tmp.write_text(content, encoding="utf-8", newline="\n")
    tmp.replace(ini_path)


def register_engine(
    ini_path: Path,
    name: str,
    version: str,
    engine_dir: Path,
    build_dir: Path,
) -> tuple[list[EngineEntry], int]:
    """Register or update an engine entry; prune moved/missing entries.

    Returns (updated_entries, pruned_count).
    """
    if not VERSION_RE.match(version):
        raise ValueError(f"Invalid engine version: '{version}'")

    existing = load_engines_ini(ini_path)
    target_key = norm_key(str(engine_dir))
    new_entry = EngineEntry(
        name=name,
        version=version,
        engine_dir=norm_path(engine_dir),
        build_dir=norm_path(build_dir),
    )

    kept: list[EngineEntry] = []
    pruned = 0
    replaced = False
    for e in existing:
        if norm_key(e.engine_dir) == target_key:
            kept.append(new_entry)
            replaced = True
            continue
        if not os.path.isdir(e.engine_dir) or not os.path.isdir(e.build_dir):
            pruned += 1
            continue
        kept.append(e)

    if not replaced:
        kept.append(new_entry)

    write_engines_ini(ini_path, kept)
    return kept, pruned


def remove_engine(ini_path: Path, engine_dir: Path) -> bool:
    existing = load_engines_ini(ini_path)
    target_key = norm_key(str(engine_dir))
    kept = [e for e in existing if norm_key(e.engine_dir) != target_key]
    if len(kept) == len(existing):
        return False
    write_engines_ini(ini_path, kept)
    return True


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8")

    ap = argparse.ArgumentParser(description="Haruka Engine Registry tool")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_reg = sub.add_parser("register", help="Register or update an engine")
    p_reg.add_argument("--name", default="haruka")
    p_reg.add_argument("--version", required=True)
    p_reg.add_argument("--engine-dir", required=True)
    p_reg.add_argument("--build-dir", required=True)
    p_reg.add_argument("--engines-ini", default=None)

    p_list = sub.add_parser("list", help="List registered engines")
    p_list.add_argument("--engines-ini", default=None)

    p_rm = sub.add_parser("remove", help="Remove an engine by engineDir")
    p_rm.add_argument("--engine-dir", required=True)
    p_rm.add_argument("--engines-ini", default=None)

    args = ap.parse_args()
    ini_path = default_engines_ini_path(args.engines_ini)

    try:
        if args.cmd == "register":
            entries, pruned = register_engine(
                ini_path, args.name, args.version, Path(args.engine_dir), Path(args.build_dir)
            )
            print(f"[Registry] Registered {args.name} {args.version} -> {ini_path} (total={len(entries)}, pruned={pruned})")
        elif args.cmd == "list":
            entries = load_engines_ini(ini_path)
            print(f"[Registry] {ini_path} ({len(entries)} entries)")
            for e in entries:
                print(f"  - {e.name} {e.version}: engineDir={e.engine_dir}, buildDir={e.build_dir}")
        elif args.cmd == "remove":
            removed = remove_engine(ini_path, Path(args.engine_dir))
            print(f"[Registry] Removed={removed} from {ini_path}")
    except RegistryFormatError as exc:
        sys.stderr.write(f"[Registry] ERROR: {exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
