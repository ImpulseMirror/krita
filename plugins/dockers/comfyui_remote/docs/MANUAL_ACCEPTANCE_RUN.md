# Manual acceptance run log (M1–M10)

**Branch:** `comfyui-perfect-port` @ `6a1dc7837d`  
**Started:** 2026-05-24  
**Matrix:** [MANUAL_ACCEPTANCE_MATRIX.md](MANUAL_ACCEPTANCE_MATRIX.md)  
**Tracking:** `port_progress.json` → `acceptance_manual[]`

## Prerequisites

1. ComfyUI + ETN / AI Diffusion custom nodes.
2. SDXL checkpoint on server (M2).
3. Krita built from this branch with `comfyui_remote` docker.
4. Preflight: `COMFY_URL=http://127.0.0.1:8188 ./scripts/manual_acceptance_preflight.sh`

## Automated subset (before GUI)

| Check | Command | Maps to |
|-------|---------|---------|
| Golden fixtures | `python3 plugins/dockers/comfyui_remote/scripts/export_workflow_fixture.py` | workflow parity |
| Unit tests | `plugins/dockers/comfyui_remote/scripts/build_verify.sh` or `cd build && ctest -R 'ComfyPort\|ComfyWorkflow\|ComfyUIRemoteDock'` | P5.1, M1 HTTP mock (P5.2) |

## Run order

| # | ID | Desktop | Android | Result | Notes |
|---|-----|---------|---------|--------|-------|
| 1 | M1 | ☐ | ☐ | | Connect URL, models, disconnect |
| 2 | M2 | ☐ | ☐ | | SDXL style, generate, history |
| 3 | M3 | ☐ | ☐ | | Inpaint modes + save/reload |
| 4 | M4 | ☐ | ☐ | | Regions + control generate |
| 5 | M5 | ☐ | ☐ | | Live record + ui.json |
| 6 | M6 | ☐ | ☐ | | Upscale + refine/tiled |
| 7 | M7 | ☐ | ☐ | | Animation batch |
| 8 | M8 | ☐ | ☐ | partial | Custom JSON workflow |
| 9 | M9 | ☐ | ☐ | | History + .kra roundtrip |
| 10 | M10 | ☐ | ☐ | | Settings + language restart |

After each scenario, set `manual_status` / `tested_on` / `notes` in `port_progress.json`.

## Session log

### 2026-05-24 — campaign start

- Pushed port commit `6a1dc7837d`.
- Preflight: ComfyUI not reachable at default URL; `_build` cache path stale (`/krita/_build` vs local tree) — **desktop GUI tests blocked until ComfyUI up + fresh cmake build**.
