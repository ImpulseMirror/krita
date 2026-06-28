# Port traceability — workstreams → tests & manual scenarios

**Plan:** `comfyui-perfect-port.plan.md`  
**Progress:** `port_progress.json`  
**CI script:** `../scripts/port_ci_checklist.sh`

## Automated regression

| Workstream | Test target(s) | Notes |
|------------|----------------|-------|
| P0.1 / GAP-K | `ComfyPortP51Test::testBundledPluginDataLayout` | `data/` install |
| P0.2 / GAP-B | `ComfyPortP51Test::testStyleCollectionLoadsBuiltinDigitalArtwork` | 14 styles |
| P1.2 / GAP-A | `ComfyWorkflowEngineGoldenTest`, `ComfyPortP51Test::testBuildGenerateUsesSamplerCustom` | Golden + sampler |
| P1.3 | `ComfyUIRemoteDockTest` control/inpaint builders | ControlNet chain |
| P1.4 | `ComfyRegionProcess`, regional generate tests | ETN regions |
| P1.8 | `ComfyWorkflowNormalize`, `export_workflow_fixture.py` | Golden workflow fixtures |
| P2.3 | `ComfyUIRemoteDockTest::testDocumentUiJsonRegionControlRoundtrip` | ui.json |
| P4.5 / P4.6 | `ComfyPortP51Test` loras / missing | FileLibrary |
| P5.2 | `ComfyPortP52Test` | Mock HTTP (M1 subset) |
| GAP-A2 | `ComfyPortP51Test` refine region, loadCheckpoint, control invert/union | §3.1.A complete |
| M8 | `ComfyPortM8Test` (API graph + output node) | Custom workflow parity |

## Manual acceptance (`acceptance_manual`)

| ID | Workstreams |
|----|-------------|
| M1 | P1.2, P4.5, P5.2 |
| M2 | P1.2, P4.3, P4.6 |
| M3 | P1.5, P4.1 |
| M4 | P1.3, P1.4, P2.1 |
| M5 | P1.6, P4.6 |
| M6 | P1.7, GAP-J |
| M7 | P1.6 |
| M8 | **M8**, P3.3 partial |
| M9 | P2.3 |
| M10 | P0.1, P4.4 |
| M11 | N/A (P3.1 skipped) |

## Run locally

```bash
./plugins/dockers/comfyui_remote/scripts/verify_all.sh
./plugins/dockers/comfyui_remote/scripts/verify_all.sh --with-build --with-manual --with-p10
```

## Architecture unification (`ARCHITECTURE_UNIFICATION_PLAN.md`)

Phases **A0–N11** complete (implementation). **Generate dropdown** Phases 1–3 complete. **P9** offline symbol gate in `port_ci_checklist.sh`; **runtime** pending KF5 `-devel` (`build_verify.sh`).

| Phase | Scope | Test gate |
|-------|-------|-----------|
| M1–M3 | Dock shell split, ShellInternal widgets, Connection → ObjectInfo/Ui/Probe + `ComfyConnectionInternal` | `ComfyUIRemoteDockTest`; manual Connect / refresh samplers |
| N1 | `ComfyGenerateRunnerPoll.cpp` — poll tick + download/history finish | `ComfyPortP52Test`, `ComfyUIRemoteDockTest` generate paths |
| N2 | Generate runner → Internal + Batch/GraphLive/Upload/Generate/Animation TUs | Same + animation batch manual rows |
| N3 | Live/Inpaint/Control runners → Internal + domain TUs | `ComfyUIRemoteDockTest` control/inpaint; manual M4 |
| N4 | Upscale runner + `ComfyDockUiBuilder` → domain TUs | Visual parity; Settings opens without crash |
| N5 | `ComfyDockUiBuilderGenerate` → section TUs | Visual parity all workspaces |
| N6 | `ComfySettingsDialogBuilder` → per-tab TUs + Internal | Settings all five tabs |
| N7 | `ComfyWorkflowEngine.cpp` → GraphDetail/Checkpoint/Graph/SamplerDetail/Conditioning | `ComfyWorkflowEngineGoldenTest` |
| N8 | `ComfyUIUtils.cpp` → Workflow/ObjectInfo/Presets/Plugin/Tiling/Sampling | Existing utils unit tests |
| N9 | `ComfyUIUtilsCustomWorkflow` → Convert/Params/Capture | Custom workflow manual |
| N10 | `ComfyUIUtilsDocument` + `ComfyUIUtilsMask` → domain TUs | Document sync / inpaint mask manual |
| N11 | Settings Styles tab → Widgets/Sync + Internal | Styles tab sync + builtin copy |
