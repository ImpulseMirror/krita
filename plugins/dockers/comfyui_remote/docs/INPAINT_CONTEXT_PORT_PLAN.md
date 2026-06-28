# Perfect Port Plan — Canvas Capture, Inpaint Pipeline & Settings

> **Perfect port — no exceptions.** This work is a **faithful port** of `temp/krita-ai-diffusion/`, not an approximation. Upstream behavior is the only definition of correct. **Half measures, partial ports, “good enough for now,” and WIP checkpoints are never complete** — not for a phase, a PR, or a release. A row in the defect inventory or coverage matrix that is still open means the port is **not done**. Do not mark tasks, phases, or the overall effort complete until every listed defect is closed and **P9** (automated tests) passes in full. **P10** (manual QA) is a separate release checklist — not a gate for marking implementation phases complete.

**Status:** P0–P8 landed in code. P2 code complete (P9 #14 device sign-off pending). P5 engine parity landed. **Remaining open items** table below. P9 tests extended; CI pending KF5 build.  
**Mandate:** **Nothing is out of scope.** If upstream `krita-ai-diffusion` does it and this docker does not (or does it differently), that is a **defect** to fix here.  
**Upstream source of truth:** `temp/krita-ai-diffusion/`  
**Entry symptom:** Fill/refine sends wrong pixels — preview bleeds because we use `image->projection()` instead of `Document.get_image()` with exclusion.

**Related docs:** [GENERATE_TAB_PORT_PLAN.md](GENERATE_TAB_PORT_PLAN.md), [DIFFUSION_TAB_PORT_PLAN.md](DIFFUSION_TAB_PORT_PLAN.md). Last known-good fill workflow tails: `dab5e4221f`.

### Remaining open items (tracked)

| Phase | Item | Notes |
|-------|------|-------|
| **P9** | Full automated test run | Offline symbols: `port_ci_checklist.sh` §P9. Runtime: `build_verify.sh` after KF5 build |
| **P10** | Manual QA matrix | Release checklist; includes P9 #14 Graph + ETN_KritaSelection |

---

## Audit methodology (pass 6)

Passes 1–5 still missed **workflow routing** and **conditioning** gaps. Pass 6 traced full `_prepare_workflow` → `workflow.prepare` → `inpaint` / `refine_region` dispatch (`workflow.py` L1641–1760, L1192–1253, `detect_inpaint` L946–989) and compared every port branch in `slotInpaint` (L460–484), `uploadCanvasForRefineGenerate` (L2315–2384), `ComfyRegionProcess::getRegionInpaintMask`, Live tick.

**Also re-read:** `resolve_inpaint_mode` L934–939, `process_regions` L392–425, `prepare_prompts` / `build_instructions` L1540–1603, `_add_reference_layers` L941–960, `region.py::get_region_inpaint_mask` L383–389, `region.py::get_active_region_layer` L217–224.

### Pass 5 corrections (still valid)

See table below.

### Pass 6 findings (new — pass 5 missed these)

**Corrections to earlier plan text:**

| Earlier claim | Actual upstream |
|---------------|-----------------|
| Pipeline A steps 4–5 always run | Only on **selection** branch (`mask is not None`). Region branch: `bounds = mask.bounds`, `inpaint_mode = add_object`, **skip** `compute_bounds` and `get_context`. |
| P0b invert when `strength >= 1.0` | `strength == 1.0` exactly (`get_selection_modifiers` L1568). |
| `color_match` only on `refine_region` | Also on inpaint output (`workflow.py` L1144) and upscale tiled (`L1135`). |
| `compute_bounds` when `workflow_kind` is `inpaint` | At call site `workflow_kind` is still **`generate`** or **`refine`** — becomes `inpaint` only later (L283–284). |
| Exclude list from `active_regions.control` | Always **`self.regions.control`** (root), even in edit mode (`_get_current_image` L601). |
| Animation refine → `refine_region` | `AnimationWorkspace._prepare_input` uses **`WorkflowKind.refine`** only — no mask path. |
| Context combo `count() > 3` | Upstream removes dynamic items while `count() > 3` (3 fixed presets). Port matches (`> 3`). |

### Pass 6 findings (workflow + conditioning — pass 5 missed)

| Finding | Upstream | Port today |
|---------|----------|------------|
| **Workflow graph pick** | `strength < 1 \|\| is_editing` → `refine_region`; else `inpaint`. **`use_inpaint_model` is internal** to `refine_region` (fooocus / VAE inpaint branch L1219–1234), not graph selector | `slotInpaint` L460–484: `useInpaintModel` → `buildInpaint` vs `buildRefineRegion` — **wrong axis** (SDXL @ 85% gets inpaint graph) |
| Edit @ 100% + mask | `is_editing` → `workflow_kind=refine` → **`refine_region`** even at strength 1.0 | No `editMode` in `slotInpaint`; `fullStrengthInpaint` still pads context (L185–198) |
| `process_regions` | Always in `_prepare_workflow` L263 — regional prompts/masks filtered by bounds | `slotInpaint`: **never called**. `uploadCanvasForRefineGenerate` **does** call it L2368–2384 |
| Region inpaint trigger | No selection mask → `get_active_region_layer(use_parent=not region_only)` L251–256 — **no checkbox required** | `slotInpaint` L123–126: only when `regionOnly` checked |
| `get_region_inpaint_mask` | `region_layer.get_mask(bounds)` after `Bounds.pad(..., square=min_size>0)` L383–388 | `getMaskAsQImage` on link mask source + hand pad L223–262 |
| Edit style / sampling | `active_style`, `linkedEditStyleOverride`, `cond.edit_reference` in `detect_inpaint` | `slotInpaint`: none — random seed L421, no `linkedEditStyleOverride` |
| Prompt instructions | `inpaint_instruction` only when `strength == 1.0` L274; `build_instructions` in `prepare_prompts` | `prependInpaintPromptInstructions` at **all** strengths L425 |
| Layer `<layer:>` tags | `_add_reference_layers` → real reference `ControlInput` L941–960 | `extractLayerPlaceholders` text replace only L424 |
| `buildRefineRegion` | Can attach inpaint control when `use_inpaint_model` L1219–1225 | No inpaint-model branch in `buildRefineRegion` |

---

## Audit methodology (pass 7 — closure)

**Why prior passes kept finding “new” Critical defects:** upstream has **one** orchestrator — `DocumentModel._prepare_workflow()` (L232–325). Port **fragments** it across `slotGenerate`, `tryStartRefineFromGenerate`, `slotInpaint`, `uploadCanvasForRefineGenerate`, `buildLive`. Each audit pass inspected a different fragment; D21 (dispatch), D17 (mask), D22 (`process_regions`) are **the same root problem** seen from different lines.

**Pass 7 method:** enumerate every upstream function in the inpaint/generate surface area; map each row to an existing defect ID. Re-read port for **axes not yet in inventory**.

### Pass 7 verdict

| Severity | New in pass 7? | Notes |
|----------|----------------|-------|
| **Critical** | **No** | D1 (capture), D17 (mask), D21 (dispatch) remain the only Critical axes. Re-confirmed L460–484. |
| High | No new IDs | D22–D24, D18, D19 already cover conditioning / region / edit gaps. |
| Medium | Sub-items of D8 | `resolution.prepare_image`, inpaint 2-pass upscale (workflow.py L1098–1136), `use_reference` / `get_inpaint_reference`, `batch_count`, `active_style`, Flux fill cfg=30 — **workflow-engine parity**, not separate Critical axes. |
| Low | No | D15–D16 unchanged. |

**Confidence model going forward:** completeness = every row in **Upstream coverage matrix** below maps to a defect ID and closes via **P9 automated tests** (or P10 manual check where automation is impractical). Further audits should **verify matrix rows**, not re-discover `_prepare_workflow` piecemeal.

### Root fix (implementation strategy)

Do **not** keep patching `slotInpaint` in isolation. Port **`prepareGenerateWorkflow()`** = faithful C++ translation of `_prepare_workflow()` → single caller for Generate button + `tryStartRefineFromGenerate` + `slotInpaint` routing. Phases P0/P0b feed primitives into it; P5 implements `workflow.prepare` dispatch it calls.

---

## Upstream coverage matrix (closure checklist)

Every row must be satisfied before sign-off. “Port target” names the defect(s). **P9** = automated coverage; **P10** = manual QA where noted.

| Upstream function / step | Port target today | Defect(s) |
|--------------------------|-------------------|-----------|
| `document.py::get_image` | `getCanvasAsQImage` | D1 |
| `document.py::create_mask_from_selection` | `getMaskAsQImage` + `computePaddedSelectionBounds` | D17 |
| `model.py::get_selection_modifiers` | `effectiveModeEarly` in pad path | D3 |
| `resolution.py::compute_bounds` | hand-rolled pad in `slotInpaint` | D10, D19 |
| `resolution.py::compute_relative_bounds` | `targetBoundsRelative` (verify after D17) | D17, D19 |
| `resolution.py::prepare_image` / `prepare_extent` | partial `prepareDiffusionInputExtent` | D8 |
| `CustomInpaint.get_context` | `customInpaintGetContext` | Done (P8) |
| `model.py::resolve_inpaint_mode` | `detectInpaintMode` on raw rect | D3 (auto vs modifier) |
| `region.py::get_active_region_layer` | `regionOnly` checkbox gate | D23 |
| `region.py::get_region_inpaint_mask` | `ComfyRegionProcess::getRegionInpaintMask` | D27 |
| `region.py::process_regions` | missing in `slotInpaint` | D22 |
| `model.py::_get_current_image` | `getCanvasAsQImage` | D1, D18 |
| `model.py::_add_reference_layers` | `extractLayerPlaceholders` only | D28 |
| `workflow.py::prepare_prompts` / `build_instructions` | `prependInpaintPromptInstructions` always | D26 |
| `workflow.py::detect_inpaint` / `get_params` | `detectInpaintParams` (no `editReference`) | D9, D24 |
| Workflow kind promotion (`inpaint` vs `refine_region`) | `useInpaintModel` switch | **D21** |
| `workflow.py::inpaint` | `buildInpaint` (2-pass upscale, `use_reference`, nsfw) | D8, D14 |
| `workflow.py::refine_region` | `buildRefineRegion` (inpaint-model interior) | D8, D29, D14 |
| `workflow.py::refine` | `buildRefine` via `uploadCanvasForRefineGenerate` | D1, D8 |
| `workflow.py::sampling_from_style(strength)` | raw `spinSteps` in `slotInpaint` | D8 |
| `model.py::_prepare_live_workflow` | `buildLive` img2img stub | D6, D12 |
| `model.py::_generate_custom` | `expandCustomKritaInjectionWorkflow` + ETN expand | Done (P2) |
| `AnimationWorkspace._prepare_input` | built-in batch: `buildAnimationFrame` or `buildRefine` (D7 done) | — |
| `model.py::_prepare_upscale_image` | `getCanvasAsQImage` (no exclude OK) | D1 (verify), D8 |
| `model.py::generate_control_layer` | `slotControlPreviewRun` | D1 (preview crop OK) |
| `model.py::show_preview` / `hide_preview` | history preview layer | verify in P7 |
| `model.py::_save_job_result` | `slotHistorySaveImage` raw PNG | D13 |
| `LiveWorkspace.set_result` | not ported | D12 |
| `persistence.py::RecentlyUsedSync` | KSharedConfig split | D11 |
| `ui/generation.py::CustomInpaintWidget` | Ported (3 presets + mask UUIDs, int enum persist) | D4 closed in P1 |
| Qwen L `strength = 1.0` override | hide slider only | D20 |
| Settings: `selection_*`, `color_match`, `nsfw_filter` | partial / missing nodes | D2, D8, D14 |
| Hallucinated `selection_invert` / `selection_square` | still read | D2 |

---

## Defect inventory (all must close)

| # | Defect | Severity |
|---|--------|----------|
| D1 | `getCanvasAsQImage` on inpaint/refine/animation-refine paths | Critical |
| D2 | `selection_invert` / `selection_square` settings (not upstream) | High |
| D3 | `get_selection_modifiers` uses auto-detected mode instead of `inpaint.mode` | High |
| D4 | Context combo / persistence (`context_layer_id`, mask types, item count) | High |
| D5 | `computeInpaintContextBounds` (hallucinated) | Done (P8 — replaced by `customInpaintGetContext`) |
| D6 | Live: only capture flag — missing full `_prepare_live_workflow` | High |
| D7 | Animation: img2img/refine when `strength < 1` or edit | Done (P4) |
| D8 | `buildInpaint` / `buildRefineRegion` vs `workflow.py` | High |
| D9 | Edit fill rule (`FillMode.none` when `is_editing`) | Medium |
| D10 | `compute_bounds` 100% context (`multiple=8`, `min_size=512`, `square=True`) | Medium |
| D11 | `RecentlyUsedSync` / `document_defaults` vs KSharedConfig split | Done (P6) |
| D12 | Live `set_result` compositing (`exclude_internal=False`) | Medium |
| D13 | `_save_job_result` canvas composite | Medium |
| D14 | `nsfw_filter` on workflow output | Medium |
| D15 | Dead `applyInpaintFillPreprocess()` — remove | Done (P8) |
| D16 | Visible-layer guard on capture | Low |
| D17 | Mask export: raw `getMaskAsQImage` + separate pad — not `create_mask_from_selection` (duplicate, invert, pad, `pixelData` from **padded** bounds; preprocess uses `selection_bounds` / `original_bounds`, not raw `selectedExactRect`) | Critical |
| D18 | Exclude list uses merged/active region controls — must be **root** `regions.control` only (`_get_current_image` L601) | High |
| D19 | Bounds/context: `effectiveModeEarly` + hand-rolled 100% pad — not `workflow_kind` + `compute_bounds` + `get_context(modifierMode)` | High |
| D20 | Qwen L: upstream forces `strength = 1.0` in `_prepare_workflow` (L236–237); port only hides strength UI | Medium |
| D21 | **Workflow dispatch:** `buildInpaint` vs `buildRefineRegion` keyed on `useInpaintModel` — must be `strength < 1 \|\| is_editing` → `refine_region`, else `inpaint` (orthogonal to `use_inpaint_model`) | Critical |
| D22 | **`process_regions` missing** in `slotInpaint` — no regional prompts/masks in fill/refine/inpaint path | High |
| D23 | Region inpaint gated on `region_only` checkbox — upstream uses `get_active_region_layer` whenever selection mask is `None` | High |
| D24 | `slotInpaint` skips edit path: no `linkedEditStyleOverride`, `editReference` in `detectInpaintParams`, edit-aware bounds (`refine` @ 100%) | High |
| D25 | `slotInpaint` ignores `checkFixedSeed` / `spinSeed` — always random (L421) | Medium |
| D26 | `prependInpaintPromptInstructions` at all strengths — upstream `inpaint_instruction` only when `strength == 1.0` | Medium |
| D27 | `get_region_inpaint_mask` not ported (`layer.get_mask`, `square=min_size>0`, live `min_mask_size`) | Medium |
| D28 | `_add_reference_layers` missing — `<layer:name>` tags don't attach reference controls in inpaint | Medium |
| D29 | `buildRefineRegion` lacks `use_inpaint_model` interior branch (fooocus / VAE inpaint inside `refine_region`) | Medium |

---

## Upstream canonical pipelines

### A — Generate workspace: fill/refine (`_prepare_workflow`)

**Preamble:** `workflow_kind = refine` when `strength < 1.0` or `is_editing`; else `generate`. If `arch is Arch.qwen_l`, **`strength = 1.0`** before modifiers (L236–237).

| Step | Upstream | File |
|------|----------|------|
| 1 | `get_selection_modifiers(arch, self.inpaint.mode, strength)` — combo mode, **not** `resolve_inpaint_mode()` | L248 |
| 2 | `create_mask_from_selection(smod)` → `(mask, selection_bounds)` | L249 |
| 3 | Branch on mask → `bounds`, `region_layer`, `inpaint_mode` | L251–260 |
| 4 | `process_regions(active_regions, bounds, region_layer)` | L263 |
| 5 | `prepare_prompts(..., inpaint_instruction if strength==1.0)` + `_add_reference_layers` | L271–277 |
| 6 | `_get_current_image(bounds)` when `mask or workflow_kind is refine` | L279–280 |
| 7 | If mask: promote kind (`generate`→`inpaint`, `refine`→`refine_region`); `compute_relative_bounds` | L282–288 |
| 8 | `get_params` / `detect_inpaint`; `calc_selection_pre_process(inpaint, selection_bounds, smod)` | L290–297 |
| 9 | `workflow.prepare(...)` — kind from step 7, **not** `use_inpaint_model` | L299–313 |

**Step 3 branch table** (`model.py` L251–260):

| Condition | `bounds` | `inpaint_mode` | Steps skipped |
|-----------|----------|----------------|---------------|
| `mask is None` and `get_active_region_layer(use_parent=not region_only)` not root | `get_region_inpaint_mask(region_layer, extent)` | `add_object` | `compute_bounds`, `get_context` |
| `mask is not None` (selection) | `compute_bounds(extent, mask.bounds, workflow_kind)` then `get_context(model, mask) or bounds` | `resolve_inpaint_mode()` | — |
| No mask and no region | full extent; no inpaint workflow | — | entire inpaint path |

### B — Custom web workflow (`_generate_custom` + `ETN_KritaSelection`)

| Step | Upstream | File |
|------|----------|------|
| 1 | `get_inpaint_context(selection_node)` | `custom_workflow.py` |
| 2 | `get_selection_modifiers(ctx, InpaintMode.fill, strength)` | `model/model.py` |
| 3 | `create_mask_from_selection(mods)` | `document.py` |
| 4 | `custom.prepare_mask(...)` | `custom_workflow.py` |
| 5 | `_get_current_image(bounds, exclude_internal=not is_live)` | `model/model.py` |

Note: in `_generate_custom`, `is_live` may be overridden when `ETN_KritaStyleAndPrompt` node has `sampler_preset == "live"` (re-read before capture).

### C — Live workspace (`_prepare_live_workflow`)

| Step | Upstream | File |
|------|----------|------|
| 1 | `min_mask_size = 512 if sd15 else 800` | `model/model.py` |
| 2 | `get_selection_modifiers(arch, InpaintMode.fill, strength, min_mask_size)` | `model/model.py` |
| 3 | `create_mask_from_selection` + `calc_selection_pre_process` | `document.py`, `model/model.py` |
| 4 | Region mask if no selection — `get_active_region_layer(use_parent=False)` (Live always `False`; Generate uses `use_parent=not region_only`) | `model/model.py` |
| 5 | `_get_current_image(bounds, exclude_internal=False)` | `model/model.py` |
| 6 | `workflow.prepare(..., is_live=True)` | `workflow.py` |

Live loop: `LiveWorkspace._continue_generating` → `_prepare_live_workflow` each tick.

### D — Animation workspace

**Single frame** (`_generate_frame` → `_prepare_input`):

- `requires_image = strength < 1.0 or is_editing`
- `canvas = _get_current_image(full_bounds)` if required, else extent only
- `kind = WorkflowKind.refine` when `requires_image`, else `generate` — **not** `refine_region` / no mask
- `process_regions(m.regions, bounds, m.layers.root, time=time)` — frame time on region pass

**Full animation batch** (`_generate_batch`):

- Per frame: if `strength < 1.0 or is_editing` → `layer.get_pixels(time=frame)` (active layer pixels, not full doc projection)
- Else txt2img from extent (`buildAnimationFrame` / `buildTextToImage` equivalent)
- Port: timeline switch + per-frame `buildAnimationFrame` (txt2img) or `buildRefine` (img2img) via `batchNeedsPerFrameAnimationRefine`

### Pixel capture (`_get_current_image` → `get_image`)

```python
# exclude_internal=True (default)
# NOTE: always self.regions.control (root), NOT active_regions — edit mode unchanged
exclude = [c.layer for c in self.regions.control if not c.mode.is_part_of_image]
if self._layer:
    exclude.append(self._layer)
# ValueError if no visible image layers remain
return self._doc.get_image(bounds, exclude_layers=exclude)
```

```python
# document.py — hide visible excludes → refreshProjection → pixelData → show → refresh
```

**`exclude_internal=False`:** `exclude` stays **empty** — preview and all controls remain in composite. Used for: Live `_prepare_live_workflow`, custom workflow when `is_live`, `LiveWorkspace.set_result`.

### Capture policy (every upstream caller)

| Path | `exclude_internal` | Port target |
|------|-------------------|-------------|
| Fill/refine/inpaint | `True` | `slotInpaint`, `uploadCanvasForRefineGenerate` |
| Full-canvas refine (no mask) | `True` | `uploadCanvasForRefineGenerate` |
| Live workspace | `False` | `ComfyUIRemoteDockLive.cpp` full `_prepare_live_workflow` |
| Custom workflow | `not is_live` | Custom generate path |
| Animation single-frame refine | `True` | Animation upload before `buildAnimationFrame` / img2img |
| Animation batch refine | N/A — `layer.get_pixels(time)` | Per-frame active-layer read |
| Control layer job | No exclude list | `getDocumentImage` full = `get_image` no excludes |
| Upscale | No exclude list | `ComfyUIRemoteDockUpscale.cpp` — `getDocumentImage` no excludes |
| Live `set_result` preview composite | `False` | Live result overlay path |
| Save job result PNG | `True` | History save / export composite |
| Control preview | No exclude list | `slotControlPreviewRun` — matches `generate_control_layer` |

`ControlMode.is_part_of_image`: `reference`, `line_art`, `blur` only (`resources.py`).

### Bounds (`compute_bounds` + `create_mask_from_selection`)

| Step | Rule |
|------|------|
| Mask pad (`create_mask_from_selection`) | Invert **before** pad when `replace_background && strength == 1.0`; pad from feather+padding on **original** selection diagonal; `square=False`, `multiple=16`, `min_size=256` default; mask `pixelData` from **padded** bounds |
| 100% inpaint context (`compute_bounds`) | `Bounds.pad(..., min_size=512, multiple=8, square=True)` — only when `workflow_kind` is **`generate`** at call time (`strength == 1.0` and not editing) |
| Refine (`workflow_kind == refine` at call time) | Returns `mask_bounds` only — when `strength < 1.0` **or** `is_editing` (edit @ 100% still mask bounds, not padded context) |
| Region inpaint | `bounds = mask.bounds` — no `compute_bounds` expansion |
| Custom `get_context` | Only when `InpaintMode.custom`; `automatic` → `None` |

### Context UI (`CustomInpaintWidget`)

- Visible when `inpaint.mode is custom` and (selection or region-only)
- **3 fixed items in order:** `automatic`, `mask_bounds`, `entire_image`
- Dynamic: transparency + selection masks only; UUID combo data → `context=layer_bounds` + `context_layer_id`
- `update_context_layers`: remove while `count() > 3`
- Persist: `ui.json` inpaint via `_serialize` parity — `mode`/`fill`/`context` as enum **ints**, `context_layer_id` as braced UUID string

---

## Settings impact matrix (every setting must work or be removed)

### Diffusion tab

| Setting | Default | Must affect |
|---------|---------|-------------|
| `selection_feather` | 10 | Mask pad + `calc_selection_pre_process` |
| `selection_blend` | 25 | `calc_selection_pre_process` blend |
| `selection_padding` | 6 | `create_mask_from_selection` pad |
| `color_match` | true | Output `color_match` on inpaint (`workflow.py` L1144), upscale tiled (`L1135`), `refine_region` (`L1244`) via `MiscParams` |
| `nsfw_filter` | 0 | `MiscParams` on **all** output paths (`generate`, `inpaint`, `refine`, `refine_region` — `workflow.py`); port has **no** nsfw nodes today (D14) |

### Code-only settings (no upstream UI — keep via `settings.json`)

| Setting | Default | Must affect |
|---------|---------|-------------|
| `selection_min_transition` | 32 | `get_selection_modifiers` + preprocess |
| `selection_grow_offset` | 4 | preprocess grow |

### Must remove (hallucinated)

| Key | Action |
|-----|--------|
| `selection_invert` | Delete reader; invert only when `inpaint.mode == replace_background && strength == 1.0` |
| `selection_square` | Delete reader; `square=False` for mask; `square=True` only in `compute_bounds` context step |

### Custom inpaint / document state

| Field | When applied |
|-------|--------------|
| `mode` | Always; `get_selection_modifiers` input = combo value, **not** auto-detect |
| `fill` | Custom + strength 100% + not `is_editing`; else `detect_inpaint` / `get_params` |
| `use_inpaint` | Custom → `get_params`; else `detect_inpaint` |
| `use_prompt_focus` | Custom → condition mask |
| `context` + `context_layer_id` | Custom → `get_context` only |

### `RecentlyUsedSync` (`persistence.py` → `settings.document_defaults`)

Applies to **new documents** without `ui.json`:

- `inpaint_mode`, `inpaint_fill`, `inpaint_use_model`, `inpaint_use_prompt_focus`, `inpaint_context` (field names from `RecentlyUsedSync` dataclass)
- `context_layer_id` is **not** in `document_defaults` — only per-document `ui.json` inpaint via `_serialize(CustomInpaint)`
- Skip restoring `inpaint_context == layer_bounds` on fresh doc (upstream L69–70)
- Port must use same `document_defaults` object in `settings.json` — not parallel KSharedConfig keys for same fields

---

## Implementation phases

### P0 — `getDocumentImage` + `_get_current_image` (shared primitive)

**New API** (`ComfyUIUtils`, `ComfyResources`):

1. `getDocumentImage(image, bounds, excludeNodes)` — port `KritaDocument.get_image`
2. `ControlMode::isPartOfImage(mode)`
3. `collectInpaintExcludeNodes(dock, image, excludeInternal)` — root `regions.control` only (D18); empty list when `excludeInternal=false`
4. Visible image-layer guard (upstream `ValueError` message)

**Call sites (D1):**

| File | Change |
|------|--------|
| `ComfyUIRemoteDockInpaint.cpp` | Bounds first → `getDocumentImage` |
| `ComfyUIRemoteDockGenerate.cpp` | `uploadCanvasForRefineGenerate` |
| `ComfyUIRemoteDockLive.cpp` | Canvas upload (`excludeInternal=false`) |
| `ComfyUIRemoteDockGenerate.cpp` | Animation single-frame refine upload |
| `ComfyUIRemoteDockUpscale.cpp` | `getDocumentImage` no excludes (parity `get_image`) |
| `ComfyUIRemoteDockControlGenerate.cpp` | `getDocumentImage` no excludes |
| `ComfyUIRemoteDockGenerate.cpp` | Control preview — no excludes |
| History save / live result composite | Per capture policy table |

---

### P0b — Selection modifiers + mask export (D2, D3, D10, D17, D19)

1. Port `createMaskFromSelection(image, viewManager, smod)` — full `document.py` parity (duplicate, invert, pad, `pixelData` from padded bounds, return `selection_bounds`)
2. Remove `selection_invert`, `selection_square` readers
3. `modifierMode` = `comboInpaintMode` data; `effectiveMode` = `resolve_inpaint_mode()` for workflow only
4. Invert only when `modifierMode == replace_background && strength == 1.0`
5. `calcSelectionPreProcess(..., selectionBounds)` — use `selection_bounds` from step 1, not raw `selectedExactRect`
6. Bounds: `workflow_kind` from strength/edit (+ Qwen L force 1.0, D20) → `compute_bounds` → `get_context` only if `modifierMode == custom` on selection branch
7. Delete hand-rolled 100% context QRect in `slotInpaint` (D19)
8. Update [DIFFUSION_TAB_PORT_PLAN.md](DIFFUSION_TAB_PORT_PLAN.md) — keys removed

---

### P1 — `CustomInpaintWidget` + `ui.json` (D4, D5, D9)

1. Context combo: 3 presets + mask UUIDs; filter `transparencymask` + `selectionmask`
2. `set_context` / `update_context` / `update_context_layers` (`count() > 3`)
3. Persist `context` + `context_layer_id`; load round-trip
4. Port `get_context()` — custom mode only; delete `computeInpaintContextBounds`
5. Port `get_params(is_editing)` — fill `none` when editing; fill combo enable = `strength==100% && !is_editing`
6. Port widget enable rules: Seamless (`arch.is_sdxl_like or has_controlnet_inpaint`), Focus visible (SD1.5 / SDXL), Edit toggle (`can_toggle_edit`)

---

### P2 — Custom web workflow `ETN_KritaSelection` (D5) — **Done**

Port `_generate_custom` Section B:

1. **`get_inpaint_context`** → `getInpaintContextFromSelectionNode`
2. **`get_selection_modifiers(ctx, fill, strength)`** → `getSelectionModifiersForContext` (`mask_bounds` → `multiple=1`)
3. **`create_mask_from_selection`** → `createMaskFromSelection`
4. **`prepare_mask`** → `prepareCustomWorkflowMask` (automatic / mask_bounds / entire_image; invalid context errors)
5. **Capture** → `captureCustomWorkflowKritaInput` with `exclude_internal = !customGraphLiveActive`
6. **Expand** → `expandCustomKritaWorkflowNodes` (all 7 ETN node types)
7. **`collect_parameters`** → layer export at capture bounds; UUID overrides; first image/mask layer defaults; mask via `getMaskAsQImage`
8. **Live dedup** → `computeCustomWorkflowInputFingerprint` skips unchanged graph-live submits
9. **History** → `customWorkflowMetadata`, capture bounds/mask on custom batch jobs

**P9 #14:** automated gate in `testCustomWorkflowFullSelectionPipeline`; manual Graph workspace sign-off in P10.

---

### P3 — Live workspace full port (D6, D12)

Port `_prepare_live_workflow` end-to-end in `ComfyUIRemoteDockLive.cpp`:

- `min_mask_size` 512 (SD1.5) / 800 (else)
- Selection mask + region mask branches
- Region custom grow/feather/blend when no selection
- Region lookup: `use_parent=False` (not `region_only` rule from Generate)
- `exclude_internal=False` capture
- Re-prepare each live tick (`_continue_generating`)
- `set_result` compositing with `exclude_internal=False` overlay

---

### P4 — Animation workspace (D7)

**Status:** Implemented (built-in path).

**Single frame:**

- When `strength < 1` or edit: upload `_get_current_image(full bounds)` then img2img/refine workflow (`buildRefine` via `batchNeedsPerFrameAnimationRefine`)
- When strength 100%: txt2img (`buildAnimationFrame`)

**Full batch:**

- When refine/edit: read **target layer pixels** at frame time (`getLayerProjectionByUuid` after timeline seek)
- Saved-document check before full batch; `.animation/` folder + `animation_id` in extra_data unchanged

---

### P5 — Workflow engine parity (D8, D21, D29) — **Done**

Align `ComfyWorkflowEngine.cpp` with `backend/workflow.py`:

| Upstream | Port |
|----------|------|
| `workflow.prepare` dispatch | **Route by `strength` + `is_editing`**, not `useInpaintModel` (D21) — `ComfyPrepareGenerateWorkflow` + `slotInpaint` |
| `refine_region` | `buildRefineRegion` — grow/feather mask, color_match, nsfw_filter, crop, compositing mask, **`use_inpaint_model` interior branch** (D29) |
| `inpaint` path | `buildInpaint` — verified against upstream |
| `insertMaskedFill` / fill kinds | `fillKind` from `detect_inpaint` / `get_params` |
| `apply_grow_feather` | Uses `calc_selection_pre_process` grow/feather |
| `sampling_from_style(strength)` | `resolveSamplingFromStyle` + `applyStrengthResolvedSamplingToRefine` on inpaint/live/refine generate paths; `finishWorkflowWithSamplerCustom` → `SplitSigmas` @ `start_at_step` |

Tests: `testResolveSamplingFromStyleStrength`, `testBuildRefineRegion*`, `testApplyStrengthResolvedSamplingToRefine`, `testBuildRefineRegionSplitSigmasAtStrength`, `ComfyWorkflowEngineGoldenTest` (inpaint/refine golden chains).

---

### P6 — Persistence parity (D11)

**Status:** Done.

1. `RecentlyUsedSync` fields in `settings.json` `document_defaults` — single source (`recentlyUsedSyncFromSettings`, `applyRecentlyUsedSyncFromSettings`)
2. Stopped duplicating inpaint/batch fields in KSharedConfig — legacy keys migrated once on load
3. `schedulePersistDocumentDefaults` / `persistDocumentDefaultsToSettings` writes all tracked fields including `inpaint_context` (`layer_bounds` skipped on fresh doc apply)
4. Per-document `ui.json` inpaint — `context_layer_id` via `inpaintWorkspaceToJson` (existing)
5. Live workspace strength — annotation + `flushDocumentUiJsonNow` `live` key; `scheduleDocumentUiJsonSave` on strength change in Live

---

### P7 — Output / apply paths (D13, D14) — **Done**

1. **NSFW filter:** `settings.nsfw_filter` on generate dispatch, inpaint submit, live finalize; builder params on refine/inpaint/live
2. **Save result:** `compositeJobResultOnDocument` in `slotHistorySaveImage`; batch stash `contextBounds` / `hasMask` / `regionLayerNames` per job
3. **Apply behavior:** live poll + `slotAiDiffusionApply` use `apply_behavior_live` / `apply_region_behavior_live` + `resultBounds`; `applyResultToNamedRegionLayers` shared with history

---

### P8 — Cleanup (D15, D16) — **Done**

- `applyInpaintFillPreprocess`, `computeInpaintContextBounds`, `getSelectionModifiersInvert`, `getSelectionModifiersSquare` — removed (fill is workflow-only; modifiers unified in `getSelectionModifiers`)
- Context combo: 3 fixed presets only; mask-node UUID → internal `layer_bounds` + `context_layer_id`; standalone `"layer_bounds"` combo data rejected
- `refreshInpaintContextLayers`: trim while `count() > 3`
- `document_defaults`: `layer_bounds` normalized to `automatic` on read and write

---

### P9 — Automated test matrix

Run in CI / dev build:

```bash
cmake --build build --target kritacomfyuiremotetest ComfyWorkflowEngineGoldenTest
./build/bin/kritacomfyuiremotetest
./build/bin/ComfyWorkflowEngineGoldenTest   # if built
```

| Area | Test target | Notes |
|------|-------------|-------|
| Custom web workflow P2 | `ComfyUIRemoteDockTest` | `testCustomWorkflowKritaSelection*`, `testCustomWorkflowFullSelectionPipeline`, `testExpandCustom*`, `testExtractLorasFromPromptAndMerge`, `testPrepareCustomWorkflowStyleAndPrompts`, `testCustomWorkflowLiveCapturePolicy` |
| Mask / modifiers / bounds | `ComfyUIRemoteDockTest` | `testGetSelectionModifiersAndBounds`, `testCustomInpaintContextAndParams` |
| Workflow engine | `ComfyWorkflowEngineGoldenTest`, `ComfyUIRemoteDockTest` | inpaint / refine_region golden chains (P5) |
| File library / styles | `ComfyPortP51Test`, `ComfyPortP52Test` | LoRA, style collection |

**Phase-done rule:** P0–P8 close when implementation lands in code. **P9** adds automated coverage for changed rows (run before release). **P6/P7/P9 are later phases in the implementation order, not prerequisites for finishing P0–P5.**

**P2 automated gate (replaces old P9 #14 manual-only wording):** `testCustomWorkflowFullSelectionPipeline` covers `prepare_mask` + `ETN_KritaSelection` expand bounds; `testCustomWorkflowKritaSelectionPrepare` covers modifiers/context.

---

### P10 — Manual QA matrix (release checklist)

Human verification in Krita + ComfyUI. **Not required** to mark P0–P8 implementation complete; run before release or when debugging regressions.

**Preflight:** `plugins/dockers/comfyui_remote/scripts/p10_inpaint_preflight.sh` (offline P9 gate + ComfyUI + printable checklist)

| # | Scenario |
|---|----------|
| 1 | Fill 100%, preview visible — no preview in upload |
| 2 | Refine 50%, partial selection — mask bounds, exclusions |
| 3 | Refine 50%, full canvas — `uploadCanvasForRefineGenerate` |
| 4 | Depth control excluded; reference included |
| 5 | Live tick — full projection, no excludes |
| 6 | Live with selection — mask path, min_mask_size by arch |
| 7 | Live region-only — region mask + custom grow/feather |
| 8 | `inpaint.mode=automatic` — modifiers use `automatic`, workflow uses detected mode |
| 9 | `replace_background` @ 100% — invert + feather cap |
| 10 | Custom context modes + reload document |
| 11 | Custom + edit — fill none |
| 11b | Edit @ 100% with selection — context bounds = mask only (refine `workflow_kind`), not padded fill context |
| 12 | Feather/blend/padding sliders change mask + preprocess |
| 13 | `selection_feather=0` — zero grow/feather/blend |
| 14 | Web workflow `ETN_KritaSelection` + prepare_mask (Graph workspace, live ComfyUI) |
| 15 | Animation single-frame refine — canvas upload with exclude |
| 16 | Animation batch refine — per-frame layer pixels |
| 17 | `document_defaults` on new doc — inpaint fields applied |
| 18 | NSFW filter strict — output filtered |
| 19 | Save result PNG — composites on excluded canvas |
| 20 | `selection_invert` / `selection_square` in JSON — no effect |
| 21 | Upscale / control generate — full projection (no exclude) |
| 22 | Region-only inpaint — bounds = region mask, no extra context pad |
| 23 | Mask PNG bounds match padded mask bounds; preprocess uses `selection_bounds` |
| 24 | Qwen L style — workflow uses strength 1.0 regardless of hidden slider |
| 25 | Edit mode — exclude list still from root `regions.control` |
| 26 | SDXL @ 85% + mask → `refine_region` graph, not `buildInpaint` |
| 27 | Edit @ 100% + selection → `refine_region`, context = mask bounds only |
| 28 | Multi-region fill — regional masks in workflow |
| 29 | Region inpaint without `region_only` — active linked layer |
| 30 | Fixed seed inpaint matches generate |
| 31 | `<layer:foo>` attaches reference control in inpaint |

Desktop + Android (`kritacomfyuiremote`).

---

## Upstream → port map (complete)

| Upstream | Port |
|----------|------|
| `document.py::get_image` | `getDocumentImage` |
| `document.py::create_mask_from_selection` | `createMaskFromSelection` — mask image + `selection_bounds`; pad params from `get_selection_modifiers` |
| `model.py::get_selection_modifiers` | Modifier builder (`modifierMode`) |
| `model.py::calc_selection_pre_process` | `calcSelectionPreProcess` |
| `model.py::_get_current_image` | `collectInpaintExcludeNodes` + `getDocumentImage` |
| `model.py::_prepare_workflow` | `slotInpaint` + generate routing |
| `model.py::_prepare_live_workflow` | `ComfyUIRemoteDockLive.cpp` |
| `model.py::Animation._generate_frame` / `_generate_batch` | Animation generate path |
| `resolution.py::compute_bounds` | Context bounds helper |
| `model.py::CustomInpaint.*` | P1 |
| `custom_workflow.py::*` | P2 |
| `persistence.py::RecentlyUsedSync` | P6 — done |
| `persistence.py` ui.json inpaint | P1 + P6 |
| `backend/workflow.py::refine_region` | `buildRefineRegion` |
| `backend/workflow.py::inpaint` | `buildInpaint` |
| `model.py::_save_job_result` | P7 |
| `LiveWorkspace.set_result` | P3 |

---

## Files touched

| File | Phases |
|------|--------|
| `ComfyUIUtils.h` / `.cpp` | P0, P0b, P1, P8 |
| `ComfyResources.h` / `.cpp` | P0 |
| `ComfyUIRemoteDockInpaint.cpp` | P0, P0b, P1, P5 — **D21–D28** (dispatch, `process_regions`, edit/seed, references) |
| `ComfyRegionProcess.cpp` | P0b, P5 — D27 region mask parity |
| `ComfyUIRemoteDock.cpp` | P1, P6 |
| `ComfyUIRemoteDockPrivate.h` | P1, P3, P6 |
| `ComfyUIRemoteDockGenerate.cpp` | P0, P4, P5, P7 |
| `ComfyUIRemoteDockLive.cpp` | P0, P3 |
| `ComfyUIRemoteDockControlGenerate.cpp` | P0 |
| `ComfyUIRemoteDockUpscale.cpp` | P0 |
| `ComfyUIRemoteDockHistory.cpp` | P7 |
| `ComfyUIRemoteDockSettings.cpp` | P6, P7 |
| `ComfyWorkflowEngine.h` / `.cpp` | P2, P5, P7 |
| `tests/ComfyWorkflowEngineGoldenTest.cpp` | P5, P9 |
| `tests/ComfyUIRemoteDockTest.cpp` | P9 |
| `docs/DIFFUSION_TAB_PORT_PLAN.md` | P0b |

---

## Implementation order

**Spine first:** port `prepareGenerateWorkflow()` (= `_prepare_workflow`) before more `slotInpaint` patches.

1. **P0** — `getDocumentImage` + exclude policy + all capture call sites  
2. **P0b** — `createMaskFromSelection` + modifiers + bounds helpers → feed into spine  
3. **P0c** — **`prepareGenerateWorkflow()`** — single orchestrator; wire Generate / Refine / Inpaint through it. **P0c follow-up (done):** D23 `resolveActiveRegionLayer`, D25 fixed seed in `slotInpaint`, D26 inpaint instructions only @ strength 1.0. Remaining in spine: D24, D28 (P1/P5).  
4. **P1** — custom inpaint UI + persistence (**done**)
5. **P2** — custom web workflow `ETN_KritaSelection` (**done**)
6. **P5** — `workflow.prepare` + engine parity (`buildInpaint` / `buildRefineRegion` interiors — D8 sub-items)  
7. **P3** — Live workspace full port  
8. **P4** — Animation workspace  
9. **P6** — persistence / defaults (**done**)
10. **P7** — NSFW, save composite, apply  
11. **P8** — delete hallucinated code  
12. **P9** — automated test matrix + **coverage matrix** row-by-row unit/golden coverage  
13. **P10** — manual QA matrix (release checklist; optional per-phase)  

Each phase: focused commits. Behavior only from named upstream functions — no invented heuristics.

**Done when (implementation):** P9 passes, coverage matrix all green, defect inventory empty.  
**Done when (release):** P10 manual QA complete on Desktop + Android.
