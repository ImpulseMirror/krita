#!/usr/bin/env bash
# Install Krita build dependencies on Fedora/Nobara (KF5 minimal or full from-source set).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=kf5_build_deps.inc.sh
source "$SCRIPT_DIR/kf5_build_deps.inc.sh"
# shellcheck source=krita_build_deps.inc.sh
source "$SCRIPT_DIR/krita_build_deps.inc.sh"

RUN_VERIFY=0
MODE="minimal"
DRY_RUN=0
for arg in "$@"; do
  case "$arg" in
    --verify) RUN_VERIFY=1; MODE="full" ;;
    --full) MODE="full" ;;
    --minimal) MODE="minimal" ;;
    --dry-run) DRY_RUN=1; MODE="full" ;;
  esac
done

if [[ "$RUN_VERIFY" -eq 1 ]]; then
  MODE="full"
fi

if [[ "$MODE" == "full" ]]; then
  INSTALL_PKGS=("${KRITA_BUILD_RPMS[@]}")
  LABEL="full Krita build"
else
  INSTALL_PKGS=("${KF5_DEVEL_RPMs[@]}")
  LABEL="KF5 -devel (minimal)"
fi

if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "=== dnf dry-run: $LABEL (${#INSTALL_PKGS[@]} listed) ==="
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y --assumeno --skip-unavailable "${INSTALL_PKGS[@]}" || true
    echo "[ok] dry-run complete (dnf exits non-zero on --assumeno — expected)"
  else
    echo "[FAIL] dnf not found" >&2
    exit 1
  fi
  exit 0
fi

if [[ "$(id -u)" -ne 0 ]]; then
  echo "Installing $LABEL (sudo required)..."
  exec sudo "$0" "$@"
fi

echo "=== Installing $LABEL (${#INSTALL_PKGS[@]} packages) ==="
if command -v dnf >/dev/null 2>&1; then
  dnf install -y --skip-unavailable "${INSTALL_PKGS[@]}"
else
  echo "[FAIL] dnf not found — install manually: ${INSTALL_PKGS[*]}" >&2
  exit 1
fi

echo
echo "[ok] $LABEL packages installed."

if [[ "$RUN_VERIFY" -eq 1 ]]; then
  invoker="${SUDO_USER:-${USER:-}}"
  if [[ -n "$invoker" && "$invoker" != "root" ]]; then
    echo "Running verify_all --with-build as $invoker ..."
    sudo -u "$invoker" -H bash "$SCRIPT_DIR/verify_all.sh" --with-build
  else
    echo "Run as normal user: plugins/dockers/comfyui_remote/scripts/verify_all.sh --with-build"
  fi
fi
