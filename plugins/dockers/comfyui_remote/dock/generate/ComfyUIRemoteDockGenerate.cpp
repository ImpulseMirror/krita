/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlRunner.h"
#include "ComfyGenerateRunner.h"
#include "ComfyGenerateUi.h"
#include "ComfyPrepareGenerateWorkflow.h"

void ComfyUIRemoteDock::slotGenerateReplace()
{
    m_d->generateRt.generateOneShotQueueMode = 2;
    slotGenerate();
}

void ComfyUIRemoteDock::slotGenerate()
{
    ComfyGenerateRunner::onGenerate(this);
}

void ComfyUIRemoteDock::slotBatchSubmitNext()
{
    ComfyGenerateRunner::onBatchSubmitNext(this);
}

void ComfyUIRemoteDock::dispatchBatchPromptRequest(QJsonObject workflow, int submitIndex)
{
    ComfyGenerateRunner::dispatchBatchPromptRequest(this, workflow, submitIndex);
}

void ComfyUIRemoteDock::maybeContinueCustomGraphLive()
{
    ComfyGenerateRunner::maybeContinueCustomGraphLive(this);
}

void ComfyUIRemoteDock::slotCustomGraphLiveResubmit()
{
    ComfyGenerateRunner::onCustomGraphLiveResubmit(this);
}

void ComfyUIRemoteDock::slotGenerateAnimation()
{
    ComfyGenerateRunner::onGenerateAnimation(this);
}

void ComfyUIRemoteDock::slotImportAnimation()
{
    ComfyGenerateRunner::onImportAnimation(this);
}

void ComfyUIRemoteDock::slotCancelQueue()
{
    ComfyGenerateRunner::onCancelQueue(this);
}

bool ComfyUIRemoteDock::cancelCurrentGenerateJob()
{
    return ComfyGenerateRunner::cancelCurrentJob(this);
}

void ComfyUIRemoteDock::cancelQueuedGenerateJobs()
{
    ComfyGenerateRunner::cancelQueuedJobs(this);
}

void ComfyUIRemoteDock::stopControlPreviewPolling()
{
    ComfyControlRunner::stopPreviewPolling(this);
}

void ComfyUIRemoteDock::syncControlPreviewRangeFromSettings()
{
    ComfyControlRunner::syncPreviewRangeFromSettings(this);
}

void ComfyUIRemoteDock::syncPoseGuidePeopleCountFromSettings()
{
    ComfyControlRunner::syncPoseGuidePeopleCountFromSettings(this);
}

void ComfyUIRemoteDock::slotControlPreviewRun()
{
    ComfyControlRunner::onPreviewRun(this);
}

void ComfyUIRemoteDock::slotControlPreviewPoll()
{
    ComfyControlRunner::onPreviewPollTimer(this);
}

void ComfyUIRemoteDock::beginGenerateUploadPipeline()
{
    ComfyGenerateRunner::beginUploadPipeline(this);
}

void ComfyUIRemoteDock::uploadNextGenerateRegionMask()
{
    ComfyGenerateRunner::uploadNextRegionMask(this);
}

void ComfyUIRemoteDock::finalizeGenerateWorkflowAndSubmit(QJsonObject workflow)
{
    ComfyGenerateRunner::finalizeWorkflowAndSubmit(this, workflow);
}

ComfyPrepareGenerateWorkflow::Input ComfyUIRemoteDock::prepareGenerateWorkflowInput(
    ComfyPrepareGenerateWorkflow::PrepareFlags flags) const
{
    return ComfyPrepareGenerateWorkflow::inputFromDock(this, flags);
}

bool ComfyUIRemoteDock::tryStartRefineFromGenerate()
{
    return ComfyGenerateRunner::tryStartRefineFromGenerate(this);
}

void ComfyUIRemoteDock::uploadCanvasForRefineGenerate()
{
    ComfyGenerateRunner::uploadCanvasForRefine(this);
}

void ComfyUIRemoteDock::setupGenerateInpaintMenus()
{
    ComfyGenerateUi::setupInpaintMenus(this);
}

void ComfyUIRemoteDock::showInpaintModeMenu()
{
    ComfyGenerateUi::showInpaintModeMenu(this);
}

void ComfyUIRemoteDock::reEnableGenerateUi()
{
    ComfyGenerateUi::reEnableUi(this);
}

void ComfyUIRemoteDock::updateGenerateOptions()
{
    ComfyGenerateUi::updateOptions(this);
}
