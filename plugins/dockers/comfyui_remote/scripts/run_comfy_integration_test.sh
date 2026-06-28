#!/usr/bin/env bash
# Live ComfyUI integration: 1girl @ 1024² → random 1/8 refine → proof PNGs in tests/output/
set -euo pipefail

COMFY_URL="${COMFY_URL:-http://127.0.0.1:8188}"
COMFY_CHECKPOINT="${COMFY_CHECKPOINT:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMFY_INTEGRATION_SAVE_DIR="${COMFY_INTEGRATION_SAVE_DIR:-$SCRIPT_DIR/../tests/output}"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
KRITA_BUILD="${KRITA_BUILD:-$ROOT/build}"

echo "=== ComfyUI inpaint integration test (live server) ==="
echo "COMFY_URL=$COMFY_URL"
echo "COMFY_CHECKPOINT=${COMFY_CHECKPOINT:-<auto>}"
echo "COMFY_INTEGRATION_SAVE_DIR=$COMFY_INTEGRATION_SAVE_DIR (cleared at test start)"
echo "KRITA_BUILD=$KRITA_BUILD"
echo

if ! curl -sf -m 5 "${COMFY_URL}/system_stats" >/dev/null; then
  echo "[FAIL] ComfyUI not reachable at $COMFY_URL"
  exit 1
fi

TEST_BIN="$KRITA_BUILD/bin/ComfyInpaintIntegrationTest"
if [[ ! -x "$TEST_BIN" ]]; then
  echo "[FAIL] $TEST_BIN missing — build with:"
  echo "  cd \"$KRITA_BUILD\" && cmake --build . --target ComfyInpaintIntegrationTest -j\"\$(nproc)\""
  exit 1
fi

export COMFY_INTEGRATION_TEST=1
export COMFY_URL
export COMFY_INTEGRATION_SAVE_DIR
[[ -n "$COMFY_CHECKPOINT" ]] && export COMFY_CHECKPOINT

cd "$KRITA_BUILD"
QT_QPA_PLATFORM=offscreen "$TEST_BIN" testGenerateThenRefineRandomEighthNotBlack

echo
echo "Proof images:"
ls -la "$COMFY_INTEGRATION_SAVE_DIR"
