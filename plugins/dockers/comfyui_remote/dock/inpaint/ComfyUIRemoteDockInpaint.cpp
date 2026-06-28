/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyInpaintRunner.h"

#include <QJsonObject>

void ComfyUIRemoteDock::slotInpaint()
{
    ComfyInpaintRunner::onInpaint(this);
}

void ComfyUIRemoteDock::beginInpaintUploadPipeline()
{
    ComfyInpaintRunner::beginUploadPipeline(this);
}

void ComfyUIRemoteDock::submitInpaintWorkflow(QJsonObject workflow)
{
    ComfyInpaintRunner::submitWorkflow(this, workflow);
}

void ComfyUIRemoteDock::slotInpaintPoll()
{
    ComfyInpaintRunner::onPollTimer(this);
}
