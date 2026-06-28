/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyControlRunner.h"

void ComfyUIRemoteDock::stopControlLayerJobPolling()
{
    ComfyControlRunner::stopLayerJobPolling(this);
}

void ComfyUIRemoteDock::refreshControlLayerGenerateButtons()
{
    ComfyControlRunner::refreshLayerJobGenerateButtons(this);
}

void ComfyUIRemoteDock::beginControlLayerGenerateJob(bool forRegion, int entryIndex)
{
    ComfyControlRunner::onLayerJobRun(this, forRegion, entryIndex);
}

void ComfyUIRemoteDock::slotControlLayerJobPoll()
{
    ComfyControlRunner::onLayerJobPollTimer(this);
}
