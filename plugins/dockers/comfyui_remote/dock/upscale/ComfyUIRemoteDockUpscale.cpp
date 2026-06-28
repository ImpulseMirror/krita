/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUpscaleRunner.h"

#include <QJsonObject>

void ComfyUIRemoteDock::slotUpscale()
{
    ComfyUpscaleRunner::onUpscale(this);
}

void ComfyUIRemoteDock::continueUpscaleAfterCanvasUpload(int canvasW, int canvasH, int w2, int h2)
{
    ComfyUpscaleRunner::continueAfterCanvasUpload(this, canvasW, canvasH, w2, h2);
}

void ComfyUIRemoteDock::beginUpscaleConditioningUploadPipeline()
{
    ComfyUpscaleRunner::beginConditioningUploadPipeline(this);
}

void ComfyUIRemoteDock::uploadNextUpscaleRegionMask()
{
    ComfyUpscaleRunner::uploadNextRegionMask(this);
}

void ComfyUIRemoteDock::finalizeUpscaleWorkflowAndSubmit()
{
    ComfyUpscaleRunner::finalizeWorkflowAndSubmit(this);
}

void ComfyUIRemoteDock::submitUpscaleWorkflow(const QJsonObject &workflow, bool wantRefine, bool useTiledRefine)
{
    ComfyUpscaleRunner::submitWorkflow(this, workflow, wantRefine, useTiledRefine);
}

void ComfyUIRemoteDock::slotUpscalePoll()
{
    ComfyUpscaleRunner::onPollTimer(this);
}
