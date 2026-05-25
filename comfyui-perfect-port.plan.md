---
title: ComfyUI Remote Perfect Port
type: perfect-port
progress_file: port_progress.json
reference_repo: https://github.com/Acly/krita-ai-diffusion
reference_version: "1.49.0"
reference_path: /Users/ackeejag/Source/krita-ai-diffusion
target_path: plugins/dockers/comfyui_remote
branch: comfyui-perfect-port
baseline_commit: c229af157e
no_stubs: true
---

# ComfyUI Remote — Perfect Port Completion Plan

## For agents

**Read this file + `port_progress.json` before coding.**

### Workflow

1. Pick a workstream with `status: "not_started"` whose `depends_on` are all `complete` (or empty).
2. Set its status to `in_progress` in `port_progress.json`.
3. Read `python_refs` in that workstream against `reference_path`.
4. Implement in `cpp_targets`; match Python behavior — **no placeholders**, **no “not available in this build”** unless `not_applicable` with documented reason.
5. Mark `complete` with evidence (test name, manual scenario id from `acceptance_manual`, or commit hash).
6. Prefer one workstream per session; respect `critical_path` order when unsure.

### Rules

- **Source of truth:** [Acly/krita-ai-diffusion](https://github.com/Acly/krita-ai-diffusion) `ai_diffusion/` v1.49.0
- **Target:** `plugins/dockers/comfyui_remote/` (C++, no Python at runtime)
- **Contract:** `settings.json`, `ui.json` v1, `ai_diffusion/*` annotations must round-trip with Python plugin
- **Android:** Enable Qt WebSockets + KArchive in build; ship `data/` assets
- **Perfect port — nothing skipped:** If Python exposes a behavior, control, or UI affordance on the reference path for a workstream, it is **in scope** for that workstream (or a named child workstream). Do **not** mark a workstream `complete` for an MVP subset. Do **not** defer required parity to “later” without a **new** tracked workstream id and an entry in `port_progress.json`. “Good enough” / “close enough” is not done.

### Quick pick (critical path)

`P0.1` → `P0.2` → `P1.1` → `P1.2` → `C-control-layers` → `P1.3` → `P1.4` → `P2.1` → `P2.3`

---

**Goal:** Native C++ plugin (`plugins/dockers/comfyui_remote`) must match [Acly/krita-ai-diffusion](https://github.com/Acly/krita-ai-diffusion) in behavior, UI, styling, persistence, and server integration — with **no stubs** and **no “MVP” shortcuts**. Android cannot run Python; this port is the only path to AI Diffusion on Krita Android.

**Baseline (Python):** `ai_diffusion` v1.49.0 @ `/Users/ackeejag/Source/krita-ai-diffusion`  
**Baseline (C++):** branch `comfyui-plugin` @ `c229af157e`, ~29 files, ~19k LOC  
**Primary reference:** upstream repo + [interstice.cloud docs](https://docs.interstice.cloud)

---

## 1. Executive summary

The C++ port has substantial surface area: welcome screen, five workspaces, settings dialog (6 tabs), history/queue, document `ui.json` contract, inpaint/regions/live/animation/upscale paths, tag autocomplete, and many spec-section comments (`§`). **Visually it is “close”** because layouts and labels exist.

**Functionally it is not a perfect port.** The largest gaps are:

| Area | Python | C++ today | Severity |
|------|--------|-----------|----------|
| Workflow engine | `workflow.py` (~2k LOC) builds arch-specific graphs: ControlNet, IP-Adapter, regions conditioning, refine, edit models | Minimal JSON templates + manual field patch; **no** `process_regions` / `set_control` equivalent | **Critical** |
| Style system | 14 JSON presets (checkpoint, VAE, LoRAs, samplers, quality modes) | 4 hardcoded prompt/size shortcuts + KConfig custom | **Critical** |
| Control layers (UI + gen) | Per-region list: mode, layer, preset, strength, range, generate | Preview-only block (9 modes); **not wired into main/regional generation** | **Critical** |
| Cloud + managed server | Full `CloudClient` + `server.py` install | Placeholder labels | **High** (cloud); **Medium** (managed on Android) |
| Bundled assets | 14 styles, 178 icons, 16 languages, 4 tag CSVs, presets JSON | Embedded mini JSON in code; Krita theme icons; **no** language packs | **High** |
| Graph workspace | `CustomWorkflowWidget` + param matrix + history | JSON text area + param panel; no dedicated workspace stack | **High** |
| Inpaint modes | 7 modes incl. add/remove object, replace background, custom | 3 modes (automatic, fill, expand) | **High** |
| UI chrome | `theme.py` palette-aware colors, custom switch, flat combos, plugin icons | Sparse `setStyleSheet`; standard widgets | **Medium** |

**Definition of done:** A user migrating from desktop Python plugin to Android native plugin can perform the same workflows with the same defaults, document round-trips, and visual affordances — modulo platform limits explicitly documented in §7 **Non-goals** only (e.g. managed ComfyUI install may remain external-only on Android if impractical, but **must not** be a silent stub). Anything outside §7 that exists in Python v1.49.0 remains **required** until implemented and tracked.

---

## 2. Parity model

### 2.1 Behavioral parity layers

1. **Contract parity** — `settings.json` keys, `ui.json` schema v1, annotation keys (`ai_diffusion/*`), sidecar paths (`.animation/`, `.live-frames/`), action IDs (`ai_diffusion_*`).
2. **Workflow parity** — Same Comfy node graphs for each `WorkflowKind` / workspace / arch / control configuration.
3. **UI parity** — Same controls, labels, tooltips, visibility rules, workspace switching, queue popup fields.
4. **Visual parity** — Colors, icons, spacing, custom controls (switch, interval slider, style selector, queue button paint).
5. **Platform parity** — Android: optional deps (WebSockets, KArchive) enabled in build; assets shipped inside APK or extracted to user data on first run.

### 2.2 Source-of-truth map

| Python module | C++ counterpart | Port status |
|---------------|-----------------|-------------|
| `extension.py` | `ComfyUIRemotePlugin.cpp` | OK |
| `ui/diffusion.py` | `ComfyUIRemoteDock.cpp` (welcome + stack) | Partial layout |
| `ui/generation.py` | Generate section in dock | Partial |
| `ui/upscale.py` | Upscale rows in dock | Partial |
| `ui/live.py` | `ComfyUIRemoteDockLive.cpp` | Partial |
| `ui/animation.py` | Animation block in dock | Partial |
| `ui/custom_workflow.py` | Custom JSON + params; graph placeholder | Partial |
| `ui/region.py` | `ComfyRegionPromptWidget.cpp` + dock regions | **Complete** (§3.1.D; workflow `language` on generate → P4.2) |
| `ui/control.py` | Control preview only | **Missing list UI** |
| `ui/settings.py` + tabs | `ComfyUIRemoteDockSettings.cpp` | Partial (stubs) |
| `ui/theme.py` | Ad-hoc stylesheets | **Missing** |
| `ui/widget.py` | Queue menu, strength, prompts | Partial |
| `ui/server.py` | Placeholder | **Missing** |
| `model.py` | State spread across dock + utils | Partial |
| `workflow.py` | `ComfyUIWorkflows.cpp` templates only | **Missing engine** |
| `comfy_workflow.py` | `ComfyUIUtils` conversion helpers | Partial |
| `connection.py` | `ComfyUIRemoteDockConnection.cpp` | Partial |
| `cloud_client.py` | Stub messages | **Missing** |
| `server.py` | Stub | **Missing** |
| `client.py` / `comfy_client.py` | HTTP/WS in dock | Partial |
| `style.py` | `ComfyUIRemoteDockPresets.cpp` | **Wrong semantics** |
| `resources.py` | Partial arch classify in utils | Partial |
| `control.py` | Preview + presets JSON | **No ControlLayer model** |
| `region.py` | Region list + inpaint chain | **No regional conditioning** |
| `persistence.py` | `ComfyUIUtils` + history | Partial |
| `files.py` | LoRA library only | Partial |
| `pose.py` | `ComfyUIPoseLayers.cpp` | Partial |
| `updates.py` | Welcome update panel | Partial (KArchive optional) |
| `localization.py` | Krita `i18n` only | **Missing JSON locales** |
| `text.py` | Wildcards, attention in utils | Partial; **translation not in generate path** |

---

## 3. Gap inventory (detailed)

### 3.1 Critical — workflow & generation core

#### A. Port `workflow.py` to C++ (`ComfyWorkflowEngine`) — workstream `P1.2`–`P1.8`, `GAP-A`

Python centralizes graph construction for:

- Text2img / img2img with arch branches (SD1.5, SDXL, SD3, Flux, Flux Kontext, Flux2, Illustrious, Chroma, Qwen*, Z-Image)
- Regional conditioning via `process_regions()` → `ConditioningInput` + `JobRegion` list
- Control layers: ControlNet + IP-Adapter (+ hands, segmentation, universal)
- Inpaint / refine / refine_region / upscale_simple / upscale_tiled
- Edit mode (Flux Kontext, Qwen Edit, etc.)
- Performance injections (tiled VAE, dynamic caching, batch constraints)

**C++:** `ComfyWorkflowEngine` builders for text2img/refine/inpaint/live/animation/upscale + `applyGenerationConditioning` (IP/regional/control/reference) + `SamplerCustomAdvanced` on main paths via `finishWorkflowWithSamplerCustom`. `buildGenerate` / `buildControlPreview` APIs. Golden fixtures: SD1.5 + SDXL + Flux (`tests/data/golden/`). Dock generate uses `applyGenerationConditioning`.

**Remainder — workstream `GAP-A2` (§3.1.A, complete):**

| Item | Python | C++ |
|------|--------|-----|
| `refine_region()` | Masked regional refine + color_match | `buildRefineRegion()` + dock refine-after-regions |
| `apply_control()` | Line invert, universal CN, Z-Image patch | `applyControlNetLayers` |
| `load_checkpoint_with_lora` | Nunchaku Flux + LoraLoader chain | `loadCheckpointWithLora()` |
| `inpaint()` grow | `apply_grow_feather` / INPAINT_ExpandMask | `buildInpaint` + `buildRefineRegion` |
| `scale_refine_and_decode` | Upscale refine pass | `buildUpscaleRefine` (existing) |

**M8 (complete):** API validation + output node check; Graph workspace inline JSON editor + Generate; `ComfyPortM8Test`. Full graph UI (P3.3) still deferred.

---

#### B. Style presets — ship and load real `styles/*.json` — `P0.2`, `GAP-B`

**Python:** 14 files under `ai_diffusion/styles/` (e.g. `flux.json`, `qwen-edit.json`, `z-image-turbo.json`) defining checkpoint, VAE, LoRAs, sampler preset keys, CFG, steps, style prompt templates, `uses_negative_prompt`, architecture metadata.

**C++ today:** `slotPresetChanged` cases 1–4 set hardcoded prompt strings and 512×768 sizes (`ComfyUIRemoteDockPresets.cpp`). Styles tab references `built-in/portrait.json` paths that **do not exist** in the plugin install tree.

**Work:**

1. Vendor all 14 JSON files into plugin data (e.g. `plugins/dockers/comfyui_remote/data/styles/`).
2. Implement `Style` / `StyleCollection` C++ analogue of `style.py` (load, merge user styles from `user_data_dir/styles/`, default style, quality fast/live).
3. Replace preset combo semantics: **Style** not “prompt shortcut”.
4. Connect checkpoint combo, steps/CFG/sampler, negative alert (`§13.143`), layer count row visibility (`Arch.qwen_l`), linked edit style.
5. Install rules in `CMakeLists.txt` to copy `data/` to Android asset or `QStandardPaths::AppDataLocation` on first run.

**Acceptance:** Selecting `flux-schnell` style applies same checkpoint name and sampler preset as Python; `show_builtin_styles` hides built-ins correctly.

---

#### C. Control layers — full UI + generation integration — `C-control-layers`, `P2.1`, `P2.4`, `GAP-C`

**Python (`ui/control.py`, `control.py`):** Per active region (and root): dynamic list of `ControlLayer` rows — mode combo (reference, face, style, composition, scribble, line_art, depth, pose, …), layer picker, preset slider, strength, interval slider, per-layer Generate, pose vector detection.

**C++ today:** `comboControlPreviewMode` (9 modes) + interval slider + **Run preview** only. No “Add Control Layer”. No persistence of control list in `ui.json`. No IP-Adapter modes in combo (reference, face, style, composition).

**Work:**

1. `ControlLayer` model (QUuid layer id, mode, preset index, strength, start/end, flags) + `ControlLayerList` per `RootRegion` / `Region`.
2. Region UI: embed control list under each region prompt block (match Python `RegionPromptWidget` behavior).
3. Persist control layers in `ui.json` (same schema as Python).
4. `generate_control_layer()` equivalent → preview job (already partial) + full graph insertion in `ComfyWorkflowEngine`.
5. Port remaining `ControlMode` values and `resources.control` preset resolution per arch.

**Acceptance:** Reference image + depth + pose on a region produces same workflow nodes as Python; control preview matches generated control image used in main job.

---

#### D. Regions UI & semantics — `P2.2`, `P2.3`, `GAP-D`

**Python:** `ui/region.py` (`RegionPromptWidget`, `ActiveRegionWidget`, `InactiveRegionWidget`) + `region.py` (`RootRegion`, `Region`, `RegionLink`, `get_region_inpaint_mask`).

**C++ today:** Inline `ComfyRegionPromptWidget` (chips + active editor + direct layer link + `layer_ids`); root pos/neg still only in dock prompt fields; per-region control list still separate group below buttons. Main **Generate** uses `process_regions` (P1.4).

**Work — all required for perfect port (P2.2 not done until every box checked):**

| # | Requirement | Python reference |
|---|-------------|------------------|
| D1 | Inline `RegionPromptWidget` layout: inactive above + active + inactive below | `RegionPromptWidget` |
| D2 | `ActiveRegionWidget`: positive + negative prompts (root = both; region = positive only when style hides negative) | `ActiveRegionWidget` |
| D3 | `InactiveRegionWidget`: thumbnail + clipped prompt + control mode icons | `InactiveRegionWidget` |
| D4 | Prompt header modes: full / icon / none | `PromptHeader` |
| D5 | Link button: toggle active Krita layer ↔ region (`layer_ids`); link menu for unlinked active layer | `Region.link_active`, `_show_link_menu` |
| D6 | `RegionLink` indirect: layer in linked group counts as region | `RegionLink.indirect`, `Region.is_linked` |
| D7 | Auto-activate region when active layer matches a linked region | `RootRegion.active_or_root` selection rules |
| D8 | “Active layer not linked” strip + New region / Link region actions | `ActiveRegionWidget._no_region` |
| D9 | Remove region from active header | `_remove_button` |
| D10 | `region_only` limits generate to active region | settings + `model` |
| D11 | Embedded per-region `ControlListWidget` inside region panel (not only separate dock group) | `RegionPromptWidget._control` |
| D12 | Translation chip on active prompts when `prompt_translation` set | `_language_button`, Ctrl+click translate |
| D13 | Resizable prompt line counts (incl. live/slim) | `_handle_dragging`, `prompt_line_count*` settings |
| D14 | Focus styling + `activated` signal parity for workspace shortcuts | `focused`, `activated` |
| D15 | `get_region_inpaint_mask` / mask from linked layer coverage | `region.py` |
| D16 | Add region creates layer/group per workspace (Generate vs Live) | `RootRegion.add_region` |
| D17 | Single **Generate** uses full regional conditioning when regions exist | `model._generate` + P1.4 |
| D18 | Batch “Generate regions” only if Python keeps a distinct path — verify, do not duplicate wrongly | `model._generate` |

**Follow-up (other workstreams):** D12 generate-time `conditioning.language` / `ETN_Translate` in workflow graph → **P4.2**. Region-only single-layer inpaint (`get_region_inpaint_mask` when no selection) → verify against inpaint path in **P4.1** / generate prep.

**Acceptance:** Multi-region `.kra` from Python opens in C++ with the same region list, links, prompts, and control rows; same image on the same server; UI matches §3.1.D table (thumbnails, neg prompt rules, link menu, embedded control list).

---

### 3.2 High — connection, server, graph workspace

#### E. Cloud client (`cloud_client.py`) — `P3.1`, `GAP-E`

**Gaps:** No OAuth/sign-in, no `CloudClient` HTTP/WS, no billing/user display, no cloud-specific performance preset, custom graph disabled on cloud.

**Work:** `ComfyCloudClient` C++; Connection tab `CloudWidget` parity; block graph on cloud; cloud URL + headers.

**Acceptance:** `M11-cloud-signin` — sign in → generate → apply.

---

#### F. Managed ComfyUI server (`server.py`, `ui/server.py`) — `P3.2`, `GAP-F`

**Gaps:** Placeholder “not available in this build.”

**Work:** Desktop: port `server.py` lifecycle. Android: guided external ComfyUI + required custom nodes checklist (not dead placeholder).

---

#### G. Graph / custom workspace — `P3.3`, `P3.4`, `GAP-G`

**Python:** `CustomWorkflowWidget` — workflow combo, import/save/delete, dynamic params, generate/apply, history.

**C++:** Graph workspace placeholder; JSON in Connection group.

**Work:** Dedicated graph page; `WorkflowCollection`; WebSocket publish (`COMFYUI_HAVE_QT_WEBSOCKETS` on Android).

**Acceptance:** `M8-custom-workflow`.

---

#### H. Inpaint mode & fill semantics — `P4.1`, `GAP-H`

**Python `InpaintMode`:** automatic, fill, expand, add_object, remove_object, replace_background, custom.

**C++:** 3 modes only. Fix Seamless label (`use_inpaint`). Fill/context combo icons.

---

#### I. Prompt translation — `P4.2`, `GAP-I`

**Python:** `ETN_Translate` when `prompt_translation` enabled.

**C++:** Settings only — wire into `ComfyUIRemoteDockGenerate.cpp` before submit.

---

#### J. Tiled upscale execution — `P1.7`, `GAP-J`

**Python:** `upscale_tiled` workflow. **C++:** `buildUpscaleTiled` + `computeUpscaleTiledLayoutSpec`; per-tile control/regions/IP-Adapter; `SamplerCustomAdvanced` + `applyReferenceConditioningForTile` (ReferenceLatent / ImageStitch on edit arches); upscale dock uploads at target size.

---

### 3.3 High — assets, i18n, styling

#### K. Bundle plugin data directory — `P0.1`, `GAP-K`

```
data/styles/*.json (14)
data/presets/{samplers,models,control}.json
data/tags/*.csv (4)
data/icons/*-{light,dark}.svg (178) or Krita stem map
data/language/*.json (16 + template)
```

`ComfyUIUtils::pluginDataDir()` + first-run copy on Android.

---

#### L. Theme system (`ui/theme.py`) — `P0.3`, `GAP-L`

Port palette colors, `flat_combo_stylesheet`, custom `Switch`, `QueueButton` paint, `StyleSelectWidget`, checkpoint arch icons. `ComfyTheme` namespace.

---

#### M. Localization — `P4.4`, `GAP-M`

Load `language/*.json`; `QTranslator` or string table; restart notice.

---

#### N. Pre-generate LoRA upload — `P4.6`

Python `loras_to_upload` / `ComfyClient.upload_loras`: before prompt, PUT each enabled library LoRA that is local (path + hash) but not on server (`api/etn/upload/loras/<filename>`). C++: `beginGenerateUploadPipeline` / `beginInpaintUploadPipeline` / `beginLiveUploadPipeline` (LoRA uploads → canvas upload → `/prompt`).

---

### 3.4 Medium — settings & polish

| Item | Python | C++ gap |
|------|--------|---------|
| `server_arguments` / `server_authorization` | settings.json | No UI (acceptable if advanced JSON edit documented) |
| `check_server_resources` | Auto on connect | Partial missing-resources HTML |
| `history_format` | webp/png/jpeg | Fixed webp in embed? Verify |
| `save_image_quality_*` | settings | UI exists — verify applied on save |
| `confirm_discard_image` | Used | Verify wired |
| `prompt_line_count_live` | Resize handle | Partial |
| FileLibrary checkpoints | In-memory + hash | `ComfyFileLibrary` + loras.json hash; **P4.6** uploads local LoRAs before Generate/Inpaint |
| NSFW filter enforcement | Server-side | Verify workflow receives filter value |
| LCM deprecation message | `§13.142` | Verify |
| Plugin update | ZIP extract | Needs KArchive on Android |
| Diagnostics | Collect + modal | Verify parity with Python fields |
| Negative prompt alert | Per arch | Partial |
| `apply_alternative` action | Live/custom | Verify |
| Create region action | Creates layer group | Verify Krita action parity |

---

### 3.5 UI structure differences to close

| Element | Python location | C++ status | Action |
|---------|-----------------|------------|--------|
| Welcome stack | `WelcomeWidget` | Present | Match spacing |
| Style + gear | `StyleSelectWidget` | Preset combo | Replace with style widget |
| Region prompts | In generation scroll | Separate Regions group | **Merge into generation layout** |
| Inpaint menu on Generate | `QToolButton` menu | Separate Inpaint button | Align with Python |
| Settings | Modal 960px sidebar | Present | Remove placeholder panels |
| Error box | `ErrorBox` widget | Status label | Port collapsible error box |

---

## 4. Workstreams & phased plan

| ID | Title | Phase |
|----|-------|-------|
| P0.1 | Bundle `data/` + install + Android copy | P0 |
| P0.2 | StyleCollection — 14 JSON presets | P0 |
| P0.3 | ComfyTheme | P0 |
| P0.4 | Spec traceability + CI checklist | P0 |
| GAP-A2 | workflow.py remainder (§3.1.A) | P1 |
| M8 | Custom workflow parity (manual M8) | P5 |
| P1.1 | Port resources.py | P1 |
| P1.2 | ComfyWorkflowEngine::build_generate | P1 |
| P1.3 | Control + IP-Adapter injection | P1 |
| P1.4 | process_regions + regional generate | P1 |
| P1.5 | Inpaint/refine/edit | P1 |
| P1.6 | Live + animation builders | P1 |
| P1.7 | Upscale + upscale_tiled | P1 |
| P1.8 | Golden JSON tests | P1 |
| C-control-layers | ControlLayer model | P1 |
| P2.1 | ControlLayerList UI | P2 |
| P2.2 | Region UI + semantics (full §3.1.D checklist) | P2 |
| P2.3 | ui.json region/control persist | P2 |
| P2.4 | generate_control_layer jobs | P2 |
| P3.1 | ComfyCloudClient | P3 |
| P3.2 | Managed server / Android wizard | P3 |
| P3.3 | Graph workspace layout | P3 |
| P3.4 | WebSocket workflow publish | P3 |
| P4.1 | Inpaint modes + Seamless/Focus | P4 |
| P4.2 | Prompt translation in generate | P4 |
| P4.3 | Styles tab advanced fields | P4 |
| P4.4 | Language JSON | P4 |
| P4.5 | FileLibrary checkpoints | P4 |
| P4.6 | Pre-generate LoRA upload queue | P4 |
| P5.1 | Unit tests | P5 | complete |
| P5.2 | Mock HTTP integration tests | P5 | complete |
| P5.3 | Manual matrix (Android vs Python) | P5 | complete |
| P5.4 | Android deviations doc | P5 | complete |

Status tracking: **`port_progress.json`**

---

## 5. Testing strategy

### 5.1 Automated

- Unit: `ComfyWorkflowEngine`, `StyleCollection`, `ui.json`, control presets
- Regression: C++ workflow JSON vs Python fixture exporter
- UI smoke: no placeholder strings in release build

### 5.2 Mock HTTP (P5.2)

Automated `QNetworkAccessManager` tests against `ComfyMockHttpServer` — see `tests/ComfyPortP52Test.cpp`.

### 5.3 Manual acceptance matrix (P5.3)

**Doc:** `plugins/dockers/comfyui_remote/docs/MANUAL_ACCEPTANCE_MATRIX.md`  
**Tracking:** `port_progress.json` → `acceptance_manual[]` (per-scenario `manual_status`, `cpp_ready`, `android_applicable`)

| ID | Scenario |
|----|----------|
| M1 | Connect custom ComfyUI |
| M2 | Generate SDXL |
| M3 | Inpaint modes |
| M4 | Regions + control |
| M5 | Live record |
| M6 | Upscale + refine |
| M7 | Animation batch |
| M8 | Custom workflow — workstream **M8** (API JSON + validation; full graph UI still P3.3 deferred) |
| M9 | History + ui.json roundtrip |
| M10 | Settings roundtrip |
| M11 | Cloud sign-in (N/A — P3.1 skipped) |

Run on **Python desktop** (reference), **C++ desktop**, and **C++ Android** (external ComfyUI URL). Set `manual_status` to `pass` / `fail` / `blocked` after each run.

---

## 6. Build & Android notes

| Topic | Requirement |
|-------|-------------|
| Qt WebSockets | Required for graph workflow sync |
| KArchive | Plugin update ZIP extract |
| Plugin data | Install `data/` on Android |
| Python | Not at runtime |

**Android deviations (P5.4):** [`plugins/dockers/comfyui_remote/docs/ANDROID_DEVIATIONS.md`](plugins/dockers/comfyui_remote/docs/ANDROID_DEVIATIONS.md) — intentional gaps vs Python (cloud, managed server, graph UI), platform setup, and parity retained list.

---

## 7. Non-goals (product decision only)

- ComfyUI training inside Krita  
- GGUF/Nunchaku unless Python default  

Managed server on Android: external wizard OK; **not** one-line placeholder.

---

## 8. Immediate next steps

1. **`P0.4`** — `docs/PORT_TRACEABILITY.md` + `scripts/port_ci_checklist.sh`  
2. **`GAP-A2`** — `buildRefineRegion`, control line-invert + union CN type  
3. **`M8`** — API workflow validation, Graph workspace Generate, tests  
4. Continue **manual acceptance** M1–M10 (`MANUAL_ACCEPTANCE_RUN.md`)  

---

*Plan v1.2 — perfect-port: no skipped required parity — 2026-05-24*
