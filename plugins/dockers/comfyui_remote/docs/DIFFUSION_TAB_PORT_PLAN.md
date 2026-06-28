# Diffusion tab port plan (krita-ai-diffusion `settings.py`)

Reference: [ai_diffusion/ui/settings.py `DiffusionSettings`](https://github.com/Acly/krita-ai-diffusion/blob/main/ai_diffusion/ui/settings.py#L669-L697)

## Scope

Configure → **Diffusion** matches Python `DiffusionSettings` only (five settings + stretch).

**Out of scope (removed from Diffusion tab):**
- Advanced mask preprocess UI (`selection_min_transition`, `selection_grow_offset`, `selection_invert`, `selection_square`) — not in upstream tab; `selection_min_transition` / `selection_grow_offset` remain in `settings.json` with code defaults; **`selection_invert` / `selection_square` readers removed** (invert only via `replace_background` @ 100% in `getSelectionModifiers`)
- Custom ComfyUI workflow editor — Graph workspace only (upstream)

**Filesystem:** none on this tab.

## Gap checklist

| Item | Python | Port action |
|------|--------|-------------|
| Tab scroll | `SettingsTab` → `QScrollArea` | Wrap body in scroll area |
| Title | `SettingsTab` title (+2pt) | Larger bold heading |
| Selection feather | `SliderSetting` 0–25, `%` | Title+desc left, slider 200–300px right |
| Selection blend | `SliderSetting` 0–100, `px` | Same row pattern |
| Selection padding | `SliderSetting` 0–25, `%` | Same row pattern |
| Color match | `SwitchSetting` On/Off | `ComfySwitchWidget` + state label |
| NSFW filter | `ComboBoxSetting` Disabled/Basic/Strict | Title+desc left, combo ≥230px right |
| NSFW warning | On first write when value > 0; skip if already enabled | Set session flag if `nsfw_filter > 0` on load |
| Advanced group | Not present | Remove from tab |
| Custom workflow group | Not present | Remove; widgets live under Graph workspace |
| Auto-save | `SettingsTab.write` on change | Keep per-widget `saveSettingsJson` |

## Implementation files

- `ComfyUIRemoteDockSettings.cpp` — Diffusion tab layout + wiring
- `ComfyUIRemoteDock.cpp` — Graph workspace hosts workflow params; simplify `reparentCustomWorkflowEditor`
- `ComfyUIRemoteDockPrivate.h` — drop `customWorkflowSettingsLayout` if unused

## Status

**Implemented** (2025-06-25): Diffusion tab matches Python `DiffusionSettings` — scroll area, five settings with Slider/Switch/Combo row layout, advanced mask + custom workflow removed from tab (Graph workspace only).

## Test plan

1. Diffusion tab: five controls only, descriptions visible left of sliders
2. Sliders not full-width; value label beside slider on the right
3. Color match shows On/Off + switch
4. NSFW warning once when enabling; no popup if already Basic/Strict on load
5. Graph workspace still shows custom workflow editor + ETN params
6. Inpaint/generate still read `selection_*` / `color_match` / `nsfw_filter` from settings
