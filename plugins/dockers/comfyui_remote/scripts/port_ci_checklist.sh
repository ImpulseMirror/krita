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
  plugins/dockers/comfyui_remote/scripts/build_verify.sh \
  plugins/dockers/comfyui_remote/scripts/verify_all.sh \
  plugins/dockers/comfyui_remote/scripts/commit_readiness.sh \
  plugins/dockers/comfyui_remote/README.md \
  plugins/dockers/comfyui_remote/cmake/ComfyIncludeDirs.cmake \
  plugins/dockers/comfyui_remote/scripts/reorganize_sources.py \
  plugins/dockers/comfyui_remote/scripts/check_include_paths.py \
  plugins/dockers/comfyui_remote/scripts/install_kf5_build_deps.sh \
  plugins/dockers/comfyui_remote/scripts/kf5_build_deps.inc.sh \
  plugins/dockers/comfyui_remote/scripts/krita_build_deps.inc.sh \
  plugins/dockers/comfyui_remote/scripts/build_status.sh \
  plugins/dockers/comfyui_remote/scripts/p10_inpaint_preflight.sh \
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
python3 "$PLUGIN_DIR/scripts/check_include_paths.py" >/dev/null && ok "check_include_paths.py" || bad "check_include_paths.py"

for ws in P0.4 GAP-A2 M8; do
  if grep -q "\"$ws\"" port_progress.json; then ok "workstream $ws tracked"; else bad "workstream $ws missing in port_progress.json"; fi
done

echo "=== Architecture split TUs (N6–N11, offline) ==="
for f in \
  plugins/dockers/comfyui_remote/settings/ComfySettingsDialogBuilderConnection.cpp \
  plugins/dockers/comfyui_remote/settings/ComfySettingsDialogBuilderStylesSync.cpp \
  plugins/dockers/comfyui_remote/workflow/engine/ComfyWorkflowEngineGraphDetail.cpp \
  plugins/dockers/comfyui_remote/utils/ComfyUIUtilsSampling.cpp \
  plugins/dockers/comfyui_remote/utils/custom_workflow/ComfyUIUtilsCustomWorkflowCapture.cpp \
  plugins/dockers/comfyui_remote/utils/document/ComfyUIUtilsDocumentCapture.cpp \
  plugins/dockers/comfyui_remote/utils/mask/ComfyUIUtilsMaskOps.cpp; do
  if [[ -f "$f" ]]; then ok "$f"; else bad "missing $f"; fi
done

cmakelists="plugins/dockers/comfyui_remote/CMakeLists.txt"
if grep -q '_comfy_source_roots' "$cmakelists" \
  && grep -q 'ComfyIncludeDirs.cmake' "$cmakelists"; then
  ok "CMakeLists: domain GLOB + include dirs"
else
  bad "CMakeLists missing domain source roots or ComfyIncludeDirs.cmake"
fi
for root in core network ui settings workflow utils runners dock history; do
  if [[ -d "plugins/dockers/comfyui_remote/$root" ]]; then ok "source root: $root"; else bad "missing source root: $root"; fi
done

echo "=== CMakeLists / plugin source parity ==="
flat_cpp="$(find plugins/dockers/comfyui_remote -maxdepth 1 -name 'Comfy*.cpp' | wc -l | tr -d ' ')"
if [[ "$flat_cpp" -eq 0 ]]; then ok "no flat-root Comfy*.cpp"; else bad "$flat_cpp Comfy*.cpp still at plugin root"; fi
disk_cpp="$(find plugins/dockers/comfyui_remote -name 'Comfy*.cpp' ! -path '*/tests/*' | wc -l | tr -d ' ')"
if [[ "$disk_cpp" -ge 130 ]]; then ok "domain Comfy*.cpp count ($disk_cpp)"; else bad "expected >= 130 Comfy*.cpp, found $disk_cpp"; fi
if [[ -f plugins/dockers/comfyui_remote/plugin/ComfyUIRemotePlugin.cpp ]]; then ok "plugin entry TU"; else bad "missing plugin/ComfyUIRemotePlugin.cpp"; fi

echo "=== P9 automated test symbols (offline, INPAINT_CONTEXT_PORT_PLAN) ==="
dock_test="plugins/dockers/comfyui_remote/tests/ComfyUIRemoteDockTest.cpp"
for sym in \
  testGetSelectionModifiersAndBounds \
  testCustomInpaintContextAndParams \
  testComfyInpaintModeDetectAndInstructions \
  testPrepareGenerateWorkflowKindPromotion \
  testCustomWorkflowKritaSelectionPrepare \
  testCustomWorkflowKritaSelectionPrepareAndExpand \
  testCustomWorkflowFullSelectionPipeline \
  testExpandCustomKritaWorkflowNodes \
  testExtractLorasFromPromptAndMerge \
  testPrepareCustomWorkflowStyleAndPrompts \
  testCustomWorkflowLiveCapturePolicy \
  testBuildRefineRegionFooocusBranch \
  testBuildRefineRegionInpaintControlNet \
  testComfyWorkflowEngineBuildInpaint; do
  if grep -q "void ComfyUIRemoteDockTest::${sym}()" "$dock_test"; then
    ok "P9 symbol: $sym"
  else
    bad "P9 missing test $sym in $dock_test"
  fi
done
for sym in \
  testBundledPluginDataLayout \
  testStyleCollectionLoadsBuiltinDigitalArtwork \
  testBuildGenerateUsesSamplerCustom; do
  p51="plugins/dockers/comfyui_remote/tests/ComfyPortP51Test.cpp"
  if grep -q "void ComfyPortP51Test::${sym}()" "$p51"; then
    ok "P9 symbol: $sym"
  else
    bad "P9 missing test $sym in $p51"
  fi
done
if grep -q 'isEditArch' plugins/dockers/comfyui_remote/utils/mask/ComfyUIUtilsMaskCreate.cpp \
  && grep -q 'effectiveInpaintMode = QStringLiteral("custom")' plugins/dockers/comfyui_remote/workflow/prepare/ComfyPrepareWorkflow.cpp; then
  ok "P9 code: native edit arch detect_inpaint parity"
else
  bad "P9 native edit arch parity hooks missing"
fi

if rg -l 'namespace ComfyUIUtils \{\s*\n\s*namespace ComfyUIUtils' plugins/dockers/comfyui_remote/utils/ComfyUIUtils*.cpp plugins/dockers/comfyui_remote/utils/*/*.cpp >/dev/null 2>&1; then
  bad "nested ComfyUIUtils namespace in utils TUs (link error)"
else
  ok "utils TUs: single ComfyUIUtils namespace"
fi

echo "=== N6–N11 monoliths removed (offline) ==="
for f in \
  ComfyUIUtils.cpp \
  ComfyUIUtilsDocument.cpp \
  ComfyUIUtilsMask.cpp \
  ComfyUIUtilsCustomWorkflow.cpp \
  ComfyWorkflowEngine.cpp \
  ComfySettingsDialogBuilder.cpp \
  ComfyUIRemoteDockConnection.cpp; do
  if find plugins/dockers/comfyui_remote -name "$f" ! -path '*/tests/*' | grep -q .; then
    bad "monolith still present (should be split): $f"
  else
    ok "removed: $f"
  fi
done

echo "=== scripts executable ==="
for f in plugins/dockers/comfyui_remote/scripts/*.sh; do
  [[ "$f" == *.inc.sh ]] && continue
  if [[ -x "$f" ]]; then ok "executable: ${f#plugins/dockers/comfyui_remote/}"; else bad "not executable: $f"; fi
done

echo
if [[ "$fail" -eq 0 ]]; then
  echo "Offline checklist passed."
  exit 0
fi
echo "Checklist failed."
exit 1
