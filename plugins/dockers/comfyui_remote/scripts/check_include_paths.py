#!/usr/bin/env python3
"""Verify every #include \"Comfy*.h\" resolves via cmake/ComfyIncludeDirs.cmake."""
from __future__ import annotations

import re
import sys
from pathlib import Path

PLUGIN = Path(__file__).resolve().parent.parent
CMAKE = PLUGIN / "cmake" / "ComfyIncludeDirs.cmake"
INCLUDE_RE = re.compile(r'#include\s+"((?:Comfy|kritacomfy)[^"]+\.h)"')
DIR_RE = re.compile(r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([^"]+)"')


def load_include_dirs() -> list[Path]:
    text = CMAKE.read_text(encoding="utf-8")
    dirs = [PLUGIN / m.group(1) for m in DIR_RE.finditer(text)]
    if not dirs:
        raise SystemExit(f"no include dirs parsed from {CMAKE}")
    return dirs


def resolve(header: str, dirs: list[Path]) -> Path | None:
    for d in dirs:
        candidate = d / header
        if candidate.is_file():
            return candidate
    return None


def main() -> int:
    dirs = load_include_dirs()
    missing: list[tuple[Path, str]] = []
    checked = 0
    for path in sorted(PLUGIN.rglob("*")):
        if path.suffix not in {".cpp", ".h"}:
            continue
        if "/tests/" in path.as_posix():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for header in INCLUDE_RE.findall(text):
            checked += 1
            if resolve(header, dirs) is None:
                missing.append((path.relative_to(PLUGIN), header))

    print(f"checked {checked} Comfy includes under {PLUGIN.name}/")
    if missing:
        for src, header in missing[:30]:
            print(f"  missing: {header}  (from {src})")
        if len(missing) > 30:
            print(f"  ... and {len(missing) - 30} more")
        return 1
    print("all Comfy includes resolve")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
