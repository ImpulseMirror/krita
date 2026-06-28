/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>

class ComfyUIRemoteDock;

namespace ComfyUpscaleRunner {

void onPollTimer(ComfyUIRemoteDock *dock);

/// Upscale button — canvas upload + workflow build/submit (was slotUpscale chain).
void onUpscale(ComfyUIRemoteDock *dock);
void continueAfterCanvasUpload(ComfyUIRemoteDock *dock, int canvasW, int canvasH, int w2, int h2);
void beginConditioningUploadPipeline(ComfyUIRemoteDock *dock);
void uploadNextRegionMask(ComfyUIRemoteDock *dock);
void finalizeWorkflowAndSubmit(ComfyUIRemoteDock *dock);
void submitWorkflow(ComfyUIRemoteDock *dock, const QJsonObject &workflow, bool wantRefine, bool useTiledRefine);

} // namespace ComfyUpscaleRunner
