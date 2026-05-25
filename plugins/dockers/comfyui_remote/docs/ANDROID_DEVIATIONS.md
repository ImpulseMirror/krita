# Android deviations — ComfyUI Remote (native C++)

**Reference:** Python [krita-ai-diffusion](https://github.com/Acly/krita-ai-diffusion) v1.49.0 (desktop)  
**Target:** `plugins/dockers/comfyui_remote` on **Krita Android**  
**Related:** [MANUAL_ACCEPTANCE_MATRIX.md](MANUAL_ACCEPTANCE_MATRIX.md), `port_progress.json` (P3.x skipped, M11 N/A)  
**Updated:** 2026-05-25 (external-only connection UI)

This document lists **intentional** differences between the Python desktop plugin and the native C++ plugin on Android. Items here are **not bugs** unless marked *known issue*. Anything not listed is expected to match Python behavior when using the same external ComfyUI server and models.

---

## 1. Summary

| Area | Python desktop | C++ Android |
|------|----------------|-------------|
| Runtime | Embedded Python | Qt/C++ only — **no Python** |
| ComfyUI server | Cloud, managed local, or custom URL | **Custom URL only** (user-run ComfyUI on PC/NAS/device) |
| Cloud account / billing | Yes | **Not available** (not exposed in Settings) |
| Managed ComfyUI install | `server.py` lifecycle on desktop | **Not available** — external setup required |
| Graph workflow editor | Full Custom Workflow UI | **Partial** — JSON/API path; full graph page deferred (P3.3) |
| Core generation | Generate, inpaint, live, upscale, animation, regions, control | **Supported** (same HTTP/ETN contract) |
| Document contract | `ui.json` v1, annotations | **Same** |
| Plugin assets | Installed with plugin | **Bundled in APK** (`data/`) + optional copy to user dir |

---

## 2. Intentional product deviations (P3 — skipped workstreams)

These are tracked in `port_progress.json` as `skipped` with documented `skip_reason`. The UI must **not** expose dead-end controls: Settings → Connection only shows the supported custom ComfyUI URL flow.

### 2.1 Cloud / Online Service (P3.1)

| | |
|--|--|
| **Python** | OAuth sign-in, cloud API, cloud performance preset, hosted ComfyUI. |
| **Android / C++** | No `ComfyCloudClient`. The **Online Service** connection mode and cloud performance preset are not shown. |
| **Manual test** | M11 — `not_applicable` on Android. |
| **Workaround** | Run ComfyUI elsewhere; set **Custom ComfyUI** URL (LAN). |

### 2.2 Managed local server (P3.2)

| | |
|--|--|
| **Python** | Download/install/start ComfyUI + custom nodes from Krita. |
| **Android / C++** | No managed installer or process supervisor. The **Local Managed Server** connection mode is not shown. |
| **Android expectation** | User installs ComfyUI + ETN nodes on a **reachable host** (PC, Mac, Linux box, cloud VM). Tablet/phone uses `http://<host-ip>:8188`. |
| **Docs link** | Settings exposes [Custom ComfyUI Setup](https://docs.interstice.cloud) (same as Python external path). |
| **Future** | Optional guided checklist wizard (nodes, URL test) — **not** a silent stub; tracked separately if product revives P3.2 for Android only. |

### 2.3 Graph workspace UI (P3.3)

| | |
|--|--|
| **Python** | Visual graph editor, `WorkflowCollection`, parameter widgets. |
| **Android / C++** | Custom workflow via **JSON file** / document-embedded workflow + `convertComfyUiWorkflowUiToApi` when `object_info` is loaded. No full node-graph page parity. |
| **Manual test** | M8 — `cpp_ready: partial`. |

### 2.4 WebSocket workflow publish (P3.4)

| | |
|--|--|
| **Python** | `workflow_published` over `/ws?clientId=…` for live graph sync. |
| **Android / C++** | Compiled only if `COMFYUI_HAVE_QT_WEBSOCKETS` (CMake links Qt WebSockets). Code paths in `ComfyUIRemoteDockWebWorkflow.cpp` / control generate are `#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)`. **Android builds should enable WebSockets** per plan §6, but publish parity still depends on P3.3 graph UI. |
| **Impact** | Live/control flows that rely on WS publish may differ from Python until graph workstream completes. |

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
| `pluginUserDataDir()` | Under Qt `AppDataLocation` → `…/ai_diffusion` when path contains `krita` (see `ComfyUIUtils.cpp` §13.66). |
| User styles / LoRAs / workflows | Same layout as desktop C++ (`styles/`, `database/loras.json`, `workflows/`). |

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

## 4. Parity retained on Android (no deviation)

The following match Python when using **custom ComfyUI** + same models/nodes:

- Workspaces: Generate, Inpaint, Live, Upscale, Animation, Regions, Control layers  
- `settings.json` persistence and §3.1 settings keys  
- Document `ui.json` / annotation parity (history, regions, inpaint, upscale, custom workflow blob)  
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

## 7. Migration guide (Python desktop → C++ Android)

1. Export or note ComfyUI URL, checkpoints, and custom nodes on the host machine.  
2. Install native **ComfyUI Remote** docker on Krita Android (not the Python plugin).  
3. Settings → Connection → **Custom ComfyUI** → enter LAN URL → Connect.  
4. Open an existing `.kra` created with Python: verify history/regions (M9).  
5. Use [interstice.cloud docs](https://docs.interstice.cloud) for node install on the **host** ComfyUI.
6. For custom workflows, prefer API-format JSON until graph UI (P3.3) ships.

---

## 8. References

| Resource | Location |
|----------|----------|
| Perfect port plan | `comfyui-perfect-port.plan.md` §6–§7 |
| Manual matrix | `MANUAL_ACCEPTANCE_MATRIX.md` |
| Progress / skips | `port_progress.json` → P3.1–P3.4, P5.3 `acceptance_manual` |
| Build flags | `plugins/dockers/comfyui_remote/CMakeLists.txt` |

*P5.4 — intentional gaps remain tracked as workstreams; unsupported connection modes are hidden from Settings.*
