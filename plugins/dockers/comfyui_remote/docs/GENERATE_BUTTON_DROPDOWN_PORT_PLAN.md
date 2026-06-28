# Generate Button Dropdown — Perfect Port Plan

**Source of truth:** `temp/krita-ai-diffusion/ai_diffusion/ui/generation.py` (`GenerationWidget`), `model/model.py` (`DocumentModel._prepare_workflow`, `is_editing`, `can_toggle_edit`, `can_edit`), `backend/workflow.py` (`detect_inpaint`, `detect_inpaint_mode`).

**Target:** `plugins/dockers/comfyui_remote/` — Generate workspace action row: `[Generate CTA | ▼ | region mask]`.

---

## UI cluster (upstream)

| Widget | Role |
|--------|------|
| `generate_button` | Primary CTA; label + icon from `update_generate_options()`; click → `model.generate()` |
| `inpaint_mode_button` | Down-arrow; click → `show_inpaint_menu()` — **this is the dropdown** |
| `region_mask_button` | Toggle `region_only`; visible when document has regions |
| `queue_button` | Batch / queue mode (separate from dropdown) |

Hidden state widgets (not in dropdown, driven by menu choices):

| Widget | Bound state |
|--------|-------------|
| `custom_inpaint` | Visible when `inpaint.mode == custom` and (selection or region_only) |
| `checkEditMode` (internal) | `edit_mode`; toggled from menu Edit entries |
| `comboInpaintMode` (internal) | `inpaint.mode`; synced when menu item picked |

---

## Context menu matrix

`show_inpaint_menu()` picks **one** menu from state. Width = generate button + arrow button.

### Menu selection rules (`generation.py` L952–972)

| Condition | Menu |
|-----------|------|
| `!edit_mode && arch.is_edit` | `edit_menu` |
| `strength == 1.0` && region_only | `generate_region_menu` |
| `strength == 1.0` && selection_bounds | `inpaint_menu` |
| `strength == 1.0` && else | `generate_menu` |
| `strength < 1.0` && region_only | `refine_region_menu` |
| `strength < 1.0` && selection_bounds | `refine_selection_menu` |
| `strength < 1.0` && else | `refine_menu` |

**Edit action enable:** `can_edit` (= linked edit style configured), not `arch.is_edit`. Index per menu:

- `inpaint_menu`: action `[-2]` (Edit before Custom)
- `refine_menu` / `refine_selection_menu`: action `[1]`

### 1. `generate_menu` (full canvas, strength 100%)

| Item | Sets | Workflow on Generate |
|------|------|----------------------|
| Generate | `mode=automatic`, `edit_mode=false` | `WorkflowKind.generate` txt2img/img2img |
| Edit | `mode=automatic`, `edit_mode=true` | `WorkflowKind.refine` + linked edit style if set |

### 2. `inpaint_menu` (partial selection, strength 100%)

| Item | Sets | Workflow on Generate |
|------|------|----------------------|
| Default (Auto-detect) | `mode=automatic` | `inpaint`; auto fill vs expand from selection geometry |
| Fill | `mode=fill` | `inpaint`; `fill=blur` |
| Expand | `mode=expand` | `inpaint`; `fill=border` |
| Add Content | `mode=add_object` | `inpaint`; `fill=neutral`; may enable condition mask (SD1.5) |
| Remove Content | `mode=remove_object` | `inpaint`; `fill=inpaint` |
| Replace Background | `mode=replace_background` | `inpaint`; `fill=replace` |
| Edit | `mode=add_object`, `edit_mode=true` | refine/inpaint + instruction edit |
| Generate (Custom) | `mode=custom` | `inpaint` with `inpaint.get_params()` from custom bar |

`automatic` resolves via `resolve_inpaint_mode()` → `detect_inpaint_mode(extent, selection_bounds)` (touch edge → expand, else fill).

### 3. `generate_region_menu` (region_only, strength 100%)

| Item | Sets | Workflow |
|------|------|----------|
| Generate Region | `mode=automatic` | Region mask inpaint; `inpaint_mode=add_object` from layer alpha |
| Generate Region (Custom) | `mode=custom` | Custom bar + region mask |

### 4. `refine_menu` (full canvas, strength &lt; 100%)

| Item | Sets | Workflow |
|------|------|----------|
| Refine | `mode=automatic`, `edit_mode=false` | `WorkflowKind.refine` img2img |
| Edit | `mode=automatic`, `edit_mode=true` | Linked edit refine |

### 5. `refine_selection_menu` (selection, strength &lt; 100%)

| Item | Sets | Workflow |
|------|------|----------|
| Refine | `mode=automatic` | `WorkflowKind.refine_region` |
| Edit | `mode=automatic`, `edit_mode=true` | Edit + selection |
| Refine (Custom) | `mode=custom` | Custom bar + refine_region |

### 6. `refine_region_menu` (region_only, strength &lt; 100%)

| Item | Sets | Workflow |
|------|------|----------|
| Refine Region | `mode=automatic` | refine_region on active region |
| Refine Region (Custom) | `mode=custom` | Custom + region |

### 7. `edit_menu` (`arch.is_edit` checkpoint, `!edit_mode`)

| Item | Sets | Workflow |
|------|------|----------|
| Edit | `mode=automatic` | Native edit arch workflow |
| Edit (Custom) | `mode=custom` | Custom inpaint params on edit arch |

---

## `change_inpaint_mode(mode, is_edit)`

```python
self.model.inpaint.mode = mode
if is_edit is not None:
    self.model.edit_mode = is_edit
```

`is_edit=None` for Custom entries — does **not** change `edit_mode`.

---

## Dynamic Generate button label (`update_generate_options`)

| State | Button text / icon |
|-------|-------------------|
| No selection, not region_only, editing | Edit / `edit` |
| No selection, strength 100% | Generate / `workspace-generation` |
| No selection, strength &lt; 100% | Refine / `refine` |
| Selection/region + explicit mode | Mode label (`Fill`, `Expand`, …) or `Generate Region` / `Refine Region` |
| `mode=custom` | suffix `(Custom)` / `inpaint-custom` |
| `inpaint_mode_button` visible | Always if selection/region; else only if `can_toggle_edit` |

`can_toggle_edit` = **not** `arch.is_edit` **and** linked edit style exists.

---

## Custom inpaint bar (`CustomInpaintWidget`)

Visible when `mode == custom` and (selection or region_only).

| Control | State | Effect |
|---------|-------|--------|
| Seamless | `use_inpaint` | `use_inpaint_model` in custom params |
| Focus | `use_prompt_focus` | `use_condition_mask` (SD1.5 / SDXL) |
| Edit checkbox | `edit_mode` | Inside bar for instruction edit |
| Fill combo | `fill` | Pre-fill before diffusion (disabled when editing or strength &lt; 100%) |
| Context combo | `context` + layer list | `get_context()` bounds override |

---

## Generate click routing (C++ mirror)

`slotGenerate()` → `tryStartRefineFromGenerate()`:

1. Partial document selection → `slotInpaint()` (inpaint/refine_region by strength)
2. `region_only` + active region mask → `slotInpaint()`
3. `strength < 100%` or `edit_mode` → canvas upload + `buildRefine` / regional
4. Else → full `generate` workflow

Inpaint path uses `comboInpaintMode`, `detectInpaintParams`, `prependInpaintPromptInstructions`, fill/context from custom bar.

---

## Implementation status

| Area | Status | Notes |
|------|--------|-------|
| 7 context menus + items | **Done** | `setupGenerateInpaintMenus()` |
| `btnInpaintMode` + menu exec | **Done** | `showInpaintModeMenu()` |
| Dynamic CTA | **Done** | `updateGenerateOptions()` |
| Region mask toggle | **Done** | `btnRegionMask` ↔ `checkRegionOnly` |
| Custom inpaint row | **Done** | Seamless/Focus/Edit/Fill/Context; `editModeSwitch` ↔ hidden `checkEditMode` |
| Selection poll → CTA refresh | **Done** | `pollDocumentChanges()` |
| Menu → `comboInpaintMode` sync | **Done** | `setInpaintModeKey()` |
| `show_inpaint_menu` edit branch | **Done** | `archIsEdit` → `menuEdit` (`ComfyGenerateUi.cpp`) |
| `btnInpaintMode` visibility | **Done** | `canToggleEdit` on full canvas |
| Edit action `setEnabled` | **Done** | `hasLinkedEditStyle` per menu index |
| Menu icons in menu | **Done** | `setIconVisibleInMenu(true)` |
| Persist `edit_mode` from menu | **Done** | KConfig `EditMode` on menu pick |
| Ctrl+click generate replace | **Done** | `slotGenerateReplace()` + `generateOneShotQueueMode` |
| Qwen Layered layer count row | **Done** | Hide strength, show `layerCountRow` when `Arch::QwenL` |
| Context combo dynamic layers | **Done** | `refreshInpaintContextLayers()` |
| Remove `btnGenerateViewOperations` | **Done** | Removed from dock UI |

---

## Work phases

### Phase 1 — Dropdown fidelity — **Done**

1. ~~Fix `showInpaintModeMenu()` menu selection vs upstream.~~
2. ~~Fix `can_toggle_edit` / `can_edit` / `arch.is_edit` usage in UI.~~
3. ~~Menu polish (icons, persist edit mode, arrow button height).~~
4. ~~Verify `updateGenerateOptions()` on style/checkpoint change.~~

### Phase 2 — Custom bar + generate routing

1. ~~Move Edit toggle into custom row when appropriate.~~ (`editModeSwitch` in `customInpaintRowWidget`)
2. ~~Context combo: dynamic layer entries (`InpaintContext.layer_bounds`).~~
3. ~~`updateInpaintControlsForArch()` parity (fill disabled when refining/editing).~~
4. Ctrl+click replace queue mode.

### Phase 3 — Arch-specific — **Done**

1. ~~Qwen Layered: layer count UI + workflow `layer_count`.~~
2. ~~Native edit arch (`arch.is_edit`): force custom mode in `detect_inpaint` path.~~ — `ComfyResources::isEditArch` tail override in `detectInpaintParams`; `effectiveInpaintMode` → `custom` in `ComfyPrepareWorkflow`

---

## Files

| File | Role |
|------|------|
| `ComfyUIRemoteDockGenerate.cpp` | Menus, `updateGenerateOptions`, `showInpaintModeMenu`, `tryStartRefineFromGenerate` |
| `ComfyUIRemoteDockInpaint.cpp` | Selection/region inpaint workflows |
| `ComfyUIRemoteDock.cpp` | Action row layout, hidden state widgets, polling |
| `ComfyUIUtils.cpp` | `detectInpaintMode`, `detectInpaintParams`, edit helpers |
| `ComfyUIRemoteDockPrivate.h` | Menu pointers, widget refs |

---

## Verification checklist

- [ ] No selection, SDXL style: dropdown shows Generate + Edit (Edit enabled only with linked edit style).
- [ ] Partial selection, strength 100%: dropdown shows all 7 inpaint modes + Edit + Custom.
- [ ] Partial selection, strength 50%: Refine + Edit + Refine (Custom).
- [ ] Region mask on, active region: Generate/Refine Region menus.
- [ ] Flux Kontext / Qwen edit checkpoint: `edit_menu` when not in edit_mode.
- [ ] Pick Fill → CTA reads "Fill", Generate runs inpaint with blur fill.
- [ ] Pick Custom → custom bar visible; Fill/Context affect workflow.
- [ ] Pick Edit (linked style) → `edit_mode` true, refine path on full canvas.
