#!/usr/bin/env bash
# P0.4: Verify port artifacts exist and run offline checks (no Krita binary required).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$PLUGIN_DIR/../../.." && pwd)"

fail=0
ok() { echo "[ok] $*"; }
bad() { echo "[FAIL] $*"; fail=1; }

echo "=== ComfyUI remote port CI checklist (P0.4) ==="
cd "$ROOT"

for f in \
  port_progress.json \
  comfyui-perfect-port.plan.md \
  plugins/dockers/comfyui_remote/docs/PORT_TRACEABILITY.md \
  plugins/dockers/comfyui_remote/docs/MANUAL_ACCEPTANCE_MATRIX.md \
  plugins/dockers/comfyui_remote/scripts/export_workflow_fixture.py \
  plugins/dockers/comfyui_remote/tests/ComfyWorkflowEngineGoldenTest.cpp \
  plugins/dockers/comfyui_remote/tests/ComfyPortP51Test.cpp \
  plugins/dockers/comfyui_remote/tests/ComfyPortP52Test.cpp \
  plugins/dockers/comfyui_remote/tests/ComfyPortM8Test.cpp; do
  if [[ -f "$f" ]]; then ok "$f"; else bad "missing $f"; fi
done

if [[ -d plugins/dockers/comfyui_remote/data/styles ]]; then
  n="$(find plugins/dockers/comfyui_remote/data/styles -name '*.json' | wc -l | tr -d ' ')"
  if [[ "$n" -ge 14 ]]; then ok "data/styles ($n JSON)"; else bad "data/styles count $n (expected >= 14)"; fi
else
  bad "data/styles/"
fi

if [[ -d plugins/dockers/comfyui_remote/tests/data/golden ]]; then
  ok "golden fixtures"
else
  bad "tests/data/golden"
fi

python3 "$PLUGIN_DIR/scripts/export_workflow_fixture.py" >/dev/null && ok "export_workflow_fixture.py" || bad "export_workflow_fixture.py"

for ws in P0.4 GAP-A2 M8; do
  if grep -q "\"$ws\"" port_progress.json; then ok "workstream $ws tracked"; else bad "workstream $ws missing in port_progress.json"; fi
done

echo
if [[ "$fail" -eq 0 ]]; then
  echo "Offline checklist passed."
  exit 0
fi
echo "Checklist failed."
exit 1
