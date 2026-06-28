#!/usr/bin/env bash
# P10 inpaint manual QA preflight (INPAINT_CONTEXT_PORT_PLAN.md §P10). No Krita binary required for offline part.
set -euo pipefail

COMFY_URL="${COMFY_URL:-http://127.0.0.1:8188}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"
KRITA_BUILD="${KRITA_BUILD:-$ROOT/build}"

echo "=== ComfyUI remote P10 inpaint manual preflight ==="
echo "COMFY_URL=$COMFY_URL"
echo "KRITA_BUILD=$KRITA_BUILD"
echo "Matrix: plugins/dockers/comfyui_remote/docs/INPAINT_CONTEXT_PORT_PLAN.md §P10"
echo

fail=0
warn=0
ok() { echo "[ok] $*"; }
bad() { echo "[FAIL] $*"; fail=1; }
maybe() { echo "[warn] $*"; warn=1; }

echo "--- Offline (P9 symbols + architecture TUs) ---"
if "$SCRIPT_DIR/port_ci_checklist.sh"; then
  ok "port_ci_checklist.sh"
else
  bad "port_ci_checklist.sh failed"
fi
echo

echo "--- ComfyUI server (P10 #14 needs live Graph + ETN_KritaSelection) ---"
check_http() {
  local path="$1"
  local label="$2"
  if curl -sf -m 5 "${COMFY_URL}${path}" >/dev/null; then
    ok "$label"
  else
    maybe "$label — start ComfyUI at $COMFY_URL (optional for offline-only review)"
  fi
}
check_http "/system_stats" "ComfyUI system_stats"
check_http "/object_info" "ComfyUI object_info (ETN nodes)"
check_http "/models/checkpoints" "checkpoints list"
echo

echo "--- Krita build (optional until GUI run) ---"
if [[ -x "$KRITA_BUILD/bin/krita" ]]; then
  ok "Krita binary: $KRITA_BUILD/bin/krita"
elif command -v krita >/dev/null 2>&1; then
  ok "Krita on PATH: $(command -v krita)"
else
  maybe "No Krita in $KRITA_BUILD/bin or PATH — install KF5 -devel and run build_verify.sh"
fi

if command -v rpm >/dev/null 2>&1; then
  if ! rpm -q kf5-kconfig-devel >/dev/null 2>&1; then
    maybe "KF5 -devel missing — plugins/dockers/comfyui_remote/scripts/build_verify.sh after: sudo dnf install -y kf5-kconfig-devel ..."
  fi
fi
echo

echo "--- P10 scenario checklist (mark in session notes) ---"
for n in \
  "1 Fill 100% — no preview in upload" \
  "2 Refine 50% partial selection — mask bounds" \
  "3 Refine 50% full canvas" \
  "4 Depth excluded; reference included" \
  "5 Live tick full projection" \
  "6 Live with selection — min_mask_size by arch" \
  "7 Live region-only" \
  "8 automatic mode — modifiers vs detected workflow mode" \
  "9 replace_background @ 100% — invert" \
  "10 Custom context + reload document" \
  "11 Custom + edit — fill none" \
  "11b Edit @ 100% — context = mask bounds only" \
  "12 Feather/blend/padding sliders" \
  "13 selection_feather=0" \
  "14 Graph ETN_KritaSelection + prepare_mask" \
  "15 Animation single-frame refine" \
  "16 Animation batch refine per-frame pixels" \
  "17 document_defaults on new doc" \
  "18 NSFW filter strict" \
  "19 Save result PNG on excluded canvas" \
  "20 selection_invert/square JSON no effect" \
  "21 Upscale/control — full projection" \
  "22 Region-only inpaint bounds" \
  "23 Mask PNG bounds vs padded mask" \
  "24 Qwen L strength forced 1.0" \
  "25 Edit mode exclude list from root control" \
  "26 SDXL @ 85% → refine_region not inpaint" \
  "27 Edit @ 100% → refine_region mask bounds" \
  "28 Multi-region fill" \
  "29 Region inpaint without region_only" \
  "30 Fixed seed inpaint" \
  "31 layer:foo reference control"; do
  echo "  [ ] $n"
done

echo
if [[ "$fail" -eq 0 ]]; then
  if [[ "$warn" -eq 0 ]]; then
    echo "P10 preflight ready — run scenarios in Krita + ComfyUI."
  else
    echo "P10 offline OK; fix warnings above before full GUI pass."
  fi
  exit 0
fi
echo "P10 preflight failed — fix blockers above."
exit 1
