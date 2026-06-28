#!/usr/bin/env bash
# Print build gate status and exact next command.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"

echo "=== ComfyUI remote build status ==="
echo "ROOT=$ROOT"
echo "BUILD=${KRITA_BUILD:-$ROOT/build}"
echo

if command -v rpm >/dev/null 2>&1; then
  if rpm -q kf5-kconfig-devel >/dev/null 2>&1; then
    echo "[ok] kf5-kconfig-devel installed"
  else
    echo "[BLOCKED] kf5-kconfig-devel missing"
  fi
  if rpm -q qt5-qtbase-devel >/dev/null 2>&1; then
    echo "[ok] qt5-qtbase-devel installed"
  else
    echo "[warn] qt5-qtbase-devel missing (needed for full cmake)"
  fi
  if rpm -q boost-devel >/dev/null 2>&1; then
    echo "[ok] boost-devel installed"
  else
    echo "[warn] boost-devel missing (needed for full cmake)"
  fi
else
  echo "[warn] rpm not found — cannot probe packages"
fi

if [[ -f "${KRITA_BUILD:-$ROOT/build}/build.ninja" ]]; then
  echo "[ok] build.ninja present"
else
  echo "[BLOCKED] no configured build tree — run cmake after deps install"
fi

ANDROID="$HOME/source/krita-auto-1/persistent/deps/_install"
if [[ -f "$ANDROID/lib/liblcms2.so" ]] && file "$ANDROID/lib/liblcms2.so" | grep -qE 'ARM|Android'; then
  echo "[info] krita-auto-1 deps = Android ARM — not for native desktop build"
fi

if [[ -f "$PREFIX/lib/cmake/Lager/LagerConfig.cmake" ]]; then
  echo "[info] krita-native-deps (Immer/Zug/Lager): $PREFIX"
else
  echo "[warn] Immer/Zug/Lager missing — run: $SCRIPT_DIR/build_krita_header_deps.sh"
fi

echo
echo "Next (sudo, recommended):"
echo "  $SCRIPT_DIR/install_kf5_build_deps.sh --verify"
echo "  # --verify = full Krita BuildRequires (~187 RPMs on Nobara) + build_verify"
echo "Dry-run deps (no sudo):"
echo "  $SCRIPT_DIR/install_kf5_build_deps.sh --dry-run"
echo
echo "Offline only:"
echo "  $SCRIPT_DIR/verify_all.sh"
echo "  $SCRIPT_DIR/commit_readiness.sh"
