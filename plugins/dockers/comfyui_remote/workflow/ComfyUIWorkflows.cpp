/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// §13.101: ComfyWorkflow API format — root = dict node ID string → { "class_type", "inputs" }; links = [node_id, output_slot]

#include "ComfyUIWorkflows.h"

const char defaultWorkflow[] = R"({
 "3": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["5", 0], "model": ["4", 0], "negative": ["7", 0], "positive": ["6", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"}},
 "5": {"class_type": "EmptyLatentImage", "inputs": {"batch_size": 1, "height": 512, "width": 512}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "7": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "8": {"class_type": "VAEDecode", "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
 "9": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI", "images": ["8", 0]}}
})";

const char inpaintingWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "LoadImage", "inputs": {"image": "MASK_PLACEHOLDER"}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "PROMPT_PLACEHOLDER"}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "NEGATIVE_PLACEHOLDER"}},
 "7": {"class_type": "VAEEncodeForInpaint", "inputs": {"grow_mask_by": 6, "mask": ["2", 1], "pixels": ["1", 0], "vae": ["4", 2]}},
 "8": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["7", 0], "model": ["4", 0], "negative": ["6", 0], "positive": ["5", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["4", 2]}},
 "10": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_region", "images": ["9", 0]}}
})";

const char upscaleWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "ImageScale", "inputs": {"crop": "disabled", "height": 1024, "image": ["1", 0], "upscale_method": "lanczos", "width": 1024}},
 "3": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_upscale", "images": ["2", 0]}}
})";

const char upscaleRefineWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "ImageScale", "inputs": {"crop": "disabled", "height": 1024, "image": ["1", 0], "upscale_method": "lanczos", "width": 1024}},
 "3": {"class_type": "VAEEncode", "inputs": {"pixels": ["2", 0], "vae": ["4", 2]}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "PROMPT_PLACEHOLDER"}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "NEGATIVE_PLACEHOLDER"}},
 "7": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 0.35, "latent_image": ["3", 0], "model": ["4", 0], "negative": ["6", 0], "positive": ["5", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "8": {"class_type": "VAEDecode", "inputs": {"samples": ["7", 0], "vae": ["4", 2]}},
 "9": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_upscale_refine", "images": ["8", 0]}}
})";

const char img2imgWorkflowTemplate[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "IMAGE_PLACEHOLDER"}},
 "2": {"class_type": "VAEEncode", "inputs": {"pixels": ["1", 0], "vae": ["3", 2]}},
 "3": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "CKPT_PLACEHOLDER"}},
 "4": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["3", 1], "text": "PROMPT_PLACEHOLDER"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["3", 1], "text": "NEGATIVE_PLACEHOLDER"}},
 "6": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 0.75, "latent_image": ["2", 0], "model": ["3", 0], "negative": ["5", 0], "positive": ["4", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["3", 2]}},
 "8": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_live", "images": ["7", 0]}}
})";
