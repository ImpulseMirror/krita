/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPrepareLiveWorkflow.h"

namespace ComfyPrepareLiveWorkflow {

Result prepare(const Input &input)
{
    ComfyPrepareWorkflow::Input unified;
    unified.image = input.image;
    unified.viewManager = input.viewManager;
    unified.checkpoint = input.checkpoint;
    unified.styleArch = input.styleArch;
    unified.rootPositivePrompt = input.rootPositivePrompt;
    unified.strength0to1 = input.strength0to1;
    unified.editMode = input.editMode;
    unified.activeRegions = input.activeRegions;

    ComfyPrepareWorkflow::PrepareFlags flags;
    flags.isLive = true;
    flags.requireMask = true;
    flags.captureImage = true;
    return ComfyPrepareWorkflow::prepare(unified, flags);
}

} // namespace ComfyPrepareLiveWorkflow
