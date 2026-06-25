# Performance tab port plan

Reference: [Python `PerformanceSettings` L826–937](https://github.com/Acly/krita-ai-diffusion/blob/main/ai_diffusion/ui/settings.py#L833-L944)

## Gaps (before)

| Issue | Python | Ours |
|-------|--------|------|
| Layout | Scroll + stacked rows | `QFormLayout`, no scroll |
| Animation quality | Not in tab | Extra `comboQuality` row |
| Diffusion scale / upscale tile / max MP auto | Not in tab | Extra rows |
| Cloud preset | In combo | Missing |
| Advanced block | Visible, **disabled** unless Custom | **Hidden** unless Custom |
| Dynamic caching / multi-threading | Top-level switches | Inside advanced block |
| Tiled VAE | `SwitchSetting` Always/Automatic | Radio buttons |
| Settings keys | `history_size`, `history_storage`, `max_pixel_count`, `tiled_vae` | Legacy `*_mb` only in UI save |

## Target widget order

1. Active History Size (spin + usage MB)
2. Stored History Size (spin + usage MB)
3. Performance Preset (desc, device italic label, combo incl. **Cloud**)
4. Advanced (enabled when Custom): batch, resolution, max MP, tiled VAE
5. Dynamic Caching (switch)
6. Multi-Threading (switch)

## Removed from tab (keys kept in `settings.json` with defaults)

- `comboQuality` (dock only)
- `diffusion_scale_mode`, `upscale_tile_estimate_extent`, `max_pixel_auto`

## Save compat

Write Python keys plus legacy: `history_size`/`history_active_mb`, `history_storage`/`history_document_storage_mb`, `max_pixel_count`/`max_pixel_count_mp`, `tiled_vae` + `tiled_vae_mode`.
