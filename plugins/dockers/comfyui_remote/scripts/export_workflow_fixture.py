#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Krita Project
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export Comfy API workflow golden fixtures for ComfyWorkflowEngineGoldenTest.

Writes C++-canonical graphs (same templates as ComfyUIWorkflows.cpp + ComfyWorkflowEngine)
into tests/data/golden/*.api.json.

Optional: pass --reference <krita-ai-diffusion> to also write python/*.api.json for
workflows that can be built offline (e.g. upscale_simple via object_info.json).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

PLUGIN_ROOT = Path(__file__).resolve().parents[1]
GOLDEN_DIR = PLUGIN_ROOT / "tests" / "data" / "golden"

# Mirrors plugins/dockers/comfyui_remote/ComfyUIWorkflows.cpp templates
DEFAULT_WORKFLOW: dict[str, Any] = {
    "3": {
        "class_type": "KSampler",
        "inputs": {
            "cfg": 8,
            "denoise": 1,
            "latent_image": ["5", 0],
            "model": ["4", 0],
            "negative": ["7", 0],
            "positive": ["6", 0],
            "sampler_name": "euler",
            "scheduler": "normal",
            "seed": 0,
            "steps": 20,
        },
    },
    "4": {
        "class_type": "CheckpointLoaderSimple",
        "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"},
    },
    "5": {
        "class_type": "EmptyLatentImage",
        "inputs": {"batch_size": 1, "height": 512, "width": 512},
    },
    "6": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["4", 1], "text": ""},
    },
    "7": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["4", 1], "text": ""},
    },
    "8": {
        "class_type": "VAEDecode",
        "inputs": {"samples": ["3", 0], "vae": ["4", 2]},
    },
    "9": {
        "class_type": "SaveImage",
        "inputs": {"filename_prefix": "ComfyUI", "images": ["8", 0]},
    },
}

IMG2IMG_WORKFLOW: dict[str, Any] = {
    "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
    "2": {"class_type": "VAEEncode", "inputs": {"pixels": ["1", 0], "vae": ["3", 2]}},
    "3": {
        "class_type": "CheckpointLoaderSimple",
        "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"},
    },
    "4": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["3", 1], "text": "PROMPT_PLACEHOLDER"},
    },
    "5": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["3", 1], "text": "NEGATIVE_PLACEHOLDER"},
    },
    "6": {
        "class_type": "KSampler",
        "inputs": {
            "cfg": 8,
            "denoise": 0.75,
            "latent_image": ["2", 0],
            "model": ["3", 0],
            "negative": ["5", 0],
            "positive": ["4", 0],
            "sampler_name": "euler",
            "scheduler": "normal",
            "seed": 0,
            "steps": 20,
        },
    },
    "7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["3", 2]}},
    "8": {
        "class_type": "SaveImage",
        "inputs": {"filename_prefix": "ComfyUI_live", "images": ["7", 0]},
    },
}

INPAINT_WORKFLOW: dict[str, Any] = {
    "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
    "2": {"class_type": "LoadImage", "inputs": {"image": "MASK_PLACEHOLDER"}},
    "4": {
        "class_type": "CheckpointLoaderSimple",
        "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"},
    },
    "5": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["4", 1], "text": "PROMPT_PLACEHOLDER"},
    },
    "6": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["4", 1], "text": "NEGATIVE_PLACEHOLDER"},
    },
    "7": {
        "class_type": "VAEEncodeForInpaint",
        "inputs": {
            "grow_mask_by": 6,
            "mask": ["2", 1],
            "pixels": ["1", 0],
            "vae": ["4", 2],
        },
    },
    "8": {
        "class_type": "KSampler",
        "inputs": {
            "cfg": 8,
            "denoise": 1,
            "latent_image": ["7", 0],
            "model": ["4", 0],
            "negative": ["6", 0],
            "positive": ["5", 0],
            "sampler_name": "euler",
            "scheduler": "normal",
            "seed": 0,
            "steps": 20,
        },
    },
    "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["4", 2]}},
    "10": {
        "class_type": "SaveImage",
        "inputs": {"filename_prefix": "ComfyUI_region", "images": ["9", 0]},
    },
}

UPSCALE_SIMPLE: dict[str, Any] = {
    "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
    "2": {
        "class_type": "ImageScale",
        "inputs": {
            "height": 1024,
            "image": ["1", 0],
            "upscale_method": "lanczos",
            "width": 1024,
        },
    },
    "3": {
        "class_type": "SaveImage",
        "inputs": {"filename_prefix": "ComfyUI_upscale", "images": ["2", 0]},
    },
}

UPSCALE_REFINE: dict[str, Any] = {
    "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
    "2": {
        "class_type": "ImageScale",
        "inputs": {
            "height": 1024,
            "image": ["1", 0],
            "upscale_method": "lanczos",
            "width": 1024,
        },
    },
    "3": {"class_type": "VAEEncode", "inputs": {"pixels": ["2", 0], "vae": ["4", 2]}},
    "4": {
        "class_type": "CheckpointLoaderSimple",
        "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"},
    },
    "5": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["4", 1], "text": "PROMPT_PLACEHOLDER"},
    },
    "6": {
        "class_type": "CLIPTextEncode",
        "inputs": {"clip": ["4", 1], "text": "NEGATIVE_PLACEHOLDER"},
    },
    "7": {
        "class_type": "KSampler",
        "inputs": {
            "cfg": 8,
            "denoise": 0.35,
            "latent_image": ["3", 0],
            "model": ["4", 0],
            "negative": ["6", 0],
            "positive": ["5", 0],
            "sampler_name": "euler",
            "scheduler": "normal",
            "seed": 0,
            "steps": 20,
        },
    },
    "8": {"class_type": "VAEDecode", "inputs": {"samples": ["7", 0], "vae": ["4", 2]}},
    "9": {
        "class_type": "SaveImage",
        "inputs": {"filename_prefix": "ComfyUI_upscale_refine", "images": ["8", 0]},
    },
}


def _deep_copy(obj: Any) -> Any:
    return json.loads(json.dumps(obj))


def _remap_value(v: Any, id_map: dict[str, str]) -> Any:
    if isinstance(v, list) and len(v) >= 2 and isinstance(v[0], str):
        return [id_map.get(v[0], v[0]), v[1]]
    if isinstance(v, dict):
        return {k: _remap_value(val, id_map) for k, val in v.items()}
    if isinstance(v, list):
        return [_remap_value(item, id_map) for item in v]
    return v


def normalize_api_workflow(workflow: dict[str, Any]) -> dict[str, Any]:
    entries = []
    for nid, node in workflow.items():
        inputs = node.get("inputs", {})
        sort_key = json.dumps(
            {"class_type": node["class_type"], "inputs": inputs}, sort_keys=True, separators=(",", ":")
        )
        entries.append((node["class_type"], sort_key, nid, node["class_type"], inputs))
    entries.sort(key=lambda e: (e[0], e[1]))
    id_map = {old_id: str(i + 1) for i, (_, _, old_id, _, _) in enumerate(entries)}
    out: dict[str, Any] = {}
    for i, (_, _, old_id, class_type, inputs) in enumerate(entries):
        new_id = str(i + 1)
        out[new_id] = {
            "class_type": class_type,
            "inputs": _remap_value(inputs, id_map),
        }
    return out


def _replace_input_link(workflow: dict[str, Any], old: list, new: list) -> None:
    old_id, old_out = old[0], old[1]
    new_id, new_out = new[0], new[1]
    for node in workflow.values():
        inputs = node.get("inputs", {})
        for key, val in list(inputs.items()):
            if isinstance(val, list) and len(val) >= 2 and val[0] == old_id and val[1] == old_out:
                inputs[key] = [new_id, new_out]


def _next_id(workflow: dict[str, Any], start: int = 500) -> int:
    while str(start) in workflow:
        start += 1
    return start


def replace_ksampler_with_custom(
    workflow: dict[str, Any],
    ksampler_id: str,
    arch: str,
    extent_w: int,
    extent_h: int,
) -> None:
    ks = workflow.get(ksampler_id)
    if not ks or ks.get("class_type") != "KSampler":
        return
    inp = ks["inputs"]
    model_id = inp["model"][0]
    pos_id = inp["positive"][0]
    neg_id = inp["negative"][0]
    latent_id = inp["latent_image"][0]
    cfg = float(inp.get("cfg", 7.0))
    steps = int(inp.get("steps", 20))
    denoise = float(inp.get("denoise", 1.0))
    sampler = inp.get("sampler_name", "euler")
    scheduler = inp.get("scheduler", "normal")
    seed = int(inp.get("seed", 0))
    start_at = round(steps * (1.0 - max(0.01, min(1.0, denoise))))

    nid = _next_id(workflow)
    guided_pos = pos_id
    if arch == "flux":
        guided_pos = str(nid)
        workflow[guided_pos] = {
            "class_type": "FluxGuidance",
            "inputs": {"conditioning": [pos_id, 0], "guidance": cfg if cfg > 1.0 else 3.5},
        }
        nid += 1

    guider_id = str(nid)
    nid += 1
    if arch == "flux" or abs(cfg - 1.0) < 0.001:
        workflow[guider_id] = {
            "class_type": "BasicGuider",
            "inputs": {"model": [model_id, 0], "conditioning": [guided_pos, 0]},
        }
    else:
        workflow[guider_id] = {
            "class_type": "CFGGuider",
            "inputs": {
                "model": [model_id, 0],
                "positive": [pos_id, 0],
                "negative": [neg_id, 0],
                "cfg": cfg,
            },
        }

    sigmas_id = str(nid)
    nid += 1
    if scheduler.strip().lower() == "flux2":
        workflow[sigmas_id] = {
            "class_type": "Flux2Scheduler",
            "inputs": {"steps": steps, "width": max(64, extent_w), "height": max(64, extent_h)},
        }
    else:
        workflow[sigmas_id] = {
            "class_type": "BasicScheduler",
            "inputs": {"model": [model_id, 0], "scheduler": scheduler, "steps": steps, "denoise": 1.0},
        }

    sigmas_link, sigmas_out = sigmas_id, 0
    if start_at > 0:
        split_id = str(nid)
        nid += 1
        workflow[split_id] = {
            "class_type": "SplitSigmas",
            "inputs": {"sigmas": [sigmas_id, 0], "step": start_at},
        }
        sigmas_link, sigmas_out = split_id, 1

    noise_id = str(nid)
    nid += 1
    workflow[noise_id] = {
        "class_type": "RandomNoise",
        "inputs": {"noise_seed": seed},
    }

    sampler_select_id = str(nid)
    nid += 1
    if sampler == "euler_cfgpp":
        workflow[sampler_select_id] = {
            "class_type": "SamplerEulerCFGpp",
            "inputs": {"version": "regular"},
        }
    else:
        workflow[sampler_select_id] = {
            "class_type": "KSamplerSelect",
            "inputs": {"sampler_name": sampler},
        }

    advanced_id = str(nid)
    workflow[advanced_id] = {
        "class_type": "SamplerCustomAdvanced",
        "inputs": {
            "noise": [noise_id, 0],
            "guider": [guider_id, 0],
            "sampler": [sampler_select_id, 0],
            "sigmas": [sigmas_link, sigmas_out],
            "latent_image": [latent_id, 0],
        },
    }

    del workflow[ksampler_id]
    _replace_input_link(workflow, [ksampler_id, 0], [advanced_id, 1])


def _finish_workflow(wf: dict[str, Any], ksampler_id: str, arch: str, w: int, h: int) -> dict[str, Any]:
    replace_ksampler_with_custom(wf, ksampler_id, arch, w, h)
    return wf


def build_text_to_image() -> dict[str, Any]:
    wf = _deep_copy(DEFAULT_WORKFLOW)
    wf["3"]["inputs"]["seed"] = 1234
    wf["3"]["inputs"]["steps"] = 20
    wf["3"]["inputs"]["cfg"] = 7.0
    wf["3"]["inputs"]["denoise"] = 1.0
    wf["3"]["inputs"]["sampler_name"] = "euler"
    wf["3"]["inputs"]["scheduler"] = "normal"
    wf["4"]["inputs"]["ckpt_name"] = "v1-5-pruned-emaonly.safetensors"
    wf["5"]["inputs"]["width"] = 512
    wf["5"]["inputs"]["height"] = 512
    wf["5"]["inputs"]["batch_size"] = 1
    wf["6"]["inputs"]["text"] = "golden positive"
    wf["7"]["inputs"]["text"] = "golden negative"
    return _finish_workflow(wf, "3", "sd15", 512, 512)


def build_sdxl_text_to_image() -> dict[str, Any]:
    wf = _deep_copy(DEFAULT_WORKFLOW)
    wf["3"]["inputs"]["seed"] = 1234
    wf["3"]["inputs"]["steps"] = 20
    wf["3"]["inputs"]["cfg"] = 7.0
    wf["3"]["inputs"]["denoise"] = 1.0
    wf["3"]["inputs"]["sampler_name"] = "euler"
    wf["3"]["inputs"]["scheduler"] = "normal"
    wf["4"]["inputs"]["ckpt_name"] = "zavychromaxl_v80.safetensors"
    wf["5"]["inputs"]["width"] = 1024
    wf["5"]["inputs"]["height"] = 768
    wf["5"]["inputs"]["batch_size"] = 1
    wf["6"]["inputs"]["text"] = "golden positive"
    wf["7"]["inputs"]["text"] = "golden negative"
    return _finish_workflow(wf, "3", "sdxl", 1024, 768)


def build_flux_text_to_image() -> dict[str, Any]:
    wf = _deep_copy(DEFAULT_WORKFLOW)
    wf["3"]["inputs"]["seed"] = 1234
    wf["3"]["inputs"]["steps"] = 4
    wf["3"]["inputs"]["cfg"] = 3.5
    wf["3"]["inputs"]["denoise"] = 1.0
    wf["3"]["inputs"]["sampler_name"] = "euler"
    wf["3"]["inputs"]["scheduler"] = "normal"
    wf["4"]["inputs"]["ckpt_name"] = "flux1-schnell.safetensors"
    wf["5"]["inputs"]["width"] = 1024
    wf["5"]["inputs"]["height"] = 768
    wf["5"]["inputs"]["batch_size"] = 1
    wf["6"]["inputs"]["text"] = "golden positive"
    wf["7"]["inputs"]["text"] = ""
    return _finish_workflow(wf, "3", "flux", 1024, 768)


def build_refine() -> dict[str, Any]:
    wf = _deep_copy(IMG2IMG_WORKFLOW)
    wf["1"]["inputs"]["image"] = "golden_canvas.png"
    wf["3"]["inputs"]["ckpt_name"] = "v1-5-pruned-emaonly.safetensors"
    wf["4"]["inputs"]["text"] = "golden positive"
    wf["5"]["inputs"]["text"] = "golden negative"
    wf["6"]["inputs"]["seed"] = 1234
    wf["6"]["inputs"]["steps"] = 20
    wf["6"]["inputs"]["cfg"] = 7.0
    wf["6"]["inputs"]["denoise"] = 0.4
    wf["6"]["inputs"]["sampler_name"] = "euler"
    wf["6"]["inputs"]["scheduler"] = "normal"
    return _finish_workflow(wf, "6", "sd15", 1024, 1024)


def build_inpaint() -> dict[str, Any]:
    wf = _deep_copy(INPAINT_WORKFLOW)
    wf["1"]["inputs"]["image"] = "golden_canvas.png"
    wf["2"]["inputs"]["image"] = "golden_mask.png"
    wf["4"]["inputs"]["ckpt_name"] = "v1-5-pruned-emaonly.safetensors"
    wf["5"]["inputs"]["text"] = "golden positive"
    wf["6"]["inputs"]["text"] = "golden negative"
    wf["7"]["inputs"]["grow_mask_by"] = 12
    wf["8"]["inputs"]["seed"] = 1234
    wf["8"]["inputs"]["steps"] = 20
    wf["8"]["inputs"]["cfg"] = 7.0
    wf["8"]["inputs"]["denoise"] = 0.6
    wf["8"]["inputs"]["sampler_name"] = "euler"
    wf["8"]["inputs"]["scheduler"] = "normal"
    return _finish_workflow(wf, "8", "sd15", 1024, 1024)


def build_upscale_simple() -> dict[str, Any]:
    wf = _deep_copy(UPSCALE_SIMPLE)
    wf["1"]["inputs"]["image"] = "golden_canvas.png"
    wf["2"]["inputs"]["width"] = 1024
    wf["2"]["inputs"]["height"] = 768
    wf["2"]["inputs"]["upscale_method"] = "lanczos"
    return wf


def build_upscale_refine() -> dict[str, Any]:
    wf = _deep_copy(UPSCALE_REFINE)
    wf["1"]["inputs"]["image"] = "golden_canvas.png"
    wf["2"]["inputs"]["width"] = 1024
    wf["2"]["inputs"]["height"] = 768
    wf["2"]["inputs"]["upscale_method"] = "lanczos"
    wf["4"]["inputs"]["ckpt_name"] = "v1-5-pruned-emaonly.safetensors"
    wf["5"]["inputs"]["text"] = "golden positive"
    wf["6"]["inputs"]["text"] = "golden negative"
    wf["7"]["inputs"]["seed"] = 1234
    wf["7"]["inputs"]["steps"] = 8
    wf["7"]["inputs"]["cfg"] = 8.5
    wf["7"]["inputs"]["denoise"] = 0.3
    wf["7"]["inputs"]["sampler_name"] = "euler"
    wf["7"]["inputs"]["scheduler"] = "normal"
    return _finish_workflow(wf, "7", "sd15", 1024, 768)


def export_python_upscale_simple(reference: Path, out_dir: Path) -> None:
    sys.path.insert(0, str(reference))
    try:
        from ai_diffusion.comfy_workflow import ComfyObjectInfo, ComfyWorkflow
        from ai_diffusion.image import Extent, Image
        from ai_diffusion.workflow import create, prepare_upscale_simple
    except ImportError as exc:
        print(f"skip python exports (import): {exc}", file=sys.stderr)
        return

    object_info_path = reference / "tests" / "data" / "object_info.json"
    if not object_info_path.is_file():
        print(f"skip python upscale_simple: missing {object_info_path}", file=sys.stderr)
        return

    object_info = json.loads(object_info_path.read_text(encoding="utf-8"))
    node_defs = ComfyObjectInfo(object_info)

    class _Models:
        node_inputs = node_defs

    image = Image.create(Extent(512, 384), fill=(0, 0, 0))
    wi = prepare_upscale_simple(image, "4x_NMKD-Superscale-SP_178000_G.pth", 2.0)
    flow = create(wi, _Models())
    api = flow.embed_images().root
    py_dir = out_dir / "python"
    py_dir.mkdir(parents=True, exist_ok=True)
    (py_dir / "upscale_simple.api.json").write_text(
        json.dumps(api, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"wrote {py_dir / 'upscale_simple.api.json'}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=Path,
        default=GOLDEN_DIR,
        help="Output directory for *.api.json fixtures",
    )
    parser.add_argument(
        "--reference",
        type=Path,
        default=None,
        help="Path to krita-ai-diffusion clone (optional Python exports)",
    )
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    fixtures = {
        "sd15_text2img": build_text_to_image(),
        "sdxl_text2img": build_sdxl_text_to_image(),
        "flux_text2img": build_flux_text_to_image(),
        "sd15_refine": build_refine(),
        "sd15_inpaint": build_inpaint(),
        "sd15_upscale_simple": build_upscale_simple(),
        "sd15_upscale_refine": build_upscale_refine(),
    }
    manifest = {"version": 1, "fixtures": list(fixtures.keys())}
    for name, wf in fixtures.items():
        path = out / f"{name}.api.json"
        normalized = normalize_api_workflow(wf)
        path.write_text(json.dumps(normalized, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"wrote {path}")

    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(f"wrote {out / 'manifest.json'}")

    if args.reference:
        export_python_upscale_simple(args.reference.resolve(), out)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
