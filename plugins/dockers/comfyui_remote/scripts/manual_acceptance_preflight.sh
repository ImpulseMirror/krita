#!/usr/bin/env bash
# Preflight for manual acceptance M1–M10 (see docs/MANUAL_ACCEPTANCE_RUN.md)
set -euo pipefail

COMFY_URL="${COMFY_URL:-http://127.0.0.1:8188}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"
KRITA_BUILD="${KRITA_BUILD:-$ROOT/_build}"

echo "=== ComfyUI remote manual acceptance preflight ==="
echo "COMFY_URL=$COMFY_URL"
echo

fail=0

check_http() {
  local path="$1"
  local label="$2"
  if curl -sf -m 5 "${COMFY_URL}${path}" >/dev/null; then
    echo "[ok] $label (${COMFY_URL}${path})"
  else
    echo "[FAIL] $label — is ComfyUI running at $COMFY_URL?"
    fail=1
  fi
}

check_http "/system_stats" "M1: system_stats"
check_http "/object_info" "M1: object_info"
check_http "/models/checkpoints" "M1: checkpoints list"

if [[ -d "$KRITA_BUILD/bin" ]]; then
  found=0
  for t in ComfyPortP51Test ComfyPortP52Test ComfyWorkflowEngineGoldenTest ComfyUIRemoteDockTest; do
    if [[ -x "$KRITA_BUILD/bin/$t" ]]; then
      echo "[ok] test binary: $KRITA_BUILD/bin/$t"
      found=1
    fi
  done
  if [[ "$found" -eq 0 ]]; then
    echo "[warn] No Comfy* test binaries in $KRITA_BUILD/bin — rebuild Krita after pulling comfyui-perfect-port"
    fail=1
  fi
else
  echo "[warn] KRITA_BUILD not found ($KRITA_BUILD) — configure fresh _build on this machine"
  fail=1
fi

if python3 -c "import json" 2>/dev/null; then
  if python3 "$PLUGIN_DIR/scripts/export_workflow_fixture.py" >/dev/null 2>&1; then
    echo "[ok] golden fixture exporter (Python)"
  else
    echo "[warn] export_workflow_fixture.py failed"
  fi
fi

echo
if [[ "$fail" -eq 0 ]]; then
  echo "Preflight passed. Run scenarios in docs/MANUAL_ACCEPTANCE_RUN.md with Krita (comfyui_remote plugin)."
  exit 0
fi
echo "Preflight incomplete — fix blockers before M1–M10 desktop run."
exit 1
