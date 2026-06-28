#!/usr/bin/env python3
"""Download and extract KF5 + Qt5 -devel RPMs to ~/krita-deps (no sudo)."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path.home() / "krita-deps"
RPMS = ROOT / "rpms"
PREFIX = ROOT / "root"

KF5_DEVEL = [
    "kf5-kconfig-devel",
    "kf5-kwidgetsaddons-devel",
    "kf5-kcompletion-devel",
    "kf5-kcoreaddons-devel",
    "kf5-kguiaddons-devel",
    "kf5-ki18n-devel",
    "kf5-kitemviews-devel",
]

QT_DEVEL_RUNTIME = [
    "qt5-qtbase-devel",
    "qt5-qtbase",
    "qt5-qtbase-gui",
    "qt5-qtbase-common",
    "qt5-qtsvg-devel",
    "qt5-qtsvg",
    "qt5-qtxmlpatterns-devel",
    "qt5-qtxmlpatterns",
    "qt5-qttools-devel",
    "qt5-qttools",
    "qt5-qtdeclarative-devel",
    "qt5-qtdeclarative",
    "qt5-qtquickcontrols2-devel",
    "qt5-qtquickcontrols2",
    "qt5-qtx11extras-devel",
    "qt5-qtx11extras",
    "qt5-qtnetworkauth-devel",
    "qt5-qtprintsupport-devel",
    "qt5-qtconcurrent-devel",
    "qt5-qtsql-devel",
]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main() -> int:
    RPMS.mkdir(parents=True, exist_ok=True)
    PREFIX.mkdir(parents=True, exist_ok=True)
    pkgs = KF5_DEVEL + QT_DEVEL_RUNTIME
    run(["dnf", "download", "-y", "--destdir", str(RPMS), *pkgs])
    for rpm in sorted(RPMS.glob("*.rpm")):
        if ".i686" in rpm.name:
            continue
        print(f"extract {rpm.name}")
        subprocess.run(
            f"rpm2cpio {rpm} | (cd {PREFIX} && cpio -idmu)",
            shell=True,
            check=True,
        )
    print(f"\nDone. Prefix: {PREFIX / 'usr'}")
    print("Next: source plugins/dockers/comfyui_remote/scripts/local_build_env.sh")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
