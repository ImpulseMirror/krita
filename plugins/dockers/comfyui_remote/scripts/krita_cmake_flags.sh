#!/usr/bin/env bash
# Shared cmake arguments for native Krita build (avoid stale ECM / missing Immer cache).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${KRITA_BUILD:-$ROOT/build}"
DEPS_PREFIX="${KRITA_DEPS_PREFIX:-$HOME/krita-native-deps/usr}"

immer_cmake() {
  if [[ -f "$DEPS_PREFIX/lib64/cmake/Immer/ImmerConfig.cmake" ]]; then
    echo "$DEPS_PREFIX/lib64/cmake/Immer"
  elif [[ -f "$DEPS_PREFIX/lib/cmake/Immer/ImmerConfig.cmake" ]]; then
    echo "$DEPS_PREFIX/lib/cmake/Immer"
  fi
}
zug_cmake() {
  if [[ -f "$DEPS_PREFIX/lib64/cmake/Zug/ZugConfig.cmake" ]]; then
    echo "$DEPS_PREFIX/lib64/cmake/Zug"
  elif [[ -f "$DEPS_PREFIX/lib/cmake/Zug/ZugConfig.cmake" ]]; then
    echo "$DEPS_PREFIX/lib/cmake/Zug"
  fi
}
lager_cmake() {
  if [[ -f "$DEPS_PREFIX/lib64/cmake/Lager/LagerConfig.cmake" ]]; then
    echo "$DEPS_PREFIX/lib64/cmake/Lager"
  elif [[ -f "$DEPS_PREFIX/lib/cmake/Lager/LagerConfig.cmake" ]]; then
    echo "$DEPS_PREFIX/lib/cmake/Lager"
  fi
}

IMMER_DIR="$(immer_cmake || true)"
ZUG_DIR="$(zug_cmake || true)"
LAGER_DIR="$(lager_cmake || true)"

if [[ -z "$IMMER_DIR" || -z "$ZUG_DIR" || -z "$LAGER_DIR" ]]; then
  echo "[krita_cmake_flags] missing Immer/Zug/Lager under $DEPS_PREFIX" >&2
  echo "  run: $SCRIPT_DIR/build_krita_header_deps.sh" >&2
  return 1 2>/dev/null || exit 1
fi

# Stale cache: ECM prefix, failed Immer probe, or wrong generator (plain cmake .. vs ninja).
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  stale=0
  if grep -q 'CMAKE_INSTALL_PREFIX:.*ECM/prefix' "$BUILD_DIR/CMakeCache.txt"; then
    stale=1
  fi
  if grep -q 'Immer_DIR:.*NOTFOUND' "$BUILD_DIR/CMakeCache.txt"; then
    stale=1
  fi
  if ! grep -q 'CMAKE_GENERATOR:INTERNAL=Ninja' "$BUILD_DIR/CMakeCache.txt"; then
    stale=1
  fi
  if [[ "$stale" -eq 1 ]]; then
    echo "[warn] removing stale CMakeCache (ECM prefix, Immer_DIR-NOTFOUND, or non-Ninja generator)"
    rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles" "$BUILD_DIR/_deps"
  fi
fi

CMAKE_PREFIX_PATH="$DEPS_PREFIX:/usr"
if [[ -n "${ECM_DIR:-}" ]]; then
  :
elif [[ -d /usr/share/ECM/cmake ]]; then
  export ECM_DIR=/usr/share/ECM/cmake
elif [[ -d "$HOME/source/ECM/prefix/share/ECM/cmake" ]]; then
  export ECM_DIR="$HOME/source/ECM/prefix/share/ECM/cmake"
fi

export KRITA_CMAKE_ARGS=(
  -G Ninja
  -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/install"
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
  -DImmer_DIR="$IMMER_DIR"
  -DZug_DIR="$ZUG_DIR"
  -DLager_DIR="$LAGER_DIR"
)
