#!/usr/bin/env bash
# Pre-commit gate: offline checks + all comfyui_remote changes tracked in git.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"

fail=0
ok() { echo "[ok] $*"; }
bad() { echo "[FAIL] $*"; fail=1; }
warn() { echo "[warn] $*"; }

echo "=== ComfyUI remote commit readiness ==="
echo "ROOT=$ROOT"
echo

echo "--- Offline checklist ---"
if "$SCRIPT_DIR/port_ci_checklist.sh"; then
  ok "port_ci_checklist.sh"
else
  bad "port_ci_checklist.sh failed"
fi
echo

if ! git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
  warn "not a git repo — skip tracked-file check"
else
  echo "--- git status (plugins/dockers/comfyui_remote) ---"
  rel="plugins/dockers/comfyui_remote"
  untracked=$(git -C "$ROOT" status --porcelain "$rel" | awk '$1=="??"{print $2}' || true)
  untracked_n=$(echo "$untracked" | grep -c . || true)
  modified_n=$(git -C "$ROOT" status --porcelain "$rel" | awk '$1!="??"{print}' | grep -c . || true)

  echo "modified/staged/deleted: $modified_n"
  echo "untracked: $untracked_n"

  if [[ "$untracked_n" -gt 0 ]]; then
    bad "$untracked_n untracked files — git add before commit"
    echo "$untracked" | head -20
    if [[ "$untracked_n" -gt 20 ]]; then
      echo "... and $((untracked_n - 20)) more"
    fi
  else
    ok "no untracked files under $rel"
  fi

  if [[ "$modified_n" -eq 0 && "$untracked_n" -eq 0 ]]; then
    warn "no changes under $rel"
  fi
fi

echo
if command -v rpm >/dev/null 2>&1 && ! rpm -q kf5-kconfig-devel >/dev/null 2>&1; then
  warn "KF5 -devel not installed — run install_kf5_build_deps.sh before merge (P9 runtime)"
fi

echo
if [[ "$fail" -eq 0 ]]; then
  echo "Commit readiness passed (offline). Run build_verify after KF5 install before merge."
  exit 0
fi
echo "Commit readiness failed."
exit 1
