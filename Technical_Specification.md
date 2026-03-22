# Technical Specification: Krita AI Diffusion (Generative AI for Krita)

This document describes the functionality, architecture, and UI of the **Krita AI Diffusion** plugin in sufficient detail for an agent or engineer to rebuild the application from scratch. It covers the codebase structure, configuration system, user interface (as shown in the reference screenshots), server connection, styles/workflows, and data models.

---

## 1. Overview and Purpose

**Product name (UI):** "AI Image Generation" (dock title), "Generative AI for Krita" (about section), "Configure Image Diffusion" (settings dialog title).

**Purpose:** A Krita plugin that integrates generative AI (Stable Diffusion, Flux, Illustrious, etc.) into image painting and editing. Users can:

- Generate images from text (and optionally from canvas/selection).
- Upscale and refine images.
- Use live preview and animation generation.
- Run custom ComfyUI graph workflows.
- Connect via a local ComfyUI server, a managed local server, or an online (cloud) service.

**Backend:** ComfyUI is the primary backend. The plugin acts as a client that sends workflows (prompts, checkpoints, LoRAs, control inputs) and receives generated images.

**Technology stack:** Python 3.10+, PyQt5, Krita Python API. No CMake; pure Python plugin installed as a Krita Python plugin (e.g. under a `pykrita` directory). Optional bundled `websockets` for ComfyUI WebSocket communication.

**Project goals (for UX parity):** The repository **README.md** describes user-facing goals: precision and control (selections, strength, regions, control layers); workflow integration with Krita (draw, paint, edit and generate without worrying about resolution); and local/open/free (open source models, own hardware, optional cloud). A C++ rebuild should mirror or reference this messaging in docs and first-run experience.

---

## 2. Project Structure

### 2.1 Directory Layout

```
krita-ai-diffusion/
├── ai_diffusion/                 # Main plugin package
│   ├── __init__.py               # __version__ (e.g. 1.49.0), conditional extension import
│   ├── ai_diffusion.action       # Krita action metadata (optional)
│   ├── extension.py              # Krita extension & dock registration
│   ├── root.py                   # Root app state, document/model lifecycle
│   ├── model.py                  # Per-document model (workspace, style, jobs, etc.)
│   ├── settings.py               # Settings schema, enums, persistence
│   ├── connection.py             # Connection state machine, client selection
│   ├── client.py                 # Abstract Client interface
│   ├── comfy_client.py           # ComfyUI WebSocket/HTTP client
│   ├── cloud_client.py           # Online service client
│   ├── server.py                 # Local managed ComfyUI server (install/start/stop)
│   ├── server_requirements.txt   # Managed server Python deps
│   ├── api.py                    # WorkflowInput, ConditioningInput, InpaintParams, etc.
│   ├── workflow.py               # workflow.create(), apply_strength, etc.
│   ├── comfy_workflow.py         # ComfyUI graph representation
│   ├── style.py                  # Style, StyleSettings, Styles
│   ├── files.py                  # FileLibrary, checkpoints, LoRAs
│   ├── jobs.py                   # Job, JobQueue, JobParams, JobState, JobKind
│   ├── document.py               # Document, KritaDocument, SelectionModifiers
│   ├── image.py                  # Image, Bounds, Extent, Mask, ImageCollection
│   ├── resources.py              # Arch, ControlMode, ResourceKind, model definitions
│   ├── persistence.py            # Document persistence, ModelSync, ui.json
│   ├── util.py                   # read_json_with_comments, loggers, PluginError, etc.
│   ├── eventloop.py              # asyncio drive via QTimer (20 ms)
│   ├── updates.py                # AutoUpdate, UpdateState, update check
│   ├── network.py                # RequestManager, NetworkError (HTTP)
│   ├── localization.py           # translate(), language loading
│   ├── properties.py             # ObservableProperties, Property, serialize/deserialize
│   ├── resolution.py             # compute_bounds, ScaledExtent, apply_resolution_settings
│   ├── layer.py                  # Layer, LayerManager, LayerType
│   ├── pose.py                   # Pose (e.g. for control layers)
│   ├── text.py                   # merge_prompt, extract_loras, create_img_metadata, etc.
│   ├── control.py                # ControlLayer, ControlLayerList (non-UI)
│   ├── region.py                 # Region, RootRegion, get_region_inpaint_mask
│   ├── custom_workflow.py        # CustomWorkspace, WorkflowCollection, CustomWorkflow, WorkflowSource, CustomGenerationMode (non-UI)
│   ├── platform_tools.py         # is_linux, is_macos, etc.
│   ├── win32.py                  # Windows job object / process limits (optional)
│   ├── debugpy/                  # Optional: debugpy for dev (extension.py checks)
│   ├── websockets/               # Bundled websockets package for ComfyUI (importable at websockets/src/)
│   ├── ui/                       # All PyQt5 UI
│   │   ├── __init__.py
│   │   ├── actions.py            # generate(), cancel_*, apply, set_workspace, etc.
│   │   ├── diffusion.py         # ImageDiffusionWidget (dock), WelcomeWidget
│   │   ├── settings.py          # SettingsDialog, all settings tabs
│   │   ├── generation.py        # GenerationWidget, HistoryWidget
│   │   ├── upscale.py           # UpscaleWidget
│   │   ├── live.py              # LiveWidget
│   │   ├── animation.py         # AnimationWidget
│   │   ├── custom_workflow.py   # CustomWorkflowWidget, CustomWorkflowPlaceholder
│   │   ├── style.py             # StylePresets (Styles tab), LoraItem, SamplerWidget
│   │   ├── server.py            # ServerWidget (managed server UI)
│   │   ├── widget.py            # WorkspaceSelectWidget, GenerateButton, TextPromptWidget, etc.
│   │   ├── settings_widgets.py  # SettingsTab, SliderSetting, SwitchSetting, etc.
│   │   ├── theme.py             # icon(), checkpoint_icon(), logo(), dark/light
│   │   ├── autocomplete.py      # TagCompleterDelegate, PromptAutoComplete (tag CSV)
│   │   ├── control.py           # ControlWidget, ControlListWidget
│   │   ├── region.py            # ActiveRegionWidget, RegionPromptWidget, RegionThumbnailWidget
│   │   ├── interval_slider.py   # IntervalSlider
│   │   ├── switch.py            # SwitchWidget (toggle)
│   │   └── ...
│   ├── icons/                    # SVG/PNG: add-pose, apply, cancel, control-*, context-*, etc. (-dark/-light)
│   ├── styles/                   # Built-in style presets (JSON)
│   ├── language/                 # Translation JSON (en, es-ca, es, fr, id, it, ja, ko, pt-br, ru, th, tr, zh-cn, zh-tw, new_language.json.template)
│   ├── tags/                     # Tag autocomplete CSV (Danbooru.csv, Danbooru NSFW.csv, e621.csv, e621 NSFW.csv); README.md documents format and generation
│   └── presets/                  # Samplers, models, control configs
├── ai_diffusion.desktop          # Krita plugin manifest
├── CONTRIBUTING.md               # Contributor guide: issue reporting, translations, pull requests
├── scripts/
│   ├── package.py                # Build release package
│   ├── translation.py            # Translation build
│   ├── server_requirements.in    # Source for managed server deps (→ ai_diffusion/server_requirements.txt)
│   ├── download_models.py        # Model download helper
│   ├── benchmark_report.py       # Benchmark reporting
│   ├── benchmark_html.py        # Benchmark HTML output
│   ├── docker.py                 # Docker helpers (optional)
│   ├── images.py                 # Image helpers (optional)
│   └── file_server.py            # File server (optional)
├── tests/                        # Pytest tests (conftest.py, test data in data/, images/, references/)
├── docs/                         # Documentation (e.g. Astro/MDX)
├── requirements.txt             # Dev/test dependencies (ruff, pytest, PyQt5, etc.)
└── pyproject.toml                # Project metadata and tool config
```

- **Git submodules:** **ai_diffusion/websockets** and **ai_diffusion/debugpy** are Git submodules (see `.gitmodules`). The **websockets** package is required at runtime for ComfyUI WebSocket communication and is included in the release package. The **debugpy** package is optional (developer debugging); **scripts/package.py** excludes **debugpy** from the release ZIP via its ignore list. A C++ rebuild does not use these submodules but should ship or document equivalent assets (e.g. no websockets bundle if using Qt’s QWebSocket).

**Reference project:** The Python codebase uses `requirements.txt` and `pyproject.toml` for development and test dependencies. The `tests/` directory contains pytest-based tests (`conftest.py`, fixtures, and test data in `tests/data/`, `tests/images/`, `tests/references/`); these can be used as a behavioral reference when porting or reimplementing.

### 2.2 High-Level User Flow (First-Run and Typical Use)

For parity of behavior and UX, a C++ rebuild should support the same high-level flow:

1. **First run (no server chosen):** User installs the plugin (e.g. Import Python Plugin from File), restarts Krita, opens or creates a document. The dock shows the **Welcome** view (connection status, Configure button). Clicking **Configure** opens the Configure Image Diffusion dialog; if **server_mode** is **undefined**, the **InitialSetupWidget** is shown with three options: Online Service, Local Managed Server, Custom ComfyUI. User picks one; **server_mode** is set and saved.
2. **Connection:** For Custom Server, user enters URL and clicks Connect. For Managed Server, user may need to install/start the server. For Online Service, user signs in (browser, token). When **Connection.state** becomes **connected**, the Welcome view can be dismissed and the dock switches to the active document’s workspace (Generate, Upscale, etc.).
3. **Generate:** User selects a style, enters a prompt, optionally sets strength/seed/regions/inpaint options, and clicks **Generate**. Jobs are queued and run on the client; progress is shown; on finish, result appears in history and (per settings) preview or apply is offered.
4. **Persistence:** Document state (workspace, style, regions, history, control layers) is stored in document annotations (**ui.json**, **result{N}.webp**). Settings (server mode, URL, language, etc.) are stored in **user_data_dir/settings.json**. On reopen, the same workspace and history are restored.

This flow ensures that first-time setup, reconnection, and document switching behave like the reference implementation.

### 2.3 Plugin Registration

- **Desktop file:** `ai_diffusion.desktop`
  - `Type=Service`
  - `ServiceTypes=Krita/PythonPlugin`
  - `X-KDE-Library=ai_diffusion`
  - `X-Python-2-Compatible=false`
  - `X-Krita-Manual=manual.html`
  - `Name=AI Image Diffusion`
  - `Comment=Workflows supported by generative AI (Stable Diffusion)`

- **Exact desktop file content** (for packaging parity):

```
[Desktop Entry]
Type=Service
ServiceTypes=Krita/PythonPlugin
X-KDE-Library=ai_diffusion
X-Python-2-Compatible=false
X-Krita-Manual=manual.html
Name=AI Image Diffusion
Comment=Workflows supported by generative AI (Stable Diffusion)
```

- **Plugin load mechanism:** Krita discovers the plugin via the desktop file and loads it by **importing the Python package** whose name matches **X-KDE-Library** (here, `ai_diffusion`). The package root **`ai_diffusion/__init__.py`** runs on import; when the `krita` module is available, it exports **AIToolsExtension**. The module-level code at the bottom of **`extension.py`** then runs (when the extension module is imported) and registers the extension and dock factory with **Krita.instance()**. So registration happens at **import time**, not when Krita calls a separate "load" hook. A C++ rebuild will use the host’s plugin API (e.g. a designated init or factory function) instead of import-side registration.

- **Entry points:**
  - Extension: `AIToolsExtension` in `ai_diffusion/extension.py`. Registered with `Krita.instance().addExtension(AIToolsExtension(...))`.
  - Dock: `Krita.instance().addDockWidgetFactory(DockWidgetFactory("imageDiffusion", DockWidgetFactoryBase.DockRight, ImageDiffusionWidget))`. Factory ID `"imageDiffusion"`; default position DockRight (from `DockWidgetFactoryBase`).

- **Bootstrap:** `ai_diffusion/__init__.py` sets `__version__`; if `krita` is importable, exports `AIToolsExtension`. Requires bundled `websockets` (from plugin package at `ai_diffusion/websockets/src/`, not system). On load (extension constructor): `eventloop.setup()`, `settings.load()`, `root.init()`, **SettingsDialog** is constructed (with `root.server`), and `notifier.applicationClosing.connect(shutdown)`. Krita then calls **`setup()`** on the extension, where `eventloop.run(root.autostart(update_ui_callback))` runs; the callback is the settings dialog's Connection tab **update_ui** (refreshes connection status in Configure dialog). **`createActions(window)`** is invoked by Krita to register actions; each action is created with `window.createAction("ai_diffusion_<name>", "", "")` and connected to the corresponding handler.

### 2.4 Supporting Modules (summary)

| Module | Purpose |
|--------|---------|
| `root.py` | **Root** singleton: server, connection, FileLibrary, WorkflowCollection, per-document models, null model, RecentlyUsedSync, AutoUpdate; **model_for_active_document()**, **create_model()**, **prune_models()**, **autostart()**; see §13.88 |
| `document.py` | **Document** (abstract) and **KritaDocument** (implementation): extent, layers, selection_bounds, annotations (annotate/find_annotation/remove_annotation), **create_mask_from_selection(SelectionModifiers)**, **get_image(bounds, exclude_layers)**, **resize**, **resize_canvas**, **check_color_mode**, **add_pose_character**, **import_animation**; **SelectionModifiers** (feather_rel, feather_min_px, pad_rel, pad_offset_px, size_min_px, multiple, square, invert); signals: selection_bounds_changed, current_time_changed; **playback_time_range**, **current_time**. Used by model, persistence, and workflow for canvas/selection and mask creation. |
| `connection.py` | **Connection**: client selection, state, **\_connect(url, mode, access_token)**, **sign_in** (cloud), **disconnect**; error_kind, missing_resources; see §13.89 |
| `util.py` | `read_json_with_comments`, loggers (`client_logger`), `PluginError`, `ensure`, `parse_enum`, `encode_json`, path helpers |
| `eventloop.py` | Dedicated asyncio event loop; QTimer (20 ms) drives `process_python_events()` so async ComfyUI client runs inside Qt |
| `network.py` | HTTP `RequestManager`, `NetworkError` |
| `localization.py` | `translate()` using `language/` JSON; language code from settings |
| `properties.py` | `ObservableProperties`, `Property` descriptor; `serialize`/`deserialize` for QObject state |
| `resolution.py` | `compute_bounds`, `ScaledExtent`, `apply_resolution_settings`, `prepare_diffusion_input` |
| `layer.py` | `Layer`, `LayerManager`, `LayerType`; Krita layer abstraction |
| `pose.py` | `Pose` (e.g. for control layers / OpenPose) |
| `text.py` | Prompt merge, LoRA extraction from text, `create_img_metadata`, wildcards, attention edit |
| `control.py` (root) | `ControlLayer`, `ControlLayerList`; control layer data and mode (ControlNet/IP-Adapter) |
| `region.py` | `Region`, `RootRegion`, `RegionLink`; region list and per-region prompts/control |
| `platform_tools.py` | `is_linux`, `is_macos`; platform checks for server/SSL |
| `win32.py` | Windows-only: job object / process limits (optional) |

### 2.5 README and Project Documentation Structure

The repository **README.md** at the project root is the main user-facing entry. A C++ rebuild should mirror or reference its structure so install and feature messaging stay consistent.

- **Header:** Logo (64px from `ai_diffusion/icons/logo-128.png`), title "Generative AI *for Krita*", and links: Features, Download, Installation (docs), Video, Gallery, User Guide, Discussion, Discord.
- **Purpose:** Short description of the plugin (generative AI in Krita; interstice.cloud intro; docs for install/use).
- **Goals (three bullets):** Precision and Control (selections, strength, regions, control layers); Workflow Integration (draw, paint, edit and generate; resolution-agnostic); Local, Open, Free (open models, own hardware, optional cloud).
- **Features:** Inpainting, Live Painting, Upscaling, Diffusion Models (Flux 2, Z-Image, SD 1.5/XL, Illustrious), Edit Models, ControlNet, IP-Adapter, Regions, Job Queue, History, Strong Defaults, Customization.
- **Getting Started / Installation:** Link to Plugin Installation Guide (docs); concise technical steps: OS (Windows, Linux, macOS); Hardware table (NVIDIA, AMD, Apple Silicon, CPU, XPU); steps 1–5 (Krita 5.2.0+, download plugin, Tools → Scripts → Import Python Plugin from File, restart, Settings → Dockers → AI Image Generation, Configure).
- **Optional: Custom ComfyUI Server:** ComfyUI as backend; manual or existing install; auto-connect if server running; link to required extensions and models.
- **Optional: Object selection (Krita AI Tools):** Separate plugin for segmentation; out of scope for this spec.
- **Contributing:** Link to CONTRIBUTING.md.
- **Gallery:** Media references (e.g. `media/screenshot-*.png`, `media/control-scribble-screen.png`, video previews). These are project/marketing assets; reference screenshots for the spec are in **screenshots/** (§12).
- **Technology:** A short section listing upstream projects: image generation (Stable Diffusion, Flux), diffusion backend (ComfyUI), inpainting (ControlNet, IP-Adapter). A C++ rebuild may mirror this in docs or about text for project credits.

See §13.57 and §13.116 for docs/ (Starlight/Handbook) and installation text to mirror.

### 2.6 Test Suite Overview

The **tests/** directory contains pytest-based tests that serve as the primary behavioral reference when porting or reimplementing. The following test modules exist and cover the indicated areas:

| Test module | Scope (summary) |
|-------------|------------------|
| **test_api.py** | WorkflowInput, ConditioningInput, InpaintParams, serialization/deserialization, cost, ExtentInput/ImageInput, enums |
| **test_client.py** | ComfyClient/CloudClient interface, connection, queue submission, WebSocket handling, ClientMessage/ClientEvent |
| **test_comfy_workflow.py** | ComfyUI graph structure, node IDs, links, workflow dict format |
| **test_custom_workflow.py** | Custom workflows, WorkflowCollection, params, ETN nodes, UI workflow format conversion |
| **test_files.py** | FileLibrary, checkpoints, LoRAs, file discovery and paths |
| **test_image.py** | Image, Bounds, Extent, Mask, ImageCollection, geometry, encoding/decoding |
| **test_image_transfer.py** | Cloud image transfer (service.pod.lib); optional, excluded from pyright; see §13.130 |
| **test_platform.py** | Platform detection (is_linux, is_macos, etc.) |
| **test_pose.py** | Pose, pose layers, vector layer parsing for control |
| **test_properties.py** | ObservableProperties, Property, serialize/deserialize |
| **test_resolution.py** | Resolution, compute_bounds, ScaledExtent, apply_resolution_settings, tiling |
| **test_resources.py** | ResourceKind, ResourceId, control/model resource resolution |
| **test_server.py** | Managed server (Server), install/start/stop, ServerState, packages |
| **test_service.py** | Cloud service integration, auth, API surface |
| **test_settings.py** | Settings schema, load/save, enums, defaults |
| **test_text.py** | merge_prompt, extract_loras, create_img_metadata, wildcards, attention |
| **test_updates.py** | AutoUpdate, UpdateState, update check flow |
| **test_util.py** | read_json_with_comments, path helpers, PluginError, ensure, parse_enum |
| **test_workflow.py** | workflow.create(), generate/inpaint/refine/upscale/control_image, create_control_image, workflow build for each WorkflowKind; uses tests/data/, tests/images/, tests/references/ |

**Fixtures and config:** **conftest.py** provides QtTestApp, clear_appdata, clear_results; **config.py** defines result_dir, data_dir, image_dir, reference_dir, default_checkpoint. **tests/data/** holds workflow JSON and object_info; **tests/images/** and **tests/references/** hold input masks and expected output images for regression. A C++ rebuild should use these same inputs and reference outputs where applicable to verify parity. For which tests compare against which reference images (e.g. control-image regression), see **§13.164 Test-to-reference image mapping**.

### 2.7 Development and build environment

- **Python:** **requires-python = ">=3.10"** (pyproject.toml). The plugin runs inside Krita’s embedded Python; at runtime it only requires the standard library, Qt5 (PyQt5), and the **bundled websockets** package (no pip install for end users).
- **Version:** **__version__** is defined in `ai_diffusion/__init__.py` (e.g. `"1.49.0"`). Plugin load fails with a clear **ImportError** if the websockets bundle is missing, directing the user to download a full release package.
- **Dev dependencies (requirements.txt):** Used for development and CI only: ruff, pyright, pytest, pytest-asyncio, PyQt5, Pillow, aiohttp, etc. Not shipped with the plugin.
- **Linting and type checking:** **ruff format**, **ruff check**; **pyright** with exclusions for websockets, debugpy, and tests as configured in pyproject.toml. **scripts/typeshed/krita.pyi** provides stubs for the Krita Python API when running outside Krita.
- **Development install (CONTRIBUTING.md):** Clone repo, `git submodule update --init`, then in Krita’s pykrita folder create a symlink to the `ai_diffusion` folder and a symlink to `ai_diffusion.desktop`. This allows editing the plugin in place without repackaging.
- **C++ relevance:** A C++ rebuild does not depend on this Python toolchain at runtime but should use **tests/** and **tests/data/** as the behavioral and data reference; matching test inputs and expected outputs helps ensure parity.

### 2.8 CI/CD (GitHub Actions)

The repository uses **GitHub Actions** (`.github/workflows/test.yml`) for continuous integration. This defines what the reference project considers a passing build and can inform test and release expectations for a C++ rebuild.

- **Trigger:** On every push and pull request.
- **Jobs:**
  - **check:** Lint and typecheck. Steps: checkout (with **submodules: true**, required for the websockets bundle), set up **Python 3.12**, install **requirements.txt**, run **pyright** (jakebailey/pyright-action), **ruff format --diff**, **ruff check --output-format=github**. Timeout 10 minutes.
  - **test:** Full test run. Steps: checkout with submodules, free disk space, Python 3.12, install requirements, **actions/cache** for **scripts/downloads** (key: models-v4), **python scripts/download_models.py --minimal scripts/downloads**, **python -m pytest tests/test_server.py -vs --test-install** (managed server installer test), **python -m pytest tests -vs --ci** (all tests in CI mode). Timeout 20 minutes.
- **Relevance for C++:** A C++ project may mirror this with equivalent steps (format/lint, typecheck if applicable, unit/integration tests, optional server or backend tests). The same test data under **tests/data/**, **tests/images/**, **tests/references/** can be used for behavioral parity.

### 2.9 License and repository metadata

- **LICENSE:** The project root contains a **LICENSE** file: **GNU General Public License v3.0** (GPL-3.0). A C++ rebuild that redistributes or derives from the project should comply with and retain the same license.
- **.github/FUNDING.yml:** Optional funding/sponsorship metadata for the GitHub repository; not required for plugin behavior or rebuild parity.

### 2.10 Git submodules (exact reference)

The repository uses Git submodules for bundled dependencies. The **`.gitmodules`** file at the repo root contains:

```
[submodule "ai_diffusion/websockets"]
	path = ai_diffusion/websockets
	url = https://github.com/python-websockets/websockets.git
[submodule "ai_diffusion/debugpy"]
	path = ai_diffusion/debugpy
	url = https://github.com/microsoft/debugpy.git
```

- **websockets:** Required at runtime for ComfyUI WebSocket communication; must be present in release packages (see §2.1, §13.51).
- **debugpy:** Optional developer debugging; excluded from release packaging in **scripts/package.py**.

A C++ rebuild does not use these submodules but should be aware of them when comparing or packaging the reference repo.

### 2.11 Repository assets and generated files

The following clarify what exists in the source tree versus what is generated or optional:

- **server_requirements.txt:** The file **ai_diffusion/server_requirements.txt** is **generated** from **scripts/server_requirements.in** (e.g. via `scripts/package.py` precheck or manually with `uv pip compile`). The managed server install reads **server_requirements.txt** from the plugin directory. A C++ rebuild that ships or builds a managed server should use the same source/generated layout or document the equivalent.
- **manual.html:** The desktop file references **X-Krita-Manual=manual.html**. This file is **not** present in the repository by default; it may be generated during release packaging (e.g. from README or docs). See §13.84, §13.108.
- **media/:** The README and §2.5 reference **media/screenshot-*.png**, **media/control-*-screen.png**, and **media/screenshot-video-preview.webp** for the project gallery. The **media/** folder may be absent from the source tree or listed in **.gitignore**; it is for marketing/gallery assets. Reference screenshots for this spec are in **screenshots/** (§12), not **media/**.
- **typeshed:** **scripts/typeshed/krita.pyi** provides Krita API stubs for running pyright (and tests) outside Krita. A C++ port does not need this file but may use it as a reference for the Krita API surface the plugin uses.
- **ai_diffusion.action:** The optional Krita action metadata file (**ai_diffusion/ai_diffusion.action**) may be **absent from the source tree** and included only in the release package (see §13.64, §13.151). When cloning the repo, do not rely on its presence; use the action IDs and display strings in §10.1 for parity.
- **.vscode/:** The repository may contain a **.vscode/** directory (e.g. **settings.json**, **launch.json**) for editor/IDE and debugging. This is optional developer tooling and not part of the plugin payload; a C++ rebuild need not ship or replicate it.

### 2.12 Scripts reference (purpose of each script)

The following table maps each file or folder under **scripts/** to its role. A C++ rebuild may need equivalent tooling only for packaging and managed server; the rest are development or optional.

| Script / path | Purpose |
|---------------|---------|
| **package.py** | Build release ZIP: bundle plugin, exclude debugpy, optional precheck (translation, server_requirements.txt, manual.html). See §13.77, §13.108. |
| **translation.py** | Extract translatable strings, update template (new_language.json.template), sync language files. See §13.114. |
| **server_requirements.in** | Source for managed server Python deps; compiled to **ai_diffusion/server_requirements.txt**. |
| **download_models.py** | Download or list models for tests/CI and optional packaging checks; copied into plugin dir by package. See §13.115. |
| **benchmark_report.py** | Benchmark reporting (optional). |
| **benchmark_html.py** | Benchmark HTML output (optional). |
| **docker.py** | Docker helpers (optional). |
| **docker/** | Dockerfile, nginx config, start scripts for containerized ComfyUI. See §13.119. |
| **images.py** | Image helpers (optional). |
| **file_server.py** | File server for tests (optional; e.g. HOSTMAP). |
| **typeshed/** | **krita.pyi** stubs for pyright when running outside Krita. |

### 2.13 Screenshots and repo-root reference assets

The following assets live at the **repository root** and are used for specification and development only (not shipped inside the plugin):

- **screenshots/** — When present, contains reference PNG screenshots of the Configure dialog and AI Image Generation dock (e.g. 16 images; file names like `Screenshot YYYY-MM-DD at H.MM.SS AM/PM.png`). Used as the authoritative visual reference for UI layout and appearance when rebuilding (§12). The folder may be empty in a fresh clone or minimal tree; see §13.187.
- **open-agent-tab.sh** — Optional script; may be present in the tree, not part of the plugin.
- **media/** — Referenced by README for gallery images (e.g. `media/screenshot-*.png`, `media/control-*-screen.png`). May be absent or gitignored; for project/marketing only. UI parity reference is **screenshots/** (§12), not **media/**.

A C++ rebuild should use the **screenshots/** contents as the visual reference for dialogs and dock layout; the table in §12 maps each screenshot file to the spec section it illustrates.

### 2.14 Running the reference implementation

For an engineer or agent who needs to **run the Python plugin** (e.g. to compare behavior, validate the spec, or capture UI reference):

- **Development install:** Per **CONTRIBUTING.md**: clone the repo, run **`git submodule update --init`** (required for **ai_diffusion/websockets**). In Krita’s **pykrita** folder, create a **symlink** to the **ai_diffusion** folder and a symlink to **ai_diffusion.desktop**. Restart Krita; enable the dock via Settings → Dockers → AI Image Generation.
- **Requirements:** **Krita 5.2.0 or newer**; the plugin runs in Krita’s embedded Python. No separate pip install for end users; dev/CI use **requirements.txt** (ruff, pyright, pytest, PyQt5, etc.).
- **Tests:** From repo root: **`python -m pytest tests -vs --ci`** (with **scripts/downloads** populated for model-dependent tests, e.g. **`python scripts/download_models.py --minimal scripts/downloads`**). **`--test-install`** runs the managed server installer test. Test data under **tests/data/**, **tests/images/**, **tests/references/** is the behavioral reference for workflow and image output.
- **Logs and user data:** At runtime, settings and logs go to **user_data_dir** (§9.6, §13.66); in a dev run this may be under **.appdata** if Krita’s AppData path contains the plugin. "View log files" in the Configure dialog and **Collect Diagnostics** (Plugin tab) reference this path.

A C++ rebuild does not require running the Python plugin; this section is for validation and comparison only.

### 2.15 Files and paths excluded or generated (.gitignore and packaging)

The following are **not in the repository** or are **generated**; an agent or engineer should not rely on them being present in a fresh clone:

| Path or asset | Status |
|---------------|--------|
| **.appdata**, **.logs**, **.pytest_cache**, **__pycache__**, **venv** | Ignored; created at runtime or by tests. |
| **scripts/downloads** | Ignored; holds downloaded models for CI/packaging. Populate with **download_models.py --minimal scripts/downloads** for full tests. |
| **scripts/upload.py**, **service**, **tests/server**, **tests/results**, **tests/benchmark** | Ignored; optional or test-only. |
| **manual.html** | Not in repo; **generated at package time** from README (see §13.77, §13.84, §13.108). |
| **media/** | README references it for gallery; may be gitignored or absent. UI reference is **screenshots/** (§12). |
| **screenshots/** | May be **empty** in a fresh clone (§13.187); when present, used as the spec’s visual reference. |
| **ai_diffusion/ai_diffusion.action** | Optional; may be absent in source and included only in the release package (§2.11, §13.64). |
| **krita_ai_diffusion*.zip** | Ignored; release package output. |

The **.gitignore** at repo root lists the above and other exclusions (e.g. **.idea**, **.DS_Store**, **scripts/docker/ComfyUI**). A C++ rebuild should not assume any of these paths or files exist when reading the repo; use the spec and **ai_diffusion/** source as the source of truth.

---

## 3. Configuration and Settings System

### 3.1 Storage and Persistence

- **Class:** `Settings` in `ai_diffusion/settings.py` (QObject with `Setting` descriptors).
- **Path:** `Settings.default_path` → `user_data_dir / "settings.json"`.
- **Persistence:** `load(path)`, `save(path)`; JSON with optional `//` line comments via `read_json_with_comments` (whole lines starting with `//` are stripped before parsing; see §13.135).
- **Legacy:** Migration from `ai_diffusion/settings.json` to `user_data_dir/settings.json` on first load.

### 3.2 Setting Descriptor Pattern

Each setting is defined as a class-level `Setting(name, default, desc, help="", items=None)`. The `Settings` instance holds `_values` dict; `__getattr__`/`__setattr__` read/write with `changed` signal emission. Enum values are serialized by name and restored via `str_to_enum`.

### 3.3 Enums and Value Types

| Enum / Type | Values / Notes |
|-------------|----------------|
| `ServerMode` | `undefined`, `managed`, `external`, `cloud` |
| `ServerBackend` | `cpu`, `cuda`, `mps`, `directml`, `xpu` (with platform availability) |
| `GenerationFinishedAction` | `none`, `preview`, `apply` |
| `ApplyBehavior` | `replace` ("Modify active layer"), `layer` ("New layer on top"), `layer_active` ("New layer above active") |
| `ApplyRegionBehavior` | `none`, `replace`, `layer_group`, `transparency_mask`, `no_hide` |
| `PerformancePreset` | `auto`, `cpu`, `low`, `medium`, `high`, `cloud`, `custom` |
| `ImageFileFormat` | `png`, `png_small`, `webp`, `webp_lossless`, `jpeg` (with `.extension` and `.quality`) |

- **ServerBackend:** Each value is a tuple (display label, platform_available); e.g. `cuda = ("Use CUDA (NVIDIA GPU)", not is_macos)`. The UI uses the label and the availability flag to show/enable backend options in the Connection (managed server) tab.

### 3.4 Extended Enums (jobs, connection, workflows, inpaint, control)

| Enum / Type | Values / Notes |
|-------------|----------------|
| `JobState` | **Flag** (combinable): `queued` (0), `executing` (1), `finished` (2), `cancelled` (3). In practice used as mutually exclusive states. |
| `JobKind` | `diffusion` (0), `control_layer` (1), `upscaling` (2), `live_preview` (3), `animation_batch` (4), `animation_frame` (5), `animation` (6) |
| `ConnectionState` | `disconnected` (0), `connecting` (1), `connected` (2), `error` (3), `discover_models` (10); cloud: `auth_missing` (20), `auth_requesting` (21), `auth_pending` (22), `auth_error` (23) |
| `QueueMode` | `back`, `front`, `replace` (per-document queue ordering) |
| `WorkflowKind` | `generate` (0), `inpaint` (1), `refine` (2), `refine_region` (3), `upscale_simple` (4), `upscale_tiled` (5), `control_image` (6), `custom` (7). Used for **workflow.create()** dispatch; see §9.1 and §13.161. |
| `Arch` | `sd15`, `sdxl`, `sd3`, `flux`, `flux_k`, `flux2_4b`, `flux2_9b`, `illu`, `illu_v` (Illustrious v-prediction), `chroma`, `qwen`, `qwen_e`, `qwen_e_p`, `qwen_l`, `zimage`; internal: `auto`, `all`. Helpers: `is_sdxl_like`, `is_flux_like`, `is_flux2`, `is_qwen_like`, `from_string()`, compatibility per arch. **is_edit** (true for flux_k, qwen_e, qwen_e_p, qwen_l — edit models modify input images); **supports_edit** (is_edit or is_flux2); used for edit-mode UI, linked_edit_style, and workflow. |
| `ControlMode` | `reference`, `style`, `composition`, `face`, `inpaint`, `universal`, `scribble`, `line_art`, `soft_edge`, `canny_edge`, `depth`, `normal`, `pose`, `segmentation`, `blur`, `stencil`, `hands`. Properties: `is_ip_adapter`, `is_control_net`, `can_substitute_universal(arch)`, `can_substitute_instruction(arch)`. |
| `InpaintMode` | `automatic`, `fill`, `expand`, `add_object`, `remove_object`, `replace_background`, `custom` |
| `InpaintContext` | `automatic`, `mask_bounds`, `entire_image`, `layer_bounds` |
| `FillMode` | `none`, `neutral`, `blur`, `border`, `replace`, `inpaint`, `green` |
| `ComfyRunMode` | `runtime` (0: in-process, images in memory), `server` (1: ComfyUI server, images via base64/websocket). Default for `workflow.create()` is `server`. |
| `CustomGenerationMode` | `regular`, `live`, `animation` — used by Custom (Graph) workspace to choose Generate vs Generate Live vs Generate Animation; maps to JobKind. |
| `WorkflowSource` | `document`, `remote`, `local` — source of a custom workflow (from .kra, from server, or local file). |
| `TileOverlapMode` | `auto` (0), `custom` (1) — used in Upscale workspace for "Tile Overlap: Automatic" vs "Custom" (pixels). When `auto`, workflow uses -1 for overlap; when `custom`, uses **tile_overlap** (int, px). |
| `ErrorKind` | `none`, `plugin_error`, `server_error`, `insufficient_funds`, `warning`, `incompatible_lora`, `validation_warning`; `Error(kind, message, data)` for user-facing errors. |

### 3.5 Complete Settings Schema

**Connection / Server**

| Key | Type | Default | Description (short) |
|-----|------|---------|---------------------|
| `server_mode` | ServerMode | undefined | How to connect (managed / external / cloud) |
| `server_url` | str | "127.0.0.1:8188" | URL for ComfyUI (custom server) |
| `server_path` | str | user_data_dir / "server" | Install path for managed server |
| `server_backend` | ServerBackend | platform default | CPU/CUDA/MPS/DirectML/XPU |
| `server_arguments` | str | "" | Extra CLI args for server |
| `server_authorization` | str | "" | ComfyUI auth token |
| `access_token` | str | "" | Cloud access token |
| `check_server_resources` | bool | True | Refuse connection if nodes/models missing |

**Diffusion (selection / blending / safety)**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `selection_feather` | int | 10 | Border expand/blur as % of selection size |
| `selection_min_transition` | int | 32 | Min feather pixels for denoising |
| `selection_grow_offset` | int | 4 | Binary grow (px) before feather |
| `selection_blend` | int | 25 | Transition area (px) for alpha blend |
| `selection_padding` | int | 6 | Min padding % around selection |
| `color_match` | bool | True | Match peripheral colors (requires selection) |
| `nsfw_filter` | float | 0.0 | 0=Disabled, 0.65=Basic, 0.8=Strict |

**Interface**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `language` | str | "en" | UI language (restart required) |
| `prompt_translation` | str | "" | Translate prompts to English (language code or "") |
| `prompt_line_count` | int | 2 | Prompt text editor height (lines) |
| `prompt_line_count_live` | int | 2 | Same for Live mode |
| `show_negative_prompt` | bool | False | Show negative prompt editor |
| `show_steps` | bool | False | Show steps in weights box |
| `tag_files` | list[str] | [] | Tag autocomplete file stems (e.g. from CSV) |
| `generation_finished_action` | GenerationFinishedAction | preview | Do nothing / Preview / Apply |
| `apply_behavior` | ApplyBehavior | layer | How to apply result (generation) |
| `apply_region_behavior` | ApplyRegionBehavior | layer_group | How to apply region results |
| `apply_behavior_live` | ApplyBehavior | replace | How to apply in Live mode |
| `apply_region_behavior_live` | ApplyRegionBehavior | replace | Region behavior in Live |
| `new_seed_after_apply` | bool | False | New seed after apply in Live |
| `save_image_format` | ImageFileFormat | png_small | Format for saving from thumbnails |
| `save_image_metadata` | bool | False | Embed metadata in saved PNG |
| `save_image_quality_webp` | int | 80 | WebP quality 0–100 |
| `save_image_quality_jpeg` | int | 85 | JPEG quality 0–100 |
| `save_image_file_name_format` | str | template | Template for saved filenames |
| `confirm_discard_image` | bool | True | Confirm when discarding images |
| `show_builtin_styles` | bool | True | Show built-in styles in lists |
| `debug_dump_workflow` | bool | False | Dump ComfyUI prompt to log folder |

**Performance / History**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `history_size` | int | 1000 | RAM (MB) for active history |
| `history_storage` | int | 20 | MB for stored history in .kra |
| `history_format` | ImageFileFormat | webp | Format for history images |
| `multi_threading` | bool | True | Background threads for plugin ops |
| `performance_preset` | PerformancePreset | auto | Auto/CPU/Low/Medium/High/Cloud/Custom |
| `batch_size` | int | 4 | Max batch size (when custom) |
| `resolution_multiplier` | float | 1.0 | Scale factor for generation |
| `max_pixel_count` | int | 6 | Max megapixels for generation |
| `dynamic_caching` | bool | False | First Block Cache for speed |
| `tiled_vae` | bool | False | Process output in tiles (memory) |

**Other**

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `auto_update` | bool | True | Check for updates on startup |
| `document_defaults` | dict | {} | Recently used document settings |
| `last_news` | str | "" | Last seen news digest |

Performance presets (when not `auto`/`custom`) map to fixed `PerformancePresetSettings`: e.g. `low` → batch_size=2, max_pixel_count=2, tiled_vae=True; `medium` → 4, 6; `high` → 6, 8; `cloud` → 8, 6.

- **PerformanceSettings** (runtime): A dataclass used when applying presets or custom performance options. Fields: `batch_size`, `resolution_multiplier`, `max_pixel_count`, `dynamic_caching`, `tiled_vae`. This is the in-memory representation of the effective performance configuration (from preset or custom sliders).

---

## 4. Configure Image Diffusion Dialog (UI)

### 4.1 Window

- **Title:** "Configure Image Diffusion"
- **Minimum size:** 960×480.
- **Layout:** Horizontal: left navigation list (fixed width ~120px) + right stacked content + footer.

### 4.2 Navigation (Left)

List widget with exactly 6 items, in order:

1. **Connection**
2. **Styles**
3. **Diffusion**
4. **Interface**
5. **Performance**
6. **Plugin**

Current item highlights (e.g. blue background). Clicking switches the right pane to the corresponding tab.

### 4.3 Footer (All Tabs)

- **Left:** Button "Restore Defaults" (resets settings and reloads UI).
- **Center:** Text "Plugin version: X.Y.Z" (e.g. 1.49.0), grey, italic.
- **Right:** Link "Open Settings folder" (opens `user_data_dir`), then button "Ok" (saves and closes).

### 4.4 Tab: Connection ("Server Configuration")

- **Server type selection:** Three options (buttons or tabs):
  - **Online Service** — status: "Signed out" / "Connecting" / "Connected" / "Error"
  - **Local Managed Server** — status: "Not installed" / "Not running" / "Connecting" / "Connected" / "Error"
  - **Custom Server** — status: "Disconnected" / "Connecting" / "Connected" / "Error"

- **Custom Server content when selected:**
  - **Server URL:** Label "Server URL", description: "URL used to connect to a running ComfyUI server. Default is 127.0.0.1:8188 (local)."
  - Single-line edit for URL (e.g. `https://xxx.ngrok.app/`).
  - **Connect** button.
  - Status line: "Connected" (green) / "Connecting" / "Disconnected" (grey) / "Error: ..." (red).
  - **View log files** link (opens log dir).
  - **Detected base models:** List of architectures (SD 1.5, SD XL, SD 3, Flux, Flux Kontext, Flux 2 Klein 4B/9B, Illustrious, Chroma, Qwen variants, Z-Image) with per-line status: "supported" or "missing &lt;components&gt;".
  - Help text: "See Custom ComfyUI Setup for required models. Check the client.log file for more details." (Custom ComfyUI Setup = link to docs).

- **Online Service:** Login/Sign out, account info, tokens, buy tokens, view account (when implemented).
- **Local Managed Server:** Install path, backend selector, install/start/stop, progress, packages (ComfyUI, custom nodes, models), logs.

- **Connection tab layout:** The tab uses a single **stacked widget** with four panels. **Index 0** = **InitialSetupWidget** (first-run: "Welcome to Image Generation in Krita", three options: Online Service, Local Managed Server, Custom ComfyUI); shown when `server_mode` is **undefined**. **Index 1** = **CloudWidget** (Online Service: Sign in, status, user info). **Index 2** = **ServerWidget** (Local Managed Server: path, backend, install/start/stop, logs). **Index 3** = **Custom server** panel (Server URL, Connect button, status, View log files link, Detected base models list). When `server_mode` is not undefined, the **ServerModeSelect** control (three server-type options) is visible and the stack shows the panel for the current mode. When undefined, only the setup widget is shown so the user picks a server type first.

### 4.5 Tab: Styles ("Style Presets")

- **Style Presets section:**
  - Current preset display: grey text showing preset name and path (e.g. "Anime (Illustrious) (built-in/anime-illustrious.json)").
  - Message: "Built-in styles cannot be modified. Click to edit a copy."
  - Buttons: dropdown (select preset), + (add), ⋮ (edit/menu), trash (delete), refresh, save.
  - Checkbox: "Show pre-installed styles".

- **Name:** Text field (e.g. "Anime (Illustrious)").

- **Model Checkpoint:** Dropdown "The Diffusion model checkpoint file", refresh icon. If checkpoint missing: yellow warning "The checkpoint used by this style is not installed."

- **Checkpoint configuration (advanced):** Collapsible section (triangle).

- **LoRA:** Description "Extensions to the checkpoint which expand its range based on additional training". Buttons: Add, Upload. Filter/dropdown "All" + refresh. List of LoRA items (select, strength %, enable/disable, remove).

- **Style Prompt:** Multi-line text. Description: "Text which is appended to all prompts. The {prompt} placeholder can be used to wrap prompts." Example: `{prompt}, masterpiece, best quality, recent, newest, absurdres, highres`.

- **Negative Prompt:** Multi-line text. Description: "Textual description of things to avoid in generated images."

- **Linked Edit Style:** Dropdown "Select an alternative style to use for instruction-based editing" (e.g. "None").

- **Sampler Settings:** "Configure sampler type, steps and CFG to tweak the quality of generated images."
  - Expandable "Quality Preset (generate and upscale)": e.g. "Alternative - Euler A".
  - Expandable "Performance Preset (live mode)": e.g. "Alternative - Euler A".

### 4.6 Tab: Diffusion ("Diffusion Settings")

- **Selection Feather:** Slider 0–25, suffix " %", description "The border is expanded and blurred by a fraction of selection size".
- **Selection Blend:** Slider 0–100, suffix " px", description "Transition area for alpha blending the result image".
- **Selection Padding:** Slider 0–25, suffix " %", description "Minimum additional padding around the selection area".
- **Color Match:** Toggle, description "Match peripheral colors and brightness with existing content. Requires a selection."
- **NSFW Filter:** Dropdown: "Disabled", "Basic", "Strict". Description "Attempt to filter out images with explicit content". (First time enabling shows a warning dialog.)

**Note:** The Settings schema (§3.5) also defines **selection_min_transition** and **selection_grow_offset**; these are **not** exposed in the Configure Diffusion tab. They are used internally for mask/denoising (see §13.43: get_selection_modifiers, calc_selection_pre_process). A C++ rebuild must support these settings in the schema and in mask creation even though they do not appear in the UI.

### 4.7 Tab: Interface ("Interface Settings")

- **Language:** Dropdown. Description: "Interface language used by the plugin - requires restart!" Options include: English, Français, Deutsch, 日本語, 简体中文, 正體中文, 한국어, Español, Italiano, Português do Brasil, Русский, Türkçe, Bahasa indonesia, ไทย, etc.
- **Prompt Translation:** Dropdown. Description: "Translate text prompts from the selected language to English". Options: "Disabled" + language list from server when connected.
- **Prompt Line Count:** Spinbox 1–10. Description: "Size of the text editor for image descriptions". Default 2.
- **Negative Prompt:** Toggle with labels "Show" / "Hide". Description: "Show text editor to describe things to avoid".
- **Show Steps:** Toggle Off/On. Description: "Display the number of steps to be evaluated in the weights box."
- **Tag Auto-Completion:** File list (tag CSV stems). Description: "Enable text completion for tags from the selected files". Buttons: "Look for new tag files", "Open folder where custom tag files can be placed". Sub-options (checkboxes, enabled when tag files set): Danbooru, Danbooru NSFW, e621, e621 NSFW.
- **Finished Generation:** Dropdown. Description: "Action to take when an image generation job finishes". Options: Do Nothing, Preview, Apply.
- **Apply Behavior:** Dropdown. Description: "Choose how result images are applied to the canvas (generation workspaces)". Options: Modify active layer, New layer on top, New layer above active.
- **Apply Behavior (Live):** Dropdown. Description: "Choose how result images are applied to the canvas in Live mode". Same options.
- **Live: New Seed after Apply:** Toggle. Description: "Pick a new seed after copying the result to the canvas in Live mode".
- **Save Image Format:** Dropdown. Description: "File format for saved images from thumbnails." Options: PNG (fast), PNG, WebP, WebP (lossless), JPEG.
- **Save Image Metadata:** Toggle. Description: "When saving generated images from thumbnails, include metadata in the PNG". Enabled only when format is PNG.
- **Dump Workflow:** Toggle. Description: "Write latest ComfyUI prompt to the log folder for test & debug".

### 4.8 Tab: Performance ("Performance Settings")

- **Active History Size:** Description "Main memory (RAM) used for the history of generated images". Spinbox (MB), range e.g. 5–20000, step 100. Right: "Currently using X.X MB" (green).
- **Stored History Size:** Description "Memory used to store generated images in .kra files on disk". Spinbox (MB), e.g. 5–2000, step 5. Right: "Currently using X.X MB" (green).
- **Performance Preset:** Description "Configures performance settings to match available hardware." Below: "Device: [CUDA] 0 NVIDIA GeForce RTX XXXX (X GB)" (from client when connected). Dropdown: Automatic, CPU, GPU low (up to 6GB), GPU medium (6GB to 12GB), GPU high (more than 12GB), Cloud, Custom.
- **Maximum Batch Size:** Slider 1–16. Description "Increase efficiency by generating multiple images at once". Value shown (e.g. 4). (Shown when preset = Custom.)
- **Resolution Multiplier:** Slider, e.g. 0.3–1.5, format "X.Xx". Description "Scaling factor for generation. Values below 1.0 improve performance for high resolution canvas."
- **Maximum Pixel Count:** Spinbox 1–99, suffix " MP". Description "Maximum resolution to generate images at, in megapixels (FullHD ~ 2MP, 4k ~ 8MP)." Optional toggle "Automatic" (when not custom).
- **Tiled VAE:** Toggle "Always" / "Automatic". Description "Conserve memory by processing output images in smaller tiles."
- **Dynamic Caching:** Toggle. Description "Re-use outputs of previous steps (First Block Cache) to speed up generation."
- **Multi-Threading:** Toggle. Description "Perform certain plugin operations in background threads".

### 4.9 Tab: Plugin ("Plugin Information and Updates")

- **Header:** Logo (swirling/abstract icon) + "Generative AI for Krita" (large).
- **Current version:** Label + value (e.g. 1.49.0).
- **Latest version:** Label + value (e.g. 1.49.0 or "Not checked" / "Checking..." / "Update failed").
- **Check for updates on startup:** Checkbox.
- **Check for Updates** button.
- **Download and Install** button (enabled when update available).
- **System Information:** Heading. Text: "Please attach this information when reporting issues!" **Collect Diagnostics** button (copies to clipboard and shows in dialog). **View log files** link.
- **Documentation and Support:** Heading. Links: Website, Handbook: Guides and Tips, GitHub, Issues, Discussions, Discord.

---

## 5. AI Image Generation Dock (Main Panel)

### 5.1 Container

- **Window title:** "AI Image Generation"
- **Content:** Single `QStackedWidget` showing one of: Welcome, Generate, Upscale, Live, Animation, Custom (Graph), or Custom Placeholder (cloud). Stack indices: 0 = Welcome, 1 = Generate, 2 = Upscale, 3 = Live, 4 = Animation, 5 = Custom, 6 = Custom Placeholder.

Which view is shown depends on: no document / not connected / update required / news → Welcome; else current document's `workspace` (generation → Generate, upscaling → Upscale, live → Live, animation → Animation, custom + cloud → Placeholder, custom → Custom).

### 5.2 Welcome View

- Logo (64×64) + title "AI Image Generation" (two lines: "AI Image" / "Generation" in code).
- **Auto-update:** When an update is available, message "A new plugin version is available!", version text, "Check for updates on startup", "Download and Install" button. When this is visible, the Connection and News sections are hidden.
- **News:** When the client has unseen news (`client.news` and digest ≠ `settings.last_news`), news text + "Ok" button. Clicking "Ok" sets `last_news` to the digest, saves, and emits **accepted**. When news is visible (and no update overlay), Connection is hidden. Until the user dismisses news or completes setup, the dock can keep showing Welcome so the user sees the message.
- **Connection:** Status text (e.g. "Not connected to server." / "Connected to server at …"), error text if any, **Configure** button (opens Configure Image Diffusion). Shown when there is no update overlay and no unseen news.
- **When Welcome is shown:** The dock shows the Welcome view when **model is None** (no document), **not connected**, **requires_update** (update available), or **has_news** (unseen news). After the user dismisses news or connects, **update_content()** switches to the appropriate workspace if a document is open and connected.
- **Welcome layout (vertical order):** Header row (logo 64×64 + title "AI Image\nGeneration", 12pt font) → 12pt spacing → **AutoUpdateWidget** → **NewsWidget** → **ConnectionWidget** → 24pt spacing → footer links (right-aligned: Interstice.cloud | GitHub Project | Discord) → stretch. **Visibility:** At most one of the three (AutoUpdate, News, Connection) is visible at a time: if update is visible, Connection and News are hidden; if news is visible and update is not, Connection is hidden; if neither update nor news, Connection is visible. This ensures a single clear call-to-action.
- Footer links: Interstice.cloud | GitHub Project | Discord.

### 5.3 Workspace Selector (All Workspace Views)

At top of Generate, Upscale, Live, Animation, and Graph views:

- **Dropdown (or toolbar button with menu):** Icon + label for current mode. Menu items:
  - **Generate** (sparkle/magic icon)
  - **Upscale**
  - **Live**
  - **Animation**
  - **Graph**

Selecting an item sets the document's `workspace` and switches the stacked content. The **Animation** workspace has no dedicated keyboard action (`switch_workspace_animation` does not exist); it is selectable only via this dropdown.

### 5.4 Generate View

- **Workspace selector** (as above).
- **Style selector:** Dropdown showing current style name (e.g. "Anime (NoobAI XL)").
- **Prompt:** Multi-line text, placeholder "Describe the content you want to see, or leave empty."
- **Negative prompt:** (If enabled in settings) Multi-line, placeholder "Describe content you want to avoid.", often with distinct background (e.g. reddish-brown).
- **Strength:** Slider + numeric (e.g. "Strength: 100%" or "30%") with up/down arrows. Optional checkbox to enable/disable.
- **Seed:** Input "Seed: XXXXXXXX" + dice icon for random seed.
- **Generate** button (primary, wide, with sparkle icon). The button has a **dropdown menu** whose contents depend on context: when **strength &lt; 100%** and **region-only** is enabled, the menu shows "Refine Region" and "Refine Region (Custom)"; when strength is 100% and region-only, "Generate Region" options; when the document has a selection, inpaint or refine-selection options (e.g. Inpaint, Expand, Refine Selection); when **edit mode** is available, "Edit" / "Edit (Custom)"; otherwise "Generate" / "Generate (Custom)". Optional dropdown for queue options (batch/queue mode) is separate.
- **Queue / batch:** Batch count, queue mode (e.g. front/back/replace).
- **History:** Horizontal or grid list of thumbnails with timestamps and prompts; click to preview, double-click or "Apply" to apply to canvas. Context menu for save/discard.
- **Layer count:** **LayerCountWidget** — spinbox or control for **layer_count** (e.g. 1–8). Visible **only when** the current style's architecture is **Qwen Layered** (`Arch.qwen_l`); otherwise hidden. Used for layered generation (multiple output layers). Bound to **model.layer_count**.
- **Edit mode:** Toggle **Edit** (instruction-based editing; uses **linked_edit_style** when set) vs normal **Generate**. Visible and enabled when **can_toggle_edit** is true (style/arch supports instruction editing).
- **Region-only:** Toggle to limit generation to the **active region** only (**region_only**); when set, only the active region mask and prompt are used.
- **Regions / Inpaint:** Options for "Generate", "Generate Region", layer/selection usage. Inpaint mode (automatic, fill, expand, add_object, remove_object, replace_background, custom), inpaint context (automatic, mask_bounds, entire_image, layer_bounds), fill mode, and "use inpaint model" / "use prompt focus" toggles. How region results are applied is governed by **Apply Region Behavior** (settings): none, replace, layer_group, transparency_mask, no_hide.
- **Progress bar** and error area when running.
- Right-side icons: Settings (gear), dice (random seed), T+ (text/prompt), layers (add to layer).

### 5.5 Upscale View

- **Workspace selector.**
- **Style / upscaler:** Dropdown (e.g. "Default (4x_NMKD-Superscale-SP_178000_G)" or model name).
- **Scale:** Slider (e.g. 1.0–4.0) + spinbox "Scale: X.XXx", "Target size: W x H".
- **Refine upscaled image:** Checkbox. When checked:
  - **Refinement model:** Dropdown (e.g. "Anime (NoobAI XL)").
  - **Strength:** Slider "X%".
  - **Image guidance:** Slider "X%".
  - **Tile Overlap:** "Automatic" or "X px".
  - **Use Prompt:** Toggle.
- **Upscale** button.
- Progress and error area.

### 5.6 Live View

- **Workspace selector.**
- **Style** dropdown.
- **Prompt** and optionally **Negative prompt.**
- **Strength** slider.
- **Seed** + dice.
- **Full Animation** / **Single Frame** radio (for animation vs single image).
- **Generate** or **Generate Animation** button.
- **Live preview** area (updating preview).
- Optional control layers / reference.

### 5.7 Animation View

- **Workspace selector.**
- **Style** dropdown (with **Speed/Quality** / sampling-quality selector: Fast vs Quality).
- **Prompt** and **Negative prompt.**
- **Strength** slider.
- **Add Control Layer** button; **Control list** (same as in Generate/Live).
- **Full Animation** / **Single Frame** radio buttons (**batch_mode**).
- **Generate Animation** or **Generate Frame** button (label and tooltip depend on batch_mode).
- **Queue** button (no batch options; **supports_batch=False**).
- **Progress bar** and **Error** area.
- **Target layer** dropdown: visible only when **Single Frame** is selected; lists image layers as "Target layer: {name}"; selects which layer receives the generated frame via **layer.write_pixels**. Persisted as **AnimationWorkspace.target_layer** (QUuid).
- **Preview area** (QLabel): in Single Frame mode shows the current target layer or last generated result (**target_image_changed**); in Full Animation mode empty until batch completes.
- **AnimationWorkspace** (model): **sampling_quality** (fast/quality), **target_layer** (QUuid), **batch_mode** (bool). Persisted in **ui.json** under **animation**.
- **Full Animation:** Document must be saved. Generates one job per keyframe in **playback_time_range**; frames written to **`{document_directory}/{document_stem}.animation/frame-{frame}.png`**. On batch completion, **document.import_animation(keyframes, start)** and active layer renamed to **"[Generated] {start}-{end}: {params.name}"**.
- **Single Frame:** Generates at **document.current_time** and writes result to **target_layer**; **target_image_changed** updates preview.

### 5.8 Graph (Custom Workflow) View

- **Workspace selector.**
- Workflow list / selector (workflows from **WorkflowSource**: document, remote, or local; each **CustomWorkflow** has id, source, workflow, path). **WorkflowCollection** uses icons per source: **file-json** (local), **web-connection** (remote), **file-kra** (document).
- **Open Web UI** button: Tooltip/label *"Open Web UI to create custom workflows"*. On click, opens **client.url** in the system default browser (e.g. `QDesktopServices.openUrl(QUrl(client.url))`) and calls **model.custom.switch_to_web_workflow()**, which subscribes to remote workflow updates for **5 minutes** so workflows created or published in ComfyUI’s web UI appear in the plugin’s workflow list. **Client** must expose a **url** property (e.g. ComfyUI base URL or cloud API base).
- Parameter inputs, layer and selection inputs; **Open Settings** button (opens Configure Image Diffusion).
- **CustomGenerationMode** (regular / live / animation) selects which action is shown: **Generate**, **Generate Live**, or **Generate Animation**; it drives JobKind (diffusion, live_preview, animation) and the visible button.
- When server is **Online Service (cloud)**, show **CustomWorkflowPlaceholder** (message that custom graph is not available on cloud) instead of full custom workflow UI.

### 5.9 Visual design and look-and-feel

For UI parity, a C++ rebuild should match the following visual conventions. The **screenshots** in **screenshots/** (§12) are the authoritative reference for layout and appearance.

- **Logo:** Source asset **`ai_diffusion/icons/logo-128.png`**. Displayed at **64×64** px in the dock Welcome header and in the Configure dialog Plugin tab. README uses the same image at 64px.
- **Theme:** Dark/light theme is derived from the application palette (`theme.is_dark`). Icons use **`-dark`** / **`-light`** suffixes (e.g. `control-pose-dark.svg`, `control-pose-light.svg`). Semantic colors (green for success, yellow for warning, red for error) are defined in **theme.py** (§13.61).
- **Spacing and typography:** Welcome view uses 12pt spacing between header and content, 24pt before footer links; header title "AI Image\nGeneration" is two lines at ~12pt. Configure dialog minimum size 960×480; left nav ~120px.
- **Controls:** Combo boxes use **flat_combo_stylesheet** (transparent background, selection highlight). Toggles use **SwitchWidget** (rounded track, sliding thumb, ~120 ms animation). Progress bar color switches by **ProgressKind** (generation vs upload) (§13.18, §13.95).
- **Icons:** Loaded via **theme.icon(name)** from **`ai_diffusion/icons/`**; lookup order `{name}-{dark|light}.svg` then `{name}-{dark|light}.png`. Same paths and naming allow asset reuse or drop-in replacement in a C++ build.

---

## 6. Workspaces (Modes)

| Workspace | Enum value | Numeric | UI label | Main widget |
|-----------|------------|---------|----------|-------------|
| generation | Workspace.generation | 0 | Generate | GenerationWidget |
| upscaling | Workspace.upscaling | 1 | Upscale | UpscaleWidget |
| live | Workspace.live | 2 | Live | LiveWidget |
| animation | Workspace.animation | 3 | Animation | AnimationWidget |
| custom | Workspace.custom | 4 | Graph | CustomWorkflowWidget or CustomWorkflowPlaceholder |

Workspace is stored per document (model). Persistence and serialization use the enum name (e.g. `"generation"`); numeric values are for reference when matching the codebase. Switching workspace updates the dock's stacked widget and the workspace selector label. **Note:** There is no `switch_workspace_animation` action; the Animation workspace is reachable only via the workspace selector dropdown (see §10.1).

---

## 7. Server Connection

### 7.1 Connection States

`ConnectionState`: disconnected, connecting, connected, error, discover_models; for cloud also auth_missing, auth_pending, auth_requesting, auth_error.

### 7.2 Server Modes

1. **External (Custom Server):** User provides `server_url`. Client: `ComfyClient`. No install; user runs ComfyUI.
2. **Cloud (Online Service):** `CloudClient`, default API URL, auth via `access_token`, sign-in in browser.
3. **Managed (Local Managed Server):** `Server` in `server.py`. Install path `server_path`, install/update ComfyUI + custom nodes + optional models, start/stop process, backend selection. Version file at `path/.version`.

### 7.3 Custom Server Connection Flow

- User sets URL and clicks Connect.
- Client connects (WebSocket/HTTP). On success: state → connected, then discover_models (list checkpoints, LoRAs, etc.). Missing resources reported in "Detected base models" and optionally block connection if `check_server_resources` is true.

### 7.4 Detected Base Models (Architectures)

List shown in Connection tab (from `resources.Arch` and client): SD 1.5, SD XL, SD 3, Flux, Flux Kontext, Flux 2 Klein 4B, Flux 2 Klein 9B, Illustrious, Illustrious v-prediction (`illu_v`), Chroma, Qwen, Qwen Edit, Qwen Edit Plus, Qwen Layered, Z-Image. Each line: "supported" or "missing &lt;component names&gt;".

### 7.4a Connection client access and error_kind

**Connection** exposes:
- **client** — When state is **connected**, returns the current **Client** instance; use only when connection is guaranteed (e.g. status text). Asserts state is connected.
- **client_if_connected** — Returns the current **Client** or **None** if not connected. Used throughout the UI (NewsWidget, style resolution, translation options, checkpoint/upscaler lists, custom workflow, etc.) so code can safely check "if client := connection.client_if_connected" before using client.news, client.user, client.features, or client models.

**Connection** also stores **error_kind** (string) for retry/UI logic. Values: **`"network"`** (HTTP/connection failure; autostart may try fallback URL), **`"missing_resources"`** (required nodes/models missing), **`"unknown"`** (other errors). On HTTP **401 Unauthorized**, the plugin clears **access_token** and sets connection state to error; **error_kind** is set so the UI can show appropriate messaging. A C++ rebuild should set and handle these values for parity (e.g. autostart only tries fallback when error_kind is network).

### 7.5 ComfyUI Client and Protocol

- **ComfyClient** (`comfy_client.py`): Default URL `http://127.0.0.1:8188`. Uses both HTTP (prompt queue, system info) and WebSocket (real-time progress and output). Bundled `websockets` package required.
- **Queue:** One active job at a time; one waiting slot (`QueuedJob`). `JobInfo`: id (UUID), work (`WorkflowInput`), node_count, sample_count.
- **WebSocket message types:** `executing` (increment node count), `execution_cached` (nodes list), `progress` (sample count). Progress value = `0.2 * (nodes / (node_count + 1)) + 0.8 * (samples / max(sample_count, 1))` so progress does not reach 100% until images received.
- **Workflow submission:** Graph built via `workflow.create(WorkflowInput, ClientModels, ComfyRunMode)` → `ComfyWorkflow` (dict); sent to ComfyUI prompt API; client emits `ClientMessage` (ClientEvent progress/finished/error, job_id, progress, images, result, error).

### 7.5a ComfyUI backend version requirement

The plugin expects the ComfyUI **prompt** API to return **`prompt_id`** in the response and to use that same ID in WebSocket messages (`execution_start`, `executed`, etc.). **Minimum supported ComfyUI version: 0.3.45.** If the server returns a `prompt_id` that does not match the submitted job ID, the client raises **ValueError** with the message: *"Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"* A C++ rebuild should enforce the same check and surface the same user-facing error so users know to upgrade their server.

### 7.6 Event Loop and Bootstrap

- **eventloop.py:** Dedicated asyncio event loop; `QTimer` with 20 ms interval calls `process_python_events()` so the loop runs inside Qt. `run(future)` schedules a coroutine; `run_until_complete(future)` blocks.
- **Startup order:** Extension constructor: `eventloop.setup()` → `settings.load()` → `root.init()` (Server, Connection, FileLibrary, WorkflowCollection, null model, RecentlyUsedSync, AutoUpdate, signal connections) → `eventloop.run(root.autostart(update_ui_callback))`. Shutdown: `root.server.terminate()`, `eventloop.stop()`.

### 7.7 ComfyUI HTTP and WebSocket API

The plugin talks to ComfyUI over HTTP and WebSocket. Base URL is the server URL (e.g. `http://127.0.0.1:8188`). All endpoints are relative to that base.

**HTTP endpoints used:**

| Method | Endpoint | Purpose |
|--------|----------|---------|
| GET | `object_info` | Node definitions (inputs/outputs) for custom workflows and model lists |
| GET | `system_stats` | Device info (type, name, VRAM); used for Performance Preset and Device display |
| POST | `prompt` | Submit workflow; body: `{ "prompt": workflow.root, "client_id": "<uuid>", "prompt_id": "<job_id>" }`; response includes `prompt_id` (must match for ComfyUI 0.3.45+) |
| POST | `interrupt` | Cancel current execution; body `{}` |
| POST | `queue` | Remove jobs; body `{ "delete": [ "<job_id>", ... ] }` |
| PUT | `api/etn/image/<id>` | Upload input image bytes for workflow (custom node API) |
| GET | `api/etn/image/<id>` | Download result image by id (after execution) |
| GET | `api/etn/model_info/<folder>?offset=&limit=` | Paginated model list; folders include `checkpoints`, `diffusion_models`, `unet_gguf`; `_meta.total` for pagination |
| GET | `api/etn/translate/<lang>/<text>` | Translate prompt text to English |
| GET | `api/etn/languages` | Supported prompt translation languages |
| POST | `api/etn/workflow/subscribe` | Subscribe to shared workflows; body `{ "client_id": "<id>" }` |
| POST | `api/etn/workflow/unsubscribe` | Unsubscribe; body `{ "client_id": "<id>" }` |
| (upload) | `api/etn/upload/loras/<file_id>` | Upload LoRA file (streaming); progress reported via ClientEvent.upload |

**WebSocket:** Connect to `{base_url}/ws?clientId={client_id}`. Optional auth via query or headers (e.g. `server_authorization`). Messages are JSON (or binary for embedded images).

**WebSocket message types (incoming):**

| type | Purpose |
|------|---------|
| `status` | Connection ready; client may emit ClientEvent.connected |
| `execution_start` | Job started; `data.prompt_id` = job id |
| `executing` | Node progress; `data.node` can be null when done |
| `execution_cached` | Cached nodes list |
| `progress` | Sample/value progress |
| `executed` | Node finished; `data.output` may contain images (with `source`/`id` for HTTP fetch) or other outputs |
| `execution_interrupted` | Job cancelled |
| `execution_error` | Job failed; `data.exception_message`, `data.traceback` |
| `etn_workflow_published` | Shared workflow from another client; ClientEvent.published |

Binary frames may carry PNG image data (e.g. for embedded result images). Result images can also be referenced in `executed` with `source: "http"` and `id`; then the client fetches via GET `api/etn/image/<id>`.

After a successful connection, **apply_performance_preset(settings, device_info)** is called so Performance tab and generation use the correct batch size and limits. On HTTP 401 (Unauthorized), the plugin clears `access_token` and sets connection state to error; **error_kind** is set to `"network"` for connection errors and used for retry logic (e.g. autostart tries fallback URL only when error_kind is network).

### 7.8 Cancel flow (user actions to backend)

The three cancel actions (§10.1) map to the following backend behavior. A C++ rebuild should implement the same order and API calls so cancel semantics match.

- **Cancel Active** (action **cancel**): **actions.cancel_active()** → **model.cancel(active=True)**. If the current document’s **JobQueue** has any job in **executing** state, **connection.interrupt()** is called, which runs **client.interrupt()** → HTTP POST **interrupt** (body `{}`). The server stops the currently running job; the client receives **execution_interrupted** on the WebSocket and marks the job as cancelled.
- **Cancel Queued** (action **cancel_queued**): **actions.cancel_queued()** → **model.cancel(queued=True)**. **model.clear_queued()** collects all jobs in **JobState.queued**, removes them from the per-document **JobQueue**, and returns their **job_id** list. **connection.cancel(job_ids)** runs **client.cancel(job_ids)**. For **ComfyClient**: HTTP POST **queue** with body `{ "delete": [ "<job_id>", ... ] }` for those IDs; the client also clears its waiting slot and in-memory queue for those IDs and emits **ClientEvent.interrupted** for each so the UI updates. For **CloudClient**, jobs in the executor queues are marked **JobState.cancelled** in memory.
- **Cancel All** (action **cancel_all**): **actions.cancel_all()** → **model.cancel(active=True, queued=True)**. The implementation runs **queued** first, then **active**: first **clear_queued()** and **connection.cancel(to_cancel)** (POST **queue** delete for queued job IDs), then if any job is executing, **connection.interrupt()** (POST **interrupt**). So the server’s running job is stopped and its queued slot is cleared in one user action.

---

## 8. Styles, Checkpoints, LoRA, Prompts

### 8.1 Style Preset (JSON)

- **Location:** Built-in: `ai_diffusion/styles/*.json`. User: `user_data_dir / "styles"`.
- **Fields (example):** `name`, `version`, `architecture`, `checkpoints` (list of filenames or identifiers), `loras`, `style_prompt`, `negative_prompt`, `vae`, `clip_skip`, `v_prediction_zsnr`, `rescale_cfg`, `self_attention_guidance`, `preferred_resolution`, `linked_edit_style`, `sampler`, `sampler_steps`, `cfg_scale`, `live_sampler`, `live_sampler_steps`, `live_cfg_scale`.
- **Version and architecture:** The **version** field is an integer (e.g. `2`) used for style schema compatibility when loading/saving. The **architecture** field may be an **Arch** enum name (e.g. `sdxl`, `flux`) or the string **`"auto"`**; when `"auto"`, the effective architecture is inferred from the checkpoint at runtime (all built-in styles in the repo use `"auto"`).
- **Style class:** `Style` (QObject): filepath, name, architecture, checkpoints, loras, style_prompt, negative_prompt, sampling params, VAE, etc. Load/save via StyleSettings.

### 8.2 Model Checkpoints

- **API:** `CheckpointInput`: checkpoint name, version (Arch), vae, loras, clip_skip, etc.
- **File list:** `FileLibrary.checkpoints` (local + remote from client after connection).

### 8.3 LoRA

- **API:** `LoraInput`: name, strength, storage_id.
- **Prompt:** Can reference LoRAs in text (e.g. `<lora:name>` or `<lora:name:weight>`). Style preset can list LoRAs with default strength.

### 8.4 Prompts

- **Conditioning:** `ConditioningInput`: positive, negative, style string, control list, regions (each with mask, bounds, positive, control, loras), language, edit_reference.
- **Style merge:** Style's `style_prompt` may contain `{prompt}`; merged with user positive prompt.
- **Comments and wildcards:** Before submission, prompts are passed through **strip_prompt_comments** (remove text after `#` unless escaped as `\#`) and **eval_wildcards** (replace `{a|b|c}` with one option chosen by seed). See §13.35.
- **Attention editing:** Prompts may use `(text:weight)` syntax; the UI supports Ctrl+Up/Down to adjust weight (±0.1, clamped). See §13.35.
- **Layer placeholders:** `<layer:name>` in the prompt is replaced with "Picture {n}" and layer names are collected for workflow binding (§13.35).
- **Translation:** If `prompt_translation` set, prompts are translated to English via server/translation node.
- **Tag autocomplete:** From `tag_files` (CSV paths); completion in prompt field (e.g. Danbooru, e621).

### 8.5 Prompt syntax (special constructs)

The following prompt constructs are implemented in `text.py` and used during workflow build and UI editing. A C++ rebuild must support the same syntax and behavior for parity.

| Construct | Syntax | Behavior |
|-----------|--------|----------|
| **Comments** | `#` rest of line | **strip_prompt_comments()** removes text after `#` unless the `#` is escaped as `\#` (backslash before hash). Used before sending prompts to the model. |
| **Wildcards** | `{option1\|option2\|option3}` | **eval_wildcards(text, seed)** replaces each `{...\|...}` with one option chosen deterministically via `random.Random(seed)`; multiple wildcards in one prompt are resolved in one pass (up to 10 iterations until no more matches). Pattern: `(\{[^{}]+\|[^{}]+\})`. |
| **LoRA in prompt** | `<lora:name:strength>` or `<lora:name>` | **extract_loras()** parses the prompt; `name` is matched against FileCollection (LoRA files); optional `strength` overrides style/default. Invalid or missing LoRA raises PluginError. |
| **Layer placeholders** | `<layer:name>` | Replaced with "Picture {n}" and layer names collected for workflow binding; **extract_layers()** returns (prompt, layer_names). |
| **Attention weight** | `(text:weight)` or `<text>` | Parsed by **parse_expr()** (ExprNode tree). **edit_attention(text, positive)** adjusts weight: Ctrl+Up increases by 0.1, Ctrl+Down decreases by 0.1; brackets can be `()`, `<>`, `[]`, `{}`. Weight is a float (e.g. 1.2). |
| **UTF-16 cursor** | (no visible syntax) | Prompt widget uses Krita/Qt string indexing; **char16_len()**, **char16_index_to_str_index()**, **str_index_to_char16_index()** convert between Python string indices and Qt/QString char16 positions for cursor and selection. See §13.133. |

---

## 9. Data Models and APIs

### 9.1 Workflow

- **WorkflowInput** (api.py): kind, images, models, sampling, conditioning, inpaint, crop_upscale_extent, upscale, control_mode, batch_count, color_match, nsfw_filter, custom_workflow. Serialization: `to_dict()` / `from_dict()` with optional image blobs.
- **WorkflowKind:** generate, inpaint, refine, refine_region, upscale_simple, upscale_tiled, control_image, custom.
- **ComfyUI:** Graph as dict (node id → { class_type, inputs }); built in `comfy_workflow.py`; entry point `workflow.create(WorkflowInput, ClientModels, ComfyRunMode)` returns `ComfyWorkflow`; sent to ComfyUI prompt API.

### 9.2 Workflow and API Types (detailed)

- **Image types (image.py):** `Extent(width, height)`, `Bounds(x, y, width, height)`, `Point`, `Image`, `Mask`, `ImageCollection`; extent/bounds used throughout workflow and resolution.
- **ExtentInput:** input (Extent), initial (Extent), desired (Extent), target (Extent) — resolutions for input, initial gen, hi-res refinement, and canvas target.
- **ImageInput:** extent (ExtentInput), initial_image, hires_image, hires_mask (Image | None), layer_count.
- **InpaintParams:** mode (InpaintMode), target_bounds (Bounds), fill (FillMode), grow, feather, blend (px), use_inpaint_model, use_condition_mask, use_reference; `clamped()` for bounds.
- **RegionInput:** mask (Image), bounds (Bounds), positive (str), control (list[ControlInput]), loras (list[LoraInput]).
- **ControlInput:** mode (ControlMode), image (Image | None), strength, range (tuple[float, float]).
- **CustomWorkflowInput:** workflow (dict), params (dict), positive_evaluated, negative_evaluated, models (CheckpointInput | None), sampling (SamplingInput | None).
- **CustomWorkflow:** id (str), source (WorkflowSource), workflow (ComfyWorkflow), path (Path | None). Represents a single custom workflow; name = id without `.json`.
- **WorkflowSource:** document, remote, local — where the workflow was loaded from (.kra, server, or local file).
- **CustomGenerationMode:** regular, live, animation — per-document mode for the Graph workspace; determines which generate button and JobKind are used.
- **UpscaleInput:** model (str), tile_overlap (int; -1 = automatic).

### 9.3 Jobs

- **JobParams:** bounds (Bounds), name (str), regions (list[JobRegion]), metadata (dict; prompt, style, strength, sampler, checkpoint, control layer info), seed, has_mask, is_layered, inpaint_mode (InpaintMode | None), frame (tuple[int,int,int]), animation_id (str), resize_canvas (bool). Legacy: from_dict migrates old prompt/negative_prompt/etc. into metadata.
- **JobRegion:** layer_id (str), prompt (str), bounds (Bounds), is_background (bool).
- **Job:** id (str | None), kind (JobKind), state (JobState, Flag), params (JobParams), control (ControlLayer | None), timestamp, results (ImageCollection), in_use (dict[int, bool]).
- **JobQueue:** Per-document queue; submit to client; signals: **count_changed**, **selection_changed**, **job_finished**, **job_discarded**, **result_used**, **result_discarded**; thumbnails, apply/discard/save, persistence via ModelSync.

### 9.4 Model (Per-Document)

- **Model** (model.py): workspace, style, strength, **region_only** (bool: when true, generation uses only the active region; toggle in Generate view), **edit_mode** (bool: instruction-based "Edit" vs normal "Generate"; uses linked_edit_style when set), **can_toggle_edit** (read-only: true when style/arch supports instruction editing), batch_count, seed, fixed_seed, resolution_multiplier, queue_mode, translation_enabled, layer_count, progress, error; sub-objects: **inpaint** (CustomInpaint: mode, fill, use_inpaint, use_prompt_focus, context, context_layer_id; see §13.169), upscale, regions, layers, custom, jobs. Effective performance at runtime is represented by a **PerformanceSettings**-like structure (batch_size, resolution_multiplier, max_pixel_count, dynamic_caching, tiled_vae) derived from the selected preset or custom values.
- **Document–model lifecycle:** There is one **Model** instance per open document, created by **Root.create_model(document)** when the document is first used. When the user closes a document or switches to another document, **Root** retains the model for the now-inactive document until **prune_models** is called (e.g. when the number of cached models would otherwise grow without bound). When a document is closed, **Root.prune_models(document_id)** discards that document's model; the next time that document is opened, **create_model** is invoked again and state is restored from document annotations (**ui.json**, **result{N}.webp**). A C++ rebuild should implement the same create/prune lifecycle so memory use stays bounded and document state is correctly rehydrated on reopen.

### 9.5 Apply Result Flow

- **apply_result(model, image, params, behavior, region_behavior, prefix)** applies a generated image to the document. If **params.resize_canvas** is true and image extent differs from document extent, **document.resize_canvas(width, height)** is called first. Then:
  - **No regions (or region_behavior is none):** If **behavior** is **ApplyBehavior.replace**, **LayerManager.update_layer_image(active_layer, image, bounds)** is used (this creates a new layer with the merged content and removes the old layer so undo works). Otherwise **LayerManager.create(name, image, bounds, above=…)** creates a new layer; name includes prefix, params.name, and seed; position is above active or on top per behavior.
  - **With regions:** For each **JobRegion**, **create_result_layer** builds one result layer; **ApplyRegionBehavior** controls how: **replace** (update layer in place), **layer_group** (create group and place layer), **transparency_mask** (apply as mask), **no_hide** (layer without hiding others). **RestoreActiveLayer** context manager restores the active layer to the one that was active for the region that matched the current active layer.
- **LayerManager.update_layer_image(layer, image, bounds, keep_alpha)** creates a new layer with the same name and merged pixel data (existing content + image at bounds), then removes the old layer, so a single undo step reverts the change.

**Layer and LayerManager (layer.py):** **Layer** wraps a Krita Node; exposes id, name, type (LayerType), bounds, get_pixels/get_mask, write_pixels, get_pixel_frames (for animation). **LayerManager** is created per document; methods: **create(name, img, bounds, make_active, parent, above)** (paint layer), **create_vector(name, svg)**, **create_mask(name, img, bounds, parent)**, **create_group(name, above)**, **create_group_for(layer)** (wrap layer in a group), **update_layer_image(layer, image, bounds, keep_alpha)**. Properties: root, active, all, images, masks, image_extent. Layer type filtering uses **LayerType** (paint, vector, group, file, clone, fill, filter, transparency mask, selection mask, etc.; is_image, is_mask, is_filter).

### 9.6 Paths

- **plugin_dir:** Package dir of `ai_diffusion`.
- **user_data_dir:** Resolved in util.py; see §13.66 for full logic (QStandardPaths when in Krita → `ai_diffusion` or `krita-ai-diffusion` subfolder; `plugin_dir.parent / ".appdata"` when krita not importable).
- **log_dir:** `user_data_dir / "logs"`. Log files: **client.log**, **server.log**. Legacy: logs were previously under `plugin_dir / ".logs"`; on first run the plugin migrates any existing log files from `.logs` to `user_data_dir / "logs"`. Logging uses **RotatingFileHandler** (e.g. 10 MB max, 4 backups). **Collect Diagnostics** (Plugin tab) reads the last 300 lines of each log file and includes them in the clipboard output. **Note:** CONTRIBUTING.md refers to the "`.logs` subfolder of the plugin installation folder"; that is the legacy path. The "View log files" link in the Configure dialog and diagnostics use **log_dir** (user_data_dir / "logs") after migration.

### 9.7 Document Persistence

- **Storage:** Document state is stored in Krita document annotations. Main key: **`ui.json`** — JSON blob containing: **model-level persisted properties** (from `serialize(model)`): `workspace`, `style`, `strength`, `region_only`, `edit_mode`, `batch_count`, `seed`, `fixed_seed`, `resolution_multiplier`, `queue_mode`, `translation_enabled`, `layer_count`, plus root-region prompt/strength and region list; and **sections**: `version` (persistence format version, currently 1), `preview_layer`, **`inpaint`** (CustomInpaint state: mode, fill, use_inpaint, use_prompt_focus, context, context_layer_id; see §13.169), `upscale`, **`live`**, **`animation`**, `custom`, `history` (list of _HistoryResult: id, slot, offsets, params, kind, in_use), **`root`** (main regions root), **`edit`** (edit_regions — regions used for instruction-based/edit mode, distinct from root), `control`, `regions` (each with nested `control`). A C++ rebuild must persist and restore the same top-level keys so documents remain compatible. History images: annotation **key** is always **`result{N}.webp`** (N = slot index); the **stored bytes** follow `settings.history_format` (e.g. webp). Images stored as QByteArray; offsets list used to split multi-image blobs.
- **RecentlyUsedSync:** Synced to `settings.document_defaults`. Fields: style, batch_count, translation_enabled, inpaint_mode, inpaint_fill, inpaint_use_model, inpaint_use_prompt_focus, inpaint_context, upscale_model. When a new document is opened and has no `ui.json`, these defaults are applied to the new model; when user changes style/batch/etc., defaults are updated and saved.
- **Document API (document.py):** `annotate(key, QByteArray)`, `find_annotation(key) → QByteArray | None`, `remove_annotation(key)`; `create_mask_from_selection(SelectionModifiers) → (Mask, Bounds) | (None, None)`; `get_image(bounds?, exclude_layers?) → Image`; `resize(extent)`, `resize_canvas(width, height)`; `check_color_mode() → (True, None) | (False, error_message)`; `add_pose_character(layer)` (for control/pose layers); `import_animation(files, offset)` (animation import). **Properties:** **`playback_time_range`** → `(start_frame, end_frame)` (animation timeline range); **`current_time`** → current frame index (used by Animation Single Frame and Live). **Signals:** `selection_bounds_changed`, **`current_time_changed`** (e.g. KritaDocument polls every 20 ms and emits when current time changes). **SelectionModifiers:** feather_rel, feather_min_px, pad_rel, pad_offset_px, size_min_px, multiple, square, invert.

### 9.7a Document annotation keys (reference)

All document state is stored under the **`ai_diffusion/`** prefix when calling Krita’s annotation API. The plugin uses **logical keys** (e.g. `"ui.json"`) in code; the implementation passes **`ai_diffusion/{key}`** to Krita. For a C++ rebuild, use the same keys and prefix so documents remain compatible.

| Logical key | Purpose |
|-------------|---------|
| **`ui.json`** | Main document state: version, preview_layer, inpaint, upscale, live, animation, custom, history (list of result refs), root (regions), edit (edit_regions), control, regions. |
| **`result{N}.webp`** | History image blob for slot index N (N = 0, 1, 2, …). Stored format follows `settings.history_format` (e.g. webp). Multi-image results use one blob with byte offsets in the history entry. |
| **`document_id`** | UUID string identifying the document; used to bind a Model to a document when reopening. Written when missing; if the same ID appears in two open documents, a new ID is assigned to one (copy handling). |

Lookup order for history images: try **`result{slot}.webp`** first, then **`result{slot}`** (no extension) for legacy documents (§13.39).

### 9.8 Client Interface (summary)

- **Client** (client.py): Abstract interface. Connect/disconnect; submit workflow; subscribe to events. **url** (str): Base URL of the server or API (e.g. ComfyUI `http://127.0.0.1:8188` or cloud API base); used by the Graph view **Open Web UI** button to open the server in the system browser. **ClientMessage:** event (ClientEvent), job_id, progress, images, result, error. **ClientEvent:** progress, finished, interrupted, error, connected, disconnected, queued, upload, published, output, payment_required. **ClientModels:** checkpoints, loras, etc. (from server). **DeviceInfo:** type, name, vram (GB). **MissingResources:** used for "Detected base models" and optional connection blocking. **SharedWorkflow**, **TextOutput**, **JobInfoOutput** for custom/output handling. **User** (when client is cloud): id, name, images_generated, credits (see §13.121). **ServerError:** exception subclass used for server-side errors.

### 9.9 Workflow Build and Resource Resolution

- **Workflow build:** `workflow.create(WorkflowInput, ClientModels, ComfyRunMode)` builds the full ComfyUI graph (comfy_workflow.py) and returns a `ComfyWorkflow` instance (dict representation). Depends on ClientModels for checkpoint/LoRA/controlnet availability.
- **Resource resolution (resources.py):** `ResourceKind` (checkpoint, text_encoder, vae, controlnet, clip_vision, ip_adapter, lora, model_patch, upscaler). `ResourceId(kind, arch, identifier)` where identifier is ControlMode | UpscalerName | str. `resource_id()`, `search_path()`, `is_required()`. Large model-filename maps per Arch/ControlMode (e.g. controlnet filenames for sd15/scribble, sdxl/depth, etc.) drive "Detected base models" and missing-resource reporting.

---

## 10. Krita Integration

### 10.1 Actions (extension.py)

Created with `window.createAction("ai_diffusion_<name>", "", "")`:

- **settings** → open Configure Image Diffusion.
- **generate** → trigger generate.
- **cancel** → cancel active job.
- **cancel_queued** / **cancel_all** → cancel queued/all.
- **toggle_preview** → toggle preview.
- **apply** → apply current result to canvas (using settings apply_behavior / apply_region_behavior).
- **apply_alternative** → "Apply result (layer)": in Live workspace, applies the current live result as a new layer (ignores replace; uses layer behavior so the result is always applied to a new layer). No-op or standard apply in other workspaces when implemented.
- **create_region** → create region.
- **switch_workspace_generation** / **switch_workspace_upscaling** / **switch_workspace_live** / **switch_workspace_graph** → set workspace. There is **no** **switch_workspace_animation** action; the Animation workspace is selectable only via the workspace dropdown (see §5.3, §6).
- **toggle_workspace** / **toggle_edit_mode** → toggle mode (see §13.195 for exact behavior: cycle workspace vs flip edit_mode).

**Action display strings (for UI parity):** The optional `ai_diffusion.action` file defines the user-visible text for each action. A C++ rebuild should use equivalent strings:

| Action ID | Display text |
|-----------|--------------|
| ai_diffusion_generate | Generate image |
| ai_diffusion_cancel | Cancel current job |
| ai_diffusion_cancel_queued | Cancel queued jobs |
| ai_diffusion_cancel_all | Cancel all jobs |
| ai_diffusion_toggle_preview | Toggle preview |
| ai_diffusion_apply | Apply result |
| ai_diffusion_apply_alternative | Apply result (layer) |
| ai_diffusion_create_region | Create region |
| ai_diffusion_switch_workspace_generation | Switch workspace: Generate |
| ai_diffusion_switch_workspace_upscaling | Switch workspace: Upscale |
| ai_diffusion_switch_workspace_live | Switch workspace: Live |
| ai_diffusion_switch_workspace_graph | Switch workspace: Graph |
| ai_diffusion_toggle_workspace | Toggle workspace |
| ai_diffusion_toggle_edit_mode | Toggle edit mode |
| ai_diffusion_settings | Show Settings |

**Apply action scope:** The **apply** action has effect only in **Generate** workspace (applies the selected history result to the canvas using apply_behavior / apply_region_behavior) and **Live** workspace (applies the current live result; **apply_alternative** in Live applies as new layer). In **Upscale**, **Animation**, and **Graph (Custom)** workspaces, the apply action has no effect (no handler in the action dispatch). Upscale and Animation results are applied automatically or via workspace-specific flows (e.g. upscale output to layer, animation frames to target layer or import_animation).

(Exact action IDs and menu placement depend on Krita version; the plugin does not define menu bars, only actions.)

### 10.2 Dock

- **Factory ID:** "imageDiffusion".
- **Default position:** DockRight.
- **Widget:** ImageDiffusionWidget (stack of Welcome, Generate, Upscale, Live, Animation, Custom, CustomPlaceholder).

### 10.3 Document and Canvas

- Active document and canvas determine `root.model_for_active_document()`. On canvas change, dock updates content. Model holds workspace, style, prompts, history, and job queue for that document.
- **Dock lifecycle:** The dock overrides **canvasChanged(canvas)**; when the canvas (or view) changes, it calls **update_content()** only when **canvas is not None and canvas.view() is not None**, so the correct workspace view is shown for the active document and no update runs with a missing canvas/view. When **root.model_created** is emitted (a new Model is created for a document), the dock’s **register_model(model)** connects **model.workspace_changed** to **update_content()** so switching workspace or document updates the stacked widget correctly.

### 10.4 Lifecycle and Initialization

- **Startup:** Extension constructor runs `eventloop.setup()`, `settings.load()`, `root.init()` (creates Server, Connection, FileLibrary, WorkflowCollection, null model, RecentlyUsedSync, AutoUpdate; connects connection.message_received, models_changed). **SettingsDialog** is constructed during extension init (with `root.server`). Then `eventloop.run(root.autostart(update_ui_callback))` where the callback is the settings dialog's Connection tab **update_ui** (refreshes connection status in Configure Image Diffusion). Shutdown: `root.server.terminate()`, `eventloop.stop()`.
- **Per-document model:** When the active document changes, `root.model_for_active_document()` prunes closed documents, finds or creates a Model for the active Krita document. If the document has no `ui.json` annotation, `RecentlyUsedSync.track(model)` applies document_defaults (style, batch_count, inpaint_*, etc.). New model gets a `ModelSync` that loads from `ui.json` if present, subscribes to model/jobs/regions/control, and writes annotations on change; history images written when jobs finish.

### 10.5 Theme and Icons

- **theme.py:** `icon(name)` loads from `ai_diffusion/icons/`; names often have `-dark` / `-light` suffix for theme-aware icons. Lookup order: **`{name}-{dark|light}.svg`** then **`{name}-{dark|light}.png`** (so SVG is preferred, PNG is fallback; e.g. control-canny_edge uses .png). `checkpoint_icon(arch, format, client)` for checkpoint list. **`logo()`** returns a pixmap from **`icons/logo-128.png`** (the plugin logo); the UI scales it to 64×64 in the dock header and in the Configure dialog Plugin tab. `set_text_clipped(label, text)`, `screen_scale(widget, size)` for layout.
- **Theme colors (for visual parity):** Palette-derived and semantic constants: `base`, `green`, `yellow`, `red`, `grey`, `highlight`, `progress_alt`, `active`, `line`, `line_base`; `is_dark` (bool) for dark vs light theme. Status and error text use these (e.g. yellow for warnings, green for "Connected"). Combo boxes use **`flat_combo_stylesheet`** (transparent background, selection highlight).
- **Icons (icons/):** Many SVG/PNG files: workspace and actions (add-pose, apply, cancel, etc.), control types (control-depth, control-canny_edge, control-pose, etc.), context (context-automatic, context-mask, context-layer, etc.), and UI indicators (alert, warning). Naming: **`{name}-dark.svg`** / **`{name}-light.svg`** (or .png). A C++ rebuild should use the same paths and naming so assets can be shared or swapped.

---

## 11. Rebuild Checklist

To rebuild this application from scratch, implement at least:

1. **Plugin shell:** Desktop file (including X-Krita-Manual), extension registration, dock registration (factory ID "imageDiffusion", DockWidgetFactoryBase.DockRight), load/save settings from `user_data_dir/settings.json`, event loop driving async client (§7.6).
2. **Settings:** Full schema in §3.5, all tabs (§4.4–4.9) with correct controls and bindings.
3. **Connection:** Three server modes, custom URL + Connect, status and "Detected base models" from client; ConnectionState and cloud auth states (§3.4).
4. **Styles tab:** Preset list, add/edit/copy/delete, checkpoint dropdown, LoRA list, style/negative prompt, sampler presets.
5. **Dock:** Welcome (connection + configure), workspace switcher (Generate, Upscale, Live, Animation, Graph), and one widget per workspace as in §5.4–5.8.
6. **Generate:** Style, prompt, negative prompt (if enabled), strength, seed, Generate button, history list with preview/apply; **Apply Region Behavior** and region/inpaint options (mode, context, fill, use inpaint model) as in §5.4.
7. **Upscale:** Scale, target size, optional refine (model, strength, image guidance), Upscale button.
8. **Live / Animation:** Prompt, strength, Full Animation vs Single Frame, Generate/Generate Animation button, live preview where applicable.
9. **Graph:** Workflow selector and params; **Open Web UI** button (opens client.url in browser, **switch_to_web_workflow** for 5-minute remote workflow subscription); placeholder when cloud is selected.
10. **ComfyUI client:** Connect (HTTP + WebSocket), queue submission, WebSocket progress (executing/execution_cached/progress), model discovery, device info for Performance tab (§7.5).
11. **Job queue and history:** Per-document queue, JobKind/JobState (§3.4), thumbnails (HistoryWidget: 96×96 px, LeftToRight flow, IconMode, Apply/Context buttons, applied overlay star.png; §13.28, §13.28a), apply/discard/save, persistence in .kra via ui.json and result{N}.webp (§9.6).
12. **Control layers:** ControlNet/IP-Adapter layers (add control layer, mode, strength, layer ref); JobKind.control_layer; control in JobParams.metadata and in persistence control/regions (§9.6).
13. **Regions:** Root regions and per-region prompts/control; serialized in ui.json (root, regions, control); RegionInput and JobRegion (§9.2, §9.3).
14. **Inpaint options:** InpaintMode, InpaintContext, FillMode, InpaintParams; UI for mode, context, fill, use_inpaint_model, use_prompt_focus (§3.4, §9.2). Persist **CustomInpaint** (model.inpaint) under **inpaint** in ui.json, including **context_layer_id** when context is layer_bounds (§13.169).
15. **Localization:** All user-visible strings via translation (language dropdown and JSON files in `language/`).
16. **Theme and icons:** icon()/checkpoint_icon()/logo(); icons in `icons/` with dark/light variants (§10.5). Match visual design and look-and-feel (§5.9). See §13.153 for the list of icon name stems used in the UI.
17. **Version and about:** Plugin version in footer and Plugin tab (e.g. 1.49.0); update check and diagnostics as in §4.9.
18. **Performance (runtime):** Apply preset or custom values into a **PerformanceSettings**-like structure (batch_size, resolution_multiplier, max_pixel_count, dynamic_caching, tiled_vae) used when building workflows (§3.5, §9.4).
19. **Resolution and tiling:** **ScaleMode** and **TileLayout** for scaling strategy and tiled processing (§13.23, §13.24).
20. **Custom workflow params:** **ParamKind** and **CustomParam**; ETN_KritaStyle, ETN_KritaImageLayer, ETN_KritaMaskLayer, ETN_Parameter node types and parameter widgets (§13.25).
21. **Errors and first-run:** **Error** / **ErrorKind** and **ErrorBox** display; **InitialSetupWidget** for first-time server choice (§13.27, §13.33).
22. **Strength and animation:** Strength snapping (**StrengthSnapping** / **StrengthSpinBox**); **SamplingQuality** (fast/quality) for animation (§13.32, §13.34).
23. **Krita version:** Require Krita 5.2.0+ and document in install/setup (§13.30, §13.40).
24. **Prompt processing:** Strip `#` comments (with `\#` escape), evaluate wildcards `{a|b|c}` with seed, support attention syntax `(text:weight)` and Ctrl+Up/Down weight editing, and replace `<layer:name>` with "Picture {n}" for layer binding; full syntax in §8.5 (§13.35).
25. **Saved image metadata:** When saving from history with "Save Image Metadata", embed A1111-style metadata via create_img_metadata (§13.36).
26. **Updates and news:** Implement UpdateState and AutoUpdate flow; show NewsWidget in Welcome when client.news is set and not yet seen; persist last_news (§13.37, §13.38). Use the update check endpoint and response shape in §13.160 (and INTERSTICE_URL from §13.159 if supporting override).
27. **Annotation fallback:** When loading history images from document annotations, try key with extension then key without extension (§13.39).
28. **Client interface:** Implement Client.news, Client.user, and Client.missing_resources where applicable (§13.41).
29. **ComfyUI API:** Use the HTTP endpoints and WebSocket protocol in §7.7 (prompt, object_info, system_stats, interrupt, queue, image upload/download, model_info, translate, workflow subscribe); enforce prompt_id matching and minimum ComfyUI version 0.3.45 (§7.5a); handle 401 by clearing access_token and set error_kind for retry logic. Embed workflow images as base64 in node inputs before submit (§13.139).
30. **Document color mode:** Before generate/upscale, call check_color_mode(); block and show error unless document is RGBA and 8-bit depth (§13.42).
31. **Selection modifiers:** Build SelectionModifiers from settings and arch via get_selection_modifiers(); use calc_selection_pre_process() for grow/feather/blend in workflow (§13.43).
32. **Preview layer:** Persist and restore preview_layer ID in ui.json; restore via try_set_preview_layer on load (§13.44).
33. **Apply result flow:** Implement apply_result and create_result_layer with LayerManager.update_layer_image (replace = new layer + merge + remove old for undo) and create; support ApplyRegionBehavior for region results (§9.5). **apply_alternative** action: in Live workspace, apply current result as new layer (§10.1).
34. **Live recording and animation import:** Live workspace records frames to `{doc_dir}/{stem}.live-frames/frame-N.webp`; Animation Full Animation batch uses `{stem}.animation/frame-{frame}.png`; document.import_animation(files, offset) creates keyframe layers; Single Frame writes to target_layer (§13.45, §13.74).
35. **Collect Diagnostics:** Build diagnostics string (version, platform, paths, settings redacted, log tails), redact username, truncate for issue body; copy to clipboard from Plugin tab (§13.47).
36. **Built-in styles:** Ship or reference the style JSON filenames in ai_diffusion/styles/ for default presets (§13.46).
37. **Action display strings:** Use the action IDs and display text from §10.1 (table) so menus and shortcuts match.
38. **Dock lifecycle:** On **canvasChanged** call **update_content()** only when **canvas** and **canvas.view()** are non-null; on **model_created** register the model and connect **workspace_changed** to **update_content()** (§10.3).
38a. **Toggle actions:** Implement **toggle_workspace** (cycle workspace enum: generation→upscaling→live→animation→custom→generation) and **toggle_edit_mode** (flip model.edit_mode) per §13.195.
39. **Connection error_kind and client access:** Set and handle **error_kind** (network, missing_resources, unknown) and 401 → clear access_token (§7.4a). Expose **client_if_connected** (returns Client | None) and **client** (when connected) so UI and style resolution can safely use the client (§7.4a, §13.89).
40. **Tag files:** Support built-in tag stems (Danbooru, e621, etc.) and tag_files setting; path ai_diffusion/tags/. Escape parentheses in tag names on insert so they do not break attention syntax (§13.48, §13.138).
41. **IntervalSlider:** Dual-handle range slider for control layer strength range (§13.49).
42. **Managed server resources:** Use resources.comfy_url, comfy_version, required_custom_nodes (and optional_custom_nodes) for install and "Detected base models" (§13.50).
43. **Websockets / ComfyUI WS:** Implement WebSocket client for ComfyUI `/ws?clientId=...`; Python bundle path is ai_diffusion/websockets/src/ (§13.51).
44. **Document ID and copy handling:** Store **ai_diffusion/document_id** (UUID); assign new ID when same ID appears in two open documents (§13.52).
45. **Control image creation:** Implement **create_control_image** with the correct ComfyUI preprocessor per ControlMode (hands→MeshGraphormer-DepthMapPreprocessor, scribble, line_art, soft_edge, canny_edge, depth, normal, pose, segmentation) and **is_lines** inversion for line-type modes (§13.53).
46. **Prompt resize handle:** Optionally provide a draggable resize handle at the bottom of the prompt editor for user-resizable height (§13.54).
47. **Control presets:** Load **control.json** from ai_diffusion/presets/; structure: mode → "all" / arch → list of {strength, start, end}; use for default control layer strength/range (§13.55).
48. **Sampler presets:** Load **samplers.json** (preset name → sampler, scheduler, steps, minimum_steps, cfg) for Styles tab and workflow build (§13.56).
49. **User documentation:** Mirror or reference README/docs content: installation (Krita 5.2+), hardware support, optional custom ComfyUI, common issues (§13.57).
50. **ETN and custom node names:** Recognize and emit the ComfyUI/ETN/INPAINT node class names used in workflow build and custom workflow UI (§13.58).
51. **WebSocket client_id:** Generate one UUID per client session; use it in WebSocket URL and in prompt/queue/subscribe HTTP bodies (§13.59).
52. **models.json:** Parse **presets/models.json** (required/recommended, id, name, files with path/url/sha256, alternatives) for managed server install and "Detected base models" (§13.60).
53. **Theme and styling:** Use theme color constants (green, yellow, red, grey, highlight, progress_alt) and **flat_combo_stylesheet** for status text and combo boxes; support dark/light via palette or is_dark (§13.61).
54. **WorkflowInput serialization:** Support to_dict/from_dict with optional image_data (bytes + offsets); ImageCollection.to_bytes/from_bytes; implement WorkflowInput.cost for cloud token display and insufficient-funds messaging (§13.62).
55. **Image and geometry:** Provide Point, Extent, Bounds, Image, ImageCollection with the key static methods and properties (pad, clamp, restrict, expand, intersection, etc.) used in resolution and workflow (§13.63).
56. **Action metadata:** Use action IDs and display strings from §10.1; optional ai_diffusion.action XML format for Krita menu/shortcut parity (§13.64).
57. **Cloud image transfer:** If implementing cloud client, support inline vs transfer-by-size for large images per backend API (§13.65).
58. **User data directory:** Resolve user_data_dir per §13.66 (QStandardPaths when in Krita, .appdata when not); log_dir and legacy .logs migration.
59. **Toggle controls:** Provide a SwitchWidget-style toggle (checkable, sliding thumb, ~120 ms animation) for LoRA enable, Upscale "Use Prompt", settings toggles, and custom workflow booleans (§13.68).
60. **Document annotations:** Use annotation name prefix **ai_diffusion/** and description **"AI Diffusion Plugin: " + key** when writing document annotations (§13.69).
61. **Tests as reference:** Use tests/conftest.py (QtTestApp, clear_appdata/clear_results), tests/config.py (paths, default_checkpoint), and tests/data/, tests/images/, tests/references/ for behavioral parity when porting (§13.70).
62. **CI/CD (optional):** Use .github/workflows/test.yml as reference for what constitutes a passing build (lint, typecheck, format, pytest with --ci, managed server test with --test-install, submodules, model cache); see §2.8.
63. **Connection tab and Welcome parity:** Use Connection tab stacked layout (InitialSetupWidget, Cloud, Managed, Custom) and ServerModeSelect visibility as in §4.4; format "Detected base models" and missing-resources text as in §13.71; wire generate action to per-workspace handlers (§13.72); use Welcome ConnectionWidget status and error strings (§13.73) for UI parity.
64. **Animation workspace and Document timeline:** Implement **AnimationWorkspace** (sampling_quality, target_layer, batch_mode), **Document.playback_time_range** and **Document.current_time** (and **current_time_changed**); Animation view target_layer dropdown and preview area; .animation folder + PNG for Full Animation, Single Frame → target_layer (§5.7, §9.7, §13.74).
65. **Welcome accepted signal:** Emit an **accepted** (or equivalent) signal from the welcome view when the user dismisses news or completes setup; connect it to the dock's **update_content()** so the correct view is shown after refresh (§13.78).
66. **Krita annotation API:** Use the host's document annotation API with the same key prefix **ai_diffusion/** and, where supported, description **"AI Diffusion Plugin: " + key**; implement annotate/find_annotation/remove_annotation with the same semantics (§9.7a for key list, §13.69, §13.79).
67. **Plugin vs server version:** Keep plugin version (user-facing) separate from server/resources version where a managed server or resource set is versioned; document both in diagnostics and Plugin tab (§13.80).
68. **Autostart fallback:** When server_mode is undefined and autostart fails, set server_mode to cloud and clear connection error so the user sees the Online Service option; when it succeeds, set server_mode to external and persist URL (§13.81).
69. **Release packaging:** Ship the same logical assets as the Python release: desktop manifest, plugin payload, icons, styles, language, presets, tags; exclude dev-only paths (e.g. debugpy); document what is included (§13.77).
70. **Queue mode and seed:** Implement **QueueMode** (back = append, front = insert first, replace = clear then add) when enqueueing jobs (§13.86). Store and display **seed** as an integer; use **logo-128.png** for the plugin logo (§13.85, §10.5). **X-Krita-Manual** (manual.html) is optional (§13.84).
71. **Root and model lifecycle:** Implement **Root** with server, connection, files, workflows, models list, null model, RecentlyUsedSync, AutoUpdate; **prune_models**, **create_model**, **model_for_active_document**, **model_created** signal, **autostart** (§13.88).
72. **Connection flows:** Implement **Connection** client selection, **\_connect(url, mode, access_token)**, **sign_in** (cloud), **disconnect**, **error_kind**, **missing_resources** (§13.89).
73. **Queue UI:** Provide **QueuePopup** menu (Back, Front, Replace) and **QueueButton** with **supports_batch** where applicable (§13.92).
74. **Document polling:** Poll document selection and **current_time** (e.g. 20 ms), emit **selection_bounds_changed** and **current_time_changed** (§13.93).
75. **Save filename template:** Support **save_image_file_name_format** with placeholders **document_name**, **job_timestamp**, **job_index**, **prompt** (§13.94).
76. **ProgressBar and style filtering:** Style progress bar by **progress_kind** (upload vs generation); filter styles by client support on connect (§13.95, §13.97).
77. **VerificationState (managed server):** Use **VerificationState** / **VerificationStatus** for model integrity checks and fix flows if implementing managed server (§13.96).
78. **PoseLayers and pose control:** Implement **PoseLayers**-equivalent singleton (or per-document pose tracking) with timer-driven refresh from vector layer SVG shapes; **add_pose_character** / **add_character** for "Add Pose" in control UI (§13.98).
79. **C++ porting:** When rebuilding in C++, follow the mappings in **§14 (C++ and Porting Notes)** for asyncio/WebSocket, JSON comments, Krita API, and paths so behavior and layout match.
80. **ComfyWorkflow and UI workflow format:** Implement **ComfyWorkflow** API format (root = node id string → {class_type, inputs}; links as [node_id, output_slot]) and, for custom workflows, the **UI workflow format** (version, nodes, links) and conversion to API format (§13.101).
81. **Selection mask modifiers:** Apply **SelectionModifiers.square** and **.invert** in **create_mask_from_selection** (invert selection when invert=true; use square for bounds so result is square) (§13.102).
82. **Custom workflow validation:** Enforce at most one **ETN_KritaStyleAndPrompt** node and set **validation_error** with the same message when violated (§13.103).
83. **Model–widget binding:** Implement **bind** / **bind_combo** / **bind_toggle** (or equivalent two-way sync) and **serialize** / **deserialize** for persist-marked properties so model and UI stay in sync (§13.104).
84. **Live progress indicator:** Provide a **SpinnerWidget**-style compact progress indicator (arc + percentage) in the Live view during generation (§13.105).
85. **Region list UI:** Use **ActiveRegionWidget** and **InactiveRegionWidget** for active vs inactive regions in the region prompt UI (§13.106).
86. **Packaging:** If building a release package, generate **manual.html** from README (or equivalent) and run **update_model_checksums** (or equivalent) as in §13.108; **screenshots/** are the spec reference; **media/** in README is project gallery only (§13.110).
87. **LayerCountWidget:** In the Generate view, show a layer-count control (e.g. spinbox 1–8) only when the current style's architecture is **Qwen Layered** (`Arch.qwen_l`); bind to **model.layer_count** (§5.4, §9.4).
88. **Welcome visibility:** Show Welcome when **model is None**, **not connected**, **requires_update**, or **has_news**; when **has_news**, show NewsWidget until user clicks Ok (§5.2, §13.78).
89. **Tag CSV format:** Use tag files with columns **tag**, **type**, **count**, **aliases** per **tags/README.md** for autocomplete (§13.112).
90. **Cloud/custom LoRA upload:** If implementing cloud or custom server with LoRA upload, implement **loras_to_upload**, upload progress (ClientEvent.upload), and backend size limits per §13.113.
91. **Root and edit regions:** Implement **regions** and **edit_regions** (both RootRegion); **active_regions** returns edit_regions when workspace is generation and edit_mode is true, else regions. Persist as **root** and **edit** in ui.json; use active_regions for conditioning and region UI (§13.125).
92. **Execution flow:** Implement the full path from Generate click → workflow build → queue → client submit → WebSocket progress → finished → history/apply as in §13.126.
93. **History discard and JobQueue.Item:** On **result_discarded(Item)**, update the stored **result{slot}.webp** annotation in place (remaining images + new offsets); use **Item(job_id, index)** for identity (§13.131, §13.136).
94. **Prompt and config parsing:** Use **sanitize_prompt** for **JobParams.name** (§13.132); UTF-16 semantics for prompt cursor/selection (§13.133); **read_json_with_comments** strips **//** lines for settings/JSON, not **#** (§13.135).
95. **RecentlyUsed and action metadata:** Do not apply **inpaint_context = layer_bounds** from document_defaults (§13.137); include **ai_diffusion.action** root **&lt;text&gt;** if shipping action metadata (§13.134).
96. **Dialogs and confirmations:** Implement the same user-facing dialogs as in §13.140 (NSFW first-time, discard image, clear history, persistence load failure, managed server stop/uninstall/delete, custom workflow overwrite/delete, LoRA file too large) and §13.202 (file/directory pickers: Import Workflow, Select Directory for server path, Select LoRA file).
97. **HTTP and UX parity:** Add **ngrok-skip-browser-warning: 69420** header for ComfyUI HTTP requests when connecting to arbitrary URLs (§13.141); show LCM deprecation message when server rejects LCM (§13.142); show region negative-prompt alert icon when style ignores negative prompt (§13.143).
98. **History and diagnostics:** Track per-slot history size for "Currently using X.X MB" and active-history pruning (§13.145); offer Collect Diagnostics with clipboard copy and optional dialog showing the same text (§13.146).
99. **Apply action scope:** Wire the **apply** action only for Generate (selected history result) and Live (current live result); in Upscale, Animation, and Graph workspaces the action has no effect (§10.1).
100. **Upscale workspace model:** Implement **UpscaleWorkspace** with **TileOverlapMode** (auto/custom), **tile_overlap**, **unblur_strength** (image guidance), and persistence under **upscale** in ui.json (§13.147).
101. **FileLibrary storage:** Use in-memory checkpoint list (from server) and **user_data_dir/database/loras.json** for LoRA persistence (§13.148).
102. **LiveWorkspace and LiveScheduler:** Implement **LiveWorkspace** (is_active, is_recording, strength, result_available, keyframes folder) and **LiveScheduler** (delay, grace period, poll_rate) with persistence under **live** in ui.json (§13.149).
103. **Persistence version:** Use **version = 1** in ui.json and increment on breaking changes (§13.150).
104. **Action metadata:** Ship **ai_diffusion.action** at **ai_diffusion/ai_diffusion.action** with **ai_diffusion_settings** using **activationFlags=0** (§13.151).
105. **Krita document API:** Use the same annotation and document APIs (setAnnotation, annotation, selection, currentTime, importAnimation, etc.) as in §13.152 for C++ porting.
106. **Selection bounds and entire-document:** Implement _selection_bounds from selection x/y/width/height and _selection_is_entire_document (0,0, full extent, all 0xff) so create_mask_from_selection returns (None, None) when the whole document is selected (§13.154, §13.158).
107. **Action shortcuts:** Ship action metadata with empty shortcuts (no default key bindings) unless the host requires them (§13.155).
108. **Layer mask bounds:** For mask-type layers, compute bounds from selection/pixelData rather than node.bounds() (§13.157).
109. **Plugin installation path:** On load, if not in a pykrita directory and not a git repo, log a warning that installation path may break user files/settings (§13.165).
110. **Styles tab LoRA warnings:** In the Styles tab LoRA list, show "not installed on the server" and "special/server LoRA" warnings per LoraItem when connected (§13.166).
111. **Custom workflow import:** Use file dialog title "Import Workflow" and filter "Workflow Files (*.json);;All Files (*)" for importing custom workflows (§13.167).
112. **Repository alignment:** Treat this spec and **ai_diffusion/** source as the source of truth for folder names, API, and UI; CONTRIBUTING.md may use "langauge" for the language folder (§13.168).
113. **Document lifecycle and edge cases:** When a document is closed, its model is pruned and queued jobs for that document are no longer shown; no explicit server cancel is required (§13.176). Offline behavior uses normal error/status handling for update check and cloud (§13.176).
114. **Animation timeline API:** Use Krita **playBackStartTime()** and **playBackEndTime()** for **playback_time_range** so Full Animation batch uses the correct frame range (§13.177).
115. **Message routing:** Route **ClientMessage** by **job_id** to the single **Model** that owns that job via **Root._find_model(job_id)**; only that model’s **handle_message** runs (§13.181).
116. **Server queue and QueuedJob:** Respect ComfyUI’s one running + one queued slot; keep remaining jobs in the plugin’s per-document **JobQueue**; implement **QueueMode** (back/front/replace) for order and clear semantics (§13.182).
117. **MissingResources:** Support both **list[CustomNode]** and **dict[Arch, list[ResourceId]]** variants and **.get(arch)** for Connection tab “Detected base models” and missing-nodes list (§13.183).
118. **create_result_layer:** Implement all four **ApplyRegionBehavior** behaviors (replace, layer_group, transparency_mask, no_hide) and **RestoreActiveLayer** for per-region apply (§13.184).

---

## 12. Screenshot Reference

Reference screenshots are in the [`screenshots/`](screenshots/) folder. The following table ties each image to the spec section it illustrates.

| Screenshot / View | Spec section | Image file |
|-------------------|--------------|------------|
| Configure Image Diffusion – **Connection** tab (Custom Server, URL, Connected, Detected base models list) | §4.4 | [Screenshot 2026-02-28 at 11.18.02 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.18.02%20AM.png) |
| Configure Image Diffusion – **Styles** tab (Style Presets, Name, Model Checkpoint, LoRA, Style Prompt, Negative Prompt, Sampler Settings) | §4.5 | *(see Interface/other config screens for similar layout)* |
| Configure Image Diffusion – **Diffusion** tab (Selection Feather/Blend/Padding, Color Match, NSFW Filter) | §4.6 | [Screenshot 2026-02-28 at 11.23.26 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.23.26%20AM.png), [Screenshot 2026-02-28 at 11.23.33 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.23.33%20AM.png) |
| Configure Image Diffusion – **Interface** tab (Language, Prompt Translation, Prompt Line Count, Negative Prompt, Show Steps, Tag Auto-Completion, Finished Generation, Apply Behavior, Save format/metadata, Dump Workflow) | §4.7 | [Screenshot 2026-02-28 at 11.20.48 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.20.48%20AM.png), [Screenshot 2026-02-28 at 11.20.30 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.20.30%20AM.png), [Screenshot 2026-02-28 at 11.21.29 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.21.29%20AM.png), [Screenshot 2026-02-28 at 11.21.35 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.21.35%20AM.png), [Screenshot 2026-02-28 at 11.19.28 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.19.28%20AM.png) |
| Configure Image Diffusion – **Performance** tab (Active/Stored History Size, Performance Preset, Device, Batch Size, Resolution Multiplier, Max Pixel Count, Tiled VAE, Dynamic Caching, Multi-Threading) | §4.8 | *(see other config tabs for layout)* |
| Configure Image Diffusion – **Plugin** tab (Generative AI for Krita, version, Check for updates, Collect Diagnostics, Documentation links) | §4.9 | [Screenshot 2026-02-28 at 11.23.56 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.23.56%20AM.png) |
| AI Image Generation dock – **Generate** mode (workflow/style dropdown, prompt, strength, Generate button) | §5.4 | [Screenshot 2026-02-28 at 11.21.42 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.21.42%20AM.png), [Screenshot 2026-02-28 at 11.19.35 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.19.35%20AM.png), [Screenshot 2026-02-28 at 11.19.45 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.19.45%20AM.png), [Screenshot 2026-02-28 at 11.23.49 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.23.49%20AM.png) |
| AI Image Generation dock – **Upscale** mode (upscaler, scale, refine options, Upscale button) | §5.5 | [Screenshot 2026-02-28 at 11.23.41 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.23.41%20AM.png) |
| AI Image Generation dock – **Live / Animation** mode (prompt, strength, Full Animation / Single Frame, Generate Animation) | §5.6, §5.7 | [Screenshot 2026-02-28 at 11.19.15 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.19.15%20AM.png) |
| AI Image Generation dock – empty canvas / mode selector (Generate, Upscale, Live, Animation, Graph) | §5.3, §6 | [Screenshot 2026-02-28 at 11.24.11 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.24.11%20AM.png) |

The **screenshots/** folder is located at the **repository root** (same level as `ai_diffusion/`, `scripts/`, `tests/`). It is not part of the shipped plugin payload. All 16 reference screenshots in `screenshots/`:

- **Connection:** [Screenshot 2026-02-28 at 11.18.02 AM.png](screenshots/Screenshot%202026-02-28%20at%2011.18.02%20AM.png)
- **Interface (multiple):** [11.19.28](screenshots/Screenshot%202026-02-28%20at%2011.19.28%20AM.png), [11.20.30](screenshots/Screenshot%202026-02-28%20at%2011.20.30%20AM.png), [11.20.48](screenshots/Screenshot%202026-02-28%20at%2011.20.48%20AM.png), [11.21.29](screenshots/Screenshot%202026-02-28%20at%2011.21.29%20AM.png), [11.21.35](screenshots/Screenshot%202026-02-28%20at%2011.21.35%20AM.png)
- **Diffusion:** [11.23.26](screenshots/Screenshot%202026-02-28%20at%2011.23.26%20AM.png), [11.23.33](screenshots/Screenshot%202026-02-28%20at%2011.23.33%20AM.png)
- **Plugin:** [11.23.56](screenshots/Screenshot%202026-02-28%20at%2011.23.56%20AM.png)
- **Dock – Generate / general:** [11.19.35](screenshots/Screenshot%202026-02-28%20at%2011.19.35%20AM.png), [11.19.45](screenshots/Screenshot%202026-02-28%20at%2011.19.45%20AM.png), [11.21.42](screenshots/Screenshot%202026-02-28%20at%2011.21.42%20AM.png), [11.23.49](screenshots/Screenshot%202026-02-28%20at%2011.23.49%20AM.png)
- **Dock – Animation:** [11.19.15](screenshots/Screenshot%202026-02-28%20at%2011.19.15%20AM.png)
- **Dock – Upscale:** [11.23.41](screenshots/Screenshot%202026-02-28%20at%2011.23.41%20AM.png)
- **Dock – empty:** [11.24.11](screenshots/Screenshot%202026-02-28%20at%2011.24.11%20AM.png)

The **screenshots/** folder may contain additional images beyond the 15 referenced above (e.g. extra angles or variants). The table above maps the primary reference screenshots to spec sections; use them as the authoritative visual reference for layout and appearance when rebuilding the UI.

---

## 13. Additions and Omissions (for C++ Rebuild)

The following details are not fully spelled out elsewhere in this spec but exist in the repo and affect behavior or parity when rebuilding.

### 13.1 Dock Stack Order (exact indices)

The `QStackedWidget` in **ImageDiffusionWidget** adds widgets in this order (so indices match §5.1): **0** = Welcome, **1** = Generation, **2** = Upscale, **3** = Live, **4** = Animation, **5** = Custom (Graph), **6** = Custom Placeholder. The code uses this order in `diffusion.py`; the Welcome view is shown when `model is None` or not connected or update required or has news.

**update_content() logic:** When choosing which child widget to show, the dock evaluates in order:

| Condition | Stack index | Widget shown |
|-----------|-------------|--------------|
| `model is None` \| not connected \| `requires_update` \| `has_news` | 0 | Welcome |
| `model.workspace == generation` | 1 | GenerationWidget |
| `model.workspace == upscaling` | 2 | UpscaleWidget |
| `model.workspace == live` | 3 | LiveWidget |
| `model.workspace == animation` | 4 | AnimationWidget |
| `model.workspace == custom` and `settings.server_mode == cloud` | 6 | CustomWorkflowPlaceholder |
| `model.workspace == custom` | 5 | CustomWorkflowWidget |

Before showing a workspace widget (indices 1–6), the dock sets that widget’s **`model`** property to the current document’s model so the view displays the correct state. A C++ rebuild should implement the same conditions and widget assignment so view switching matches.

### 13.2 ServerState (managed server lifecycle)

The **Server** (managed ComfyUI) uses a **ServerState** enum not listed in §3: `not_installed`, `missing_resources`, `installing`, `stopped`, `starting`, `running`, `verifying`, `uninstalling`, `update_required`. These drive the Connection tab status for "Local Managed Server" (e.g. "Not installed", "Not running", "Connecting", "Connected", "Error", "Server version is outdated"). The enum uses explicit integer values (see §13.99 for the value table and **InstallationProgress** for install progress callbacks).

### 13.3 Autostart and fallback URL

When `server_mode` is **undefined**, autostart tries connection to **two** URLs: first `settings.server_url`, then **`127.0.0.1:8000`** (ComfyUI Desktop default port), with limited retries for the first and more for the second. On success, `server_mode` is set to `external` and `server_url` is updated.

### 13.4 Prompt import from image file

On creating a new **Model** for a document, **import_prompt_from_file(model)** runs (`persistence.py`). If the document was opened from an image file (`.png`, `.jpg`, `.jpeg`, `.webp`) and the root region prompts are empty, the plugin reads embedded metadata: **A1111** format (`parameters` text, split on "Negative prompt:" and "Steps:") or **ComfyUI** format (JSON in `prompt` text, CLIPTextEncode nodes). Filled prompts are applied to the model’s root region so that "open image → Generate" can reuse existing prompts.

### 13.5 CustomStyleInput (custom workflow API)

Used when evaluating custom (graph) workflows. **CustomStyleInput** (`api.py`) has: `models` (CheckpointInput), `sampling` (SamplingInput), `positive_prompt`, `negative_prompt`. It is distinct from **CustomWorkflowInput** (which holds the graph and params). Both are needed for a full C++ API surface.

### 13.6 File and FileLibrary model

**File** (`files.py`): `id`, `name`, `source` (FileSource), `format` (FileFormat), optional `path`, `hash`, `size`, `metadata`, etc. **FileSource**: `unavailable`, `local`, `remote` (flags). **FileFormat**: `unknown`, `checkpoint`, `diffusion`, **`lora`**. **FileCollection** is a list model of `File`; **FileLibrary** is a named tuple of `checkpoints: FileCollection` and `loras: FileCollection`. **FileFilter** is a proxy for filtering by availability/name. Checkpoint list and LoRA list in the UI and client model discovery rely on this. See §13.91 for **lora** in FileFormat.

### 13.7 Client and checkpoint types

- **CheckpointInfo**: `filename`, `arch`, `format` (FileFormat), `quantization` (Quantization). Optional **is_inpaint**, **is_refiner** (bool from server/model discovery); used for checkpoint list filtering (§13.75). **Quantization**: `none`, `svdq` (used for checkpoint compatibility).
- **ClientJobQueue**: generic queue; jobs are consumed in order; **put(job, front=True)** inserts at front for priority.
- **ClientFeatures**: optional feature flags from server (e.g. cloud limits, supported nodes); used by cloud and custom workflow.
- **OutputBatchMode**: `default`, `images`, `animation`, `layers` — how custom workflow output is batched.
- **JobInfoOutput**: `name`, `offset` (Point), `batch_mode`, `resize_canvas` — metadata for custom workflow result application.

### 13.8 RegionLink

**RegionLink** (`region.py`): `direct` (layer linked directly to region), `indirect` (layer in a group linked to region), `any`. Used when resolving which layers belong to a region for mask/prompt.

### 13.9 Style and StyleSettings (extra fields)

Style presets and **StyleSettings** include **VAE** (string, e.g. "Checkpoint Default"), **v_prediction_zsnr**, **rescale_cfg**, **self_attention_guidance**, **preferred_resolution**, and **linked_edit_style** (for instruction-based editing). §8.1 lists many of these; the VAE and linked-edit-style behavior (dropdown in Styles tab) should be implemented for parity.

### 13.10 Language files (actual list)

Under `ai_diffusion/language/`: `en.json`, `es-ca.json`, `es.json`, `fr.json`, `id.json`, `it.json`, `ja.json`, `ko.json`, `pt-br.json`, `ru.json`, `th.json`, `tr.json`, `zh-cn.json`, `zh-tw.json`, and `new_language.json.template`. There is no `de.json` in the repo; the Interface tab may still list "Deutsch" if translations are added elsewhere or the list is configurable. **Note:** CONTRIBUTING.md refers to the folder as "langauge"; the actual folder name in the repo is **language**.

### 13.11 Presets and resources

- **Presets** (`ai_diffusion/presets/`): `control.json`, `models.json`, `samplers.json` (control configs, model definitions, sampler presets).
- **resources.py**: **CustomNode** (named tuple for required custom nodes), **ResourceId**, **resource_id()**, **required_resource_ids** and **recommended_resource_ids** (and large model-filename maps) define "Detected base models" and missing-resource checks. A C++ rebuild needs the same resource IDs and filename mappings for each Arch/ControlMode.

### 13.12 Tests as behavioral reference

**tests/conftest.py** sets up a **QtTestApp** that runs the asyncio event loop via `QCoreApplication.processEvents()`, plus session fixtures for clearing appdata and results. Tests in **tests/** use **tests/data/** (e.g. `workflow-ui.json`, `object_info.json`), **tests/images/** (masks, regions), and **tests/references/** (reference images). These are the behavioral reference for workflow build, resolution, and image output when porting.

### 13.13 Optional / developer

- **scripts/typeshed/krita.pyi**: Stub types for the Krita Python API; useful for type checking, not required at runtime.
- **ai_diffusion/ai_diffusion.action**: Optional Krita action metadata file (referred to in §2.1).
- **docs/**: Starlight (Astro) documentation site; not needed for plugin logic but useful for UX copy and setup instructions (e.g. Custom ComfyUI Setup). See §13.116.

### 13.14 Krita annotation namespace

All document annotations are stored under the **`ai_diffusion/`** prefix when calling Krita’s API. The plugin’s `annotate(key, value)` / `find_annotation(key)` use logical keys (e.g. `"ui.json"`, `"result0.webp"`); the implementation passes **`ai_diffusion/{key}`** to Krita. A C++ rebuild must use the same prefix (e.g. `ai_diffusion/ui.json`) when reading/writing document annotations.

### 13.15 Document identity

**KritaDocument** identity is tied to the annotation **`ai_diffusion/document_id`** (a UUID). When a document is first opened, this ID is set if missing; it is used to match an existing **Model** to a document when the same file is reopened (e.g. after closing and reopening the .kra). Required for correct model–document binding in a C++ port.

### 13.16 Restore Defaults behavior

Clicking **Restore Defaults** in the Configure dialog calls `settings.restore(init=False)`. This resets all settings to their defaults **and** sets **`server_mode`** to **`ServerMode.managed`** (not `undefined`). So after restore, the effective server type is always **Local Managed Server**.

### 13.17 Performance preset (auto) and exact preset values

When **Performance Preset** is **Automatic**, **`apply_performance_preset(settings, device)`** chooses a preset from **DeviceInfo**: `device.type == "cpu"` → **CPU**; `device.type == "cloud"` → **Cloud**; `device.vram <= 6` → **GPU low**; `device.vram <= 12` → **GPU medium**; else → **GPU high**.  

**PerformancePresetSettings** (fixed values per preset, in `settings.py`):

| Preset | batch_size | resolution_multiplier | max_pixel_count | tiled_vae |
|--------|------------|------------------------|-----------------|-----------|
| cpu    | 1          | 1.0                    | 2               | false     |
| low    | 2          | 1.0                    | 2               | true      |
| medium | 4          | 1.0                    | 6               | false     |
| high   | 6          | 1.0                    | 8               | false     |
| cloud  | 8          | 1.0                    | 6               | false     |

### 13.18 ProgressKind (progress bar styling)

**ProgressKind** (model.py): **`generation`** (0), **`upload`** (1). Used to style the progress bar: when **upload**, the bar uses **`theme.progress_alt`** (e.g. amber) instead of the default highlight; otherwise it reflects generation progress. Needed for correct UI feedback during upload vs generation.

### 13.19 History storage pruning, slot allocation, and debounced save

- **Slot allocation:** History result images are stored in document annotations with keys **`result{N}.webp`** where **N** is a **slot index**. **ModelSync** maintains **`_slot_index`** (next free slot). When a job finishes, it is assigned the current **`_slot_index`** (then incremented). **\_HistoryResult** stores **slot**, **offsets** (byte offsets for multi-image blobs), **params**, **kind**, and **in_use** (dict mapping image index to whether that image is still "in use" for display/apply). On load, the maximum slot in existing history is used to reinitialize **\_slot_index** so new results do not overwrite old ones. When pruning, freed slots are not reused; new results always get new slots.
- **Pruning:** When total stored history size (in bytes) exceeds **`history_storage`** (MB) × 1024 × 1024, **ModelSync** removes the **oldest** history entries first (by slot order), deletes the corresponding **`result{slot}.webp`** annotation, and updates the in-memory history list.
- **Debounced save:** **ModelSync** does not write **ui.json** on every change. It uses a **1 second** debounce: **`_save_later()`** schedules **`_delayed_save()`**; the actual **`_save()`** runs only after 1 second with no further changes. This reduces annotation writes during rapid edits.

### 13.20 CheckpointInput and API field names

In **api.py**, **CheckpointInput** uses the field name **`checkpoint`** (str) for the checkpoint filename/identifier, not "checkpoint name". **LoraInput** uses **`name`**, **`strength`**, **`storage_id`**. These names are used in serialization and workflow build.

### 13.21 Welcome view header

The Welcome view header text is **"AI Image\nGeneration"** (two lines) in the code, with logo 64×64. The spec’s "AI Image Generation" is equivalent; for pixel-perfect parity the line break should be preserved.

### 13.22 Language list and Deutsch

The Interface tab language list is built from available **language/** JSON files plus any configured options. The repo has no **de.json**; "Deutsch" may appear only if a translation is added or the list is extended. The actual language files present are listed in §13.10 (en, es-ca, es, fr, id, it, ja, ko, pt-br, ru, th, tr, zh-cn, zh-tw, plus **new_language.json.template**).

### 13.23 ScaleMode and resolution scaling

**ScaleMode** (`resolution.py`): `none`, `resize`, `upscale_small`, `upscale_fast`, `upscale_quality`. Used when preparing diffusion input to decide how the image is scaled: **none** = no scaling; **resize** = simple downscale or small upscale (e.g. bilinear); **upscale_small** = factor &lt; 1.5; **upscale_fast** / **upscale_quality** = use fast or quality upscale model. This drives `prepare_diffusion_input` and tiled/refine workflows.

### 13.24 TileLayout (tiled processing)

**TileLayout** (`resolution.py`): Computes a grid of tiles over an image for tiled upscale/refine. Fields: `image_extent`, `tile_extent`, `min_size`, `padding`, `blending`, `tile_count`. Methods: `from_denoise_strength(extent, min_tile_size, strength, multiple)`, `start(coord)`, `end(coord)`, `coord(index)`, `bounds(index)`, `total_tiles`. Used when processing large images in tiles to avoid running out of memory.

### 13.25 Custom workflow parameter types (ParamKind and CustomParam)

**ParamKind** (`custom_workflow.py`): `image_layer`, `mask_layer`, `number_int`, `number_float`, `toggle`, `text`, `prompt_positive`, `prompt_negative`, `choice`, `style`. **CustomParam** (NamedTuple): `kind`, `name`, `default`, `min`, `max`, `choices`. Parameters are discovered from the ComfyUI graph: **ETN_KritaStyle** → style; **ETN_KritaImageLayer** / **ETN_KritaMaskLayer** → image/mask layer; **ETN_Parameter** with type "number (integer)", "number", "toggle", "text", "prompt (positive)", "prompt (negative)", "choice" → corresponding ParamKind. Names can have a group prefix (e.g. `"1. Group Name/Parameter"`) for display and ordering. The Graph view builds **IntParamWidget**, **FloatParamWidget**, **BoolParamWidget**, **TextParamWidget**, **PromptParamWidget**, **ChoiceParamWidget**, **StyleParamWidget** from these. A C++ rebuild must support the same node types and parameter kinds for custom workflow UI.

### 13.26 SamplerPreset and scheduler

**SamplerPreset** (`style.py`): `sampler` (str), `scheduler` (str), `steps` (int), `cfg` (float), `lora` (str | None), `minimum_steps` (int), `hidden` (bool). The **scheduler** is separate from the sampler (e.g. "karras"); **SamplingInput** in `api.py` has both `sampler` and `scheduler`. Style presets and sampler configuration must persist and use both for correct ComfyUI node setup.

### 13.27 Error display and ErrorBox

**Error** (`model.py`): `kind` (ErrorKind), `message` (str), optional `data`. **ErrorBox** (`ui/widget.py`): Shows the current model error; uses theme colors (e.g. red for error, yellow for warning). Special handling: **ErrorKind.insufficient_funds** shows a payment/account UI; **ErrorKind.incompatible_lora** formats a warning with LoRA details. **no_error** = `Error(ErrorKind.none, "")`. Rebuild must show errors in the dock (e.g. above Generate button) and support the same ErrorKind values and optional data for cloud/payment flows.

### 13.28 History item context menu

For each history thumbnail the context menu includes: **Copy Prompt**, **Copy Prompt (Evaluated)**, **Copy Strength**, **Copy Style** (disabled if style not found), **Copy Seed**, **Info to Clipboard**, separator, **Save Image** (disabled if document unsaved), **Discard Image**, separator, **Clear History**. Copy actions write to the active region or root prompt/strength/seed/style; Info copies diagnostics to clipboard.

### 13.28a History list UI (HistoryWidget layout and constants)

**HistoryWidget** (`ui/generation.py`) is the horizontal list of result thumbnails in Generate and Custom workflow views. For visual and behavioral parity, a C++ rebuild should match:

- **Thumbnail size:** **96×96** px (**\_thumb_size = 96**); icon size uses **theme.screen_scale(widget, QSize(96, 96))** for HiDPI.
- **Layout:** **QListWidget** with **flow** = **LeftToRight**, **viewMode** = **IconMode**, **resizeMode** = **Adjust**, **selectionMode** = **ExtendedSelection**, **frameStyle** = **NoFrame**, no horizontal scrollbar (vertical only).
- **Applied indicator:** Items that have been applied to the canvas show an overlay icon from **theme.icon_path / "star.png"** (loaded via **Image.load()**). The icon is drawn at offset **(thumb.extent.width - 28, 4)** (28 px from the right edge, 4 px from the top) for visual parity. The **in_use** state per image index is stored in **\_HistoryResult.in_use** and used for this indicator.
- **Buttons:** **Apply** button (icon **apply**, label "Apply") and **Context** (menu) button shown when an item is selected; both use **\_button_css** (border, semi-transparent background, hover). Apply triggers **item_activated** (double-click equivalent).
- **Items:** **AnimatedListItem** is used for entries that may still be loading or animating; it subclasses **QListWidgetItem** and is used when adding jobs to the list so the thumbnail can show a loading/placeholder state until the result image is ready.
- **Signals:** **item_activated** (emitted on double-click or Apply); **itemClicked** for preview, **itemDoubleClicked** → **item_activated**, **customContextMenuRequested** for the context menu (§13.28).
- **item_info(item)** returns **(job_id, index)** for the selected list item; used by **apply_result** and context menu actions.

### 13.29 Generate view: main action and inpaint menu

The Generate view has a main action menu (e.g. dropdown or toolbar) for the operation: **Generate**, **Refine**, **Edit** (instruction-based), **Refine Region**, **Edit** (per-region), **Edit (Custom)** (custom inpaint workflow). These map to **InpaintMode** (e.g. automatic for Edit/Refine) and **edit_mode** (instruction-based vs normal). The same menu may offer **Generate Region** and layer/selection usage. Rebuild should offer equivalent entries so users can switch between generate, refine, and edit without leaving the view.

### 13.30 Krita version requirement

The plugin requires **Krita 5.2.0 or newer** (see README and installation docs). The reference Python implementation **does not enforce this at runtime** (no version check in code); the minimum is documented in the README and installation guide only. A C++ port should document the same minimum and may optionally add a runtime check (e.g. on extension load) to warn or refuse when running on an older Krita.

### 13.31 WorkflowCollection and custom workflow sources

**WorkflowCollection** loads custom workflows from: (1) **local**: `user_data_dir / "workflows"` (`.json` files); (2) **remote**: `connection.workflows` (dict id → graph from server); (3) **document**: workflows embedded in the .kra (via persistence). On connect, it processes pending workflows and scans the folder; **WorkflowSource**: `document`, `remote`, `local`. **CustomWorkflow**: `id`, `source`, `workflow` (ComfyWorkflow), `path` (Path | None). Rebuild must use the same folder path and connection workflow API so that local, remote, and document workflows all appear in the Graph workspace.

### 13.32 Strength snapping (StrengthSpinBox)

**StrengthSnapping** uses **apply_strength(strength, steps, min_steps)** and **snap_to_percent** so that the strength slider/spinbox snaps to valid step boundaries (e.g. 25%, 50%, 75% corresponding to start_at_step). **StrengthSpinBox** uses **StrengthSnapping.nearest_percent** in **stepBy** (arrow keys, scroll) so that adjustments snap to the nearest valid percent; manual keyboard entry still allows any 1–100. This avoids invalid step counts when using live/animation presets with fewer steps.

### 13.33 InitialSetupWidget (first-time server choice)

When the user has not yet chosen a server type, the Connection tab (or a dedicated flow) can show **InitialSetupWidget**: title "Welcome to Image Generation in Krita", short explanation, and three options: (1) **Online Service** — "Login or Sign up"; (2) **Local Managed Server** — "Start Installation"; (3) **Custom ComfyUI** — "Connect via URL". Each sets **server_mode** and saves; the widget emits **finished(ServerMode)**. Rebuild should offer an equivalent first-run choice so users can pick cloud, managed, or custom server in one place.

### 13.34 Animation: SamplingQuality

**SamplingQuality** (`model.py`): **fast**, **quality**. Stored on the animation workspace (e.g. **AnimationWorkspace** or model sub-object); used to choose sampler/steps for animation (fast = fewer steps / live-like; quality = more steps). The Animation view exposes a "Speed/Quality" or similar dropdown (e.g. "Fast", "Quality"). This affects **workflow.create** and step counts when generating animation frames.

### 13.35 Prompt processing (comments, wildcards, attention, layers)

- **strip_prompt_comments** (`text.py`): Before building the workflow, all positive, negative, and per-region prompts are passed through **strip_prompt_comments**. Text after `#` on a line is removed unless the `#` is escaped with a backslash (`\#`). Used in `workflow.create()` so comments in the prompt editor are not sent to the model.
- **Wildcards**: Syntax `{option1|option2|option3}`; options are pipe-separated inside curly braces. **eval_wildcards(text, seed)** uses a deterministic RNG from `seed` to pick one option per occurrence; nested braces supported (e.g. `{a|{b|c}}`). Evaluated during workflow build (positive, negative, region prompts). Same seed yields same choices.
- **Attention weight editing**: Prompts can use attention syntax: `(text:weight)` or `[text:weight]` etc., with weight a number (e.g. `1.2`). **edit_attention(text, positive)** parses this (via **parse_expr** / **ExprNode**), adjusts weight by +0.1 (positive=True, e.g. Ctrl+Up) or −0.1 (Ctrl+Down), clamps to [−2.0, 2.0], and re-serializes. Bracket pairs: `()`, `<>`, `[]`, `{}`. Used in the prompt widget (e.g. **TextPromptWidget**) for Ctrl+arrow key handling.
- **extract_layers**: Pattern **`<layer:name>`** in the prompt is replaced with **"Picture {n}"** (n = 1, 2, …); the list of layer names is returned and used so the workflow can bind layer images to the correct inputs. Used when building workflows that take multiple layer images.

### 13.36 create_img_metadata and saved image metadata

**create_img_metadata(params: JobParams)** (`text.py`) builds an A1111-style metadata string for embedding in PNG when **save_image_metadata** is enabled. It includes: full prompt (with LoRA tags), negative prompt, steps, CFG scale, seed, width×height, optional strength, and LoRA list as `<lora:name:weight>`. Written when the user saves an image from the history thumbnails. A C++ rebuild should produce the same format so that "Save Image Metadata" and external tools (e.g. A1111) can read prompts and parameters from saved PNGs.

### 13.37 UpdateState and AutoUpdate

**UpdateState** (`updates.py`): `unknown`, `checking`, `available`, `latest`, `downloading`, `installing`, `restart_required`, `failed_check`, `failed_update`. **AutoUpdate** checks `default_api_url` (e.g. `https://api.interstice.cloud`) for latest version; state drives **AutoUpdateWidget** in the Welcome view (visibility and button text). **AutoUpdateWidget.is_visible** is true when `auto_update` is on and state is not `latest`, `failed_check`, or `checking`. Update package is downloaded and unpacked; on success, state becomes `restart_required`. Rebuild must implement the same states and Welcome behavior so that "Check for updates" and "Download and Install" work correctly.

### 13.38 News (client.news) and NewsWidget

**News** (`client.py`): NamedTuple **`(text, digest)`**. Digest = first 16 characters of SHA256 of text (for change detection). **Client.news** may return a **News** instance (e.g. **CloudClient** sets it from server `user_data.news`). **NewsWidget** (Welcome view) shows **news.text** when the client has news and the digest differs from what the user has seen. **last_news** (settings) stores the last seen digest; "Ok" sets `last_news = digest` and saves. Welcome view shows NewsWidget when there is unseen news and no update overlay; "Ok" hides it and refreshes. Rebuild should support the same Client.news contract and Welcome layout so server-driven announcements appear correctly.

### 13.39 Annotation lookup fallback

**_find_annotation(document, name)** (`persistence.py`): First tries **document.find_annotation(name)** (e.g. `"result0.webp"`). If missing, tries the key without extension (e.g. `"result0"`). This allows older documents that stored history images under a key without extension to still load. A C++ port should use the same fallback when reading history image annotations.

### 13.40 Installation and packaging

- **Krita version**: Plugin requires **Krita 5.2.0 or newer** (README, install docs). Document and enforce this for a C++ port.
- **Install method**: Users install via **Tools → Scripts → Import Python Plugin from File…** and select the release ZIP. The plugin is then loaded from a `pykrita` directory (or similar) per Krita’s Python plugin layout.
- **Extension check**: In **extension.py**, the plugin checks that its parent directory is named `pykrita` or that the parent has a `.git` directory; otherwise it logs a warning that the install path may break user files and settings.
- **scripts/**: In addition to §2.1: **server_requirements.in** is the source for managed server dependencies; **scripts/docker/** contains Dockerfile, nginx config, and helpers for running ComfyUI in Docker. **tests/config.py** is test-only configuration. Rebuild need not implement Docker/scripts but should be aware of server_requirements and packaging layout for parity.

### 13.41 Client abstract interface (news, user, missing_resources)

**Client** abstract base defines optional **news** → News | None, **user** → User | None, and **missing_resources** → MissingResources | None. **ComfyClient** returns None for news/user; **CloudClient** may return News from user_data and User from auth. **MissingResources** is used for "Detected base models" and optional connection blocking. A C++ rebuild should implement the same interface so that connection, cloud, and custom server clients can be swapped without changing the UI contract.

### 13.42 Document color mode

Before starting generation or upscaling, the plugin calls **document.check_color_mode()**. **KritaDocument** returns **(True, None)** only when the document color model is **RGBA** and color depth is **U8** (8-bit integer). Otherwise it returns **(False, message)** where the message is a localized string describing the mismatch (e.g. "Color model must be RGB/Alpha", "Color depth must be 8-bit integer"). The UI blocks the operation and shows this message via the model error (e.g. ErrorBox). The stub **Document** used when no document is open returns **(True, None)**. A C++ port must enforce the same check so that generation never runs on unsupported color modes.

### 13.43 Selection modifiers and mask preprocess

**get_selection_modifiers(arch, inpaint_mode, strength, min_size=256)** builds **SelectionModifiers** from settings and arch: **feather_rel** = `settings.selection_feather / 100 * strength`; **feather_min_px** = `round(settings.selection_min_transition * strength)`; **pad_rel** = `settings.selection_padding / 100`; **pad_offset_px** = `settings.selection_grow_offset`; **size_min_px** = min_size; **multiple** = `arch.latent_compression_factor` (e.g. 8); **invert** = False. For **InpaintMode.replace_background** when strength is 1.0, feather is clamped to at most 0.01 and **invert** = True. This structure is passed to **document.create_mask_from_selection(mod)** to create the selection mask and bounds.

**calc_selection_pre_process(inpaint, bounds, mods)** computes **grow**, **feather**, and **blend** (in pixels) for the workflow from **mods** and **bounds.extent.diagonal**: feather from feather_rel × size and feather_min_px (unless invert); grow = selection_grow_offset + feather//2; blend = min(settings.selection_blend, grow + feather//2). These drive denoise_mask and composite_mask in the ComfyUI graph (dilate/blur/erode). Rebuild must use the same formulas so selection edges and blending match.

### 13.44 Preview layer

The **preview layer** is the Krita layer used for live-preview or generation preview display. Its ID is stored in persistence as **preview_layer** (string) in **ui.json**. On load, **model.try_set_preview_layer(uid)** restores the layer reference if a layer with that ID exists in the document. **model.preview_layer_id** returns that layer’s ID string or "". The preview layer is optional; the UI may show a placeholder or the canvas when no preview layer is set. A C++ port should persist and restore this ID so that the user’s chosen preview layer is restored when reopening the document.

### 13.45 Live recording and animation import

In **Live** workspace, when the user enables recording, frames are written to a folder next to the document: **`{document_directory}/{document_stem}.live-frames/frame-{N}.webp`**. Frames are saved on each result update via **_save_frame**. **Import Animation** (or equivalent action) calls **document.import_animation(files, offset)** with the list of frame files and the start time index; the document creates keyframe layers (one image per frame at the given time range). In **Animation** workspace, **Full Animation** batch writes each frame to **`{document_directory}/{document_stem}.animation/frame-{frame}.png`** (PNG, not webp); when the batch completes, **document.import_animation(keyframes, start)** is called and the active layer is renamed to **"[Generated] {start}-{end}: {params.name}"**. **Single Frame** mode does not use a folder; it writes the result directly to the selected **target_layer** via **layer.write_pixels**. A C++ rebuild must implement the same folder conventions (.live-frames + webp for Live, .animation + png for Animation batch) and **import_animation** contract for parity.

### 13.46 Built-in style preset files

Built-in styles are stored under **`ai_diffusion/styles/`**. The following JSON files exist in the repo (for UI parity and default preset names): **anime-illustrious.json**, **anime-noobai-xl.json**, **chroma.json**, **cinematic-photo.json**, **cinematic-photo-xl.json**, **digital-artwork.json**, **digital-artwork-xl.json**, **flux.json**, **flux-kontext.json**, **flux-schnell.json**, **flux2-klein.json**, **qwen.json**, **qwen-edit.json**, **z-image-turbo.json**. User styles are stored in **user_data_dir / "styles"**. Style list (Styles tab and dropdown) merges built-in and user presets when **show_builtin_styles** is true.

### 13.47 Collect Diagnostics

**collect_diagnostics(redact_user=True)** (root.py) builds a single text block for the Plugin tab "Collect Diagnostics" button. Content: plugin version, Krita version, Python version, platform/architecture/processor, CUDA capability (if any), plugin dir, user data dir, settings (with **access_token** and **server_authorization** redacted), last 300 lines of client log, and if server_mode is managed the last 300 lines of server log. If **redact_user** is true, Windows username (from path) or Linux $HOME username is replaced with a placeholder. Output is truncated to fit GitHub issue body limit (65536 − 5000). The button copies this string to the clipboard and may show it in a dialog. A C++ port should produce equivalent diagnostics for support and bug reports.

### 13.48 Tag autocomplete files

Built-in tag CSV files live under **`ai_diffusion/tags/`**: **Danbooru.csv**, **Danbooru NSFW.csv**, **e621.csv**, **e621 NSFW.csv**. Settings **tag_files** stores a list of **file stems** (e.g. `["Danbooru", "e621"]`); the UI and **TagCompleterDelegate** resolve paths from the tag directory (or a user-configured folder). A C++ rebuild should support the same stems and CSV format for prompt autocomplete.

### 13.49 IntervalSlider (control layer range)

**IntervalSlider** (`ui/interval_slider.py`) is a horizontal dual-handle range slider (min/max). It is used in **ControlWidget** for control layer strength **range** (low/high). Signals: **rangeChanged(min, max)**, **intervalChanged(low, high)**, **sliderPressed(id)**, **sliderMoved(id, value)**, **sliderReleased(id)** with handle IDs **LowHandle** / **HighHandle**. Only horizontal orientation is supported. A C++ rebuild needs an equivalent widget for control layer range input.

### 13.50 Managed server: ComfyUI version and required custom nodes

The **managed** (local) server installs a specific ComfyUI revision and a fixed set of custom nodes. In **resources.py**:

- **comfy_url**: `"https://github.com/comfyanonymous/ComfyUI"`
- **comfy_version**: Git commit hash (e.g. `"fe52843fe55b92dedaabff684294dd7a115d2204"`); archive URL is `{comfy_url}/archive/{comfy_version}.zip`.
- **version**: Server/resources version string (e.g. `"1.48.0"`), used as server version identifier.

**required_custom_nodes** (list of **CustomNode**): each has **name**, **folder**, **url**, **version** (commit), **nodes** (list of node class names). Required nodes include: ControlNet preprocessors (comfyui_controlnet_aux), IP-Adapter (ComfyUI_IPAdapter_plus), External Tooling Nodes (comfyui-tooling-nodes), Inpaint nodes (comfyui-inpaint-nodes). **optional_custom_nodes** include GGUF and Nunchaku. The server installs these in order; "Detected base models" and connection blocking depend on these node names and resource IDs. A C++ rebuild that offers a managed server must use the same URLs, versions, and node lists (or document where they are defined).

**Managed server Python/runtime versions** (server.py): The managed server uses **uv** for venv and pip (installed under `path/uv` or system); **Python 3.12** for the venv. **torch** and **torchvision** are platform-specific: Linux → torch 2.8.0, torchvision 0.23.0; Windows/macOS → torch 2.9.1, torchvision 0.24.1. **nunchaku** (optional) uses version tuples (e.g. 1.2.0 with torch2.8 on Linux, torch2.9 elsewhere). These constants are used when building **server_requirements.txt** and during install. A C++ rebuild that implements the managed server install should use the same versions for compatibility with the same ComfyUI/custom node expectations.

### 13.51 Websockets bundle and import path

The plugin **requires** a bundled **websockets** Python package for ComfyUI WebSocket communication. **ComfyClient** imports it as **`from .websockets.src import websockets`**, so the bundle must be at **`ai_diffusion/websockets/src/`** (the package root is `websockets` inside `src`). **__init__.py** checks for the module with **`importlib.util.find_spec(".websockets.src", "ai_diffusion")`** and raises a clear error if missing. A C++ rebuild does not need this bundle but must implement equivalent WebSocket client behavior for the ComfyUI `/ws?clientId=...` endpoint.

**__init__.py load order:** (1) Set **`__version__`** (e.g. `"1.49.0"`). (2) If the websockets bundle is not findable, **raise ImportError** with a message directing the user to download a full release package (not just source) from the releases page. (3) Only if the **`krita`** module is importable, **export** **`AIToolsExtension`** from **`extension`** (so tests or headless environments that do not have Krita can still import the package for other modules). When Krita loads the plugin, it imports `ai_diffusion` and then uses `AIToolsExtension`; the extension constructor runs as in §2.3. A C++ rebuild need not replicate this two-step check but should ensure the plugin fails clearly when dependencies are missing and does not assume Krita API is present in all contexts (e.g. tests).

### 13.52 Document identity and copy handling

**KritaDocument** is keyed by annotation **`ai_diffusion/document_id`** (UUID string). When a document is first opened and has no such annotation, a new UUID is written via **krita_document.setAnnotation("ai_diffusion/document_id", "document unique identifier", QByteArray(id))**. **KritaDocument.active()** resolves the active document and reuses a cached **KritaDocument** instance when the same document_id is found. If the same **document_id** appears in two open documents (e.g. one is a copy of the other), the code assigns a **new** ID to the current document so each has a distinct Model (see issue #2164). A C++ port must use the same annotation key and copy-detection logic for correct model–document binding.

### 13.53 Control image creation (create_control_image and preprocessors)

**create_control_image** (`workflow.py`) builds a ComfyUI subgraph that turns a user image into a control input (e.g. depth map, pose, edges). It is used for **WorkflowKind.control_image** and when generating a control layer preview. Behavior depends on **ControlMode**:

- **hands:** Uses **MeshGraphormer-DepthMapPreprocessor** (node from ControlNet/custom nodes). Inputs: `image`, `resolution` (shortest side of extent or bounds, multiple of 64), `mask_type="based_on_depth"`, `rand_seed` (or generated). If bounds are given, image is cropped to bounds, processed, then composited back onto an empty image at the same position. Output is used like a depth control image.
- **scribble:** **PiDiNetPreprocessor** (safe="enable") then **ScribblePreprocessor** with same resolution.
- **line_art:** **LineArtPreprocessor** (coarse="disable").
- **soft_edge:** **AnyLineArtPreprocessor_aux** with `merge_with_lineart="lineart_standard"`, `lineart_lower_bound=0`, `lineart_upper_bound=1`, `object_min_size=36`, `object_connectivity=1`.
- **canny_edge:** **CannyEdgePreprocessor** (low_threshold=80, high_threshold=200).
- **depth:** **DepthAnythingV2Preprocessor** with ckpt_name `depth_anything_v2_vitb.pth`.
- **normal:** **BAE-NormalMapPreprocessor**.
- **pose:** Uses workflow helper **estimate_pose** (OpenPose-style preprocessor).
- **segmentation:** **OneFormer-COCO-SemSegPreprocessor**.

**ControlMode.is_lines** is true for scribble, line_art, soft_edge, canny_edge. When true, the result image is **inverted** (ControlNet expects white lines on black background) before scaling to target and sending. Input extent is normalized to a multiple of 64 and at least 512 before preprocessing (except for hands, which uses 64 multiple). A C++ rebuild must use the same node names and parameters so control previews and control-image jobs match ComfyUI expectations.

### 13.54 Prompt widget resize handle

The **TextPromptWidget** (prompt and negative prompt editors in the dock) can show an optional **ResizeHandle** at the bottom: a small draggable strip (e.g. 22×8 px) with a dotted pattern and vertical-resize cursor. When **is_resizable** is true, the handle is visible; the user drags it to change the effective height of the prompt area (e.g. to show more lines). The widget emits **handle_dragged(y_pos)**; the parent updates the widget height or line count from the drag. The **Prompt Line Count** setting (spinbox 1–10) still sets the initial/default size; the handle allows on-the-fly resize without opening settings. A C++ rebuild may implement an equivalent draggable handle for parity with the current UI.

### 13.55 Control preset (control.json) structure

Control layer default strength/range presets are stored in **`ai_diffusion/presets/control.json`**. Structure: top-level keys are **control mode** identifiers (e.g. `"default"`, `"reference"`, `"style"`). Each value is an object with key **`"all"`** (and optionally arch-specific keys such as **`"zimage"`**, **`"flux"`**) mapping to an **array of presets**. Each preset is an object: **`strength`** (float), **`start`** (float, 0–1), **`end`** (float, 0–1). Example: `"default"` → `"all"` → `[{ "strength": 0.7, "start": 0.0, "end": 0.5 }, { "strength": 1.0, "start": 0.0, "end": 1.0 }]`. The UI uses these when adding a control layer to prefill strength and range (min/max). A C++ rebuild should use the same file path and structure so default control layer presets match.

### 13.56 Sampler preset (samplers.json) structure

Sampler presets are stored in **`ai_diffusion/presets/samplers.json`**. Each key is a **preset name** (e.g. `"Default - DPM++ 2M"`); the value is an object: **`sampler`** (str, ComfyUI sampler name), **`scheduler`** (str, e.g. `"karras"`, `"normal"`), **`steps`** (int), **`minimum_steps`** (int), **`cfg`** (float). Optional: **`lora`** (str | null), **`hidden`** (bool). Used by the Styles tab "Quality Preset" and "Performance Preset" and by **SamplingInput** in workflow build. A C++ rebuild should read the same format for style/sampler configuration.

### 13.57 Installation and user documentation

The repo **README.md** and the **docs/** tree (and published site **docs.interstice.cloud**) contain user-facing content a C++ port should mirror or reference:

- **Installation:** Krita 5.2.0 or newer; install via **Tools → Scripts → Import Python Plugin from File…** with the release ZIP; enable dock via **Settings → Dockers → AI Image Generation**; first-run Configure for server choice.
- **Hardware support:** NVIDIA (CUDA), AMD (custom ComfyUI setup), Apple Silicon (MPS on macOS 14+), CPU (slow), XPU (Windows/Linux). Recommended: ≥6 GB VRAM for local generation.
- **Optional custom ComfyUI:** Manual or existing ComfyUI; plugin can connect via URL; list of [required extensions and models](https://docs.interstice.cloud/comfyui-setup) for compatibility.
- **Common issues / FAQ:** Linked from README and docs (e.g. common-issues).

A C++ rebuild should document the same minimum Krita version, install steps, and hardware/backend options so users get consistent guidance.

### 13.58 ComfyUI and ETN custom node class names

The plugin’s workflow build and custom workflow UI depend on the following **ComfyUI node class names**. A C++ rebuild must recognize these in **object_info** and/or emit them in built graphs for parity.

**External Tooling Nodes (ETN_, from comfyui-tooling-nodes):**  
`ETN_LoadImageCache`, `ETN_SaveImageCache`, `ETN_Translate`, `ETN_ApplyMaskToImage`, `ETN_LoadImageBase64`, `ETN_LoadMaskBase64`, `ETN_InjectImage`, `ETN_InjectMask`, `ETN_ReturnImage`, `ETN_NSFWFilter`, `ETN_BackgroundRegion`, `ETN_DefineRegion`, `ETN_ListRegionMasks`, `ETN_AttentionMask`, `ETN_TileLayout`, `ETN_ExtractImageTile`, `ETN_ExtractMaskTile`, `ETN_MergeImageTile`, `ETN_GenerateTileMask`, `ETN_ReferenceImage`, `ETN_ApplyReferenceImages`, `ETN_KritaCanvas`, `ETN_KritaSelection`, `ETN_Parameter`, `ETN_KritaImageLayer`, `ETN_KritaMaskLayer`, `ETN_KritaStyle`, `ETN_KritaStyleAndPrompt`.

**Custom workflow parameter / layer binding (for ParamKind and UI):**  
`ETN_KritaStyle` → style param; `ETN_KritaImageLayer` / `ETN_KritaMaskLayer` → image/mask layer; `ETN_Parameter` → number_int, number_float, toggle, text, prompt_positive, prompt_negative, choice (by `type` input). **ETN_KritaStyleAndPrompt** is used to detect instruction-based editing support and to bind style+prompt in custom workflows.

**Inpaint nodes (from comfyui-inpaint-nodes):**  
`INPAINT_LoadFooocusInpaint`, `INPAINT_ShrinkMask`, `INPAINT_StabilizeMask`, `INPAINT_ColorMatch`.

**Control/preprocess (controlnet_aux etc.):**  
`MeshGraphormer-DepthMapPreprocessor`, `PiDiNetPreprocessor`, `ScribblePreprocessor`, `LineArtPreprocessor`, `AnyLineArtPreprocessor_aux`, `CannyEdgePreprocessor`, `DepthAnythingV2Preprocessor`, `BAE-NormalMapPreprocessor`, `OneFormer-COCO-SemSegPreprocessor`. For pose: **`DWPreprocessor`** (used by **estimate_pose**; inputs: image, resolution, detect_hand/detect_body/detect_face, bbox_detector, pose_estimator).

**Other:**  
`GrowMask` (comfy_extras), `ImageUpscaleWithModel`, and standard ComfyUI loader/sampler/KSampler node names as used in **comfy_workflow.py**. The **required_custom_nodes** and **optional_custom_nodes** lists in **resources.py** define the exact repos and node names used for "Detected base models" and managed server install; a C++ rebuild should keep those in sync or document where they are defined.

### 13.59 WebSocket client_id

The ComfyUI WebSocket URL is **`{base_url}/ws?clientId={client_id}`**. The **client_id** is a **UUID string** generated when the client instance is created and **reused for the entire session**. The same **client_id** is sent in the **prompt** API body (`"client_id"`) and in **queue** / **api/etn/workflow/subscribe** bodies. ComfyUI uses it to associate WebSocket messages with the correct client. A C++ rebuild must generate and persist one UUID per client and pass it consistently in the WebSocket URL and all relevant HTTP requests.

### 13.60 models.json preset structure (managed server)

The file **`ai_diffusion/presets/models.json`** defines required and optional models for the managed server and "Detected base models" checks. Structure:

- **Top-level keys:** `"required"`, optionally `"recommended"` (each an array of model entries).
- **Each model entry:** `id` (string or array of IDs, e.g. `"clip_vision-ip_adapter-sd15"` or `["clip_vision-ip_adapter-sd15", "clip_vision-ip_adapter-sdxl"]`), `name` (display name), `files` (array of file objects).
- **Each file object:** `path` (relative path under ComfyUI root), `url` (download URL), `sha256` (optional checksum); or nested entries with `id`, `path`, `url`, `sha256` for multi-file resources (e.g. upscaler 2x/3x/4x).
- **Alternatives:** Optional `alternatives` array of path strings for alternate locations the server may already have.

**resources.py** loads this file and uses it for resource resolution and missing-resource reporting. A C++ rebuild that implements a managed server or "Detected base models" must parse the same structure.

### 13.61 Theme color values (visual parity)

**theme.py** derives **is_dark** from the application palette (`Window` role lightness &lt; 128). Semantic colors used in UI (status, errors, highlights) are:

| Constant      | Dark theme   | Light theme  |
|---------------|--------------|--------------|
| base         | From palette (Base) |
| green        | `#30b030`    | `#209020`    |
| yellow       | `#c0c030`    | `#706020`    |
| red          | `#d07a40`    | `#c07630`    |
| grey         | `#888`       | `#606060`    |
| highlight    | `#8df`       | `#357`       |
| progress_alt | `#a16207`    | `#ca8a04`    |
| active       | From palette (Highlight) |
| line         | Background.darker(120) |
| line_base    | Base.darker(120) |

**flat_combo_stylesheet** (for combo boxes): transparent background, no border, padding `1px 12px 1px 2px`, selection color = **highlight**. Used for style/context/inpaint dropdowns. A C++ rebuild should use these values (or equivalent palette-derived logic) so status text and combo styling match.

### 13.62 WorkflowInput serialization and cost

- **Serialization:** **WorkflowInput** supports **to_dict(image_format, max_image_size)** and **from_dict(data)**. **Serializer** replaces **Image** references in the object graph with integer indices; when **image_format** is set, it collects images into an **ImageCollection** and adds to the output **image_data: { bytes, offsets }** (single blob + byte offsets per image). **Deserializer** reconstructs **WorkflowInput** and resolves image indices from **ImageCollection.from_bytes(blob, offsets)**. Used when sending workflows to the server (e.g. cloud) or persisting. **max_image_size** &gt; 0 triggers validation that no extent exceeds that size (raises **ValueError**).

- **Cost:** **WorkflowInput.cost** is a computed property used for cloud token estimation and UI (e.g. **GenerateButton** shows cost on hover when **model.estimate_cost(kind)** &gt; 0). Formula uses **passes_count**, **diffusion_extent**, **sampling.actual_steps**, and an arch-dependent **base cost** (e.g. sd15=1, sdxl-like=2, flux-like=4, zimage=6). A C++ rebuild that supports cloud should implement the same formula so "Insufficient funds" and cost display match.

### 13.63 Image and geometry types (image.py)

- **Point:** `x`, `y`; `__add__`, `__sub__`, `__mul__`, `__floordiv__`; **clamp(bounds)**.
- **Extent:** `width`, `height`; **at_least(min_size)**, **multiple_of(multiple)**, **scale_keep_aspect(target)**, **scale_to_pixel_count(pixel_count)**; properties **longest_side**, **shortest_side**, **pixel_count**, **diagonal**; **from_points**, **from_qsize**, **Extent.largest**, **Extent.min**, **Extent.ratio**.
- **Bounds:** `x`, `y`, `width`, `height`; **from_extent**, **from_points**, **from_qrect**; **scale**, **pad(padding, min_size, multiple, square)**, **clamp(extent)**, **restrict(within)**, **expand(include)**, **apply_crop(image_bounds)**, **at_least(min_size)**, **minimum_size(min_size, max_extent)**, **intersection**, **union**; **relative_to(reference)**; property **extent**.
- **Image:** Wraps **QImage**; **load(path)**, **create(extent, fill)**, **from_packed_bytes(data, extent, channels)**, **from_base64(data)**, **from_bytes(buffer, format)**; **copy(image)**; properties **width**, **height**, **extent**, **is_rgba**, **is_mask**. **ImageCollection** aggregates images and provides **to_bytes(format)** / **from_bytes(blob, offsets)** for workflow serialization (format from **ImageFileFormat**).

These types are used throughout **resolution.py**, **workflow.py**, and **document.py**. A C++ rebuild should provide equivalent value types and helpers so bounds, padding, and image handling match.

### 13.64 ai_diffusion.action file format

The optional **ai_diffusion.action** file is **XML**: root **ActionCollection** with **version** (e.g. `2`), **name** (e.g. `"Scripts"`); inside **Actions** with **category** **Scripts**, each **Action** has **name** (e.g. `ai_diffusion_generate`), **text** (display string, e.g. "Generate image"), **icon**, **shortcut**, **isCheckable**, **activationFlags**, **activationConditions**. In the repo, **all actions except ai_diffusion_settings** use **activationFlags** = **10000** and **activationConditions** = **0** (Krita-specific; 10000 is often used for script actions); **ai_diffusion_settings** uses **activationFlags** = **0** and **activationConditions** = **0** (see §13.151). Krita uses this file to show action labels and shortcuts in menus when the file is present. The spec table in §10.1 matches the **text** values in this file. A C++ rebuild can replicate the same action IDs and display strings; if shipping XML, use the same activationFlags/activationConditions for parity.

### 13.65 Cloud image transfer (optional)

For the **cloud** (Online Service) client, large input images may be sent via a separate transfer path rather than inline base64: the backend can expose an upload URL (e.g. R2 or similar) and the workflow payload references the transfer URL. The repo’s **tests/test_image_transfer.py** (when **service/pod/lib** exists) exercises **image_transfer.send_images** / **receive_images** with size thresholds (**max_b64_size**, **max_inline_size**). A C++ rebuild that implements the cloud client should support the same contract (inline vs transfer by size) if the cloud API expects it; otherwise this is backend-specific and can be omitted for a local-only or custom-server rebuild.

### 13.66 User data directory resolution

**user_data_dir** is resolved in **util.py** as follows. When the **krita** module is not importable (e.g. running tests or headless), it is **`plugin_dir.parent / ".appdata"`** (created if missing). When running inside Krita: use **QStandardPaths.AppDataLocation**; if that path exists and contains **"krita"** in its name, use **`{AppDataLocation} / "ai_diffusion"`**; otherwise use **QStandardPaths.GenericDataLocation** and **`{GenericDataLocation} / "krita-ai-diffusion"`**. **log_dir** is **user_data_dir / "logs"**. Legacy log migration: on first run, if **plugin_dir / ".logs"** exists, move its contents into **log_dir** and remove the old folder (for compatibility with v1.14 and earlier). A C++ rebuild should use the same platform paths and subfolder names so settings, server install, styles, and logs end up in the expected locations.

### 13.67 Optional debugpy (developer mode)

If the path **ai_diffusion/debugpy/src** exists at extension load time, the extension **inserts** that path into **sys.path** and, on successful **import debugpy**, calls **debugpy.listen(("127.0.0.1", 5678), in_process_debug_adapter=True)** and logs "Developer mode: debugpy listening on port 5678". This allows attaching an external debugger (e.g. VS Code with debugpy) to the plugin. No user-facing behavior changes; a C++ rebuild may omit this or provide an equivalent developer-only hook.

### 13.68 SwitchWidget (toggle control)

**SwitchWidget** (`ui/switch.py`) is a checkable **QAbstractButton** used as an on/off toggle in the UI. It is used for: LoRA enable/disable in the Styles tab; "Use Prompt" in the Upscale refine section; toggle-type settings in **SettingsTab**; and custom workflow boolean parameters. Behavior: **setCheckable(True)**, fixed size policy; painted as a rounded track with a sliding thumb (highlight/dark when checked, thumb position animated on click, 120 ms). **is_checked** / **is_checked setter** map to **isChecked()** / **setChecked()**. A C++ rebuild should provide an equivalent toggle control where the spec or UI lists "toggle" or "Switch" so layout and behavior match.

### 13.69 Krita document annotation API (description parameter)

**KritaDocument.annotate(key, value)** calls Krita's **setAnnotation** with three arguments: **name** = **`"ai_diffusion/" + key`**, **description** = **`"AI Diffusion Plugin: " + key`**, and **value** (QByteArray). **find_annotation(key)** and **remove_annotation(key)** use **`ai_diffusion/` + key** for the name. The description string is optional metadata for the host; a C++ rebuild can use the same name prefix and description pattern for consistency and tooling.

### 13.70 Test harness and pytest

Tests live in **tests/** and use **pytest**. **conftest.py** provides: **QtTestApp** (wraps **QCoreApplication**, drives **eventloop.setup()** and **eventloop.run(coro)** with **processEvents()** until the coroutine completes); **clear_appdata** and **clear_results** session fixtures that assume **user_data_dir** is **.appdata** (under the project) so tests do not touch real user data. **tests/config.py** defines **result_dir**, **data_dir**, **image_dir**, **reference_dir**, **server_dir** (or **AI_DIFFUSION_TEST_SERVER_DIR**), and **default_checkpoint** per **Arch**. Pytest options: **--test-install**, **--cloud**, **--ci**, **--benchmark**. Test collection order is modified so workflow tests run in a defined order (e.g. sd15 before sdxl before flux). When porting or reimplementing, use **tests/data/** (e.g. **workflow-ui.json**, **object_info.json**), **tests/images/** (masks, region assets), and **tests/references/** (expected output images) as behavioral and regression references.

### 13.71 Missing resources display (Connection tab)

When the client reports **MissingResources**, the Connection tab (Custom Server panel) shows one of two formats. **(a) List format** (missing custom nodes): heading "The following ComfyUI custom nodes are missing or too old", then a `<ul>` of nodes with name and URL; then "Please install or update the custom node package…" and "If nodes are still missing, check the ComfyUI output at startup for errors." **(b) Dict format** (missing models per arch): if any **Arch.all** (common) resources are missing, first a "Missing common models (required):" list with resource name and file names; then "Detected base models:" with one `<li>` per architecture (skip **Arch.all** and **Arch.illu_v**): **&lt;arch&gt;**: either "supported" (no missing) or "missing &lt;comma-separated component names&gt;". After both, append: "See [Custom ComfyUI Setup](link) for required models. Check the client.log file for more details." Style: grey text when connected, default when error. A C++ rebuild should produce the same structure and wording for parity.

### 13.72 Generate action dispatch (per-workspace)

The **ai_diffusion_generate** action (keyboard/shortcut or menu) calls **actions.generate()**, which dispatches by the **active document's workspace**: **generation** → `model.generate()`; **upscaling** → `model.upscale_image()`; **live** → `model.generate_live()`; **animation** → `model.animation.generate()`; **custom** → `model.custom.generate()`. If there is no active document, nothing runs. A C++ rebuild must wire the generate action to the same per-workspace handlers so that "Generate" in each workspace does the correct operation.

### 13.73 Welcome view connection status strings

The **ConnectionWidget** in the Welcome view shows a status label and optional error label. For UI and localization parity, use these (or their translated equivalents): **disconnected / error:** "Not connected to server."; **error:** error line "Connection attempt failed! Click below to configure and reconnect." (yellow); **managed + disconnected (not update_required):** "Server is not installed or not running. Click below to start."; **managed + disconnected + update_required:** "Server version is outdated. Click below to upgrade."; **auth_missing:** "Not signed in. Click below to connect."; **connecting:** "Connecting to server..."; **discover_models:** "Discovering models" + " (current/total)"; **connected:** "Connected to server at {url}. Create a new document or open an existing image to start!" (error label hidden). The **Configure** button opens the Configure Image Diffusion dialog.

### 13.74 Animation workspace (AnimationWorkspace, folder, Single Frame vs Full Animation)

- **AnimationWorkspace** (model.py): Sub-object of **Model**; properties **sampling_quality** (SamplingQuality.fast | quality), **target_layer** (QUuid — layer that receives Single Frame output), **batch_mode** (True = Full Animation, False = Single Frame). Persisted in **ui.json** under key **animation** via Property/serialize. Signals: **sampling_quality_changed**, **target_layer_changed**, **batch_mode_changed**, **target_image_changed** (emitted when a frame result is ready for preview).
- **Folder convention:** **Full Animation** uses **`{document_directory}/{document_stem}.animation/`** (not .live-frames). Frame files are **`frame-{frame}.png`** (PNG). Document must be saved before starting a Full Animation batch.
- **Single Frame:** No folder; result is written to **target_layer** via **Layer.write_pixels(image, bounds, make_visible=False)**. Generation uses **document.current_time**; if the timeline changes before the job finishes, the plugin can report "Generated frame does not match current time".
- **Full Animation:** One **JobKind.animation_batch** job per keyframe in **document.playback_time_range**; **animation_id** (UUID) groups them. Frames are appended to **keyframes** list; when **frame == end**, **import_animation(keyframes, start)** is called and the new layer is renamed **"[Generated] {start}-{end}: {params.name}"**. Cached execution (same image as previous frame) reuses the previous frame file path in the keyframes list.
- **Animation view UI:** **AnimationWidget** (ui/animation.py): target_layer dropdown populated from **model.layers.images**; visible only when **batch_mode** is False. Preview area shows **target_image_changed** image scaled to fit. Rebuild must support **playback_time_range** and **current_time** on Document for correct behavior.

### 13.75 Checkpoint list filtering (model discovery)

When building the checkpoint list from ComfyUI **object_info** or **api/etn/model_info**, the plugin **excludes** entries where: **arch** is unknown (None); **is_inpaint** is true and arch is not Flux (inpaint-only checkpoints other than Flux are not shown in the main list); **is_refiner** is true (refiner-only checkpoints are omitted from the style checkpoint dropdown). This keeps the Styles tab and workflow checkpoint selection to base/inpaint Flux models. A C++ rebuild should apply the same filters so the checkpoint list matches.

### 13.76 Python and runtime requirements

The project uses **Python 3.10 or newer** (`pyproject.toml`: `requires-python = ">=3.10"`). A C++ rebuild does not depend on Python but should document equivalent runtime requirements (e.g. Qt/Krita API version, C++ standard). The plugin is loaded by Krita's Python interpreter; extension and dock registration happen at import time (top-level calls in `extension.py`).

### 13.77 Release packaging (scripts/package.py)

The release ZIP is built by **scripts/package.py**. It includes: **ai_diffusion.desktop**; the **ai_diffusion/** directory (excluding `.*`, `*.pyc`, `__pycache__`, **debugpy**); **scripts/download_models.py** copied into the plugin dir; the bundled **websockets** package; and optional precheck steps (translation update, **server_requirements.txt** from **server_requirements.in**). A C++ rebuild would ship equivalent assets: desktop manifest, plugin binary/resources, icons, styles, language, presets, tags, and any bundled runtime. The **ai_diffusion.desktop** file is at the repo root; its exact content is given in §2.2.

### 13.78 Welcome view accepted signal and dock refresh

**WelcomeWidget** emits **accepted** when the user dismisses the news dialog (clicks "Ok") or completes an action that should refresh the dock (e.g. after initial setup). The **ImageDiffusionWidget** connects **self._welcome.accepted.connect(self.update_content)** so that after the user acknowledges news or setup, the dock re-evaluates whether to show Welcome or a workspace (e.g. if connection is now ready). A C++ rebuild should emit an equivalent signal from the welcome view and connect it to the dock's content update so the UI switches correctly.

### 13.79 Krita document annotation API (exact names)

**KritaDocument** uses the following Krita document API for annotations: **setAnnotation(name, description, value)** with `name = "ai_diffusion/" + key`, `description = "AI Diffusion Plugin: " + key`, and `value` as QByteArray; **annotation(name)** to read (returns QByteArray; empty size is treated as missing); **removeAnnotation(name)** to delete. A C++ port must use the same name prefix and, if the host API supports it, the same description pattern for tooling and consistency.

### 13.80 Server/resources version vs plugin version

**resources.py** defines a **version** string (e.g. `"1.48.0"`) used as the server/resources version identifier (managed server, custom nodes, model lists). The plugin **__version__** (e.g. `"1.49.0"`) in **ai_diffusion/__init__.py** is the user-facing plugin version. They can differ: not every plugin release requires a server update. The managed server stores its version in **path/.version**. A C++ rebuild should keep the same distinction (plugin version vs server/resources version) where applicable.

### 13.81 Autostart failure and server_mode fallback

When **server_mode** is **undefined**, **root.autostart()** tries to connect to **settings.server_url** then **127.0.0.1:8000** (ComfyUI Desktop default). If connection **succeeds**, it sets **server_mode** to **external** and **server_url** to the working URL and saves. If **all attempts fail**, it sets **connection.state** to **disconnected**, **connection.error** to **""**, and **settings.server_mode** to **ServerMode.cloud** (so the Configure dialog shows cloud as the selected option and the user can switch to Online Service). A C++ rebuild should replicate this fallback so first-run and autostart behavior match.

### 13.82 Event loop helpers (wait_until, process_events)

**eventloop.py** provides **run_until_complete(future)** (blocks until the future completes) and **wait_until(condition, iterations, no_error)** (async poll with sleep). **process_events()** is the 20 ms timer callback that drives the asyncio loop. Tests use **run_until_complete** and **wait_until** for synchronous-style tests. A C++ port that uses async or a similar event loop should provide equivalent blocking and polling helpers where needed for tests or sync code paths.

### 13.83 ControlMode.hands and test coverage

**ControlMode.hands** is implemented in **create_control_image** via **MeshGraphormer-DepthMapPreprocessor**. In **tests/test_workflow.py**, the **test_create_control_image** parametrized test **skips** **ControlMode.hands** with the message "No longer supported". Reference images **test_create_hand_refiner_image_*.png** exist in **tests/references/** from when hands were fully supported. A C++ rebuild may implement hands control for completeness; the test skip indicates that in the current repo, hands control image generation is not actively validated.

### 13.84 X-Krita-Manual and manual.html

The **ai_diffusion.desktop** file sets **X-Krita-Manual=manual.html**. This path is relative to the plugin directory. The repo does **not** ship a **manual.html** file; the plugin works without it. If present, Krita can offer a "Manual" or help entry that opens this file. A C++ rebuild may omit it or ship an equivalent manual (e.g. HTML or link to docs).

### 13.85 Seed type and display

**Seed** is stored as an **integer** on the model (`Property(0, persist=True)` in **model.py**). **workflow.generate_seed()** returns a new random integer seed. When **fixed_seed** is **false**, each generation uses a new seed from **generate_seed()**; when **true**, the stored **model.seed** is used. The UI displays it as "Seed: &lt;integer&gt;" (e.g. "Seed: 12345678"); the dice icon triggers **generate_seed()** to pick a new value. Seeds are passed through **JobParams** and **SamplingInput** and used for wildcard evaluation and reproducibility. A C++ rebuild should use the same integer type and persistence so seeds round-trip correctly in **ui.json**.

### 13.86 Queue mode behavior (back, front, replace)

**QueueMode** controls where new jobs are placed in the per-document queue and whether the queue is cleared first. **back**: new jobs are **appended** to the queue (default). **front**: new jobs are **inserted at the front** (submitted with `front=True` so they run before previously queued jobs). **replace**: the client queue is **cleared** (e.g. ComfyUI **queue** delete) before the new job(s) are added; effectively "replace queue then add". The Generate view exposes these as "at the Back", "in Front (new jobs first)", and "Replace Queue". Animation and other workspaces may not expose queue mode (e.g. **supports_batch=False**). A C++ rebuild must apply the same semantics when enqueueing so cancel/order behavior matches.

### 13.87 Development and build reference (optional)

The Python project uses **pyproject.toml** (metadata, **requires-python** = `>=3.10`), **requirements.txt** (dev/test deps), **ruff** and **black** for lint/format, **pyright** for type checking, and **pytest** for tests (**tests/**, **conftest.py**). Release packaging is via **scripts/package.py**. **CONTRIBUTING.md** at repo root covers issue reporting, translations, and pull requests (see §13.111). A C++ rebuild does not need this tooling; use **tests/** and **scripts/** as behavioral and packaging references (e.g. test data in **tests/data/**, **tests/images/**, **tests/references/**).

### 13.88 Root class and model lifecycle

**Root** (`root.py`) is the singleton application root. It exists once and holds: **\_server** (Server), **\_connection** (Connection), **\_files** (FileLibrary), **\_workflows** (WorkflowCollection), **\_models** (list of **PerDocument**: model + optional ModelSync), **\_null_model** (Model with stub Document when no document is open), **\_recent** (RecentlyUsedSync), **\_auto_update** (AutoUpdate). **init()** creates these and connects **connection.message_received** and **connection.models_changed**. **prune_models()** removes entries whose **document.is_valid** is false. **create_model(doc)** builds a new Model for a KritaDocument, appends PerDocument, calls **RecentlyUsedSync.track(model)**, attaches **ModelSync(model)**, runs **import_prompt_from_file(model)**, emits **model_created**, and returns the model. **model_for_active_document()** calls **prune_models()**, gets **KritaDocument.active()**; if none, returns None; else finds an existing model for that document or calls **create_model(doc)** and returns it. **active_model** returns that model or **\_null_model**. **autostart(callback)** tries managed server start (if mode is managed and server stopped), then **connection._connect** with appropriate URL/mode/token; on undefined server_mode it tries server_url then 127.0.0.1:8000 as in §13.3. A C++ rebuild must implement the same lifecycle so one Model per open document and correct switching when the active document changes.

### 13.89 Connection class (client selection and flows)

**Connection** (`connection.py`) holds **\_client** (Client | None), **\_task** (asyncio.Task | None), **\_workflows** (dict for remote workflows), **\_temporary_disconnect**, **error_kind** (str), **missing_resources** (MissingResources | None). Properties: **state**, **error**, **progress**; **client** (returns client when connected, asserts); **client_if_connected** (returns **\_client**, may be None — use this in UI and style resolution so code safely checks "if client := connection.client_if_connected"); **user** (delegates to client.user when connected). Signals: **state_changed**, **error_changed**, **progress_changed**, **models_changed**, **message_received**, **workflow_published**. **\_connect(url, mode, access_token)** sets state to connecting, then: for **ServerMode.cloud** with empty token sets **state = auth_missing**; else **CloudClient.connect** or **ComfyClient.connect**; on success runs model discovery and sets **state = connected**. **sign_in()** runs **\_sign_in(CloudClient.default_api_url)**: sets **\_client = CloudClient**, **state = auth_requesting**, opens browser URL from **client.sign_in()**, then receives **access_token** and calls **\_connect**. **disconnect()** cancels **\_task**, clears **\_client**, sets state to disconnected. A C++ rebuild should mirror this so cloud sign-in, custom URL connect, and managed server connect/disconnect behave the same.

### 13.90 PromptHeader (region UI)

**PromptHeader** (`ui/region.py`): enum **full**, **icon**, **none**. Used by **ActiveRegionWidget** and **RegionPromptWidget** to control the header style: **full** = full text header visible when regions exist; **icon** = icon-only header; **none** = no header. Live and Custom workflow views use **PromptHeader.icon** or **none** for compact layout. Rebuild should support the same options for region prompt UI.

### 13.91 FileFormat (files.py) — lora

**FileFormat** in **files.py** includes **lora = 3** in addition to **unknown**, **checkpoint**, **diffusion**. LoRA files are listed and filtered separately in the UI and client model discovery. A C++ rebuild should include **lora** in the FileFormat enum when implementing FileLibrary and checkpoint/LoRA lists.

### 13.92 Queue popup and queue button

The Generate view (and others that support batch) use **QueueButton** and **QueuePopup** (`ui/widget.py`). **QueuePopup** is a QMenu with items: add job "at the Back", "in Front (new jobs first)", "Replace Queue", corresponding to **QueueMode.back**, **front**, **replace**. **QueueButton** opens this popup and shows the current mode label. **supports_batch** controls whether the popup is shown; Animation workspace uses **supports_batch=False**. Rebuild should provide the same menu options and labels for queue ordering.

### 13.93 Document polling (selection and timeline)

**KritaDocument** runs a **QTimer** (e.g. 20 ms) that calls **\_poll()**. In **\_poll()**: if document is **valid** (**is_valid** — document still in **Krita.instance().documents()**), it gets **selection_bounds** from the document selection; if different from **\_selection_bounds**, it updates and emits **selection_bounds_changed**. It also reads **current_time**; if changed from **\_current_time**, it updates and emits **current_time_changed**. When the document is no longer valid (e.g. closed or removed), **\_poll()** calls **\_poller.stop()** so no further signals are emitted. The timer therefore runs only while the document instance exists and is valid. This drives reactive updates for selection-based generation and Animation Single Frame / Live. A C++ port should use the same or similar poll interval, the same stop-when-invalid behavior, and emit the same signals so the dock and model stay in sync with the canvas and timeline.

### 13.94 Save image file name template

**save_image_file_name_format** (settings) is a string template for filenames when saving from history. Default: **`"{document_name}-generated-{job_timestamp}-{job_index}-{prompt}"`**. When saving, the code uses **str.format()** with: **document_name** (document stem), **job_timestamp** (e.g. ISO format), **job_index** (1-based), **prompt** (trimmed). Rebuild should support the same placeholders so saved filenames are consistent and user-customizable.

### 13.95 Custom ProgressBar widget (generation vs upload)

**ProgressBar** (`ui/generation.py`) subclasses **QProgressBar** and uses **model.progress_kind** (§13.18): when **ProgressKind.upload**, the bar uses **theme.progress_alt** (e.g. amber) for upload progress; otherwise it uses the default progress color. The same widget is reused in Custom workflow view. Rebuild should apply the same styling so users can distinguish generation progress from upload progress.

### 13.96 VerificationState and model integrity (managed server)

**VerificationState** (`resources.py`): **not_verified**, **in_progress**, **verified**, **mismatch**, **error**. **VerificationStatus** (NamedTuple): **state** (VerificationState), **resource** (ModelResource), optional **actual_sha256** or **error** (str). Used when the managed server verifies installed models (e.g. **verify_model_integrity**, **Server.fix_models**). **mismatch** means file checksum differs from expected; **error** means read/check failed. A C++ rebuild that implements managed server install and "Detected base models" should support the same verification states and status for integrity checks and fix flows.

### 13.97 Style filtering on connect

When **Connection.state** becomes **connected**, each **Model**’s **\_init_on_connect** runs: it calls **filter_supported_styles(Styles.list().filtered(), client)** to get styles supported by the current client (arch, resources). If the current **model.style** is not in that list and the list is non-empty, the model’s style is reset to the first supported style. If **model.upscale.upscaler** is empty, it is set to **client.models.default_upscaler**. Rebuild should apply the same filtering so that after connect only supported styles and upscalers are shown/used.

### 13.98 PoseLayers and pose control from vector layers

**PoseLayers** (`document.py`) is a process-wide singleton that tracks vector layers used for pose control. It maintains a **Pose** per vector layer (keyed by layer `uniqueId()`), runs a **QTimer** every **500 ms** to refresh pose data from the layer's SVG shapes, and updates the **Pose** via **Pose.update(shapes, resolution)**. **add_character(layer)** (called from the control UI "Add Pose" flow) creates or gets the **Pose** for that vector layer, generates default SVG with **Pose.create_default(extent, people_count).to_svg()**, adds shapes via **layer.addShapesFromSvg(svg)**, then runs **_update** so the pose stays in sync. **KritaDocument.add_pose_character(layer)** delegates to this singleton's **add_character**. The control UI (**ControlWidget**, **ControlListWidget**) calls **root.active_model.document.add_pose_character(self._control.layer)** when the user adds a pose from the current control layer. A C++ rebuild must provide equivalent pose-from-vector-layer handling and timer-driven updates so pose control layers behave the same.

### 13.99 ServerState enum values and InstallationProgress

**ServerState** (`server.py`) uses explicit integer values; if a C++ port ever serializes or stores these, use the same values:

| State             | Value |
|-------------------|-------|
| not_installed     | 0     |
| missing_resources  | 2     |
| installing        | 3     |
| stopped           | 4     |
| starting          | 5     |
| running           | 6     |
| verifying         | 7     |
| uninstalling      | 8     |
| update_required   | 9     |

**InstallationProgress** (NamedTuple): **stage** (str), **progress** (DownloadProgress or (current, total) tuple or None), **message** (str). The managed server uses this for install/update progress callbacks so the Connection tab can show stage and progress. A C++ rebuild that implements the managed server should use the same structure for progress UI.

### 13.100 Optional scripts (file_server, images)

- **scripts/file_server.py**: Optional test helper. Simple HTTP server that serves model files from a local directory (e.g. after **download_models.py**). Used with **HOSTMAP=1** to replace remote URLs for install testing. Not required for plugin behavior; useful for development and CI.
- **scripts/images.py**: Optional image/build helper. Not required for plugin runtime. Documented here so a C++ rebuild is aware these scripts exist but need not be reimplemented.

### 13.101 ComfyWorkflow graph structure and UI workflow format

The **ComfyWorkflow** class (`comfy_workflow.py`) builds the graph sent to the ComfyUI prompt API and supports loading workflows from the **UI workflow format** (e.g. saved from ComfyUI or embedded in documents).

**API format (prompt API payload):** **`root`** is a dict mapping **node ID string** (`"1"`, `"2"`, …) to **`{ "class_type": str, "inputs": dict }`**. Each key is a string; node IDs are assigned sequentially (1, 2, …) via **`add()`**. **Inputs** may be primitives (int, float, bool, str) or **links** represented as **`[node_id, output_slot]`** (e.g. `[3, 0]` = output 0 of node 3). **Output** is a NamedTuple **`(node: int, output: int)`**. **ComfyNode** is **`(id, type, inputs)`** with helpers **`input(key, default)`** and **`output(index)`**. **ComfyWorkflow** methods: **`add(class_type, output_count, **inputs)`** (normalizes Output to `[node, output]`, adds default values from **node_defs**, assigns next node ID, returns Output or tuple of Outputs); **`add_cached(class_type, output_count, **inputs)`** (deduplicates identical node+inputs); **`node(node_id)`**, **`find(type)`**, **`find_connected(output)`**; **`import_graph(existing, node_defs)`** (takes API-format dict, reorders by dependency, clones into new workflow); **`from_dict(d)`** / **`to_dict()`** for serialization. **node_count** and **sample_count** are set for progress; **images** and **image_data** hold image refs for upload. A C++ rebuild must produce the same **root** structure and link format so ComfyUI accepts the prompt.

**UI workflow format:** Used when loading/saving custom workflows from `.json` files or document-embedded workflows. Structure: **`version`** (number), **`nodes`** (array of node objects), **`links`** (array of link tuples). Each **node** has: **`id`** (int), **`type`** (str, e.g. `"GrowMask"`, **`PrimitiveNode`**), **`pos`**, **`size`**, **`flags`**, **`order`**, **`mode`**, **`inputs`** (array of **`{ "name", "type", "link" }`** where **link** is link id or null), **`outputs`** (array with **`name`**, **`type`**, **`links`** = list of link ids), **`properties`**, **`widgets_values`** (array of widget values for INT/FLOAT/BOOL/STRING/COMBO inputs). **PrimitiveNode** has **`widgets_values`** with a single value and is used for constants; it is not converted to the API graph but its value is inlined where links reference it. **links** is an array of **`[link_id, source_node_id, source_output_slot]`**; when an input has **`connection["link"]`** = link_id, the corresponding link entry gives the source node and output slot for **`inputs[field_name] = [link[1], link[2]]`**. **`_convert_ui_workflow(w, node_defs)`** converts this UI format to the API format (dict by node id → **class_type** + **inputs**); it requires **node_defs** (ComfyObjectInfo) to map widget order to field names. A C++ rebuild that loads/saves custom workflow files or document workflows must support the same UI format and conversion so workflows round-trip correctly. See **tests/data/workflow-ui.json** for an example.

### 13.102 SelectionModifiers.square and .invert (create_mask_from_selection)

**SelectionModifiers** (§9.7, §13.43) includes **square** (bool) and **invert** (bool). In **KritaDocument.create_mask_from_selection(mod)** (`document.py`): if **mod.invert** is true, the document selection is **inverted** (e.g. **selection.invert()**) before the mask is created. The **bounds** (and padding) are then computed with **square=mod.square** so that when **square** is true, the resulting bounds are forced to a square (e.g. via the **pad** or bounds helper that accepts **square**). A C++ rebuild must apply **invert** to the selection and pass **square** into the bounds/padding logic so selection-based generation and inpaint behave the same.

### 13.103 Custom workflow validation (single ETN_KritaStyleAndPrompt)

**CustomWorkspace** (`custom_workflow.py`) validates the selected custom workflow in **\_validate_workflow(wf)**. The workflow must contain **at most one** node of type **ETN_KritaStyleAndPrompt**. If **style_and_prompt_node_count > 1**, **validation_error** is set to the localized message: **"Workflow contains multiple 'Krita Style & Prompt' nodes, but only one is allowed."** Otherwise **validation_error** is cleared. This message is shown in the Graph (Custom Workflow) view so the user cannot run a workflow with multiple style/prompt nodes. A C++ rebuild must enforce the same rule and display the same (or translated) message for parity.

### 13.104 Model–widget binding (properties.py)

The UI keeps model state and widgets in sync via **Binding** and helper functions in **properties.py**. **Binding** (NamedTuple) holds **model_connection** and **widget_connection** (QMetaObject.Connection); **disconnect()** disconnects both; **disconnect_all(bindings)** clears a list of connections or Bindings. **Bind** enum: **one_way** (model → widget only), **two_way** (model ↔ widget). **bind(model, model_property, widget, widget_property, mode)** connects model’s `{property}_changed` to the widget setter and, if two_way, the widget’s change signal to the model setter; returns a connection or **Binding**. **bind_combo(model, model_property, combo, mode)** does the same for **QComboBox** using **findData** / **currentData** and **currentIndexChanged**. **bind_toggle(model, model_property, widget, mode)** uses the widget’s **checked** property and **toggled** signal. **serialize(obj)** / **deserialize(obj, data)** persist all properties marked **persist=True** on the object (Enum → value, QUuid → string). A C++ rebuild should provide equivalent two-way binding and serialization so workspace/model state and UI stay consistent and persist correctly.

### 13.105 SpinnerWidget (Live view progress indicator)

**SpinnerWidget** (`ui/live.py`) is a small progress indicator used in the **Live** workspace. Fixed size **48×20** px; **WA_TransparentForMouseEvents** so it does not block input. Paints a **120° arc** (grey, with brightness animation) and a **progress percentage** text (e.g. "45%"). A **QTimer** at **50 ms** (20 FPS) drives rotation and brightness; **start_animation()** / **stop_animation()** show/hide and start/stop the timer; **set_progress(progress)** updates the displayed percentage. Used next to or above the live preview area during generation. A C++ rebuild should provide a similar compact progress indicator for the Live view so users get feedback during live generation.

### 13.106 InactiveRegionWidget and region list UI

**RegionPromptWidget** (`ui/region.py`) shows the **active region** (with prompt, strength, control) and a list of **inactive** regions. **InactiveRegionWidget** is a **QFrame** used for each non-active region: **objectName** "InactiveRegionWidget", **background-color** from **theme.base**. Clicking an inactive region makes it the active region. The active region uses **ActiveRegionWidget**; inactive ones use **InactiveRegionWidget** so the user can see and switch between multiple regions. **PromptHeader** (full / icon / none) controls how much header text is shown. A C++ rebuild should support the same active/inactive region list and header options for the regions UI.

### 13.107 CustomInpaintWidget (Generate view inpaint options)

**CustomInpaintWidget** (`ui/generation.py`) is the widget in the **Generate** view that exposes inpaint-specific options: mode (automatic, fill, expand, add_object, remove_object, replace_background, custom), context (automatic, mask_bounds, entire_image, layer_bounds), fill mode, and toggles such as "use inpaint model" and "use prompt focus". It is embedded in **GenerationWidget** (e.g. **self.custom_inpaint**). The main generate action menu (Generate, Refine, Edit, Refine Region, etc.) and these options together define how generation and inpaint behave. A C++ rebuild should provide equivalent controls and bind them to **InpaintParams** and **InpaintMode** / **InpaintContext** / **FillMode** as in §9.2 and §5.4.

### 13.108 Release packaging (manual.html and precheck)

**scripts/package.py** builds the release ZIP. In addition to §13.77: **manual.html** is **not** shipped in the repo; it is **generated at package time** by **convert_markdown_to_html(README.md, plugin_dst / "manual.html")**. The **precheck()** step runs **translation.update_template()**, **translation.update_all()**, and **update_model_checksums(root / "scripts" / "downloads")** (from **ai_diffusion.resources**). **update_model_checksums** reads model files from the **scripts/downloads** directory (gitignored); that path should contain downloaded model files so SHA256 checksums can be computed and written into the models preset (e.g. **presets/models.json**). CI and contributors may run **download_models.py --minimal scripts/downloads** to populate it before packaging. The packaged plugin includes **download_models.py** (copied from **scripts/** into the plugin dir) and **LICENSE**. A C++ rebuild that ships a manual should either generate it from README or document the source; if using a managed server or download helper, equivalent checksum handling may be needed.

### 13.109 Managed server packages UI (PackageState, WorkloadsTab)

The **Local Managed Server** tab uses **ServerWidget** (`ui/server.py`). **PackageState** enum: **available**, **selected**, **installed**, **disabled** — per-package install/selection state. **PackageItem** holds a package (e.g. **ModelResource**), **state** (PackageState), and UI **status** (e.g. checkbox). **PackageGroupWidget** displays a group of packages (e.g. "ComfyUI", "Upscalers", "SD 1.5", "SD XL", "Flux", "Z-Image") with **PackageItem**s; **values(states)** and **\_update_item_visibility** reflect backend support (e.g. **\_backend_supports**) and workload selection. **WorkloadsTab** is the sub-tab that shows required vs optional packages and workload toggles; **CustomPackageTab** shows custom/extra packages. **ModelCheckBox** ties a checkbox to **PackageState.selected** vs **available**. Filtering by **Arch** and backend (e.g. **\_filter_by_arch**, **\_enabled_workloads**) ensures only supported packages are offered. A C++ rebuild that implements the managed server UI should use the same states and package/group structure so install and "Detected base models" behavior match.

### 13.110 README, media assets, and related projects

The repository **README.md** references a **media/** directory for gallery images (e.g. **media/screenshot-1.png**, **media/control-scribble-screen.png**). These are **project/marketing** assets and may not be present in every clone; the **Technical Specification** reference screenshots are in **screenshots/** and are the ones tied to §12. The README also mentions **Krita AI Tools** ([Acly/krita-ai-tools](https://github.com/Acly/krita-ai-tools)) as a **separate plugin** for object selection and segmentation; that project is out of scope for this spec. A C++ rebuild need not ship README media assets; use **screenshots/** for UI parity reference.

### 13.111 CONTRIBUTING.md (contributor guide)

**CONTRIBUTING.md** at the repository root is linked from the README and describes how to contribute. It covers: **Reporting issues** (check existing issues, attach log files from the plugin's `.logs` subfolder or link in connection settings); **Translations** (language files in `ai_diffusion/language/`, use `new_language.json.template` for new languages, placeholder syntax must be preserved); **Contributing code** (discuss larger changes via issues, submit pull requests). Log files are stored in **user_data_dir/logs** (see §9.6); CONTRIBUTING.md may still reference the legacy **.logs** subfolder—the plugin and Connection tab "View log files" link use the current log directory. A C++ rebuild may provide equivalent contributor documentation; the log path and language folder are the same as in the Python plugin.

### 13.112 Tag CSV format (tags/README.md)

The **ai_diffusion/tags/README.md** file documents the tag autocomplete CSV format. Each file is comma-separated with columns: **tag**, **type**, **count**, **aliases**. **type** is numeric (Danbooru categories: general, artist, copyright, character, meta) and is used to color/sort entries; **count** is usage count for sorting; **aliases** is currently unused. The first line is the header. The README also describes how the shipped CSVs (Danbooru, e621) were generated (BigQuery, sqlite, etc.) and **truncation rules** (e.g. general/meta tags with count &lt; 20 omitted, artist/copyright/character with count &lt; 50 omitted) when regenerating or validating tag files. For a C++ rebuild, support the same column layout so the same tag files can be used; the README is optional for packaging but useful for maintainers.

### 13.113 Cloud and custom server LoRA upload

For **Online Service (cloud)**, LoRAs that are not already on the server are uploaded before or during job submission. **CloudClient.send_lora(workflow)** iterates over **loras_to_upload(workflow, client_models)** and calls **_upload_lora** (POST for upload URL, then streaming PUT with optional SHA256 header); progress is reported via **ClientEvent.upload**. The backend may enforce a max LoRA size (e.g. MB); oversized LoRAs raise a user-facing error. For **ComfyClient** (custom server), **upload_loras(work, job_id)** uploads missing LoRAs via HTTP PUT to **api/etn/upload/loras/<file_id>**. When the user adds a LoRA via "Upload" in the Styles tab, the file dialog uses the filter **"LoRA files (*.safetensors)"** (title e.g. "Select LoRA file"). A C++ rebuild that supports cloud or custom server with LoRA upload must implement the same flow and, for cloud, the same progress/error handling; use the same file filter for the upload dialog for parity.

### 13.114 Translation build (scripts/translation.py)

**scripts/translation.py** is used to extract translatable strings and maintain language files. **Source strings** are extracted with the regex **`_\(\s*"(.+?)"[\,|\s*\)]`** (i.e. `_("...")` calls) from **ai_diffusion/** (excluding **icons**, **websockets**, **debugpy**, **.pytest_cache**, **__pycache__**). **parse_source(dir)** walks the tree and returns the set of strings. **new_language.json.template** has structure: **id**, **name**, **translations** (object: source string → translation or null). **Commands:** (1) **template** — **update_template()** writes the template from current source strings; (2) **update** — **update_all()** syncs all existing language JSON files (except en.json) so their **translations** keys match current source (removes obsolete, adds new with null); (3) **&lt;lang_id&gt;** — creates a new language file (e.g. **de**, **fr**) with **--name** for display name and **--outdir** defaulting to **ai_diffusion/language**. Package **precheck()** runs **translation.update_template()** and **translation.update_all()**. A C++ rebuild that supports the same language JSON format and key set can reuse the same **language/** files and optionally an equivalent extraction step (e.g. from Qt tr() or a custom marker).

### 13.115 Download models script (scripts/download_models.py)

**download_models.py** is copied into the plugin package and is used to download required/optional models for a ComfyUI server (managed install or user-run). **Usage:** `python download_models.py [destination] [options]`; **destination** is the path where models are placed (e.g. ComfyUI root or a workspace). It uses **resources.required_models**, **resources.optional_models**, **ModelResource**, **ModelFile** (path, url, **ResourceId**, **sha256** checksum). **list_models(...)** accepts flags: **sd15**, **sdxl**, **flux**, **flux2**, **illu**, **zimage**, **upscalers**, **checkpoints**, **controlnet**, **prefetch**, **deprecated**, **minimal**, **recommended**, **all**, **backend** (**ModelRequirements**: cuda, cuda_fp4, no_cuda), **exclude**. **--dry-run** only lists what would be downloaded. **resources.update_model_checksums(root / "scripts" / "downloads")** is called during package **precheck()** to update checksums in model/preset data. A C++ rebuild that ships a managed server or a download helper should support the same resource list, checksums, and destination layout so install and "Detected base models" stay consistent.

### 13.116 Documentation (docs/) — Starlight and structure

The **docs/** tree is the project’s user-facing documentation, published at **docs.interstice.cloud**. It is built with **Starlight** (Astro): **astro.config.mjs** sets **site: 'https://docs.interstice.cloud'**, **integrations: [starlight(...)]** with **title: 'Krita AI Handbook'**, **logo**, **favicon**, **social** (GitHub, Discord, YouTube), and a **sidebar** with **Setup** (Installation, ComfyUI Setup, Common Issues) and **Guides** (First Steps, Selection Fill, Text Prompts, Control Layers, Regions, Edit Models, Samplers, Custom Graphs, etc.). Content lives in **docs/src/content/docs/** as **.mdx** files. The following content pages exist in the repo (for parity of docs and links): **index.mdx**, **installation.mdx**, **comfyui-setup.mdx**, **common-issues.mdx**, **basics.mdx**, **selections.mdx**, **prompts.mdx**, **control-layers.mdx**, **regions.mdx**, **edit-models.mdx**, **models.mdx**, **base-models.mdx**, **samplers.mdx**, **resolutions.mdx**, **custom-graph.mdx**. Static assets in **src/assets/**; **public/** for favicon etc. **npm run dev** / **npm run build**; deployment targets **Cloudflare Pages** (e.g. **npx wrangler pages deploy dist**). The spec’s "Astro/MDX" refers to this Starlight-based site. Not required for plugin logic; useful for UX copy, installation, and ComfyUI setup instructions when rebuilding or documenting a C++ port.

### 13.117 Pytest configuration (pytest.ini and conftest)

**tests/pytest.ini**: **asyncio_mode = auto**, **asyncio_default_fixture_loop_scope = module**, **norecursedirs = benchmark images references results server** (so pytest does not collect from those subdirs). **tests/conftest.py** adds CLI options: **--test-install**, **--cloud**, **--ci**, **--benchmark**. **pytest_collection_modifyitems** orders workflow tests: sd15 (1), sdxl (2), flux (3), flux2 (4), cloud (10/11). **QtTestApp** wraps **QCoreApplication**, **eventloop.setup()**, and **eventloop.run(coro)** with **processEvents()** until the coroutine completes. **clear_appdata** and **clear_results** (session, autouse) assume **user_data_dir** is **.appdata** and wipe **data/** and **result_dir** so tests do not touch real user data. **local_download_server** fixture runs **scripts/file_server.py** on port **51222** and sets **network.HOSTMAP = network.HOSTMAP_LOCAL** for the test. A C++ test harness may mirror these options and fixture semantics (isolated appdata, optional local file server, ordered workflow tests) for behavioral parity.

### 13.118 Benchmark scripts (scripts/benchmark_report.py, benchmark_html.py)

**benchmark_report.py**: Reads benchmark output images from a folder by **prefix** (e.g. **benchmark_inpaint_apple-tree**, **benchmark_inpaint_bruges**, …) and **suffix** (e.g. **_sdxl_noprompt_4213_local.png**). For each prefix, builds a horizontal strip of images (resized to 480 px width, LANCZOS), saves as **compressed/{prefix}.jpg**, and writes **results.md** with section headers and image links. Used for visual comparison of benchmark runs. **benchmark_html.py**: Produces HTML output for benchmark results. These scripts are for **performance and regression benchmarking**; they are not required for plugin behavior. A C++ project may use equivalent tooling or the same image naming for cross-checking.

### 13.119 Docker (scripts/docker/)

**scripts/docker/** provides a containerized ComfyUI environment for development or hosted use. **Dockerfile**: **base** stage uses **nvidia/cuda:12.8.1-base-ubuntu22.04**, installs system deps (build-essential, git, git-lfs, libgl1, etc.), **uv** for Python, **venv** with **python3.12**, installs from **requirements.txt** and optional wheels (e.g. nunchaku). Copies **ComfyUI/** and **extra_model_paths.yaml**; **download_models.py --dry-run /workspace**; symlink **/workspace/models → /models**. **Final** stage adds nginx, Jupyter, rclone, etc. **start.sh** / **pre_start.sh** handle runtime (e.g. model download, server start). **nginx/** contains **nginx.conf**, **502.html**, **README.md**. **extra_model_paths.yaml** configures ComfyUI model paths. Not required for the plugin itself; useful for CI, cloud backends, or local ComfyUI-in-Docker workflows. A C++ rebuild need not ship Docker; awareness of the layout helps when testing against a containerized server.

### 13.120 requirements.txt (dev only) and extension path warning

**requirements.txt** at the repo root is for **development and CI only** (ruff, pyright, debugpy, pytest, aiohttp, PyQt5, Pillow, etc.). The plugin at runtime runs inside **Krita’s embedded Python** and does not install these; only the standard library and Qt5 are guaranteed in Krita’s environment. The **managed server** uses **ai_diffusion/server_requirements.txt** (from **server_requirements.in**), not **requirements.txt**. — On extension **__init__** (**extension.py**), if the plugin directory’s **parent** is not named **pykrita** and the parent is not a **.git** directory, a **warning** is logged: *"Plugin is not installed in a 'pykrita' directory, this may break user files and settings. Detected installation path is: …"*. This helps distinguish a development/unpacked layout from a proper Krita plugin install. A C++ port can omit the check or emit an equivalent warning when the install path looks nonstandard.

### 13.121 User type (Cloud account)

**User** (`client.py`) is a QObject with **ObservableProperties** used when the client is the **CloudClient** (Online Service). Properties: **id** (str), **name** (str), **images_generated** (int, Property), **credits** (int, Property). Signals: **images_generated_changed**, **credits_changed**. The cloud backend sets these from **user_data** after sign-in (e.g. **user_data["id"]**, **user_data["name"]**, **user_data["images_generated"]**, **user_data["credits"]**). The **UserWidget** in the Connection tab (Cloud panel) displays the user name and token/credit info; "View account" and "Buy tokens" actions use this data. A C++ rebuild that implements the Online Service client should provide an equivalent **User** type and bind it to the Cloud account UI so account and balance display match.

### 13.122 Network module (NetworkError, DownloadProgress, exceptions)

**network.py** provides HTTP and download helpers used by **ComfyClient**, **CloudClient**, **Server**, and **updates**.

- **NetworkError:** Exception with **code** (int, QNetworkReply error code), **message** (str), **url** (str), optional **status** (int, HTTP status if available), optional **data** (dict, parsed JSON error body). **NetworkError.from_reply(reply)** builds an instance from a **QNetworkReply** (extracts status, tries to parse error payload for message).
- **OutOfMemoryError:** Subclass of **NetworkError**; used when the server reports out-of-memory (e.g. generation failed due to VRAM).
- **Disconnected:** Exception (no payload); used when the connection is closed unexpectedly.
- **RequestManager:** Async HTTP client built on **QNetworkAccessManager**; used for GET/POST, file download, and progress reporting.
- **DownloadProgress** (NamedTuple): **received** (float, MB), **total** (float, MB), **speed** (float, MB/s), **value** (float, 0–1 progress). Used by **Server** and **updates** for install/update progress; **InstallationProgress.progress** can be a **DownloadProgress** or a (current, total) tuple. **DownloadHelper** in network.py computes **DownloadProgress** from byte counts and optional total.

A C++ rebuild that implements managed server install, update checks, or cloud/client HTTP should handle **NetworkError** (and optionally **OutOfMemoryError**, **Disconnected**) and provide equivalent **DownloadProgress** semantics for progress UI.

### 13.123 ComfyObjectInfo (object_info response)

**ComfyObjectInfo** (`comfy_workflow.py`) wraps the ComfyUI **GET object_info** response (node definitions). The client fetches **object_info** after connecting and passes it to workflow build and custom workflow loading. **Constructor:** `ComfyObjectInfo(nodes: dict[str, dict])` where **nodes** is the raw JSON (node class name → node definition). **Methods:** **`params(node_class, category="")`** returns a dict of input parameter names to default values (for INT, FLOAT, BOOL, STRING, COMBO from the node definition); **`options(node_class, param_name)`** returns the list of options for a COMBO input; **`inputs(node_name, category)`** returns the "required" or "optional" input dict for the node; **`outputs(node_name)`** returns output names. **`__contains__(node_class)`** and **`__bool__`** support membership and truth checks. **ComfyWorkflow** uses **node_defs** (ComfyObjectInfo) to fill default inputs when adding nodes and when converting **UI workflow format** to API format (**\_convert_ui_workflow** requires **node_defs** to map widget order to field names). A C++ rebuild must parse the same **object_info** structure and provide equivalent lookup so workflow build and custom workflow conversion match.

### 13.124 Control layer strength storage (strength_multiplier and preset_value)

**ControlLayer** (`control.py`) stores strength internally as an integer for UI precision. **`strength_multiplier = 50`** (class constant): the displayed strength is **strength / strength_multiplier** (e.g. 35 → 0.7). **`preset_value`** (int, 1–4) selects a preset from **control.json** (see §13.55); **`use_custom_strength`** (bool) indicates whether the user overrode with custom strength/range. When serializing to **JobParams.metadata** (e.g. **set_control**), strength is written as **c.strength / ControlLayer.strength_multiplier**. **ControlLayer** also has **clip_vision_extent = Extent(224, 224)** for IP-Adapter-style inputs. **max_preset_value = 4**. A C++ rebuild should use the same multiplier and preset/custom distinction so control layer strength and persistence match.

### 13.125 Root and Edit Regions (dual region system)

The **Model** holds two separate region trees: **regions** (root) and **edit_regions**. Both are **RootRegion** instances. **active_regions** returns **edit_regions** when the workspace is **generation** and **edit_mode** is true (instruction-based "Edit" mode); otherwise it returns **regions**. Conditioning, prompts, and region UI (e.g. **RegionPromptWidget**) use **model.active_regions** so that in Edit mode the user works with edit_regions (and **active_style** becomes **edit_style** when set). Persistence stores them under **root** and **edit** in **ui.json** (§9.7). **ModelSync** tracks both; **RecentlyUsedSync** and workflow build use **active_regions**. A C++ rebuild must implement the same dual trees and **active_regions** logic so that toggling Edit mode switches which region set is used without mixing the two.

### 13.126 End-to-end job execution flow

From user action to result, the flow is: (1) **User** clicks Generate (or Apply/Refine/Edit etc.); **actions.generate()** dispatches by workspace to **model.generate()**, **model.upscale_image()**, **model.generate_live()**, **model.animation.generate()**, or **model.custom.generate()**. (2) **Workflow build**: Document and model state (selection, regions, control layers, style, prompt, strength, seed, inpaint options) are gathered; **document.check_color_mode()** is called; **WorkflowInput** is built; **workflow.create(WorkflowInput, ClientModels, ComfyRunMode)** produces a **ComfyWorkflow** (graph dict). (3) **Queue**: Job (JobParams, kind) is created; **JobQueue** enqueues per **QueueMode** (back/front/replace); client **put(job)** (or **put(job, front=True)**). (4) **Client**: **ComfyClient** (or CloudClient) sends the graph via HTTP POST **prompt**; WebSocket receives **executing**, **execution_cached**, **progress**, then **executed** (or **execution_error** / **execution_interrupted**). (5) **Result**: **ClientMessage** with **ClientEvent.finished** and images; **Job.results** is set; **Job.state** → finished; **ModelSync** writes history image to **result{N}.webp** and appends to **ui.json** history list; **job_finished** signal updates UI (thumbnails, preview, apply/discard). (6) **Apply** (if user chooses): **apply_result(model, image, params, behavior, region_behavior, prefix)** updates or creates layers via **LayerManager**. A C++ rebuild should replicate this sequence so generation, progress, history, and apply behave the same.

### 13.127 InpaintParams.clamped() range

**InpaintParams.clamped()** (api.py) returns a copy of the params with **grow** and **feather** clamped to the range **0–499** (via **clamp(params.grow, 0, 499)** and **clamp(params.feather, 0, 499)**). This limit is used when building the workflow so that mask preprocessing does not exceed the backend’s expected range. A C++ rebuild should apply the same clamp when passing inpaint params into the workflow.

### 13.128 Plugin load and websockets requirement

**ai_diffusion/__init__.py** runs at plugin load. It sets **__version__** and then checks for the bundled **websockets** package via **importlib.util.find_spec(".websockets.src", "ai_diffusion")**. If the spec is not found, it **raises ImportError** with this exact message (so the plugin does not load at all):

```
"Could not find websockets module. This indicates that it was not installed with the"
" plugin. Please make sure to download a plugin release package (NOT just the source!). You"
" can find the latest release package here:"
" https://github.com/Acly/krita-ai-diffusion/releases"
```

Only if **krita** is importable does it then export **AIToolsExtension**. A C++ port does not depend on the Python websockets bundle but should document or mirror this user-facing guidance (e.g. “use a full release package”) if distributing a hybrid or source-only package.

### 13.129 pyproject.toml and development exclusions

**pyproject.toml** defines: **dynamic = ["version"]**, **requires-python = ">=3.10"**, and tool config for **ruff**, **black**, **pyright**, and **pytest**. **pyright** **exclude** list includes: **tests/server**, **tests/test_image_transfer.py**, **ai_diffusion/websockets**, **ai_diffusion/debugpy**. Thus type-checking and standard CI do not cover the managed server test suite or the cloud image-transfer test. A C++ rebuild need not replicate this file, but when using the repo as reference, treat excluded paths as optional or environment-specific (e.g. test_image_transfer depends on cloud backend; server tests require a server environment).

### 13.130 test_image_transfer (cloud-only, optional)

**tests/test_image_transfer.py** tests image send/receive via **service.pod.lib.image_transfer** (cloud backend). It is **excluded from pyright** (pyproject.toml) and is not part of the core plugin behavior. A C++ port that does not implement the cloud service can ignore this test; if implementing cloud image transfer, see §13.65 for inline vs transfer-by-size behavior.

### 13.131 History result discard and in-place annotation update

When the user **discards a single image** from a multi-image result (e.g. one of several batch outputs), **JobQueue** emits **result_discarded(Item)** with **Item(job_id, index)**. **ModelSync._remove_image** does **not** delete the history entry or the annotation. It finds the **\_HistoryResult** for that job, gets the job’s **results** (ImageCollection), removes the image at **index** from the collection, calls **to_bytes()** on the remaining images, and **overwrites** the same annotation **result{slot}.webp** with the new blob and updated offsets. **in_use** for that job is preserved. So “discard” here means “remove this image from the result set”; only **job_discarded** (entire job removed) triggers **\_remove_results**, which deletes the annotation and history entry. Pruning (§13.19) removes oldest **entries** when over **history_storage**; per-image discard only shrinks the blob. A C++ rebuild must implement the same in-place update for **result_discarded** so multi-image history entries stay consistent.

### 13.132 sanitize_prompt (job/layer names)

**sanitize_prompt(prompt: str)** (`util.py`) is used when building **JobParams.name** (e.g. for new layer names and history labels). Behavior: if **prompt** is empty, return **"no prompt"**; otherwise take the first **40 characters**, then keep only characters that are **alphanumeric** or in **" _-"**. Used in **model.py** when creating job params so layer names and history display are safe and concise. A C++ rebuild should apply the same rules so layer names and history labels match.

### 13.133 UTF-16 and prompt widget (char16_len, cursor indices)

**char16_len(text)** and **str_index_to_char16_index** / **char16_index_to_str_index** (`text.py`) deal with **UTF-16 code units** (e.g. one emoji = 2 units). Qt’s **QString** is UTF-16, so the prompt widget uses these for cursor position and selection (e.g. in **TextPromptWidget** and attention-editing logic). **char16_len("😀") == 2**; **char16_len("hello") == 5**. A C++ rebuild using Qt should use **QString::length()** (or equivalent) for the same semantics so selection and Ctrl+Up/Down attention editing work correctly.

### 13.134 ai_diffusion.action root text

The **ai_diffusion.action** XML file has a root-level element **&lt;text&gt;AI Tools: Image Generation&lt;/text&gt;** immediately under **ActionCollection** (before or alongside the **Actions** block). This may be used by Krita as the menu or category label for the plugin’s actions. §13.64 describes the per-action structure; a C++ rebuild that ships equivalent action metadata should include this root **text** if the host uses it for menu grouping.

### 13.135 read_json_with_comments (JSON files: // not #)

**read_json_with_comments(path)** (`util.py`) is used for **settings.json** and other JSON config files (not for prompt text). It **strips lines** whose **stripped** form **starts with "//"** (the whole line is dropped), then parses the remainder as JSON. It does **not** support **"#"** comments in JSON. The **"#"** comment handling is only in **strip_prompt_comments** (§13.35) for **prompt** text (with `\#` escape). A C++ rebuild that parses settings or workflow JSON should strip **//**-style lines, not #, when implementing the equivalent of read_json_with_comments for config files.

### 13.136 JobQueue.Item (result identity)

**JobQueue** exposes signals **result_used** and **result_discarded** with an **Item** argument. **Item** is a simple value (e.g. **job_id: str**, **index: int**) identifying which result image was used or discarded. **ModelSync** uses it in **\_remove_image** to find the job and update the annotation in place (§13.131). **apply_result** and the apply action use the selected item to determine which image and params to apply. A C++ rebuild must define an equivalent **Item** (or pair job_id + index) so persistence and apply flows receive the same identity.

### 13.137 RecentlyUsedSync and InpaintContext.layer_bounds

When **RecentlyUsedSync.track(model)** applies **document_defaults** to a new model (no existing **ui.json**), it sets **model.inpaint.context** from **self.inpaint_context** only when **self.inpaint_context != InpaintContext.layer_bounds.name**. So **layer_bounds** is **never** restored from recent defaults; new documents get **automatic**, **mask_bounds**, or **entire_image** from recent, but not **layer_bounds**. This avoids applying a document-specific context as a default. A C++ rebuild should replicate this condition so default inpaint context behavior matches.

### 13.138 Tag autocomplete: parentheses escaping

In the tag autocomplete UI (**autocomplete.py**), when a tag name contains parentheses (e.g. for attention syntax), the completion delegate escapes them on **insert**: display/selection use the raw tag name; on insert into the prompt, `(` and `)` are replaced with `\(` and `\)` so the inserted text is treated as literal rather than as attention brackets. This avoids breaking existing `(text:weight)` segments when the user selects a tag that includes parentheses.

### 13.139 Image encoding for ComfyUI (base64 and embed_images)

Before submitting a workflow to ComfyUI, **ComfyWorkflow.embed_images()** is called: it walks the graph and replaces any **Image** or **Mask** references in node inputs with base64-encoded data. **Image.to_base64(format)** (and **Mask.to_base64**) encode pixel data using the given **ImageFileFormat** (default PNG). Node inputs such as `image`, `mask`, or custom ETN image inputs receive the string `"data:image/png;base64,..."` (or the appropriate MIME type for the format). The cloud client may use a size threshold and send large images via a separate upload path instead of inline base64. **debug_dump_workflow** writes the prompt (with embedded images) to **log_dir** for debugging. A C++ rebuild must use the same encoding (base64, same MIME prefix) and the same node input names so ComfyUI and custom nodes receive images correctly.

### 13.140 User-facing dialogs and confirmations

The following dialogs and confirmations are shown to the user; a C++ rebuild should implement equivalent flows for parity.

| Context | Trigger | Behavior |
|--------|---------|----------|
| **NSFW filter (first time)** | User enables NSFW filter (Basic or Strict) in Diffusion tab for the first time | **QMessageBox.warning** once per session (**DiffusionSettings._warning_shown**); title "NSFW Filter Warning"; message: "The NSFW filter is a basic tool to exclude explicit content from generated images. It is NOT a guarantee and may not catch all inappropriate content. Please use responsibly and always review the generated images." |
| **Discard image** | User discards a history result (e.g. context menu "Discard Image") | If **confirm_discard_image** is true: **QMessageBox.warning** with Yes/No; default Yes. On Yes, image is removed from history. |
| **Clear history** | User chooses "Clear History" from history context menu | **QMessageBox.warning** with Yes/No; default No. On Yes, all history entries and result annotations are removed. |
| **Persistence load failure** | **ModelSync._load()** raises while loading **ui.json** from document | **QMessageBox.warning** with title "AI Diffusion Plugin", message "Failed to load state from {filename}: {e}". Plugin continues with empty model state. |
| **Managed server: stop** | User stops the managed server | **QMessageBox.warning** (or question) to confirm stop; optional depending on implementation. |
| **Managed server: reinstall** | User clicks Reinstall in the managed server UI | **QMessageBox.question** with title "Confirm Reinstallation", message "This will reinstall the server components while keeping your downloaded models. Continue?"; Yes/No. On Yes, run reinstall (models are kept). |
| **Managed server: uninstall / delete** | User uninstalls server or deletes server data | **QMessageBox** with title "Confirm Deletion"; message "WARNING: This will delete the entire server installation INCLUDING ALL MODELS!" + "This action cannot be undone." + "Are you absolutely sure you want to continue?"; **Cancel** (default) and **Delete** (DestructiveRole). On Delete, run uninstall with delete_models=True. |
| **Custom workflow: overwrite** | User saves a workflow and name already exists | **QMessageBox.question** "Overwrite existing workflow?" Yes/No; default No. |
| **Custom workflow: delete** | User deletes a workflow | **QMessageBox.question** to confirm; default No. |
| **Custom workflow: error** | Unhandled exception in custom workflow execution | **QMessageBox.critical** with "Error" and exception message. |
| **LoRA file too large** | User uploads a LoRA that exceeds cloud/server size limit | **QMessageBox.warning** "File too large" with size in MB. |

Implement the same button roles (Yes/No/Cancel, default button) and destructive styling where applicable so users get consistent confirmation flows.

### 13.141 HTTP headers (ngrok and ComfyUI)

**ComfyClient** adds the request header **`ngrok-skip-browser-warning: 69420`** to HTTP requests (e.g. when connecting to a ComfyUI server behind ngrok). This prevents ngrok from showing its browser warning page and allows the plugin to connect. A C++ rebuild that uses **QNetworkAccessManager** (or equivalent) to talk to ComfyUI should add this header when connecting to arbitrary URLs so that ngrok and similar tunnels work without user intervention.

### 13.142 LCM sampler deprecation (cloud/server)

When the server indicates that the LCM sampler is no longer supported (e.g. cloud backend), the plugin may surface the message: **"LCM is no longer supported by the server. Please change the Style's sampling method to 'Realtime - Hyper'."** (See **client.py** **_lcm_warning**.) A C++ rebuild that supports cloud or managed server should show an equivalent message when the backend rejects LCM so users can switch to the replacement sampler.

### 13.143 Region UI: negative-prompt warning

In the **region prompt** UI (**RegionPromptWidget** / **ActiveRegionWidget**), when the selected **style** does not use the negative prompt (e.g. some architectures ignore it), the plugin shows an **alert** icon (**theme.icon("alert")** — **icons/alert-dark.svg**, **icons/alert-light.svg**) next to the negative prompt field, with tooltip: **"The selected Style does not use the negative prompt."** The icon is visible only when **show_negative_prompt** is true and the style does not support negative prompt. A C++ rebuild should show the same indicator so users understand when the negative prompt has no effect.

### 13.144 Layer is_confirmed (LayerManager)

**Layer** (`layer.py`) has **is_confirmed** (bool). New layers created by **LayerManager.create(...)** start with **is_confirmed=False**. The layer becomes confirmed (**is_confirmed=True**) when the user performs an action that "commits" the layer (e.g. after apply or a confirm step). **LayerManager** uses this to distinguish newly created layers from existing ones (e.g. for filtering or undo behavior). A C++ rebuild that implements **LayerManager** and **update_layer_image** / **create** should use the same convention so layer lifecycle and listing (e.g. **images** that are confirmed) match.

### 13.145 Active history memory tracking (ModelSync)

**ModelSync** maintains **\_memory_used: dict[int, int]** mapping **slot index** → **size in bytes** of the image blob stored in **result{slot}.webp**. When a job finishes, **\_save_result_images** stores the blob size in **\_memory_used[slot]**; when loading from annotations, it sets **\_memory_used[item.slot] = images_bytes.size()**. The **total** of **\_memory_used** is compared against **settings.history_size** (MB) to drive the "Currently using X.X MB" display in the Performance tab and to decide when to prune active history (oldest entries removed until under the limit). Pruning of **stored** history uses **history_storage** (MB) and removes oldest **\_HistoryResult** entries and their annotations. A C++ rebuild should track per-slot sizes and aggregate them for the same UI and pruning behavior.

### 13.146 Collect Diagnostics dialog

The **Plugin** tab "Collect Diagnostics" button copies the diagnostics string to the clipboard and may open a **QDialog** window that displays the same text (e.g. in a read-only text area) so the user can review or copy again. The dialog is a simple window with the diagnostics content and optionally a close button. A C++ rebuild should offer the same: clipboard copy plus optional in-dialog view for support and issue reporting.

### 13.147 TileOverlapMode and UpscaleWorkspace (model)

**TileOverlapMode** (`model.py`): **`auto`** (0), **`custom`** (1). Used by **UpscaleWorkspace** for the "Tile Overlap" control: when **auto**, the workflow receives overlap **-1** (automatic); when **custom**, it uses **tile_overlap** (int, pixels). The Upscale view exposes a combo or toggle "Automatic" / "Custom" and, when Custom, a spinbox for tile overlap in px.

**UpscaleWorkspace** (sub-object of Model): **upscaler** (str), **factor** (float), **use_diffusion** (bool), **strength**, **unblur_strength** (image guidance; UI label "Image guidance"), **tile_overlap_mode** (TileOverlapMode), **tile_overlap** (int), **use_prompt** (bool), **can_generate** (bool), **target_extent** (computed). Persisted in **ui.json** under **upscale**. **params** builds **UpscaleParams** with overlap = -1 when mode is auto else **tile_overlap**. A C++ rebuild should implement the same enum and workspace properties so the Upscale view and workflow build match.

### 13.148 FileLibrary storage paths (checkpoints vs LoRAs)

**FileLibrary** (`files.py`) stores checkpoint and LoRA file lists differently. **Checkpoints** use **FileCollection()** with **no database path** — the collection is in-memory only and is populated from the ComfyUI server (model discovery) after connection. **LoRAs** use **FileCollection(database_dir / "loras.json")** where **database_dir** defaults to **user_data_dir / "database"**. So **user_data_dir/database/loras.json** persists the LoRA file list (id, name, source, path, etc.); checkpoints are not persisted to disk by the plugin and come from the server each session. **FileLibrary.load(database_dir)** creates the singleton; **FileCollection.load()** / **save()** read/write the JSON only when **\_database** is set. A C++ rebuild should use the same paths and semantics so that LoRA list survives restarts and checkpoints are refreshed from the client.

### 13.149 LiveWorkspace and LiveScheduler (Live mode model)

**LiveWorkspace** (`model.py`) is the per-document sub-model for the Live workspace. Properties (with **persist=True** where noted): **is_active** (bool), **is_recording** (bool), **strength** (float, persist), **has_result** (bool). Signals: **is_active_changed**, **is_recording_changed**, **strength_changed**, **seed_changed**, **has_result_changed**, **result_available(Image)**, **modified**. Internal state: **_result**, **_result_params**, **_keyframes_folder**, **_keyframe_start**, **_keyframe_index**, **_keyframes** (list of Path). When recording is enabled, frames are written to **{document_directory}/{document_stem}.live-frames/frame-{N}.webp**; when recording stops, **import_animation** is called. **LiveScheduler** (same module) controls when to trigger the next live generation: **delay_threshold**, **default_grace_period**, **poll_rate** (async sleep between checks), **average_generation_time**, **grace_period** (shorter when generation is slow). **should_generate(input)** and **notify_generation_started/finished** drive the loop. Persistence: **live** in **ui.json** stores serialized LiveWorkspace (strength, etc.). A C++ rebuild must implement the same properties and recording/import flow so Live mode and frame export match.

### 13.150 Persistence format version constant

The **ui.json** state includes a top-level **version** field (integer). In **persistence.py** the constant **version = 1** is written by **ModelSync._save()** and read on load. The comment states: "Version of the persistence format, increment when there are breaking changes." A C++ rebuild should define the same constant and increment it when changing the serialized shape of **ui.json** so old documents can be detected and migrated or rejected with a clear message.

### 13.151 ai_diffusion.action file location and settings action

The repository **ships** the action metadata file at **ai_diffusion/ai_diffusion.action** (same directory as **extension.py**). It is XML with the structure described in §13.64 and §13.134. **Exception:** the **ai_diffusion_settings** action uses **activationFlags=0** and **activationConditions=0**, whereas all other actions use **activationFlags=10000** and **activationConditions=0**. So the phrase "all actions use activationFlags = 10000" in §13.64 is inaccurate — the settings action uses **0**. A C++ rebuild that ships equivalent action metadata should replicate this so menu/activation behavior matches Krita’s handling of the settings action.

### 13.152 Krita document API used by the plugin (C++ porting)

**KritaDocument** uses the following Krita Python API (or equivalent in C++) for document and annotations. A C++ port should call the same host APIs with the same semantics.

| Purpose | API (Python) | Notes |
|--------|---------------|--------|
| Annotations | **document.setAnnotation(name, description, value)** | name = `"ai_diffusion/" + key`, description = `"AI Diffusion Plugin: " + key`, value = QByteArray |
| Read annotation | **document.annotation(name)** | Returns QByteArray; empty size treated as missing |
| Remove annotation | **document.removeAnnotation(name)** | name = `"ai_diffusion/" + key` |
| Selection bounds | **document.selection()** → **Selection** | Bounds derived from selection shape (see **_selection_bounds** in document.py) |
| Animation timeline | **document.currentTime()**, **document.playBackStartTime()**, **document.playBackEndTime()** | Frame indices for Animation workspace and Live |
| Import animation | **document.importAnimation(file_paths, offset, fps)** | fps = 1 in the plugin; raises if import fails |
| Resize canvas | **document.resizeImage(0, 0, width, height)** | Used when applying result with resize_canvas |
| Resolution | **document.resolution()** | Used as document.resolution / 72.0 for vector scaling |
| Active document | **Krita.instance().activeDocument()** | For **KritaDocument.active()** and **is_active** |
| All documents | **Krita.instance().documents()** | For **is_valid** (document still in list) |
| Document ID from annotation | **document.annotation("ai_diffusion/document_id")** | Read before creating KritaDocument to reuse same ID |
| Selection object | **document.selection()** | Returns Krita Selection; use x(), y(), width(), height() for bounds; see §13.154, §13.158 |

**KritaDocument** is cached in a **WeakValueDictionary** keyed by **document_id** so one wrapper exists per open document. **Document** (stub) is used when no document is open; its **is_active** returns **True** when **activeDocument() is None** (i.e. “no document” context). A C++ rebuild should use the same annotation key prefix and document lifecycle so annotations and model–document binding match.

### 13.153 Icon names (theme.icon) used in the UI

The UI loads icons via **theme.icon(name)** from **ai_diffusion/icons/**; each name resolves to **{name}-dark.svg** / **{name}-light.svg** (or .png fallback) per theme. **theme.logo()** uses **logo-128.png** (scaled to 64×64 in the dock and Plugin tab). **theme.checkpoint_icon(arch, …)** maps architectures to **sd-version-*** stems. For parity, a C++ rebuild should provide icons for at least the following **name** stems used in the codebase:

| Category | Icon name stems |
|----------|-----------------|
| **Workspaces** | workspace-generation, workspace-upscaling, workspace-live, workspace-animation, workspace-custom |
| **Actions / buttons** | apply, apply-layer, cancel, generate, refine, refine-region, random, seed, settings, save, discard, upload, import, reset, remove, filter, more |
| **Queue** | queue-active, queue-inactive, queue-upload, queue-waiting |
| **Live / animation** | play, pause, record, record-active |
| **Regions** | region-add, region-prompt, region-alpha, region-alpha-active, root |
| **Context / inpaint** | context, context-automatic, context-mask, context-layer, context-image; fill, fill-empty; inpaint-automatic, inpaint-fill, inpaint-expand, inpaint-add_object, inpaint-remove_object, inpaint-replace_background, inpaint-custom |
| **Control** | control-add, control-generate, add-pose; control-reference, control-style, control-composition, control-face, control-inpaint, control-universal, control-scribble, control-line_art, control-soft_edge, control-canny_edge, control-depth, control-normal, control-pose, control-segmentation, control-hands, control-blur, control-stencil |
| **Link** | link, link-active, link-off, link-disabled |
| **Misc** | warning, alert, interstice, resolution-multiplier; file-json, file-kra, web-connection; comfyui; **star.png** (history applied overlay, from **theme.icon_path**) |
| **Checkpoint (arch)** | sd-version-15, sd-version-xl, sd-version-3, sd-version-flux, sd-version-flux-k, sd-version-flux-2, sd-version-illu, sd-version-illu-v, sd-version-chroma, sd-version-qwen, sd-version-z-image (used by **checkpoint_icon**) |
| **Logo** | logo-128.png (via **theme.logo()**, not **icon()**) |

The repo contains additional icons (e.g. edit, copy-image) and one icon per **ControlMode** as **control-{mode.name}**. Providing the stems above ensures the main dock, Configure dialog, and workspace widgets render correctly; the full set is under **ai_diffusion/icons/** with **-dark** / **-light** suffixes.

### 13.154 Selection bounds and entire-document detection

**Selection bounds** are obtained from the Krita **Selection** object via **`selection.x()`**, **`selection.y()`**, **`selection.width()`**, **`selection.height()`**. The helper **`_selection_bounds(selection)`** returns **Bounds(x, y, width, height)**. This is used in **KritaDocument._poll()** for **selection_bounds_changed** and in **create_mask_from_selection** for padding and mask extent.

**Entire-document detection:** **`_selection_is_entire_document(selection, extent)`** returns True when (1) the selection bounds start at (0, 0), (2) the bounds cover the full document extent (width and height), and (3) the selection **pixelData** over the bounds is all **0xff** (fully selected). When this returns True, **create_mask_from_selection** returns **(None, None)** so no mask is created and generation uses the full image rather than a selection-based mask. A C++ rebuild must implement the same check so that "generate with full-document selection" behaves like "generate without selection".

### 13.155 Action shortcuts (no defaults)

The shipped **ai_diffusion.action** file has **empty** **`<shortcut></shortcut>`** elements for all actions. No default keyboard shortcuts are defined by the plugin; users assign shortcuts in Krita’s shortcut settings (e.g. Settings → Configure Krita → Keyboard Shortcuts). A C++ rebuild that ships action metadata should leave shortcuts empty for parity, or document any default shortcuts if the host supports them.

### 13.156 Language folder path and CONTRIBUTING

The correct path for language files is **`ai_diffusion/language/`** (see §2.1, §13.10). **CONTRIBUTING.md** in the repo contains a typo (**"langauge"**); a C++ rebuild and contributor docs should use **language**. The template file is **new_language.json.template** in that folder.

### 13.157 Layer bounds for mask-type layers

For **mask-type** layers (e.g. transparency masks), **Layer.compute_bounds()** does **not** use **node.bounds()** because Krita returns the full image extent for mask nodes. Instead, it creates a temporary **krita.Selection**, sets it from **node.pixelData(*bounds)**, and uses **selection.x()**, **selection.y()**, **selection.width()**, **selection.height()** to get the actual pixel-containing bounds. This is used when resolving region masks and layer extent. A C++ port that implements **LayerManager** and region mask bounds must use the same approach for mask layers so region bounds are correct.

### 13.158 Krita Selection API used for mask and bounds

**KritaDocument.create_mask_from_selection** and **Layer.compute_bounds** (for masks) use the following Krita selection APIs. A C++ port should use the same host APIs:

| Purpose | API (Python) | Notes |
|--------|---------------|--------|
| Selection bounds | **selection.x()**, **selection.y()**, **selection.width()**, **selection.height()** | Used by _selection_bounds and create_mask_from_selection |
| Duplicate selection | **selection.duplicate()** | Used before invert so the original is not modified |
| Invert selection | **selection.invert()** | When SelectionModifiers.invert is true |
| Pixel data for mask | **selection.pixelData(x, y, w, h)** | Returns bytes; used for Mask and for _selection_is_entire_document (all 0xff check) |
| Selection from pixel data | **krita.Selection()** then **selection.setPixelData(data, x, y, w, h)** | Used in Layer.compute_bounds for mask layers to get tight bounds |

### 13.159 Environment variables

The plugin and scripts respect the following environment variables. A C++ rebuild that needs parity (e.g. testing, cloud/update endpoints) should support the same names and defaults where applicable.

| Variable | Used in | Purpose |
|----------|---------|---------|
| **INTERSTICE_URL** | updates.py, cloud_client.py | Override base URL for update check and cloud API. Default: `https://api.interstice.cloud`. |
| **INTERSTICE_WEB_URL** | cloud_client.py | Override web URL for cloud sign-in / account. Default: `https://www.interstice.cloud`. |
| **AI_DIFFUSION_ENV** | util.py | When set to `WORKER`, enables worker context (e.g. for background or headless use). In WORKER context, **create_logger** uses **StreamHandler(sys.stdout)** instead of **RotatingFileHandler**, so log output goes to stdout. |
| **KRITA_AI_DIFFUSION_DEBUG_IMAGE** | settings.py | Optional path for debug image output (e.g. dump folder). Exposed as **Settings.debug_image_folder** (read-only, not persisted); when set, the plugin may write debug images to this path. |
| **HOSTMAP** | network.py | When set, uses **HOSTMAP_LOCAL** so HTTP requests can be redirected (e.g. tests with **scripts/file_server.py**). |
| **SSL_CERT_FILE** | comfy_client.py | On Linux, if unset, set to `/etc/ssl/cert.pem` for SSL. |
| **PSModulePath**, **SYSTEMROOT** | server.py | Windows: managed server process may clear or set these when launching the ComfyUI subprocess. |
| **ONEDNN_MAX_CPU_ISA** | server.py | **Set** (not read) by the managed server when starting the ComfyUI subprocess: when backend is not CPU, the plugin sets this to **AVX2** in the subprocess environment as a workaround (see issue #401). |

### 13.160 Update check API (endpoint and response)

The **AutoUpdate** flow (§13.37) uses an HTTP API to determine the latest plugin version and download URL.

- **Endpoint:** **GET** `{api_url}/plugin/latest?version={current_version}`  
  **api_url** is **AutoUpdate.default_api_url** (or **INTERSTICE_URL** env), typically `https://api.interstice.cloud`. **current_version** is the running plugin’s **__version__** (e.g. `1.49.0`).

- **Response (JSON):**  
  - **version** (str): Latest version string.  
  - **url** (str): Download URL for the update package (required when version differs from current).  
  - **sha256** (str): SHA256 checksum of the package (required when version differs).

- **UpdatePackage** (NamedTuple in updates.py): **version**, **url**, **sha256**. Stored after a successful check when an update is available; used by the install step to download and verify the package.

- **Behavior:** If **version** equals current, state becomes **latest**. If **version** differs but **url** or **sha256** is missing, state becomes **failed_check**. Otherwise state becomes **available** and the package is stored for "Download and Install". A C++ rebuild that implements update check should use the same endpoint, query parameter, and response shape so it can reuse the same backend or document an alternative.

### 13.161 Workflow build entry points (workflow.create dispatch)

**workflow.create(WorkflowInput, ClientModels, ComfyRunMode)** in **workflow.py** is the single entry point that builds the ComfyUI graph. It dispatches on **WorkflowInput.kind** to one of the following internal functions. A C++ rebuild must implement equivalent logic for each kind:

| WorkflowKind | workflow.py function | Purpose |
|--------------|----------------------|---------|
| **generate** | **generate()** | Text-to-image; latent + conditioning + decode. |
| **inpaint** | **inpaint()** | Inpainting with mask, fill, optional inpaint model. |
| **refine** | **refine()** | Refine full image (e.g. strength-based denoise). |
| **refine_region** | **refine_region()** | Refine within a region/mask. |
| **upscale_simple** | **upscale_simple()** | Single upscaler node, no tiling. |
| **upscale_tiled** | **upscale_tiled()** | Tiled upscale with optional refinement. |
| **control_image** | **create_control_image()** | Preprocess and encode control (e.g. depth, pose, scribble). |
| **custom** | **expand_custom()** | Custom (graph) workflow; injects images, style, prompts into ETN nodes. |

Each function receives a **ComfyWorkflow** instance (graph builder), **ClientModels** (or **ModelDict** for arch-specific resources), and the relevant **WorkflowInput** parts (images, models, conditioning, sampling, inpaint, upscale, custom_workflow). **create()** constructs **ScaledExtent** from **WorkflowInput.extent**, **Conditioning** from **WorkflowInput.conditioning**, and **MiscParams** from batch_count, layer_count, color_match, nsfw_filter. A C++ port should mirror this dispatch and the same helper types so workflow output matches ComfyUI expectations.

### 13.162 Binding lifecycle and disconnect_all

UI widgets that bind to a **Model** (or sub-objects like **UpscaleWorkspace**, **LiveWorkspace**) store a **list of bindings** (e.g. **Binding** tuples or connection handles from **properties.bind()**). When the **model** changes—e.g. user switches document, workspace, or style—the widget must **disconnect** the old bindings before attaching new ones so that signals from the previous model do not update the widget. The codebase uses **Binding.disconnect_all(connections)** (or equivalent) in property setters (e.g. **GenerationWidget.model**, **UpscaleWidget.model**, **CustomWorkflowPlaceholder.model**). **Binding** (in **properties.py**) is a **NamedTuple** of **model_connection** and **widget_connection** (Qt signal/slot connections); **disconnect_all** disconnects each. A C++ rebuild should implement the same pattern: when the active document or workspace changes, clear existing bindings and re-bind to the new model so the UI reflects the correct state and does not leak updates from the old model.

### 13.163 Example data formats (for parsing and validation)

The following minimal examples illustrate the shape of key persisted and API payloads. A C++ rebuild can use them to validate parsing and serialization.

**ui.json (minimal top-level structure):**  
The main document state blob contains at least: **version** (integer, currently 1), **preview_layer** (string or null), **inpaint**, **upscale**, **live**, **animation**, **custom** (each an object or null), **history** (array of _HistoryResult objects with id, slot, offsets, params, kind, in_use), **root** and **edit** (region trees), **control** (control layer list), and per-region **control** under regions. See §9.7 and §9.7a for full key list.

**Style preset JSON (built-in example):**  
One built-in style file (e.g. **ai_diffusion/styles/flux.json**) has the form: **name**, **version** (integer, e.g. 1), **architecture** ("auto" or Arch name), **checkpoints** (array of checkpoint filenames), **loras** (array, may be empty), **style_prompt**, **negative_prompt**, **vae**, **clip_skip**, **v_prediction_zsnr**, **self_attention_guidance**, **preferred_resolution**, **linked_edit_style** (string or empty), **sampler**, **sampler_steps**, **cfg_scale**, **live_sampler**, **live_sampler_steps**, **live_cfg_scale**. Example fragment: `{"name":"Flux","version":1,"architecture":"auto","checkpoints":["flux1-dev.safetensors",...],"loras":[],"style_prompt":"photo of {prompt}","negative_prompt":"",...}`. All built-in styles use **architecture: "auto"**.

**ComfyUI prompt API request body:**  
POST **prompt** sends JSON: **prompt** (object — the workflow graph, node id → { class_type, inputs }), **client_id** (UUID string), **prompt_id** (job UUID string). Response must include **prompt_id** matching the request (ComfyUI 0.3.45+).

**Language file (language/en.json):**  
Structure: **id** (string, e.g. "en"), **name** (display name, e.g. "English"), **translations** (object mapping source string → translation string; for English the source language, translations may be empty). Example: `{"id":"en","name":"English","translations":{}}`. Other language files have the same keys with **translations** filled for each source string used in the plugin.

### 13.164 Test-to-reference image mapping (regression parity)

Workflow and image tests in **tests/test_workflow.py** that perform pixel comparison use reference images under **tests/references/**. A C++ rebuild can use the same inputs (from **tests/images/**, **tests/data/**) and compare output against these references to verify parity.

| Test / scenario | Reference image(s) | Notes |
|-----------------|---------------------|--------|
| **test_create_control_image** (parametrized by ControlMode) | **test_create_control_image_{mode.name}.png** (e.g. test_create_control_image_scribble.png, test_create_control_image_pose.png, test_create_control_image_depth.png, test_create_control_image_canny_edge.png, test_create_control_image_line_art.png, test_create_control_image_soft_edge.png, test_create_control_image_normal.png, test_create_control_image_segmentation.png, test_create_control_image_hands.png) | **test_create_control_image** loads **reference_dir / image_name** and asserts **Image.compare(result, reference) < threshold** (0.015 for pose, 0.005 otherwise). ControlMode.hands is skipped. |
| **tests/test_image.py** (mask ops) | **references/mask/mask_op_subtract.png**, **references/mask/mask_op_add.png** | Used for Mask operation regression. |
| Other **run_and_save** tests | Results written to **result_dir** (tests/results/); not all tests compare to a reference | Many workflow tests only write output to result_dir; control image tests are the main regression set with reference PNGs in **tests/references/** |

Input images for control tests: **tests/images/adobe_stock.jpg**. Config: **tests/config.py** defines **reference_dir**, **image_dir**, **result_dir**, **data_dir**; **default_checkpoint** per Arch. Using the same paths and reference set ensures a C++ implementation can validate control-image and mask behavior against the reference implementation.

### 13.165 Plugin installation path check and warning

On extension load (**extension.py** constructor), the plugin checks whether it is installed in the expected location: the parent of the extension directory is expected to be named **pykrita** (Krita’s Python plugin folder), or the parent may be a git checkout (has **.git**). If the parent is neither (e.g. user copied the folder elsewhere), the plugin logs a **warning**: *"Plugin is not installed in a 'pykrita' directory, this may break user files and settings. Detected installation path is: {path}"*. This helps users who install incorrectly (e.g. wrong folder) get feedback. A C++ rebuild should perform an equivalent check and log or display a similar warning so behavior and diagnostics match.

### 13.166 Styles tab: LoRA warnings (not installed, special LoRA)

In the **Styles** tab, each **LoraItem** in the LoRA list can show inline warnings. When a LoRA is selected and the client is connected: (1) If the LoRA file is **not installed on the server** (missing from **client.models.resources** or **FileSource.unavailable**), a warning icon and text are shown: *"The LoRA file is not installed on the server."* (2) If the LoRA is a **special/reserved** server resource (e.g. a built-in LoRA identified by **client.models.resources** keys such as `lora-*`), a different warning is shown so the user knows it is managed by the server. The **LoraItem** uses **\_show_lora_warnings(lora)** to toggle visibility of the warning icon and message. A C++ rebuild should show the same two warning states in the Styles tab LoRA list for parity.

### 13.167 Custom workflow: Import Workflow file dialog

When the user imports a custom workflow in the **Graph** view, the plugin shows a **QFileDialog.getOpenFileName** with title *"Import Workflow"*, filter *"Workflow Files (*.json);;All Files (*)"*, and initial directory the user’s home. The selected file is loaded via **model.custom.import_file(path)**. A C++ rebuild should use the same dialog title and filter so the Import Workflow action behaves the same.

### 13.168 Repository alignment and optional files

The repository may contain **untracked or optional files** (e.g. **open-agent-tab.sh**, **screenshots/**) that are not part of the shipped plugin payload. The Technical Specification is aligned with the **ai_diffusion/** package, **scripts/**, **tests/**, and **docs/** as the authoritative description of structure and behavior. When in doubt, treat this document and the **ai_diffusion/** source as the source of truth for folder names (e.g. **language**, not "langauge" as in CONTRIBUTING.md), API surface, and UI behavior.

### 13.169 Inpaint workspace state (CustomInpaint) and ui.json inpaint key

The per-document inpaint configuration is held by a **CustomInpaint** sub-object on **Model** (`model.inpaint`). It is persisted under the **`inpaint`** key in **ui.json** via **ModelSync** (serialize/deserialize with **persist=True** properties). A C++ rebuild must persist and restore the same fields for document parity.

**CustomInpaint** (model.py) properties:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| **mode** | InpaintMode | automatic | Inpaint mode (automatic, fill, expand, add_object, remove_object, replace_background, custom). |
| **fill** | FillMode | neutral | Fill mode for masked area (none, neutral, blur, border, replace, inpaint, green). |
| **use_inpaint** | bool | True | Use dedicated inpaint model when available. |
| **use_prompt_focus** | bool | False | Use prompt/condition mask (use_condition_mask in InpaintParams). |
| **context** | InpaintContext | automatic | Inpaint context (automatic, mask_bounds, entire_image, layer_bounds). |
| **context_layer_id** | QUuid | (empty) | When **context** is **layer_bounds**, the layer whose bounds define the context; ignored otherwise. |

**Signals:** mode_changed, fill_changed, use_inpaint_changed, use_prompt_focus_changed, context_changed, context_layer_id_changed, modified.

**Behavior:** **get_params(mask, is_editing)** returns **InpaintParams** with mode, bounds from mask, fill (or FillMode.none when is_editing), use_inpaint_model, use_condition_mask. **get_context(model, mask)** returns the **Bounds** to use for conditioning when context is mask_bounds, entire_image, or layer_bounds; for layer_bounds it uses **model.layers.find(context_layer_id)** and expands layer bounds to include the mask bounds. **RecentlyUsedSync** does not apply **inpaint_context = layer_bounds** to new documents (§13.137). The Generate view **CustomInpaintWidget** and inpaint dropdown bind to this object; see §5.4 and §13.107.

### 13.170 Open Web UI and switch_to_web_workflow (Graph workspace)

When the user clicks **Open Web UI** in the Graph (Custom Workflow) view, the plugin (1) opens **client.url** in the default browser via **QDesktopServices.openUrl(QUrl(client.url))**, and (2) calls **CustomWorkspace.switch_to_web_workflow()** on the current document’s custom workspace.

**switch_to_web_workflow()** (`custom_workflow.py`): Subscribes to remote workflow updates so that workflows created or published in ComfyUI’s web UI appear in the plugin’s workflow list. Implementation: connects **WorkflowCollection.rowsInserted** to **\_set_workflow_index** (so the newly received workflow becomes the selected workflow), starts a **QTimer** for **5 minutes** (5 * 60 * 1000 ms), and on timeout calls **\_clear_switch_workflow()** which disconnects **rowsInserted** and stops the timer. The client must already be subscribed to workflow updates (e.g. **api/etn/workflow/subscribe** with **client_id**) so that **workflow_published** / **etn_workflow_published** WebSocket messages add the workflow to **WorkflowCollection**; **\_set_workflow_index** then selects that workflow. A C++ rebuild should implement the same “open browser + subscribe for 5 minutes” behavior so that creating a workflow in the ComfyUI UI and publishing it shows up in the plugin without manual refresh.

### 13.171 Logging configuration (logger names and format)

**util.py** creates two loggers via **create_logger(name, path)**. Logger names: **krita.ai_diffusion.client** (writes to **client.log**), **krita.ai_diffusion.server** (writes to **server.log**). Formatter: **"%(asctime)s %(levelname)s %(message)s"**. When not in WORKER context, **RotatingFileHandler** is used with **maxBytes=10*1024*1024** (10 MB), **backupCount=4**, **encoding="utf-8"**. Log level is **DEBUG**. A C++ rebuild that produces compatible log files or integrates with the same diagnostics tooling can use these logger names (or equivalent) and the same format; **Collect Diagnostics** reads the last 300 lines of each log file from **log_dir**.

### 13.172 Resolution and region build helpers (compute_relative_bounds, process_regions)

When building workflow conditioning and job regions from the document’s region tree, the plugin uses two helpers that a C++ rebuild must replicate for correct behavior.

- **compute_relative_bounds(image_bounds, mask_bounds)** (`resolution.py`): Transforms bounds so the mask is expressed relative to the context area. **image_bounds** = bounds of the context area passed to diffusion (relative to full canvas); **mask_bounds** = bounds of the mask and vicinity (relative to full canvas). Returns **(canvas_bounds, relative_mask_bounds)** where **relative_mask_bounds** is **mask_bounds.relative_to(image_bounds)**. Used when composing region masks and conditioning so the diffusion context and mask align correctly (e.g. in **model._get_conditioning** and region refine flows).

- **process_regions(root, bounds, parent_layer, min_coverage, time)** (`region.py`): Builds **ConditioningInput** and **list[JobRegion]** from a **RootRegion** and canvas **bounds**. Optional **parent_layer** restricts to a branch of the region tree; **min_coverage** filters out negligible regions; **time** is used for animation layer masks. Returns **(ConditioningInput, job_info)** where **job_info** is a list of **JobRegion** (layer_id, prompt, bounds, is_background). Used when building **WorkflowInput.conditioning** and **JobParams.regions** so region prompts and control are applied correctly. Implementation walks **root.layers** (or parent_layer children), resolves **root.find_linked(layer)** for each, merges **root.positive** with per-region prompts via **workflow.merge_prompt**, and collects **root.control.to_api(bounds, time)** plus per-region control.

A C++ rebuild that omits or alters these helpers will produce wrong region masks, conditioning, or job metadata.

### 13.173 SettingsDialog singleton and show(style)

**SettingsDialog** is a **singleton**: the class stores **\_instance** and **instance()** returns it. The extension constructs it once in the constructor as **SettingsDialog(root.server)** and reuses that instance. There is only one Configure Image Diffusion dialog per Krita session.

**show(style: Style | None = None)**: When the user opens the dialog (e.g. via the Configure button or **ai_diffusion_settings** action), the caller may pass an optional **style**. If **style** is not None, the dialog **opens with the Styles tab selected** (**\_list.setCurrentRow(1)**) and **styles.current_style = style** so that style is shown in the Styles tab (e.g. when the user clicks “edit a copy” for a built-in style, the dialog can open directly to Styles with that style loaded for editing). **read()** and **connection.update_ui()** are called before **show()** so the dialog state is up to date. A C++ rebuild should support the same single instance and **show(style)** so the “edit style” flow opens Configure with the Styles tab and preselected style.

### 13.174 ClientOutput and DummyImage (custom workflow and dry-run)

- **ClientOutput** (`client.py`): Type alias **dict | SharedWorkflow | TextOutput | JobInfoOutput**. When a custom (graph) workflow or other job completes, the client may return a **ClientMessage** with **result: ClientOutput | None**. The UI and **CustomWorkspace.handle_output** dispatch on the actual type (e.g. **JobInfoOutput** for result application with name, offset, batch_mode, resize_canvas). A C++ rebuild that implements custom workflow result handling must support the same union and the same **JobInfoOutput** (and optional **TextOutput** / **SharedWorkflow**) semantics.

- **DummyImage** (`image.py`): Subclass of **Image** used as a placeholder when no real pixel data is needed. Created with **DummyImage(extent)**. Used in **model** when **dryrun=True** (e.g. **\_get_current_image** returns **DummyImage(bounds.extent)** so workflow build can proceed without reading canvas pixels). **ImageCollection** and comparison logic treat **DummyImage** specially (e.g. **pad** returns **DummyImage** for the target extent). A C++ rebuild that supports dry-run or optional image paths should provide an equivalent placeholder image type so workflow validation and parameter resolution behave the same.

### 13.175 Configure dialog navigation list (exact size and order)

The Configure Image Diffusion left navigation list uses **QListWidget** with **setFixedWidth(120)**. Each list item is created with **setSizeHint(QSize(112, 24))** (width 112, height 24). The six items, in order, are: **Connection**, **Styles**, **Diffusion**, **Interface**, **Performance**, **Plugin**. The **Connection** tab content is a **QStackedWidget** with indices **0** = **InitialSetupWidget**, **1** = **CloudWidget**, **2** = **ServerWidget**, **3** = custom server panel (**\_connection_widget**). A C++ rebuild should use the same widths and order so layout and keyboard/screen-reader navigation match.

### 13.176 Document lifecycle, offline behavior, accessibility, and style version

The following behaviors are implied by the codebase but not fully spelled out elsewhere; a C++ rebuild should match them for parity.

- **Document close with pending jobs:** The **JobQueue** is per-**Model**, and the **Model** is per document. When a document is closed, **KritaDocument.is_valid** becomes false (document no longer in **Krita.instance().documents()**). The next time **Root.prune_models()** runs (e.g. when **model_for_active_document()** is called), the **Model** for that document is removed from **Root._models**. Queued and executing jobs for that document are then no longer visible in the UI; the plugin does **not** explicitly cancel those jobs on the ComfyUI (or cloud) server when the document is closed. The client may still process one active job. A C++ rebuild may replicate this (abandon in UI only) or may optionally cancel server-side jobs when the document is closed for stricter resource use.

- **Offline / no network:** When the machine has no network access, the **update check** (§13.37) fails and **UpdateState** becomes **failed_check**. **Cloud** connection attempts fail with normal connection/network error handling. No special "offline" message or mode is required; existing status and error strings (e.g. Connection tab, Welcome view) are sufficient. A C++ rebuild should use the same flow so update and cloud behave consistently when offline.

- **Accessibility:** The plugin relies on standard Qt controls and does not define specific accessibility (a11y) behavior. Settings and controls use **description** text and tooltips where applicable (e.g. in **SettingsTab** and Configure dialog). Focus order follows the tab and control layout. A C++ rebuild should preserve descriptive labels and consider logical focus order for keyboard-only users so that dialogs and the dock remain usable without a mouse.

- **Style preset JSON version:** Built-in and user style presets include a **version** field (integer, e.g. 1 or 2). The plugin uses it for schema compatibility when loading; missing or older-version fields may be filled with defaults. A C++ rebuild that loads the same style files should implement equivalent version handling (e.g. ignore unknown fields, default missing fields) so that style presets round-trip and older presets still load.

### 13.177 Animation timeline (playback_time_range source)

**Document.playback_time_range** returns **(start_frame, end_frame)** for the animation timeline. In **KritaDocument** (`document.py`), this is implemented as **`(self._doc.playBackStartTime(), self._doc.playBackEndTime())`** — i.e. the Krita document’s playback start and end time in frames. **Full Animation** batch generation uses this range to determine how many frames to generate (one job per keyframe in that range). A C++ rebuild must use the same Krita document API (**playBackStartTime**, **playBackEndTime**) so that Animation workspace behavior matches.

### 13.178 Mask operations and Image.compare (regression and tests)

**Mask operations** (`image.py`): **Image** provides class methods for combining mask images used in region and workflow logic. **Image.mask_add(lhs, rhs)** returns a new Image with masks combined using **QPainter.CompositionMode_SourceOver**. **Image.mask_subtract(lhs, rhs)** uses **CompositionMode_SourceOut** so the result has lhs with rhs cut out. Both require same extent; implementation uses **\_mask_op** and QPainter. **Mask** (class at line 762) wraps single-channel mask data; the **Image** mask ops are used when building region masks and in tests. **tests/test_image.py** regression uses **tests/references/mask/mask_op_subtract.png** and **mask_op_add.png** (§13.164).

**Image.compare(img_a, img_b)** (`image.py`): Static method used for regression tests (e.g. control image output). Returns a **float** (RMSE — root mean square error between pixel arrays). Implementation uses numpy: `np.sqrt(np.mean((a - b) ** 2))`; both images must have the same extent (assertion). In **tests/test_workflow.py**, **test_create_control_image** compares result to reference with **threshold 0.015** for pose, **0.005** for other control modes; **Image.compare(result, reference) < threshold**. A C++ rebuild that runs the same regression tests should implement equivalent mask ops and **compare** (same RMSE formula) so test parity holds.

### 13.179 Upscale FactorWidget (scale control layout)

The **Upscale** view scale control is implemented as **FactorWidget** (`ui/upscale.py`): a **slider** (e.g. 1.0–4.0), a **spinbox** showing "Scale: X.XXx", and a **target size** label "Target size: W x H" (computed from document extent × factor). **FactorWidget** syncs slider and spinbox; **value** getter/setter bind to **model.upscale.factor**. **enterEvent** / **leaveEvent** are used for hover state (e.g. optional tooltip or highlight). **UpscaleWidget** embeds **FactorWidget** and binds factor and **target_extent** to the model. A C++ rebuild should provide the same combined control (slider + spinbox + target size display) so the Upscale workspace layout and behavior match (§5.5).

### 13.180 GitHub Actions workflow (exact step names and versions)

The repository CI (**.github/workflows/test.yml**) uses the following exact step and action versions for reference when mirroring CI or test expectations:

- **Checkout:** **actions/checkout@v3** with **submodules: true** (required for websockets bundle).
- **Python:** **actions/setup-python@v5** with **python-version: '3.12'**, **cache: 'pip'**.
- **Typecheck:** **jakebailey/pyright-action@v1** with **pylance-version: latest-release** (not raw pyright).
- **Format:** **ruff format --diff**; **Lint:** **ruff check --output-format=github** (both with **if: !cancelled()**).
- **Test job:** **Free Disk Space** step; **actions/cache@v3** with **path: scripts/downloads**, **key: models-v4**; **Download models:** **python scripts/download_models.py --minimal scripts/downloads**; **Test installer:** **python -m pytest tests/test_server.py -vs --test-install**; **Test:** **python -m pytest tests -vs --ci**.

A C++ project may replicate equivalent steps (format/lint, unit and integration tests, optional server/install test) and use the same test data paths and **--ci** semantics for parity.

### 13.181 Message routing (ClientMessage to Model)

**Connection** emits **message_received(ClientMessage)** for every event from the client (progress, finished, error, etc.). **Root** is connected to this signal and implements **\_handle_message(msg)**. The message carries **job_id** (the workflow prompt_id / job UUID). **Root._find_model(job_id)** looks up which **Model** has that job: it iterates **Root._models** and returns the model for which **model.jobs.find(job_id)** returns a job. Only that **Model** receives the message: **model.handle_message(msg)**. If no model has the job (e.g. document was closed), the message is dropped. **Model.handle_message** then dispatches on **msg.event**: **ClientEvent.queued** → update job state; **progress** → update model.progress; **upload** → progress_kind = upload; **output** → handle custom workflow output; **finished** → save results, update history, emit job_finished; **interrupted** / **error** → _finish_job with error state; **payment_required** → _finish_job with error. A C++ rebuild must implement the same routing (single consumer per job_id) so that progress and results update the correct document’s queue and history.

### 13.182 QueuedJob and server queue semantics

The ComfyUI server accepts **one running job** and **one queued job** at a time (two slots total). **ComfyClient** holds a **QueuedJob** instance representing that single “waiting” slot: when the user queues multiple jobs from the plugin, only one is actually sent to the server; the rest remain in the **Model’s JobQueue** (in-memory list). When the running job completes, the client submits the next job from the plugin’s queue (if any) into the now-free server slot. **QueueMode** (back, front, replace) controls how new jobs are ordered in the plugin’s queue and whether the server queue is cleared before adding: **back** = append to plugin queue; **front** = insert at front (so the new job is submitted to the server next); **replace** = clear server queue (POST **queue** delete) and plugin queue, then add the new job(s). **ClientJobQueue** (in the client) is the asyncio-facing queue fed by **put(job, front=True)** etc.; the plugin’s **JobQueue** (per Model) is the source of jobs that get put into the client. A C++ rebuild must respect the same two-slot server limit and the same ordering/clear semantics so cancel and queue behavior match.

### 13.183 MissingResources structure and usage

**MissingResources** (`client.py`) is an **Exception** subclass holding **missing**: either **list[CustomNode]** (when custom nodes are missing or too old) or **dict[Arch, list[ResourceId]]** (when model files are missing per architecture). **MissingResources.get(arch)** returns **self.missing** if it is a list (same list for all archs), else **self.missing.get(arch, [])**. The Connection tab uses this to display “Detected base models” and “The following ComfyUI custom nodes are missing…” (§13.71). **ComfyClient.connect** raises **MissingResources(missing)** after model discovery if required resources are absent and **check_server_resources** is true; **Connection._connect** catches it and sets **connection.missing_resources** and state to error. **connection.missing_resources** is then used by the Connection tab to render the list (nodes with name/URL vs. per-arch “supported” / “missing &lt;components&gt;”). A C++ rebuild must support both variants and the same **.get(arch)** behavior so the Connection tab and optional connection blocking work correctly.

### 13.184 create_result_layer and ApplyRegionBehavior (per-region apply)

When applying a result that has **JobParams.regions**, **apply_result** calls **create_result_layer(image, params, job_region, region_behavior, prefix)** for each **JobRegion**. **create_result_layer** (model.py) resolves the **region_layer** from **job_region.layer_id** and then branches on **ApplyRegionBehavior**. See also §13.185 for SDXL inpainting (Fooocus) workflow path.

- **replace**: If the region layer is not a group, **LayerManager.update_layer_image(region_layer, image, bounds)** (in-place update). If it is a group, a new layer is created above the group with the result and the group is kept.
- **layer_group**: A new group is created (or an existing group is reused); a new paint layer with the result image is created inside it; the region’s child layers are listed for visibility.
- **transparency_mask**: **region_layer.get_mask(layer_bounds)** is used to get mask data; **LayerManager.create_mask("Transparency Mask", mask, layer_bounds, region_layer)** creates a transparency mask on the region layer.
- **no_hide**: A new layer with the result is created; **unless** **no_hide** or **params.has_mask**, sibling **child_layers** of the region are set **is_visible = False** so only the new result layer is visible in that region.

**RestoreActiveLayer** (context manager) restores the active layer to the one that was active for the region that matched the current active layer, so focus returns to the correct region after apply. A C++ rebuild must implement the same four behaviors and the same region-layer resolution so that “Apply” with regions produces the same layer structure and visibility.

---

### 13.185 SDXL inpainting (Fooocus inpaint conditioning)

For **inpaint** and **refine_region** when **Arch is SDXL**, **use_inpaint_model** is true, and the ComfyUI backend has the inpaint control model, the workflow uses a dedicated inpainting path: **vae_encode_inpaint_conditioning** (encodes image and mask for inpainting), **load_fooocus_inpaint** (loads the Fooocus inpaint patch/model), and **apply_fooocus_inpaint** (applies the patch to the main model). The **models.fooocus_inpaint** resource (from resources.py / presets) identifies the required model file. When this path is used, the conditioning and latent are prepared for inpainting; otherwise the workflow uses **set_latent_noise_mask** on the standard VAE latent. A C++ rebuild that targets the same ComfyUI graph output must emit the same node sequence for SDXL inpainting so that server-side execution matches.

### 13.186 Localization module API

The **localization** system (`ai_diffusion/localization.py`) provides all user-visible strings. A C++ rebuild should implement the same contract for UI parity and translations.

- **Language** (NamedTuple): **id** (str, e.g. `"en"`), **name** (str, e.g. `"English"`), **path** (Path to the JSON file). **Language.from_file(filepath)** loads a language file and returns a Language or None if invalid.
- **Localization** class: Holds **\_id**, **\_name**, **\_translations** (dict key → translated string). **translate(key, \*\*kwargs)** returns `_translations.get(key, key) or key`, then if kwargs are provided, formats the string with `str.format(**kwargs)` (for placeholders like `{count}`). **Localization.load(id, filepath)** reads JSON and returns a Localization instance.
- **Localization.init(settings_path)**: Reads **language** from `settings.json` (default `"en"`), then loads `language/{language}.json` from the plugin’s language directory. Returns the Localization instance or a default (id=`"en"`, name=`"English"`, empty translations) on failure.
- **Localization.scan()**: Scans **language/** for `*.json` files, calls **Language.from_file** on each, returns **list[Language]** of valid entries. Used to populate the Interface tab language dropdown.
- **Localization.available**: Class-level list set to **Localization.scan()** at module load. **Localization.current** is set to **Localization.init()** at module load.
- **translate(key, \*\*kwargs)** (module-level): Wrapper that calls **Localization.current.translate(key, \*\*kwargs)**. All UI strings use `_("...")` which is this function.
- **Language file JSON structure**: **id** (str), **name** (str), **translations** (object: source string key → translation string or null). Keys in translations are the source (English) strings; values are the translated strings. Format placeholders (e.g. `{name}`) must be preserved in translations. See §13.10 for the list of language files; **new_language.json.template** documents the schema for new languages.

### 13.187 Screenshots folder availability

The **screenshots/** folder at the repository root is the reference for UI layout and appearance (§12). This folder **may be empty** in a fresh clone, a minimal source tree, or when reference assets are not committed. When present, the screenshots provide the authoritative visual reference for Configure dialog tabs and dock views. A C++ rebuild should not assume the folder or specific filenames exist; use the spec text and, when available, the screenshot images for parity.

### 13.188 FillMode UI exposure

The **FillMode** enum (§3.4) has seven values: **none**, **neutral**, **blur**, **border**, **replace**, **inpaint**, **green**. The **Generate** view inpaint options (**CustomInpaintWidget**) expose only **five** options in the fill dropdown: **None**, **Neutral**, **Blur**, **Border**, **Inpaint** (see `generation.py` fill_mode_combo). The values **replace** and **green** are used internally by the workflow (e.g. **detect_inpaint** in workflow.py) and are not user-selectable in the UI. A C++ rebuild should show the same five options in the inpaint fill combo for parity.

### 13.189 ui.json top-level structure (serialization contract)

**ModelSync._save()** writes **ui.json** as a single JSON object. Top-level keys and their source:

| Key | Source / content |
|-----|-------------------|
| **version** | Integer constant (e.g. 1); see §13.150. |
| **preview_layer** | Model.preview_layer_id (QUuid string). |
| **inpaint** | Serialized CustomInpaint (mode, fill, use_inpaint, use_prompt_focus, context, context_layer_id). |
| **upscale** | Serialized UpscaleWorkspace (upscaler, factor, use_diffusion, strength, unblur_strength, tile_overlap_mode, tile_overlap, use_prompt). |
| **live** | Serialized LiveWorkspace (strength, etc.). |
| **animation** | Serialized AnimationWorkspace (sampling_quality, target_layer, batch_mode). |
| **custom** | Serialized CustomWorkspace (workflow id, params, etc.). |
| **history** | Array of **_HistoryResult** dicts: id, slot, offsets, params (JobParams), kind, in_use. |
| **root** | Serialized RootRegion (region list container). |
| **edit** | Serialized edit_regions (RootRegion). |
| **control** | Array of serialized ControlLayer for root regions. |
| **regions** | Array of serialized Region; each entry includes nested **control** array for that region. |

In addition, **properties.serialize(model)** writes all **Model** properties that have **persist=True**: workspace, style (filename string), strength, region_only, edit_mode, batch_count, seed, fixed_seed, resolution_multiplier, queue_mode, translation_enabled, layer_count. These appear as top-level keys in the same JSON object. **deserialize** / **serialize** use the **Property** descriptor and **encode_json** for enums/QUuid. A C++ rebuild must produce and consume this same shape so documents open correctly across implementations.

### 13.190 Welcome view layout (exact order)

The **WelcomeWidget** (`ui/diffusion.py`) builds its layout in this order (top to bottom). A C++ rebuild should match this for visual parity:

1. **Header row:** Logo (64×64 from theme.logo()) and title label **"AI Image\nGeneration"** (two lines, font-size 12pt).
2. **Spacing:** 12 px.
3. **AutoUpdateWidget** (visible when update available; see §13.37).
4. **NewsWidget** (visible when client has news and digest ≠ last_news).
5. **ConnectionWidget:** status label, error label (optional), **Configure** button.
6. **Info line:** Links "Interstice.cloud | GitHub Project | Discord" (open external links).

The stack of update/news/connection is additive; when visible, each widget is shown in that order. No spacer between ConnectionWidget and the info line in the code; the info line is added after the connection widget.

### 13.191 Document color mode (exact contract)

**Document.check_color_mode()** returns **tuple[Literal[True], None] | tuple[Literal[False], str]**. Supported configuration: **color model** = **"RGBA"**, **color depth** = **"U8"** (8-bit integer). If the document uses a different model or depth, the method returns **(False, message)** where **message** is built from the format string (see `document.py`): **"Incompatible document: Color {model|depth} must be {RGB/Alpha|8-bit integer} (current {model|depth}: {value})"**. The plugin uses this before running workflows so that generation fails with a clear error instead of corrupt output. A C++ rebuild should enforce the same contract and message format.

### 13.192 Confirm discard image vs Clear History

The setting **confirm_discard_image** (§3.5) gates **only** the single-image **"Discard image"** action (per history thumbnail context menu). When **true**, a **QMessageBox.warning** confirmation is shown before discarding; when **false**, the image is discarded without confirmation. **"Clear History"** (same context menu) **always** shows a confirmation dialog regardless of **confirm_discard_image**. A C++ rebuild should replicate this so that "Discard image" respects the setting and "Clear History" always asks for confirmation.

### 13.193 Animation and Live frame output paths

- **Live workspace:** When recording is enabled, frames are written to **{document_directory}/{document_stem}.live-frames/frame-{N}.webp** (N = 0, 1, 2, …). The folder is created on first record; when recording stops, **import_animation** is called with the list of frame paths (§13.149).
- **Animation workspace (batch):** When generating a full animation, the document must be saved. Frames are written to **{document_parent}/{document_stem}.animation/frame-{frame}.png** (e.g. `/path/to/doc.kra` → `/path/to/doc.animation/frame-0.png`). The folder is created once per batch run; **frame** is the Krita timeline frame index. After all frames complete, **import_animation** is called. A C++ rebuild should use the same paths and naming so that generated animations can be re-imported and match Krita’s animation API.

### 13.194 RecentlyUsedSync fields (complete list)

**RecentlyUsedSync** (`persistence.py`) tracks the following fields; they are stored in **settings.document_defaults** (dict) and applied to a new **Model** when the document has no existing **ui.json**:

| Field | Type / values |
|-------|----------------|
| style | str (style filename) |
| batch_count | int |
| translation_enabled | bool |
| inpaint_mode | str (InpaintMode name) |
| inpaint_fill | str (FillMode name) |
| inpaint_use_model | bool |
| inpaint_use_prompt_focus | bool |
| inpaint_context | str (InpaintContext name) |
| upscale_model | str (upscaler name) |

**from_settings()** initializes from **settings.document_defaults**; **track(model)** connects to model signals and updates these fields (and saves settings) when the user changes style, batch count, inpaint options, or upscaler. A C++ rebuild that implements "recently used" defaults for new documents should persist and apply the same set of fields.

### 13.195 Toggle actions (toggle_workspace, toggle_edit_mode) behavior

**ai_diffusion_toggle_workspace:** When invoked (e.g. via shortcut or menu), **actions.toggle_workspace()** runs. It gets the active document’s model; if present, it sets **model.workspace** to the **next** value in the **Workspace** enum, wrapping around: **generation → upscaling → live → animation → custom → generation**. The implementation uses `list(Workspace)` and `(index + 1) % len(list)`. A C++ rebuild must use the same enum order so that “Toggle workspace” cycles through the same sequence.

**ai_diffusion_toggle_edit_mode:** When invoked, **actions.toggle_edit_mode()** runs. It gets the active document’s model and sets **model.edit_mode = not model.edit_mode**. This only affects behavior when the current workspace is **generation** and the style supports instruction-based editing (**can_toggle_edit**); the Generate view shows the Edit toggle and uses **linked_edit_style** when edit_mode is true. A C++ rebuild should implement the same single boolean flip so shortcuts and menu match.

### 13.196 Prompt widget keyboard and focus behavior

**TextPromptWidget** (`ui/widget.py`) uses the following keyboard and focus behavior for parity:

- **Tab:** **setTabChangesFocus(True)** — Tab moves focus to the next widget, not inserted as a character.
- **Shift+Enter:** Emits **activated** (same as clicking the main Generate action from the prompt context); used to submit from the prompt field without using the mouse.
- **Ctrl+Backspace (DeleteStartOfWord):** Handled via **ShortcutOverride** so the **QPlainTextEdit** (or equivalent) processes it and Krita does not consume it; the plugin accepts the event so the prompt widget can delete the word before the cursor.
- **Completer active:** When **PromptAutoComplete** is showing (tag completion popup), **focusNextPrevChild(next)** returns **False** so Tab does not leave the prompt widget; arrow keys and Enter are used to pick a completion. **action_keys** in the completer determine which keys are passed to the completer instead of the editor.
- **Ctrl+Up / Ctrl+Down:** Documented in §8.5 and §13.35 for attention weight adjustment; **handle_weight_adjustment** uses **select_on_cursor_pos** (parenthesis block or current word) to decide which segment gets the weight change.

A C++ rebuild should replicate Tab focus, Shift+Enter activation, Ctrl+Backspace handling, and completer-open behavior so the prompt field feels the same.

### 13.197 Configure dialog initial size and placement

**SettingsDialog** sets **setMinimumSize(960, 480)**. On **__init__**, it also **resizes** the window: if **QGuiApplication.screenAt(QCursor.pos())** returns a screen, it uses **screen.availableSize()**, sets **min_w = min(size.width(), QFontMetrics(self.font()).width("M") * 100)** (cap width by character count), and calls **self.resize(QSize(min_w, int(size.height() * 0.8)))**. So the dialog opens at up to 80% of the screen height and a width capped by the font-based minimum. A C++ rebuild should apply the same minimum size and optional initial resize so the Configure dialog opens at a usable size on first show.

### 13.198 Reconnection and retry behavior

- **User retry:** The Connection tab (Custom Server and Managed Server panels) provides a **Connect** (or equivalent) button. When in **disconnected** or **error** state, the user can click Connect to call **Connection.connect()**, which runs **\_connect(settings.server_url, settings.server_mode, settings.access_token)** again. No automatic periodic retry is performed; reconnection is user-initiated.
- **Automatic reconnect message:** If the client detects disconnection while in use (e.g. WebSocket closed unexpectedly), the plugin may set **connection.error** to a message such as *"Disconnected from server, trying to reconnect..."* and attempt to reconnect internally in some code paths; the exact conditions are implementation-dependent. A C++ rebuild should at least support user-initiated Connect and document any automatic reconnect behavior for parity.
- **error_kind:** As in §7.4a, **error_kind** (**network**, **missing_resources**, **unknown**) is used so that autostart fallback (e.g. try second URL) runs only for **network** errors.

### 13.199 Document persistence versioning and upgrade

**ui.json** includes a top-level **version** integer (currently **1**; see §13.150). When **ModelSync._load()** (or equivalent) reads **ui.json**:

- **Unknown or future version:** The codebase does not currently implement a formal migration path for **version** &gt; 1. A C++ rebuild should define behavior when the stored **version** is greater than the implementation’s supported version (e.g. show a warning and load with defaults, or ignore unknown keys).
- **Missing or older version:** If **version** is missing or less than the current format version, the implementation may treat it as version 1 and fill missing keys with defaults so that documents saved by older plugin versions still open.
- **Increment on breaking changes:** When the serialized shape of **ui.json** changes in a backward-incompatible way, increment the **version** constant and document the change so that a C++ port can implement the same constant and migration if needed.

### 13.200 Plugin tab documentation links (exact grouping)

The Plugin tab (**AboutSettings**) shows two link groups with exact copy and URLs for parity:

- **First group (_links_text):** "Website" → https://www.interstice.cloud, "Handbook: Guides and Tips" → https://docs.interstice.cloud, "GitHub" → https://github.com/Acly/krita-ai-diffusion (with line breaks &lt;br&gt; between items).
- **Second group (_contact_text):** "Issues" → https://github.com/Acly/krita-ai-diffusion/issues, "Discussions" → https://github.com/Acly/krita-ai-diffusion/discussions, "Discord" → https://discord.gg/pWyzHfHHhU (with line breaks between items).

The UI may render these as clickable links (e.g. QLabel with rich text or openUrl). §4.9 lists "Website, Handbook, GitHub, Issues, Discussions, Discord"; the above gives the exact URL mapping and two-group layout used in the code.

### 13.201 Attention weight selection logic (select_on_cursor_pos)

For **Ctrl+Up** / **Ctrl+Down** in the prompt widget, the segment that receives the weight change is determined by **select_on_cursor_pos(text, cursor_pos)** (`text.py`). Logic:

- Prefer **select_parenthesis_block(text, cursor_pos, ["(", "<"], [")", ">"])** — the smallest parenthesis or angle-bracket block containing the cursor (e.g. `(foo:1.2)` or `<bar>`). If the cursor is inside such a block, that range is used for the weight edit.
- Otherwise use **select_current_word(text, cursor_pos)** — the word (alphanumeric/underscore run) containing the cursor.

So weight adjustment applies to the enclosing `(…)` / `<…>` segment if the cursor is inside one; otherwise to the current word. **edit_attention** then parses that segment, adjusts weight by ±0.1, clamps to [−2.0, 2.0], and re-serializes. Tests in **tests/test_text.py** cover **select_on_cursor_pos** and **select_current_word**. A C++ rebuild should implement the same selection rules so attention editing matches.

### 13.202 File and directory picker dialogs

Besides the confirmation and message dialogs in §13.140, the plugin uses **QFileDialog** in three places. A C++ rebuild should provide equivalent file/directory pickers with the same titles and filters so UX matches.

| Context | API | Title (localized) | Filter / options |
|--------|-----|-------------------|------------------|
| **Import Workflow** (Graph view) | **getOpenFileName** | "Import Workflow" | "Workflow Files (*.json);;All Files (*)"; initial directory: user's home. See §13.167. |
| **Managed server install path** (Connection tab, Local Managed Server) | **getExistingDirectory** | "Select Directory" | **ShowDirsOnly**; user selects the folder where the managed ComfyUI server will be installed (saved as **server_path**). |
| **Select LoRA file** (Styles tab, add LoRA from file) | **getOpenFileName** | "Select LoRA file" | "LoRA files (*.safetensors)". The selected file is added to the style's LoRA list (and may be uploaded to server/cloud per client). |

Implement the same titles and filters so that file chooser behavior and accessibility (e.g. screen readers) match the reference.

### 13.203 WorkflowInput serialization: image format and max_image_size

When the plugin serializes a **WorkflowInput** via **to_dict(image_format, max_image_size)** (implemented by **Serializer** in api.py):

- **image_format** (default **ImageFileFormat.webp**): Controls the format used when embedding image data (initial_image, masks, control images) as base64 in the serialized payload. **Serializer.run(work, image_format)** encodes **Image** fields into an **image_data** blob (bytes + offsets). Omit or use webp for parity with the reference.
- **max_image_size** (default **0**): When **max_image_size > 0**, **\_check_image_size(workflow, max_image_size)** runs before serialization. It checks **workflow.extent** (input, initial, desired, target) and **workflow.crop_upscale_extent**; if any **Extent** has **longest_side > max_image_size**, it raises **ValueError** with message *"Image size WxH exceeds maximum of N"*. This prevents oversize payloads.

**CloudClient** calls **job.work.to_dict(max_image_size=16*1024)** when sending the workflow to the cloud backend, so the cloud enforces a maximum dimension of **16384** pixels. A C++ rebuild that implements cloud submission should use the same limit (or the backend’s documented limit) so behavior and error messages match.

### 13.204 Plugin version single source of truth

The plugin’s user-facing version string (e.g. **1.49.0**) is defined **only** in **ai_diffusion/__init__.py** as **__version__**. The Configure dialog footer, Plugin tab, update check, and diagnostics all use this value. A C++ rebuild should define the version in one place and reference it everywhere so packaging and “Plugin version: X.Y.Z” stay consistent.

### 13.205 Automatic inpaint mode (detect_inpaint_mode)

When the user selects **InpaintMode.automatic**, the effective mode is derived from document extent and selection area. **detect_inpaint_mode(extent: Extent, area: Bounds)** in **workflow.py** returns:

- **InpaintMode.expand** — when **area.width >= extent.width** OR **area.height >= extent.height** (selection touches or exceeds document bounds in at least one dimension).
- **InpaintMode.fill** — otherwise.

This is used when building **WorkflowInput** so that "automatic" inpaint behaves as expand for outpainting-style selections and fill for in-canvas selections. A C++ rebuild must implement the same heuristic so automatic inpaint mode matches.

### 13.206 detect_inpaint() — InpaintParams derivation from mode, arch, and conditioning

When **InpaintMode** is not automatic, **workflow.detect_inpaint(mode, bounds, arch, cond, strength)** builds **InpaintParams** with mode-specific **fill**, **use_inpaint_model**, **use_condition_mask**, and **use_reference**. A C++ rebuild that produces equivalent ComfyUI graphs must replicate this logic.

- **Fill by mode (when cond.edit_reference is False):** fill → **FillMode.blur**; expand → **FillMode.border**; add_object → **FillMode.neutral**; remove_object → **FillMode.inpaint**; replace_background → **FillMode.replace**. For fill or expand with **Arch.flux2_4b** and **edit_reference True**: **FillMode.green**, **use_inpaint_model = True**. For any mode with **edit_reference True** (other arch): **FillMode.none**.
- **use_reference:** True when mode is fill or expand and **cond.positive == ""**.
- **use_inpaint_model by arch:** **sd15**: strength > 0.5; **sdxl-like**: strength > 0.8; **flux / zimage**: strength == 1.0; flux2_4b with edit_reference as above.
- **use_condition_mask (SD1.5 only):** True when mode is add_object, **cond.positive != ""**, and no structural control in **cond.control**.
- **Edit models (arch.is_edit):** **result.mode = InpaintMode.custom**, **result.fill = FillMode.none**.

Used by **model** when resolving inpaint params for the workflow. See **workflow.detect_inpaint** and **model._get_inpaint_params** for the full dispatch.

### 13.207 create_region action and RootRegion.create_region / create_region_layer / create_region_group

The **ai_diffusion_create_region** action calls **actions.create_region()**, which invokes **model.regions.create_region(group=(model.workspace != Workspace.live))**. So:

- **Live workspace:** **group=False** — creates a single new paint layer linked as a region ("Region N").
- **Other workspaces (Generate, Upscale, Animation, Graph):** **group=True** — creates a new group with a paint layer inside, or links the active layer if it can be used as the region link target.

**RootRegion** (region.py) also exposes:

- **create_region_layer()** — calls **create_region(group=False)** (new layer only).
- **create_region_group()** — calls **create_region(group=True)** (new group + layer).

**create_region(group)** behavior: **link target** = **Region.link_target(layers.active)** (e.g. active layer or its group). If that target is a paint or group layer and not already linked to a region, the new region is linked to it and no new layer is created. Otherwise: if **group** is True, **layers.create_group("Region {len(self)}")** and **layers.create("Paint layer", parent=layer)**; if **group** is False, **layers.create("Region {len(self)}")**. Then **\_add(layer)** adds the region to the root. A C++ rebuild should implement the same action dispatch and **create_region** / **create_region_layer** / **create_region_group** semantics so that "Create region" and UI "Add region" buttons behave the same.

### 13.208 Windows managed server: job object (attach_process_to_job)

On **Windows**, when the **managed** (local) server starts the ComfyUI subprocess, **platform_tools** (or equivalent) calls **win32.attach_process_to_job(pid)** so the child process is attached to a **job object** with **JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE**. When Krita (the parent) exits or is killed, the job is closed and the ComfyUI process is terminated. **win32.py** uses **CreateJobObjectA**, **SetInformationJobObject** with **JOBOBJECT_EXTENDED_LIMIT_INFORMATION** and **LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE**, **AssignProcessToJobObject**, and **OpenProcess(PROCESS_TERMINATE | PROCESS_SET_QUOTA)**. A C++ rebuild that implements the managed server on Windows should attach the server process to a job object with the same limit so that closing Krita cleans up the server process; on non-Windows platforms this is a no-op.

### 13.209 Seed value range (generate_seed)

**workflow.generate_seed()** returns **random.randint(0, 2\*\*31 - 1)** — a 32-bit non-negative integer. The comment in code notes that Qt widgets do not support int64, so the plugin uses 32-bit seeds. Seeds are stored and displayed as integers in this range; wildcard evaluation and reproducibility use the same value. A C++ rebuild should use the same range (e.g. 0 to 2^31 − 1) so that seeds round-trip correctly in the UI and match behavior when reopening documents or replaying workflows.

### 13.210 GenerateButton hover cost display (cloud)

When the client is the **Online Service (cloud)** and **client.user** is set, the **GenerateButton** (and equivalent primary action buttons in other workspaces) shows the **estimated token cost** on **mouse hover**. **model.estimate_cost(kind)** is called with the current workspace’s **JobKind** (e.g. diffusion, upscaling); it uses **dryrun=True** (see §13.211) to build a **WorkflowInput** without reading canvas pixels and returns **work.cost**. The button paints the cost value (integer) and the **interstice** icon (**theme.icon("interstice")**) on the **right side** of the button (e.g. after the seed display), only when **hovered** and **cost > 0**. A C++ rebuild that supports cloud should show the same cost-on-hover so users see token usage before generating.

### 13.211 dryrun in _prepare_workflow and estimate_cost

**Model._prepare_workflow(dryrun=False)** is used both for real generation and for **estimate_cost**. When **dryrun=True**: (1) **conditioning** and **job_regions** are not fully built — **process_regions** is skipped and **conditioning** is an empty **ConditioningInput**, **job_regions** an empty list; (2) **image** is not read from the document — **\_get_current_image** returns **DummyImage(bounds.extent)** so no pixel read occurs; (3) **prepare_prompts** and the rest of the workflow input build still run so **WorkflowInput** has a valid **cost**. **estimate_cost(kind)** calls **\_prepare_workflow(dryrun=True)** for **JobKind.diffusion**, **\_prepare_upscale_image(dryrun=True)** for upscaling, and equivalent for custom workflow, then returns **work.cost**. A C++ rebuild that implements cloud token display must use the same dry-run semantics so cost is computed without document/canvas access and without submitting a job.

### 13.212 Batch seed increment and wildcard re-evaluation

When **enqueue_jobs** is called with **count > 1** (batch generation), each job in the batch gets a distinct seed so outputs vary. The seed for the **i**-th job (0-based) is **`sampling.seed + i * settings.batch_size`**. For **i > 0**, prompts are re-evaluated with the new seed: **prepare_prompts(original_cond, style, seed, arch, inpaint_mode)** is called so that **eval_wildcards** in positive/negative and region prompts produces different choices per image. **params.metadata** and **params.name** are updated from the new prompt result. A C++ rebuild must use the same seed formula and re-run prompt preparation (including wildcards) for each batch index so batch generation matches the reference behavior and remains reproducible for a given base seed.

### 13.213 In-dock resolution multiplier (QueuePopup)

The **QueuePopup** (opened from the Generate button area or queue control) can show a **resolution multiplier** control when the performance preset is **Custom**. In the reference implementation this is a **QSlider** with integer range **3–15**; the displayed value is **value/10** (e.g. "1.0 x") and the stored **model.resolution_multiplier** is **value/10** (float 0.3–1.5). The Performance tab (Configure dialog) uses a float slider 0.3–1.5 directly. A C++ rebuild should use the same effective range (0.3–1.5) and, if using an integer slider in the dock, the same mapping (e.g. 3–15 → 0.3–1.5) so values round-trip correctly between dock and settings.

### 13.214 compute_batch_size (effective batch for extent)

**resolution.compute_batch_size(extent, min_size, max_batches)** computes the actual batch count used when building the workflow: it constrains the number of latent samples so that the effective extent (e.g. after tiling or scaling) does not exceed available capacity. The workflow uses **compute_batch_size(largest_extent, 512, perf.batch_size)** to set **WorkflowInput.batch_count**. A C++ rebuild that builds workflows with batch_count must use the same logic so that multi-image generation respects the performance preset and extent.

### 13.215 Tag file truncation and loading (tags/README)

The **tags/README.md** in **ai_diffusion/tags/** documents how tag CSV files are produced and optionally **truncated**. When loading tags for autocomplete, the plugin does not necessarily apply the README’s truncation (that is a build-time step for the CSV). Tag files have columns **tag**, **type**, **count**, **aliases** (§13.89). The README also describes filtering (e.g. general/meta with count &lt; 20, others with count &lt; 50) used when generating the CSVs. A C++ rebuild need only support the same CSV format and column semantics for autocomplete; truncation is relevant only if regenerating tag data from upstream sources.

### 13.216 Docs structure (docs/ as Astro project)

The **docs/** directory at the repository root is an **Astro** project (**package.json**, **astro.config.mjs**, **tsconfig.json**) with **src/**, **public/**, and **scripts/** subdirectories. It builds the user-facing documentation site (e.g. Handbook, installation, ComfyUI setup). The spec and rebuild focus on plugin behavior; the docs tree is not part of the plugin payload. A C++ rebuild that ships or links to user documentation may mirror the same content (e.g. installation, common issues) without replicating the Astro build; see §13.57 and §2.5 for what to mirror.

---

## 14. C++ and Porting Notes

The following mappings help when rebuilding the plugin in C++ (e.g. as a Krita C++/Qt plugin or a hybrid with a small Python bridge).

| Python / runtime concept | C++ / Qt equivalent or approach |
|--------------------------|-----------------------------------|
| **asyncio** (ComfyClient connect, queue, WebSocket) | Drive async I/O via **QTimer** (e.g. 20 ms) calling a step function; or use **QEventLoop::processEvents()** while waiting; or **QNetworkAccessManager** + **QWebSocket** with single-threaded event loop. The Python code uses **eventloop.run(coro)** and **process_python_events()** so the loop runs inside Qt. |
| **websockets** (ComfyUI WebSocket client) | **QWebSocket** (Qt Network). Connect to `{base_url}/ws?clientId={uuid}`; handle text/binary frames and the same message types (§7.7). |
| **JSON with comments** (settings, workflow JSON files) | **read_json_with_comments**: strip **whole lines** whose stripped form starts with **`//`** (see §13.135). For **prompt** text, **strip_prompt_comments** uses **`#`** and rest of line (respecting `\#`). Parse JSON with a standard library after stripping. |
| **Krita Python API** (Document, Node, Layer, Selection, etc.) | Krita C++ plugin API: **Krita::Document**, **Node**, **Layer**, selection and annotation APIs. Use the same annotation key prefix **ai_diffusion/** and the same semantics for **setAnnotation(name, description, value)** / **annotation(name)** / **removeAnnotation(name)**. |
| **PyQt5 signals/slots** | **Qt signals and slots**; **Q_PROPERTY** with NOTIFY for observable properties where the Python code uses **Property** and **changed** signals. |
| **Model–widget binding** | **properties.bind** / **bind_combo** / **bind_toggle** (§13.104): use Qt signal/slot or QDataWidgetMapper-style two-way binding so model and widgets stay in sync; **serialize** / **deserialize** for persist-marked properties. |
| **Plugin entry** | Python: **addExtension**, **addDockWidgetFactory** at import time. C++: Krita's C++ plugin API for extensions and dock factories (e.g. **Krita::Plugin**). |
| **Bundled websockets** | No bundle in C++; use Qt's **QWebSocket** or another C++ WebSocket client. |

- **Threading**: The Python plugin uses **multi_threading** (settings) for some background work; the main ComfyUI client runs on the asyncio loop driven by the Qt thread. In C++, use **QThread** or worker objects if needed; keep UI and client message handling on the main thread where possible.
- **Paths**: Resolve **plugin_dir** (plugin binary/resources location), **user_data_dir** (§13.66), and **log_dir** the same way so settings, server install, styles, and logs match the Python layout.
- **Tests**: The **tests/** directory and **tests/data/**, **tests/images/**, **tests/references/** are the behavioral reference. A C++ project may add its own tests; reusing the same input JSON and reference images helps ensure parity.

---

## 15. Implementation Order and Agent Guidance

For an engineer or agent rebuilding this plugin (e.g. in C++):

- **Primary checklist:** Use **§11 Rebuild Checklist** as the main ordered list of features to implement; items there are ordered by dependency and user visibility.
- **Suggested minimal path:** (1) Plugin shell, settings load/save, and connection (Custom Server only) with ComfyUI client and WebSocket. (2) Single workspace (Generate) with one style, prompt, strength, seed, and Generate button; job queue and history with apply. (3) Document persistence (ui.json, result{N}.webp) and per-document model. (4) Remaining workspaces (Upscale, Live, Animation, Graph), then Styles tab, then Connection (Managed, Cloud), then control layers and regions, then dialogs and polish.
- **Reference artifacts:** Use **screenshots/** (§12) for UI layout; **tests/data/**, **tests/images/**, **tests/references/** for inputs and expected outputs; **§13** for behavior not fully spelled out in earlier sections.
- **Spec as source of truth:** When the codebase and this document differ, treat this specification and the **ai_diffusion/** source as the source of truth; resolve ambiguities by inspecting the repo and updating the spec if needed.

---

This specification, together with the codebase and screenshots, is intended to be sufficient to rebuild the Krita AI Diffusion plugin with equivalent functionality and UI.

---

**Criticality of the missing sections (for C++ / agent rebuild):** **2.8**. The document was already highly complete relative to the repo. The missing pieces added in this pass were: **§13.210 GenerateButton hover cost display** — where and when the cloud token cost is shown on the Generate button (hover, icon + number); **§13.211 dryrun in _prepare_workflow** — how estimate_cost uses dryrun=True to compute cost without reading the canvas; **§13.212 Batch seed increment and wildcard re-evaluation** — seed formula and prompt re-evaluation per batch index (important for correct batch generation and reproducibility); **§13.213 In-dock resolution multiplier** — slider range 3–15 mapping to 0.3–1.5 in QueuePopup; **§13.214 compute_batch_size** — how effective batch count is derived from extent and performance settings; **§13.215 Tag file truncation** — reference to tags/README for CSV format and generation; **§13.216 Docs structure** — docs/ as Astro project. Of these, §13.212 is the most critical for behavioral parity (batch generation would otherwise use the same seed and wildcards for every image). The rest reduce ambiguity for UI and build parity. Overall the spec is sufficient for an engineer or agent to recreate functionality and look-and-feel in C++; the missing items were mostly edge clarifications rather than fundamental gaps.
