# ComfyUI Remote — session progress (pickup guide)

**Last updated:** 2026-06-27  
**Branch:** `comfyui-perfect-port`  
**Workspace:** `/home/ackeejag/source/krita`  
**Build dir:** `build/` (not `_build`)

This README captures where the port and architecture-unification work stopped so you can resume without re-reading the full chat history.

---

## TL;DR — do this next

1. **Install KF5 devel + build + test** (sudo — required for native x86_64)
   ```bash
   plugins/dockers/comfyui_remote/scripts/install_kf5_build_deps.sh --verify
   ```
   Optional KF5/Qt-only bootstrap without sudo (`~/krita-deps`) — does **not** complete full Krita cmake. Do **not** use `krita-auto-1/persistent/deps` for desktop builds (Android ARM libs).
2. **Commit** when build green — changes staged (`git add` done); run `commit_readiness.sh` first:
   ```bash
   plugins/dockers/comfyui_remote/scripts/verify_all.sh --commit-check --with-build
   ```
3. **Manual QA** after ComfyUI running:
   ```bash
   plugins/dockers/comfyui_remote/scripts/verify_all.sh --with-manual --with-p10
   ```

---

## What is done

### Source layout (domain folders)

~191 `Comfy*` TUs live under domain folders (filenames and C++ namespaces unchanged). Plugin root keeps only build metadata, `data/`, `docs/`, `scripts/`, `tests/`.

| Folder | Contents |
|--------|----------|
| `plugin/` | Krita module entry (`ComfyUIRemotePlugin.cpp`) |
| `network/` | HTTP client, upload pipeline |
| `core/` | Resources, regions, styles, control layers, file lib, localization, openpose |
| `ui/theme/`, `ui/widgets/` | Theme + reusable widgets |
| `ui/builder/` | Dock shell UI builder (`generate/` subfolder for generate tab sections) |
| `ui/generate/` | Generate CTA / inpaint mode UI |
| `settings/` | Settings dialog builder TUs |
| `workflow/engine/`, `workflow/prepare/` | Workflow graph engine + prepare pipeline |
| `workflow/` | Workflow templates |
| `utils/` | Shared helpers (`mask/`, `document/`, `custom_workflow/` subfolders) |
| `runners/` | Job runners (`generate/`, `inpaint/`, `live/`, `upscale/`, `control/`) |
| `dock/` | Main dock + per-tab slot TUs (`connection/`, `generate/`, …) |
| `history/` | History storage, apply, preview |
| `cmake/` | `ComfyIncludeDirs.cmake` (header search paths for unchanged `#include "ComfyFoo.h"`) |

Re-run layout after adding new top-level prefixes: `python3 scripts/reorganize_sources.py --dry-run`.

### Architecture unification (A0–N11)

All phases in [`docs/ARCHITECTURE_UNIFICATION_PLAN.md`](docs/ARCHITECTURE_UNIFICATION_PLAN.md) are **implemented**. Monolithic files were split into domain TUs; deleted monoliths:

| Removed monolith | Replaced by (examples) |
|------------------|------------------------|
| `ComfyUIUtils.cpp` | `ComfyUIUtilsWorkflow.cpp`, `ObjectInfo`, `Presets`, `Plugin`, `Tiling`, `Sampling`, … |
| `ComfyUIUtilsDocument.cpp` | `ComfyUIUtilsDocumentUiJson.cpp`, `DocumentCapture.cpp` |
| `ComfyUIUtilsMask.cpp` | `ComfyUIUtilsMaskCreate.cpp`, `MaskOps.cpp` |
| `ComfyUIUtilsCustomWorkflow.cpp` | `CustomWorkflowConvert`, `Params`, `Capture` |
| `ComfyWorkflowEngine.cpp` | `GraphDetail`, `Checkpoint`, `Graph`, `SamplerDetail`, `Conditioning`, … |
| `ComfySettingsDialogBuilder.cpp` | per-tab TUs + `StylesWidgets`, `StylesSync`, `Internal` |
| `ComfyUIRemoteDockConnection.cpp` | `ConnectionObjectInfo`, `ConnectionUi`, `ConnectionProbe`, `ComfyConnectionInternal` |

Runners, dock UI builder, generate UI, history, and settings were also split per the plan (N1–N6, M1–M3).

### Generate tab / dropdown (Phases 1–3)

Documented in [`docs/GENERATE_BUTTON_DROPDOWN_PORT_PLAN.md`](docs/GENERATE_BUTTON_DROPDOWN_PORT_PLAN.md) — **done**:

- Inpaint mode menus, dynamic CTA, region mask toggle
- `can_toggle_edit` / `can_edit` / native edit arch menu routing
- Edit switch in custom inpaint row (`editModeSwitch` ↔ hidden `checkEditMode`)
- Native edit arch: `detectInpaintParams` uses `ComfyResources::isEditArch` tail override; `ComfyPrepareWorkflow` sets `effectiveInpaintMode = custom`

### Settings tab ports

| Tab | Plan | Status |
|-----|------|--------|
| Diffusion | [`DIFFUSION_TAB_PORT_PLAN.md`](docs/DIFFUSION_TAB_PORT_PLAN.md) | Implemented |
| Interface | [`INTERFACE_TAB_PORT_PLAN.md`](docs/INTERFACE_TAB_PORT_PLAN.md) | Implemented |
| Performance | [`PERFORMANCE_TAB_PORT_PLAN.md`](docs/PERFORMANCE_TAB_PORT_PLAN.md) | Implemented |
| Styles | [`STYLES_TAB_PORT_PLAN.md`](docs/STYLES_TAB_PORT_PLAN.md) | Implemented |

### Inpaint / canvas pipeline (P0–P8)

Code complete per [`docs/INPAINT_CONTEXT_PORT_PLAN.md`](docs/INPAINT_CONTEXT_PORT_PLAN.md). Automated tests exist; **runtime CI not executed** on this host.

### Verification scripts (added this session)

| Script | Purpose |
|--------|---------|
| [`scripts/port_ci_checklist.sh`](scripts/port_ci_checklist.sh) | Offline: artifacts, N6–N11 TUs, P9 test symbols, monolith removal, CMake GLOB parity, include path check |
| [`scripts/build_verify.sh`](scripts/build_verify.sh) | Offline checklist → KF5 check → `cmake` → `ninja kritacomfyuiremote_static` → `ctest` |
| [`scripts/verify_all.sh`](scripts/verify_all.sh) | Orchestrator; default offline-only; `--with-build`, `--with-manual`, `--with-p10`, `--commit-check` |
| [`scripts/build_status.sh`](scripts/build_status.sh) | Package + build-tree probe; prints next command |
| [`scripts/install_kf5_build_deps.sh`](scripts/install_kf5_build_deps.sh) | `--verify` = full Fedora Krita BuildRequires + build_verify |
| [`scripts/kf5_build_deps.inc.sh`](scripts/kf5_build_deps.inc.sh) | Shared RPM list (sourced, not executed) |
| [`scripts/commit_readiness.sh`](scripts/commit_readiness.sh) | Offline pass + fail if untracked files under plugin dir |
| [`scripts/p10_inpaint_preflight.sh`](scripts/p10_inpaint_preflight.sh) | P10 inpaint manual checklist + ComfyUI probe |
| [`scripts/manual_acceptance_preflight.sh`](scripts/manual_acceptance_preflight.sh) | M1–M10 manual acceptance preflight |

---

## What is blocked

| Blocker | Detail |
|---------|--------|
| **KF5 `-devel` not installed** | `cmake ..` fails. Run `install_kf5_build_deps.sh --verify` (sudo). |
| **P9 runtime not run** | Needs KF5 + `verify_all.sh --with-build`. |

---

## Gate status (last session)

| Gate | Status |
|------|--------|
| `verify_all.sh` (default, offline) | **Pass** |
| `verify_all.sh --with-build` | **Fail** — KF5 missing |
| `commit_readiness.sh` | **Pass** offline (230 files staged) |
| P9 runtime (`ctest`) | **Blocked** — KF5 `-devel` |
| P10 manual QA (31 scenarios) | **Not run** — needs Krita + ComfyUI |

---

## Bugs fixed during split (watch if build fails elsewhere)

1. **Double `namespace ComfyUIUtils`** in `ComfyUIUtilsMaskCreate.cpp`, `MaskOps.cpp`, `DocumentUiJson.cpp` — caused link errors; fixed; guarded in `port_ci_checklist.sh`.
2. **`qwen` vs edit arch** — `detectInpaintParams` wrongly treated all `qwen*` as edit; fixed with `ComfyResources::isEditArch` at end of function (matches upstream `workflow.detect_inpaint`).
3. **Styles N11** — `updateStylesCkptWarning` / `createJsonStyle` capture fixes (prior session).
4. **Workflow engine cross-TU** — `detail::` prefixes and `ComfyWorkflowEngineInternal.h` declarations (prior session).

---

## Key documentation map

| Doc | Use when |
|-----|----------|
| [`docs/ARCHITECTURE_UNIFICATION_PLAN.md`](docs/ARCHITECTURE_UNIFICATION_PLAN.md) | Split phases A0–N11, completion status, module map |
| [`docs/PORT_TRACEABILITY.md`](docs/PORT_TRACEABILITY.md) | Workstreams → tests |
| [`docs/INPAINT_CONTEXT_PORT_PLAN.md`](docs/INPAINT_CONTEXT_PORT_PLAN.md) | Inpaint parity, P9/P10 matrices |
| [`docs/MANUAL_ACCEPTANCE_MATRIX.md`](docs/MANUAL_ACCEPTANCE_MATRIX.md) | M1–M10 release scenarios |
| [`docs/MANUAL_ACCEPTANCE_RUN.md`](docs/MANUAL_ACCEPTANCE_RUN.md) | Manual run log template |
| [`docs/GENERATE_TAB_PORT_PLAN.md`](docs/GENERATE_TAB_PORT_PLAN.md) | Generate workspace layout |
| [`docs/GENERATE_BUTTON_DROPDOWN_PORT_PLAN.md`](docs/GENERATE_BUTTON_DROPDOWN_PORT_PLAN.md) | Generate CTA / inpaint menus |

Upstream reference clone: `temp/krita-ai-diffusion/` (may be `.cursorignore`d; use shell `rg` if IDE search misses it).

---

## Resume checklist

- [ ] Run `install_kf5_build_deps.sh --verify` (needs sudo password)
- [ ] Fix compile errors from build output if any
- [ ] `git add plugins/dockers/comfyui_remote/` → `commit_readiness.sh` passes
- [ ] Commit with message covering architecture split + generate/inpaint fixes + verify scripts
- [ ] `verify_all.sh --with-build` green
- [ ] Optional: PR from `comfyui-perfect-port`
- [ ] P10 inpaint manual pass with ComfyUI + ETN nodes
- [ ] M1–M10 manual acceptance per matrix

---

## Quick commands

```bash
# Offline sanity (always works)
plugins/dockers/comfyui_remote/scripts/verify_all.sh

# Full verification stack (after KF5 + ComfyUI)
plugins/dockers/comfyui_remote/scripts/verify_all.sh --with-build --with-manual --with-p10

# Pre-commit
plugins/dockers/comfyui_remote/scripts/verify_all.sh --commit-check

# Single plugin target (manual)
cd build && ninja kritacomfyuiremote_static
cd build && ctest -R 'ComfyPort|ComfyWorkflow|ComfyUIRemoteDock' --output-on-failure
```

---

## Notes for the next agent

- **Do not commit** unless the user explicitly asks.
- **Domain folders** — new TUs go under the matching folder per table above; `#include "ComfyFoo.h"` stays flat via `cmake/ComfyIncludeDirs.cmake`.
- **No further split work** planned unless build reveals oversized TUs or compile issues.
- **`ROOT` in scripts** must resolve to Krita repo root (`PLUGIN_DIR/../../..` from `scripts/`), not `plugins/`.
- If `cmake` picks stale Qt paths from `krita-auto-1`, run `cmake .. -UQt5Core_DIR` in `build/`.
