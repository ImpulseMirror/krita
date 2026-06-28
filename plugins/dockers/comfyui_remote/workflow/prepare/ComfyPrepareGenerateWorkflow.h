/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PREPARE_GENERATE_WORKFLOW_H_
#define COMFY_PREPARE_GENERATE_WORKFLOW_H_

#include "ComfyPrepareWorkflow.h"

class ComfyUIRemoteDock;

namespace ComfyPrepareGenerateWorkflow {

using WorkflowKind = ComfyPrepareWorkflow::WorkflowKind;

struct Input : ComfyPrepareWorkflow::Input {
    bool requireMask = true;
    bool captureImage = true;
};

using Result = ComfyPrepareWorkflow::Result;

struct PrepareFlags {
    bool requireMask = true;
    bool captureImage = true;
};

/// Delegates to `ComfyPrepareWorkflow::prepare` (generate path).
Result prepare(const Input &input);

/// Build prepare input from dock widgets + region state.
Input inputFromDock(const ComfyUIRemoteDock *dock, PrepareFlags flags);

} // namespace ComfyPrepareGenerateWorkflow

#endif
