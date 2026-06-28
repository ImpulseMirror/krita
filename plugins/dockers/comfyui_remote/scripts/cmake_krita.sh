#!/usr/bin/env bash
# Configure Krita build tree with native deps (Immer/Zug/Lager + system -devel).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${KRITA_BUILD:-$ROOT/build}"

"$SCRIPT_DIR/build_krita_header_deps.sh"
# shellcheck source=krita_cmake_flags.sh
source "$SCRIPT_DIR/krita_cmake_flags.sh"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
echo "cmake .. ${KRITA_CMAKE_ARGS[*]}"
cmake .. "${KRITA_CMAKE_ARGS[@]}"
