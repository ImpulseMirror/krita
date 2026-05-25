# Android notes — ComfyUI Remote (native C++)

**Target:** `plugins/dockers/comfyui_remote` on **Krita Android**  
**Related:** [MANUAL_ACCEPTANCE_MATRIX.md](MANUAL_ACCEPTANCE_MATRIX.md)
**Updated:** 2026-05-25

This document lists Android-specific behavior for the native ComfyUI Remote docker.

---

## 1. Summary

| Area | C++ Android |
|------|-------------|
| Runtime | Qt/C++ only |
| ComfyUI server | **Custom URL only** (user-run ComfyUI on PC/NAS/device) |
| Cloud account / billing | **Not available** (not exposed in Settings) |
| Managed ComfyUI install | **Not available** — external setup required |
| Graph workflow editor | JSON/API workflow path |
| Core generation | Generate, inpaint, live, upscale, animation, regions, control |
| Document contract | `ui.json` v1, annotations |
| Plugin assets | **Bundled in APK** (`data/`) + optional copy to user dir |

---

## 2. Unavailable connection modes

Settings → Connection only shows the supported custom ComfyUI URL flow.

### 2.1 Cloud / Online Service (P3.1)

| | |
|--|--|
| **Android / C++** | The **Online Service** connection mode and cloud performance preset are not shown. |
| **Workaround** | Run ComfyUI elsewhere; set the server URL in Settings → Connection. |

### 2.2 Managed local server (P3.2)

| | |
|--|--|
| **Android / C++** | No managed installer or process supervisor. The **Local Managed Server** connection mode is not shown. |
| **Android expectation** | User installs ComfyUI + ETN nodes on a **reachable host** (PC, Mac, Linux box, cloud VM). Tablet/phone uses `http://<host-ip>:8188`. |
| **Future** | Optional guided checklist wizard (nodes, URL test). |

### 2.3 Graph workspace UI

| | |
|--|--|
| **Android / C++** | Custom workflow via **JSON file** / document-embedded workflow + `convertComfyUiWorkflowUiToApi` when `object_info` is loaded. |

### 2.4 WebSocket workflow publish

| | |
|--|--|
| **Android / C++** | Compiled only if `COMFYUI_HAVE_QT_WEBSOCKETS` (CMake links Qt WebSockets). Code paths in `ComfyUIRemoteDockWebWorkflow.cpp` / control generate are `#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)`. |
| **Impact** | Live/control flows that rely on WS publish require Qt WebSockets in the Android build. |

---

## 3. Android platform / environment

### 3.1 ComfyUI connectivity

- **`127.0.0.1` / `localhost`** only works if ComfyUI runs **on the same device** (unusual on phones/tablets).
- Typical setup: ComfyUI on desktop (`192.168.x.x:8188`), Android on same Wi‑Fi, URL `http://192.168.x.x:8188`.
- Firewall, HTTPS reverse proxies, and certificate errors behave like desktop; plugin sends `ngrok-skip-browser-warning` header where applicable (`setComfyUIRequestHeaders`).

### 3.2 Plugin data and storage

| Path | Behavior |
|------|----------|
| `pluginInstallDataDir()` | Resolves `…/data` next to plugin `.so` in APK layout (or dev `COMFYUI_PLUGIN_SOURCE_DATA_DIR`). |
| `ensureBundledPluginDataInstalled()` | First use: copies missing presets/tags from install dir → `pluginUserDataDir()`. |
| `pluginUserDataDir()` | Under Qt `AppDataLocation` / `GenericDataLocation` → `…/comfyui_remote` (see `ComfyUIUtils.cpp` §13.66). |
| User styles / LoRAs / workflows | Stored under `styles/`, `database/loras.json`, `workflows/`. |

**CMake:** `install(DIRECTORY data/ …)` — Android packaging must include `data/` in the plugin install tree (P0.1).

### 3.3 Build-time optional features

| Flag | Purpose | Android recommendation |
|------|---------|------------------------|
| `COMFYUI_HAVE_QT_WEBSOCKETS` | ETN WebSocket client | **Enable** in Android Krita build |
| `COMFYUI_HAVE_KARCHIVE` | Verified plugin-update ZIP extract (§13.37) | **Enable** if KF Archive available on Android |
| `COMFYUI_PLUGIN_SOURCE_DATA_DIR` | Dev/tests only | Not used in release APK |

Without KArchive: update-ZIP install path is unavailable at runtime (compile-time gated).

### 3.4 Performance and UX (environmental, not alternate code paths)

| Topic | Note |
|-------|------|
| **Memory** | Large upscale/refine tiles may OOM on low-RAM devices; reduce resolution or disable refine. |
| **LAN latency** | Live mode and LoRA pre-upload (P4.6) sensitive to network; timeouts surface as connection errors. |
| **Input** | Selection, mask, and dock layouts optimized for desktop; touch targets depend on Krita Android shell. |
| **Performance preset "Cloud"** | Hidden from Settings. Existing settings JSON values remain tolerated for compatibility. |

---

## 4. Supported on Android

The following are supported when using **custom ComfyUI** + required models/nodes:

- Workspaces: Generate, Inpaint, Live, Upscale, Animation, Regions, Control layers  
- `settings.json` persistence and §3.1 settings keys  
- Document `ui.json` / annotations (history, regions, inpaint, upscale, custom workflow blob)
- HTTP: `object_info`, `system_stats`, `/prompt`, `/history`, `/upload/image`, `/models/loras`  
- ETN: `api/etn/model_info/checkpoints`, `api/etn/upload/loras/…`, `api/etn/translate/…`  
- Style collection (14 bundled presets), sampler/control presets, tag CSVs, localization JSON  
- FileLibrary LoRA registry + pre-generate LoRA upload queue  

Automated coverage: `tests/ComfyPortP51Test.cpp`, `ComfyPortP52Test.cpp` (desktop CI; same code shipped on Android).

---

## 5. Settings UI: supported connection flow

Settings exposes only the supported external ComfyUI flow (`ComfyUIRemoteDockSettings.cpp`):

- **Server URL:** enter a running ComfyUI endpoint such as `http://192.168.x.x:8188`.
- **Connect:** tests the external server and populates model/resource status.
- Legacy `cloud`, `managed`, or `undefined` `ServerMode` values are normalized to `external`; **Restore Defaults** also writes `external`.

---

## 6. Known issues / investigation (*optional QA*)

| ID | Symptom | Status |
|----|---------|--------|
| — | *(none filed in port tracker)* | Add rows when M1–M10 Android manual runs find defects |

Use `acceptance_manual[].notes` in `port_progress.json` for scenario-specific Android failures.

---

## 7. Setup guide

1. Export or note ComfyUI URL, checkpoints, and custom nodes on the host machine.  
2. Install native **ComfyUI Remote** docker on Krita Android.
3. Settings → Connection → **Custom ComfyUI** → enter LAN URL → Connect.  
4. For custom workflows, prefer API-format JSON.

---

## 8. References

| Resource | Location |
|----------|----------|
| Manual matrix | `MANUAL_ACCEPTANCE_MATRIX.md` |
| Build flags | `plugins/dockers/comfyui_remote/CMakeLists.txt` |

*Unsupported connection modes are hidden from Settings.*
