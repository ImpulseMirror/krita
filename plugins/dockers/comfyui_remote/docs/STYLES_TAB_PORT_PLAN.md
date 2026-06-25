# Styles tab port plan (krita-ai-diffusion `style.py`)

Reference: [ai_diffusion/ui/style.py](https://github.com/Acly/krita-ai-diffusion/blob/main/ai_diffusion/ui/style.py)

## Scope

Bring Configure → **Styles** to feature parity with Python `StylePresets` + `LoraList` + `SamplerWidget`.

**Out of scope (filesystem / host):**
- Open style / checkpoint / LoRA folder buttons
- “Edit custom presets” link (opens `presets/samplers.json` on disk)
- LoRA **Upload** still uses `QFileDialog` (user picks file); no “open folder”

**Relocated (not in original Styles tab):**
- Control-layer default preset → Diffusion tab
- Fast/Quality combo → Performance tab
- Custom ComfyUI workflow editor → Diffusion tab

## Gap checklist

| Item | Python | Port action |
|------|--------|-------------|
| Style CRUD | JSON in `user_data/styles/` | `ComfyStyleCollection::create/duplicate/delete`; toolbar +/duplicate/delete |
| Editability | Only built-in read-only | Gate on `ComfyStyleEntry::isBuiltin`, not KConfig index |
| Style combo label | `Name (filename)` | `ComfyStyleCollection::comboDisplayName()` |
| Toolbar chrome | Framed panel, tool buttons | `QFrame` + add/duplicate/delete/refresh |
| Built-in hint | Italic message + copy link | Show only when built-in; link → duplicate |
| Name / checkpoint rows | Setting row (title left, control right) | Reuse `addStylesSettingRow` |
| LoRA list | Per-style JSON | Done; enable edit for user JSON |
| Prompts | `LineEditSetting` + auto-save | Single-line + persist to style JSON |
| Linked edit | Filter `supports_edit`; hide if current arch supports edit | `ComfyResources::supportsEditInstructions` |
| Sampler | Per-style JSON + `SamplerWidget` | `ComfyStyleSamplerWidget` ×2 |
| Checkpoint warnings | Missing ckpt, arch workload, VAE, text encoders | `ComfyUIUtils::styleCheckpointWarnings` (best-effort without full workload API) |
| Auto-save | `style.save()` on change | Remove Save button; persist on widget change |
| KConfig presets | N/A | Remain in dock combo for legacy; Styles tab targets JSON styles |

## Implementation files

- `ComfyStyleCollection.{h,cpp}` — CRUD, combo labels
- `ComfyStyleSamplerWidget.{h,cpp}` — quality + live sampler UI
- `ComfyUIUtils.{h,cpp}` — visible sampler names, checkpoint warnings
- `ComfyUIRemoteDock.{h,cpp}` — JSON style create/duplicate/delete, sampler from style
- `ComfyUIRemoteDockSettings.cpp` — Styles tab layout + wiring
- `ComfyUIRemoteDockPresets.cpp` — preset change applies style JSON sampler fields

## Deferred / partial

- Server **workload installed** check (needs `client.supports_arch` equivalent from ETN)
- Text-encoder missing warnings (needs resource map from server)
- Full `filter_supported_styles` (needs client capability flags); linked-edit uses `supports_edit` on arch only

## Test plan

1. Built-in style: controls disabled; “edit a copy” creates user JSON duplicate
2. User JSON style: edit name, checkpoint, loras, prompts, samplers → survives restart
3. Switch styles: sampler widgets load correct per-style preset/steps/cfg
4. Linked edit hidden for Flux Kontext-style arch; dropdown excludes current + non-edit styles
5. Duplicate / delete user style updates combo

## Implementation status (2025-06-25)

| Item | Status |
|------|--------|
| Style CRUD (create / duplicate / delete) | Done |
| Built-in read-only + copy link | Done |
| Combo label `Name (styleId)` | Done |
| Toolbar (frame, +/duplicate/delete/refresh) | Done |
| Name / checkpoint setting rows | Done |
| LoRA list per-style JSON | Done |
| One-line prompts + auto-save | Done |
| Linked edit filter + hide | Done |
| `ComfyStyleSamplerWidget` ×2 | Done |
| Checkpoint warnings (best-effort) | Done |
| Custom workflow → Diffusion tab | Done |
| Animation quality → Performance tab | Done |
| Generation uses per-style sampler | Done |
| Build (`kritacomfyuiremote_static`) | Passes |

**Still deferred:** server workload arch check, text-encoder warnings, LoRA upload max-size, filesystem actions (open folder, edit presets link).
