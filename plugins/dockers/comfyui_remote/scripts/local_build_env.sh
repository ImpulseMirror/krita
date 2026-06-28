#!/usr/bin/env bash
# Source before cmake when KF5 -devel RPMs are not installed system-wide.
# Provides KF5 + Qt5 headers/cmake from ~/krita-deps (dnf download, no sudo).
#
# WARNING: Full native Krita configure still needs system -devel packages (Immer,
# Boost, image libs, …). krita-auto-1/persistent/deps is Android ARM — do NOT
# add it to CMAKE_PREFIX_PATH for desktop x86_64 builds.
#
# Usage:
#   python3 plugins/dockers/comfyui_remote/scripts/bootstrap_user_kf5_deps.py
#   source plugins/dockers/comfyui_remote/scripts/local_build_env.sh
#
# Recommended (sudo):
#   plugins/dockers/comfyui_remote/scripts/install_kf5_build_deps.sh --verify

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"

KRITA_USER_DEPS="${KRITA_USER_DEPS:-$HOME/krita-deps/root/usr}"
ECM_PREFIX="${ECM_PREFIX:-$HOME/source/ECM/prefix}"
ANDROID_DEPS="${HOME}/source/krita-auto-1/persistent/deps/_install"

if [[ ! -d "$KRITA_USER_DEPS/lib64/cmake/KF5Config" ]]; then
  echo "[local_build_env] KF5/Qt user prefix missing — run:" >&2
  echo "  python3 $SCRIPT_DIR/bootstrap_user_kf5_deps.py" >&2
  return 1 2>/dev/null || exit 1
fi

if [[ -f "$ANDROID_DEPS/lib/liblcms2.so" ]] && file "$ANDROID_DEPS/lib/liblcms2.so" | grep -qE 'ARM|Android'; then
  echo "[local_build_env] note: $ANDROID_DEPS is Android ARM — ignored for native build" >&2
fi

_disable_broken_cmake() {
  local cmake_dir="$KRITA_USER_DEPS/lib64/cmake"
  [[ -d "$cmake_dir" ]] || return 0
  local d base
  for d in "$cmake_dir"/*; do
    [[ -d "$d" ]] || continue
    base="$(basename "$d")"
    case "$base" in
      KF5*|Qt5*|_disabled*) continue ;;
    esac
    mv "$d" "$cmake_dir/_disabled_$base" 2>/dev/null || true
  done
}
_disable_broken_cmake

export KRITA_BUILD="${KRITA_BUILD:-$ROOT/build}"
export CMAKE_PREFIX_PATH="$KRITA_USER_DEPS:${ECM_PREFIX}:${CMAKE_PREFIX_PATH:-}"
export PKG_CONFIG_PATH="$KRITA_USER_DEPS/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"

export KRITA_CMAKE_EXTRA=()

echo "[local_build_env] KRITA_USER_DEPS=$KRITA_USER_DEPS"
echo "[local_build_env] KRITA_BUILD=$KRITA_BUILD"
echo "[local_build_env] CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH"
echo
echo "Partial cmake only (KF5/Qt). Full build + P9 ctest need sudo:"
echo "  $SCRIPT_DIR/install_kf5_build_deps.sh --verify"
