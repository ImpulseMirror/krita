#!/usr/bin/env bash
# Post–architecture-unification build + unit test gate (needs KF5 -devel + configured Krita build tree).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"
BUILD_DIR="${KRITA_BUILD:-$ROOT/build}"

# shellcheck source=kf5_build_deps.inc.sh
source "$SCRIPT_DIR/kf5_build_deps.inc.sh"

echo "=== ComfyUI remote build verify (A0–N11 + P9 runtime gate) ==="
echo "ROOT=$ROOT"
echo "BUILD_DIR=$BUILD_DIR"
echo

echo "--- Offline checklist ---"
"$SCRIPT_DIR/port_ci_checklist.sh"
echo

if command -v rpm >/dev/null 2>&1; then
  missing_kf5=0
  for pkg in "${KF5_DEVEL_RPMs[@]}"; do
    if ! rpm -q "$pkg" >/dev/null 2>&1; then
      echo "[FAIL] missing RPM: $pkg"
      missing_kf5=1
    fi
  done
  if [[ "$missing_kf5" -ne 0 ]]; then
    echo
    echo "Install KF5 development packages, then re-run:"
    echo "  $SCRIPT_DIR/install_kf5_build_deps.sh"
    echo "  $SCRIPT_DIR/install_kf5_build_deps.sh --verify   # install + build_verify"
    exit 1
  fi
  echo "[ok] KF5 -devel RPMs present"
fi

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "[FAIL] build directory missing: $BUILD_DIR"
  echo "Create with: mkdir -p \"$BUILD_DIR\""
  exit 1
fi

echo "--- Krita header deps (Immer/Zug/Lager) ---"
"$SCRIPT_DIR/build_krita_header_deps.sh"
echo

if command -v rpm >/dev/null 2>&1 && ! rpm -q turbojpeg >/dev/null 2>&1; then
  echo "[warn] turbojpeg runtime missing (libjpeg-turbo cmake needs libturbojpeg.so)"
  echo "  sudo dnf install -y turbojpeg"
fi
echo

echo "--- cmake ---"
# shellcheck source=krita_cmake_flags.sh
source "$SCRIPT_DIR/krita_cmake_flags.sh"
(
  cd "$BUILD_DIR"
  if grep -q 'krita-auto-1' CMakeCache.txt 2>/dev/null; then
    echo "[warn] clearing stale Qt5 cache paths from krita-auto-1"
    cmake .. -UQt5Core_DIR "${KRITA_CMAKE_ARGS[@]}"
  else
    cmake .. "${KRITA_CMAKE_ARGS[@]}"
  fi
)

echo "--- ninja kritacomfyuiremote_static ---"
cmake --build "$BUILD_DIR" --target kritacomfyuiremote_static -j"$(nproc)"

echo "--- ctest ---"
(
  cd "$BUILD_DIR"
  ctest -R 'ComfyPort|ComfyWorkflow|ComfyUIRemoteDock' --output-on-failure
)

echo
echo "Build verify passed (P9 runtime). P10 manual QA: docs/INPAINT_CONTEXT_PORT_PLAN.md §P10."
