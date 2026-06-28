#!/usr/bin/env bash
# Preflight for manual acceptance M1–M10 (see docs/MANUAL_ACCEPTANCE_RUN.md)
set -euo pipefail

COMFY_URL="${COMFY_URL:-http://127.0.0.1:8188}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"
KRITA_BUILD="${KRITA_BUILD:-$ROOT/build}"

echo "=== ComfyUI remote manual acceptance preflight (M1–M10) ==="
echo "COMFY_URL=$COMFY_URL"
echo "KRITA_BUILD=$KRITA_BUILD"
echo

fail=0
warn=0
ok() { echo "[ok] $*"; }
bad() { echo "[FAIL] $*"; fail=1; }
maybe() { echo "[warn] $*"; warn=1; }

echo "--- Offline checklist ---"
if "$SCRIPT_DIR/port_ci_checklist.sh"; then
  ok "port_ci_checklist.sh"
else
  bad "port_ci_checklist.sh"
fi
echo

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

if [[ -x "$KRITA_BUILD/bin/krita" ]]; then
  ok "Krita: $KRITA_BUILD/bin/krita"
elif command -v krita >/dev/null 2>&1; then
  ok "Krita on PATH: $(command -v krita)"
else
  maybe "No Krita binary — build with build_verify.sh after KF5 -devel"
fi

if [[ -d "$KRITA_BUILD/bin" ]]; then
  found=0
  for t in ComfyPortP51Test ComfyPortP52Test ComfyWorkflowEngineGoldenTest ComfyUIRemoteDockTest ComfyInpaintRegressionTest ComfyHistoryThumbnailRegressionTest ComfyHistoryListLayoutRegressionTest; do
    if [[ -x "$KRITA_BUILD/bin/$t" ]]; then
      ok "test binary: $KRITA_BUILD/bin/$t"
      found=1
    fi
  done
  if [[ "$found" -eq 0 ]]; then
    maybe "No Comfy* test binaries in $KRITA_BUILD/bin — run build_verify.sh when KF5 -devel installed"
  fi
else
  maybe "KRITA_BUILD/bin missing ($KRITA_BUILD) — optional for GUI-only M1–M10"
fi

if python3 -c "import json" 2>/dev/null; then
  if python3 "$PLUGIN_DIR/scripts/export_workflow_fixture.py" >/dev/null 2>&1; then
    ok "golden fixture exporter (Python)"
  else
    maybe "export_workflow_fixture.py failed"
  fi
fi

echo
if [[ "$fail" -eq 0 ]]; then
  if [[ "$warn" -eq 0 ]]; then
    echo "Preflight passed. Run scenarios in docs/MANUAL_ACCEPTANCE_RUN.md with Krita (comfyui_remote plugin)."
  else
    echo "Preflight OK with warnings — ComfyUI reachable; fix build warnings before ctest subset."
  fi
  echo "Inpaint deep matrix (P10): scripts/p10_inpaint_preflight.sh"
  exit 0
fi
echo "Preflight incomplete — fix blockers before M1–M10 desktop run."
exit 1
