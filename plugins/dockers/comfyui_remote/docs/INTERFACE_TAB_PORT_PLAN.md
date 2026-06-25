# Interface tab port plan (krita-ai-diffusion `settings.py`)

Reference: [ai_diffusion/ui/settings.py `InterfaceSettings`](https://github.com/Acly/krita-ai-diffusion/blob/main/ai_diffusion/ui/settings.py#L699-L793)

## Scope

Configure → **Interface** matches Python `InterfaceSettings` widget list and order.

**Out of scope (not in upstream Interface tab UI):**
- `prompt_line_count_live`, `prompt_resize_handle`, `negative_prompt_line_count` — remain in `settings.json` / dock code defaults
- `tag_directory` + Browse folder — upstream uses `user_data_dir/tags` only
- `save_image_quality_jpeg` / `save_image_quality_webp` — format enum defaults in code
- `save_image_file_name_format` — setting exists in Python `Settings` but not this tab
- `confirm_discard_image` — KConfig `ConfirmDiscardImage` only (no upstream tab control)

**Filesystem:** no Browse on this tab; **Open tag folder** opens `tagsStorageDir()` only.

## Gap checklist

| Item | Python | Port action |
|------|--------|-------------|
| Tab scroll | `SettingsTab` → `QScrollArea` | Wrap body in scroll area |
| Title | +2pt bold | Match Diffusion/Styles |
| Setting rows | `SettingWidget` title+desc, control right | Bold title + desc on separate lines |
| Language | `ComboBoxSetting` | Row layout; restart hint on change |
| Prompt translation | Server languages when connected; disabled when not | `refreshInterfacePromptTranslationCombo()` on connect/disconnect |
| Prompt line count | Spin 1–10 | Row layout |
| Negative prompt | `SwitchSetting` Show/Hide | `ComfySwitchWidget` |
| Show steps | `SwitchSetting` On/Off | `ComfySwitchWidget` |
| Recent styles | Spin 0–10 | **Add** `recent_styles_count` |
| Tag files | `FileListSetting` (bundled CSV stems) | Four fixed checkboxes: Danbooru, Danbooru NSFW, e621, e621 NSFW |
| Tag actions | Reload + open user tags folder | Remove browse; open `tagsStorageDir()` |
| Finished generation | Combo | Row layout |
| Apply behavior | Combo | Row layout |
| Apply region | Combo, `show_label=False` | Combo row without left title column |
| Apply behavior (Live) | Combo | Row layout |
| Apply region (Live) | Combo, `show_label=False` | Combo row without left title |
| New seed after apply | `SwitchSetting` | `ComfySwitchWidget` |
| Save format | Combo | Row layout |
| Save metadata | `SwitchSetting`; enabled only for PNG | Enable when `png` / `png_small` |
| Dump workflow | `debug_dump_workflow` | Switch; read/write `debug_dump_workflow` (legacy `dump_workflow`) |

## Removed from tab (was extra in port)

- Live prompt line count, resize handle, negative line count spinboxes
- Tag CSV folder path + Browse
- Hard-coded Danbooru/e621 checkboxes only
- JPEG/WebP quality spinboxes
- Save filename template
- Confirm discard checkbox (KConfig only)

## Implementation files

- `ComfyUIRemoteDockSettings.cpp` — Interface tab layout + wiring
- `ComfyUIRemoteDockConnection.cpp` — refresh prompt translation combo on connect/disconnect
- `ComfyUIRemoteDock.{h,cpp}` — `refreshInterfacePromptTranslationCombo()`
- `ComfyUIRemoteDockPrivate.h` — `settingsPromptTranslationCombo` pointer
- `ComfyUIUtils.{h,cpp}` — `discoverTagFileStems()`, `debug_dump_workflow` read alias

## Status

**Implemented** (2025-06-25): Interface tab matches Python `InterfaceSettings` — scroll area, setting rows, dynamic tag files, recent styles count, switches, region combos without labels, prompt translation gated on connection.

## Test plan

1. Interface tab scrolls; rows match Python order
2. Not connected → prompt translation disabled, only Disabled
3. Connected → prompt translation enabled with language choices
4. Tag reload discovers new CSVs in user tags folder
5. Open tag folder opens user tags dir (no file picker)
6. Save metadata disabled for WebP/JPEG; enabled for PNG variants
7. `recent_styles_count` persists to settings.json
8. Dock still reads `prompt_line_count_live` / resize handle from settings when not on tab
