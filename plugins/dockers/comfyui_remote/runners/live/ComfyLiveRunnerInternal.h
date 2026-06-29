/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyLiveScheduler.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <kis_image.h>

namespace ComfyLiveRunnerInternal {

QImage cropLiveResultToTarget(const QImage &image, const ComfyPrepareLiveWorkflow::Result &prep);
/// Merge masked live server output onto captured context (same path as refine/inpaint poll).
QImage compositeLiveServerResult(const QImage &serverResult, const ComfyPrepareLiveWorkflow::Result &prep);
/// Re-run masked composite at apply time with a fresh document capture (upstream apply_result uses current layer pixels).
QImage compositeLiveServerResultAtApply(const QImage &serverResult,
                                        const ComfyPrepareLiveWorkflow::Result &prep,
                                        KisImageSP image);
QUrl comfyImageUploadUrl(const QString &serverUrl);
QByteArray computeLiveInputFingerprint(const ComfyPrepareLiveWorkflow::Result &prep,
                                       const QString &positivePrompt,
                                       const QString &negativePrompt,
                                       int seed,
                                       bool editMode,
                                       const QImage &canvasForFingerprint = QImage());
bool livePipelineBusy(const ComfyUIRemoteDock::Private *d);

} // namespace ComfyLiveRunnerInternal
