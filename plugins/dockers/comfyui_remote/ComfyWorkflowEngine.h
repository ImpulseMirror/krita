/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_WORKFLOW_ENGINE_H_
#define COMFY_WORKFLOW_ENGINE_H_

#include "ComfyResources.h"

#include <QJsonObject>
#include <QString>

namespace ComfyWorkflowEngine {

/// Parameters for basic text-to-image (WorkflowKind.generate, no regions/controls yet).
struct TextToImageParams {
    QString checkpoint;
    int width = 512;
    int height = 512;
    int batchSize = 1;
    QString positivePrompt;
    QString negativePrompt;
    qint64 seed = 0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
    double denoise = 1.0;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
};

/// Build ComfyUI API workflow JSON (CheckpointLoaderSimple + EmptyLatent + KSampler path).
/// Arch selects guidance defaults; full graph parity with workflow.py is extended incrementally.
QJsonObject buildTextToImage(const TextToImageParams &params);

/// Resolve arch from checkpoint + optional style architecture string ("auto", "sdxl", …).
ComfyResources::Arch resolveArch(const QString &checkpoint, const QString &styleArchitecture = QString());

} // namespace ComfyWorkflowEngine

#endif
