/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PREPARE_LIVE_WORKFLOW_H_
#define COMFY_PREPARE_LIVE_WORKFLOW_H_

#include "ComfyPrepareWorkflow.h"

#include <kis_types.h>

namespace ComfyPrepareLiveWorkflow {

using WorkflowKind = ComfyPrepareWorkflow::WorkflowKind;

struct Input {
    KisImageSP image;
    KisViewManager *viewManager = nullptr;
    QString checkpoint;
    QString styleArch;
    QString rootPositivePrompt;
    double strength0to1 = 1.0;
    bool editMode = false;
    QList<ComfyRegionEntry> activeRegions;
};

using Result = ComfyPrepareWorkflow::Result;

/// Delegates to `ComfyPrepareWorkflow::prepare` (live path).
Result prepare(const Input &input);

} // namespace ComfyPrepareLiveWorkflow

#endif
