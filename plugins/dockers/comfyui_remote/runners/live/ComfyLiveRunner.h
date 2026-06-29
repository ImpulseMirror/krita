/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>

class ComfyUIRemoteDock;

namespace ComfyLiveRunner {

void onPollTimer(ComfyUIRemoteDock *dock);

/// Start/stop 100ms live poll loop (upstream LiveScheduler poll_rate).
void startLivePollLoop(ComfyUIRemoteDock *dock);
void stopLivePollLoop(ComfyUIRemoteDock *dock);

/// Live timer tick — poll for canvas changes and schedule generation.
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
