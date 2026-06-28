/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>

class ComfyUIRemoteDock;

namespace ComfyLiveRunner {

void onPollTimer(ComfyUIRemoteDock *dock);

/// Live timer tick — prepare + upload + submit chain entry (was slotLiveTick).
void onTick(ComfyUIRemoteDock *dock);

void beginUploadPipeline(ComfyUIRemoteDock *dock);
void continueAfterLoraUploads(ComfyUIRemoteDock *dock);
void buildPreparedPrompts(ComfyUIRemoteDock *dock, quint32 liveSeed);
void uploadCanvasAndPrompt(ComfyUIRemoteDock *dock);
void continueAfterCanvasUpload(ComfyUIRemoteDock *dock);
void continueAfterMaskUpload(ComfyUIRemoteDock *dock);
void uploadNextRegionMask(ComfyUIRemoteDock *dock);
void continueAfterRegionMaskUpload(ComfyUIRemoteDock *dock);
void finalizeWorkflowAndSubmit(ComfyUIRemoteDock *dock, QJsonObject workflow);
void submitWorkflow(ComfyUIRemoteDock *dock, const QJsonObject &workflow);

} // namespace ComfyLiveRunner
