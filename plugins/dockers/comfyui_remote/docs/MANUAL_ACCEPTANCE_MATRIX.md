# Manual acceptance matrix — ComfyUI Remote

**Target:** `plugins/dockers/comfyui_remote` (native C++, desktop + Android APK)  
**Tracking:** `port_progress.json` → `acceptance_manual[]`  
**Android deviations:** [ANDROID_DEVIATIONS.md](ANDROID_DEVIATIONS.md)  
**Updated:** 2026-05-25

Use this matrix before release or after large port changes. Record results in `port_progress.json` (`manual_status`, `tested_on`, `notes` per scenario).

---

## How to run

| Column | Meaning |
|--------|---------|
| **Desktop** | Krita desktop build with `comfyui_remote` docker |
| **Android** | Krita Android APK with bundled `data/`; ComfyUI runs **externally** (LAN URL — no managed installer) |

**Shared setup**

1. ComfyUI server reachable (default `http://127.0.0.1:8188` desktop; LAN IP on Android).  
2. ETN / AI Diffusion custom nodes installed on the ComfyUI server.
3. At least one SDXL checkpoint + VAE on server for **M2**; inpaint model for **M3** if testing fill/inpaint modes.  
4. New empty `.kra` or test document; reset dock settings if needed.

**Status values** (set in JSON): `pending` | `pass` | `fail` | `blocked` | `not_applicable`

---

## Summary matrix

| ID | Scenario | Desktop | Android | Impl ready | Blockers / notes |
|----|----------|---------|---------|------------|------------------|
| M1 | Connect custom ComfyUI | Test | Test | Yes | — |
| M2 | Generate SDXL | Test | Test | Yes | Needs SDXL ckpt on server |
| M3 | Inpaint modes | Test | Test | Yes | Touch + selection UX differs on Android |
| M4 | Regions + control | Test | Test | Yes | Per-region control generate: verify on device |
| M5 | Live record | Test | Test | Yes | LoRA pre-upload + WS optional |
| M6 | Upscale + refine | Test | Test | Yes | Large images: memory on Android |
| M7 | Animation batch | Test | Test | Yes | Frame files beside `.kra` |
| M8 | Custom workflow | Partial test | Partial | Partial | JSON/API path exists |
| M9 | History + ui.json | Test | Test | Yes | `.kra` open/save round-trip |
| M10 | Settings roundtrip | Test | Test | Yes | Restart after language change |

---

## Scenarios (detailed)

### M1 — Connect custom ComfyUI

**Workstreams:** P1.2 connection path, P4.5 FileLibrary, P5.2 HTTP (automated subset)

**Steps**

1. Settings → Connection: enter server URL → Connect.  
2. Expect: status **Connected**, device line from `system_stats`, detected models / missing-node hint from `object_info`.  
3. Refresh checkpoints / samplers; Styles tab shows server LoRAs (warning if local enabled LoRA missing).  
4. Disconnect / invalid URL → clear error state, no stale checkpoint list.

**Pass**

- Server URL connects within normal network limits.
- Checkpoint list non-empty when server has models; ETN `model_info` filter does not empty list incorrectly.

**Android**

- Use `http://<host>:8188` (not `127.0.0.1` unless ComfyUI on device).  
- Bundled `data/styles`, presets load without manual copy.

---

### M2 — Generate SDXL

**Workstreams:** P1.2, P4.3 styles, P4.6 LoRA upload

**Steps**

1. Style preset with SDXL architecture (e.g. digital-artwork-xl).  
2. Generate workspace: prompt, 1024² or style resolution, Generate.  
3. Result layer appears; history entry created.  
4. Optional: enable library LoRA not on server → generate triggers PUT then prompt.

**Pass**

- Image generation completes with expected checkpoint, seed, and step settings.
- No silent failure; queue/history shows prompt id.

**Android**

- Same; watch upload timeout on slow LAN.

---

### M3 — Inpaint modes

**Workstreams:** P1.5, P4.1

**Steps**

1. Selection or mask; open Inpaint workspace.  
2. Exercise modes: replace, fill, seamless, focus, context layer (as exposed in UI).  
3. Generate → inpaint workflow; result on correct layer.  
4. Save document → reload → inpaint settings restored from `ui.json` / annotations.

**Pass**

- Each mode produces distinct, sensible mask/fill behavior for the same inputs.
- Labels match (Seamless / Focus, etc.).

**Android**

- Touch selection; smaller screen — verify mask upload path.

---

### M4 — Regions + control

**Workstreams:** P1.3, P1.4, P2.1, C-control-layers

**Steps**

1. Enable regional prompt; add 2+ regions with different positives.  
2. Add control layer (depth/canny/pose); set strength; generate region or full image.  
3. Control preprocessor generate (if used) uploads control image.  
4. Save → reload → regions + `control[]` in document UI state.

**Pass**

- Regional conditioning affects output; control layer visible in workflow/history.  
- Pose mode: OpenPose → vector layer path (desktop); on Android verify layer creation.

---

### M5 — Live record

**Workstreams:** P1.x live, P4.6 LoRA upload

**Steps**

1. Live workspace; connect; set strength / sampler.  
2. Record short session; frames update canvas or history per design.  
3. Stop; document `live` strength persists in `ui.json`.

**Pass**

- Live prompts submit without error; strength round-trip on save.

**Android**

- Performance and LAN latency acceptable; optional WebSocket path if built with `COMFYUI_HAVE_QT_WEBSOCKETS`.

---

### M6 — Upscale + refine

**Workstreams:** P1.7 upscale paths

**Steps**

1. Upscale workspace: factor upscale; optional refine (denoise, tile if large).  
2. Output layer / history entry.  
3. Compare tile overlap behavior on image &gt; 2k edge.

**Pass**

- Upscaled dimensions match factor; refine optional path completes.

**Android**

- OOM risk on very large canvases — note device limit in `notes`.

---

### M7 — Animation batch

**Workstreams:** P1.6 animation

**Steps**

1. Animation workspace: target layer, batch mode, single frame or small batch.  
2. Generate frame(s); files under `{doc}.animation/` if applicable.  
3. `animation` key in `ui.json` round-trip.

**Pass**

- Frame index and target layer persist; batch completes.

---

### M8 — Custom workflow

**Workstreams:** P3.3 skipped (partial UI), custom workflow utils

**Steps**

1. Load custom workflow JSON (file or embedded).  
2. If UI workflow: connect first (object_info required for conversion).  
3. Run generate with custom graph; error message if nodes missing.

**Pass**

- API-format workflow runs when nodes present.  
- Clear error when conversion impossible — not a silent stub.

**Known gap**

- Full graph editor / WebSocket publish is not required for **pass** here; document **partial** in matrix.

**Android**

- Same JSON path.

---

### M9 — History + ui.json

**Workstreams:** P2.3 history, document annotations

**Steps**

1. Run 2+ generations; open History list; apply slot to canvas.  
2. Save `.kra`; close Krita; reopen.  
3. History slots, preview layer, regions, upscale block still coherent.

**Pass**

- Reload shows same slot count and prompts.

---

### M10 — Settings roundtrip

**Workstreams:** P0.x settings, P4.4 localization

**Steps**

1. Change server URL, performance preset, language, sampler defaults, control defaults.  
2. Restart Krita.  
3. Values restored from `settings.json` under user data dir.

**Pass**

- No loss of persisted keys.
- Language change shows restart notice; after restart, UI strings from selected `data/language/*.json`.

**Android**

- User data on app storage; bundled languages in APK `data/language/`.

---

## Android platform checklist

| Topic | C++ Android |
|-------|-------------|
| Runtime | Native Qt only |
| Plugin `data/` | APK + `pluginInstallDataDir()` / first-run copy |
| ComfyUI install | External URL only |
| Cloud | Not exposed |
| WebSockets | Build flag `COMFYUI_HAVE_QT_WEBSOCKETS` |
| KArchive (plugin ZIP) | Enable in Android build |
| LoRA upload | HTTP PUT ETN path |
| Document contract | `ui.json` v1 keys |

---

## Recording results

Edit `port_progress.json` for each scenario:

```json
"manual_status": "pass",
"tested_on": { "cpp_desktop": "2026-05-24", "cpp_android": null },
"notes": "M2: seed 42, dreamshaper XL."
```

When all applicable rows are `pass` or `not_applicable`, P5.3 manual campaign can be marked done in release notes (implementation of the matrix is P5.3; executing every row is ongoing QA).
