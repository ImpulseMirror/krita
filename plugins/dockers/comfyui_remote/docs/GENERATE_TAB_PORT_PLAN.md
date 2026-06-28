# Generate Tab Port Plan

Reference: `ai_diffusion/ui/generation.py` `GenerationWidget`, dock shell `diffusion.py` L275–337.

## Target layout (Generate workspace)

1. Workspace + Style + Settings gear (done)
2. Region prompt widget (root prompts via `setRootPromptEditors`)
3. Strength row: slider + spin + layer count + Add Control + Add Region icons
4. Custom inpaint bar (Seamless, Focus, Fill, Context) — visible when mode is Custom
5. Action row: `[Generate CTA | ▼ | region mask]` + Queue button
6. Progress bar
7. History (flat, no group title)

## Gaps closed

| Upstream | Was | Fix |
|----------|-----|-----|
| Dynamic Generate CTA | Static "Generate" | `updateGenerateOptions()` |
| Inpaint mode ▼ | Hidden combos + Actions row | `btnInpaintMode` + context menus — see [GENERATE_BUTTON_DROPDOWN_PORT_PLAN.md](GENERATE_BUTTON_DROPDOWN_PORT_PLAN.md) |
| Region mask toggle | Hidden checkbox | `btnRegionMask` ↔ `checkRegionOnly` |
| Generate + Queue same row | Separate rows | `generateActionRowWidget` |
| Strength slider + icons | Label + spin only | `sliderStrength` + icon buttons |
| Custom inpaint bar | Vertical hidden combos | `customInpaintRowWidget` |
| Region prompt position | Bottom "Regions" group | Reparent into `genContentContainer` |
| Region chrome | Add/Remove/Up/Down buttons | Hidden; icons in strength row |
| "Prompt:" label | Duplicate header | Hidden |

## Files

- `ComfyUIRemoteDock.cpp` — layout construction
- `ComfyUIRemoteDockGenerate.cpp` — `updateGenerateOptions`, menus
- `ComfyUIRemoteDockPrivate.h` — widget pointers
- `ComfyUIRemoteDock.h` — method declarations
