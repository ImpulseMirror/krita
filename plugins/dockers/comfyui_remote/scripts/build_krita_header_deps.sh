#!/usr/bin/env bash
# Build Immer, Zug, Lager into ~/krita-native-deps (Krita find_package deps; not in Fedora RPMs).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEPS_ROOT="${KRITA_DEPS_ROOT:-$HOME/krita-native-deps}"
PREFIX="$DEPS_ROOT/usr"
SRC="$DEPS_ROOT/src"
BUILD="$DEPS_ROOT/build"

declare -A REPOS=(
  [immer]="https://github.com/arximboldi/immer.git|42e6beafed53f2ecd971360270f421f9c2e36642|-Dimmer_BUILD_TESTS=OFF -Dimmer_BUILD_EXAMPLES=OFF"
  [zug]="https://github.com/arximboldi/zug.git|deb266f4c7c35d325de7eb3d033f06e0809495f2|-Dzug_BUILD_TESTS=OFF"
  [lager]="https://github.com/dimula73/lager.git|0b6ab3e0e880bc36be5da4984d768fde03b7cf19|-Dlager_BUILD_TESTS=OFF -Dlager_BUILD_EXAMPLES=OFF"
)

have_all=1
[[ -f "$PREFIX/lib/cmake/Immer/ImmerConfig.cmake" ]] || have_all=0
[[ -f "$PREFIX/lib/cmake/Zug/ZugConfig.cmake" ]] || have_all=0
[[ -f "$PREFIX/lib/cmake/Lager/LagerConfig.cmake" ]] || have_all=0

if [[ "$have_all" -eq 1 ]]; then
  echo "[ok] header deps present under $PREFIX"
  exit 0
fi

mkdir -p "$SRC" "$BUILD" "$PREFIX"
nproc_jobs="$(nproc)"

for name in immer zug lager; do
  IFS='|' read -r url tag extra <<<"${REPOS[$name]}"
  src_dir="$SRC/$name"
  build_dir="$BUILD/$name"
  if [[ ! -d "$src_dir/.git" ]]; then
    echo "clone $name @ ${tag:0:8}..."
    git clone "$url" "$src_dir"
  fi
  git -C "$src_dir" fetch --depth 1 origin "$tag" 2>/dev/null || true
  git -C "$src_dir" checkout -q "$tag"
  echo "build $name..."
  cmake -S "$src_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    $extra
  cmake --build "$build_dir" -j"$nproc_jobs"
  cmake --install "$build_dir"
done

echo "[ok] installed Immer/Zug/Lager -> $PREFIX"
