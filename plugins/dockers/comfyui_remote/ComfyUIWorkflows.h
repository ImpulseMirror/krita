/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_WORKFLOWS_H_
#define COMFYUI_WORKFLOWS_H_

// Minimal ComfyUI default workflow (text2img). Node keys "3".."9" as in ComfyUI basic_api_example.
extern const char defaultWorkflow[];

// Minimal inpainting workflow: LoadImage (1=image, 2=mask), Checkpoint (4), CLIP (5,6), VAEEncodeForInpaint (7), KSampler (8), VAEDecode (9), SaveImage (10).
extern const char inpaintingWorkflowTemplate[];

// Upscale: LoadImage (1), ImageScale (2), SaveImage (3). Uses ComfyUI core ImageScale (width/height).
extern const char upscaleWorkflowTemplate[];

// img2img for Live: LoadImage (1), VAEEncode (2), Checkpoint (3), CLIP (4,5), KSampler (6), VAEDecode (7), SaveImage (8)
extern const char img2imgWorkflowTemplate[];

#endif
