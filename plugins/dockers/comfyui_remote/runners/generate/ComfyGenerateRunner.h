/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>

class ComfyUIRemoteDock;

namespace ComfyGenerateRunner {

/// Main generate/animation/graph queue poll tick (was ctor lambda).
void onPollTimer(ComfyUIRemoteDock *dock);

/// Generate button click — workflow build, upload dispatch, batch submit (was slotGenerate body).
void onGenerate(ComfyUIRemoteDock *dock);

void onGenerateAnimation(ComfyUIRemoteDock *dock);
void onImportAnimation(ComfyUIRemoteDock *dock);
void onCancelQueue(ComfyUIRemoteDock *dock);
bool cancelCurrentJob(ComfyUIRemoteDock *dock);
void cancelQueuedJobs(ComfyUIRemoteDock *dock);

/// Batch queue submit loop (was slotBatchSubmitNext).
void onBatchSubmitNext(ComfyUIRemoteDock *dock);

/// POST /prompt for one batch index.
void dispatchBatchPromptRequest(ComfyUIRemoteDock *dock, QJsonObject workflow, int submitIndex);

void maybeContinueCustomGraphLive(ComfyUIRemoteDock *dock);
void onCustomGraphLiveResubmit(ComfyUIRemoteDock *dock);

/// LoRA/control upload then workflow finalize (was beginGenerateUploadPipeline).
void beginUploadPipeline(ComfyUIRemoteDock *dock);
void uploadNextRegionMask(ComfyUIRemoteDock *dock);
void finalizeWorkflowAndSubmit(ComfyUIRemoteDock *dock, QJsonObject workflow);

bool tryStartRefineFromGenerate(ComfyUIRemoteDock *dock);
void uploadCanvasForRefine(ComfyUIRemoteDock *dock);

} // namespace ComfyGenerateRunner
