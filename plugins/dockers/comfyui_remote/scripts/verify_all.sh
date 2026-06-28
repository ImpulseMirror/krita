#!/usr/bin/env bash
# Run ComfyUI remote verification gates (offline by default; opt-in build/manual/P10).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"

RUN_BUILD=0
RUN_MANUAL=0
RUN_P10=0
RUN_INTEGRATION=0
RUN_COMMIT=0
COMFY_URL="${COMFY_URL:-http://127.0.0.1:8188}"
fail=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

  (default)          port_ci_checklist only (offline + P9 symbols)
  --with-build       cmake + ninja kritacomfyuiremote_static + ctest (needs KF5 -devel)
  --with-manual      also run manual_acceptance_preflight (needs ComfyUI)
  --with-p10         also run p10_inpaint_preflight (ComfyUI optional)
  --with-integration live ComfyUI generate→refine integration test (needs ComfyUI + built test binary)
  --commit-check     commit_readiness.sh (offline + git tracked files)
  --comfy-url URL    ComfyUI base URL (default: $COMFY_URL)

Examples:
  $(basename "$0")
  $(basename "$0") --with-build
  $(basename "$0") --with-build --with-manual --with-p10
  COMFY_URL=http://192.168.1.5:8188 $(basename "$0") --with-p10
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --offline-only) RUN_BUILD=0; shift ;;
    --with-build) RUN_BUILD=1; shift ;;
    --with-manual) RUN_MANUAL=1; shift ;;
    --with-p10) RUN_P10=1; shift ;;
    --with-integration) RUN_INTEGRATION=1; shift ;;
    --commit-check) RUN_COMMIT=1; shift ;;
    --comfy-url) COMFY_URL="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

echo "=== ComfyUI remote verify_all ==="
echo "ROOT=$ROOT"
echo

echo ">>> [1/4] port_ci_checklist (offline + P9 symbols)"
"$SCRIPT_DIR/port_ci_checklist.sh"
echo

if [[ "$RUN_BUILD" -eq 1 ]]; then
  if command -v rpm >/dev/null 2>&1 && ! rpm -q kf5-kconfig-devel >/dev/null 2>&1; then
    echo ">>> [2/4] build_verify — FAILED (KF5 -devel missing)"
    echo "Run (needs sudo password in your terminal):"
    echo "  $SCRIPT_DIR/install_kf5_build_deps.sh --verify   # full Krita BuildRequires + build"
    echo "Or install only:"
    # shellcheck source=kf5_build_deps.inc.sh
    source "$SCRIPT_DIR/kf5_build_deps.inc.sh"
    echo "  sudo dnf install -y ${KF5_DEVEL_RPMs[*]}"
    echo
    echo "No-sudo KF5/Qt bootstrap only (full Krita build still needs sudo):"
    echo "  python3 $SCRIPT_DIR/bootstrap_user_kf5_deps.py"
    echo "  source $SCRIPT_DIR/local_build_env.sh"
    fail=1
  else
    echo ">>> [2/4] build_verify (P9 runtime)"
    "$SCRIPT_DIR/build_verify.sh"
    echo
  fi
else
  echo ">>> [2/4] build_verify — skipped (pass --with-build after install_kf5_build_deps.sh)"
  echo
fi

if [[ "$RUN_MANUAL" -eq 1 ]]; then
  echo ">>> [3/4] manual_acceptance_preflight (M1–M10)"
  COMFY_URL="$COMFY_URL" "$SCRIPT_DIR/manual_acceptance_preflight.sh"
  echo
else
  echo ">>> [3/4] manual_acceptance_preflight — skipped (pass --with-manual)"
  echo
fi

if [[ "$RUN_P10" -eq 1 ]]; then
  echo ">>> [4/4] p10_inpaint_preflight"
  COMFY_URL="$COMFY_URL" "$SCRIPT_DIR/p10_inpaint_preflight.sh"
  echo
else
  echo ">>> [4/4] p10_inpaint_preflight — skipped (pass --with-p10)"
  echo
fi

if [[ "$RUN_INTEGRATION" -eq 1 ]]; then
  echo ">>> [integration] ComfyInpaintIntegrationTest (live ComfyUI)"
  COMFY_URL="$COMFY_URL" "$SCRIPT_DIR/run_comfy_integration_test.sh"
  echo
fi

if [[ "$RUN_COMMIT" -eq 1 ]]; then
  echo ">>> [commit] commit_readiness"
  "$SCRIPT_DIR/commit_readiness.sh"
  echo
fi

if [[ "$fail" -ne 0 ]]; then
  echo "verify_all failed."
  exit 1
fi
echo "verify_all complete."
