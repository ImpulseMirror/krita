/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyStyleCollection.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyLiveRunner.h"

ComfyPrepareLiveWorkflow::Input ComfyUIRemoteDock::prepareLiveWorkflowInput() const
{
    ComfyPrepareLiveWorkflow::Input prepIn;
    if (!m_d->viewManager)
        return prepIn;
    prepIn.image = m_d->viewManager->image();
    prepIn.viewManager = m_d->viewManager;
    prepIn.checkpoint = checkpointForGenerate();
    if (m_d->generate.comboPreset && m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            prepIn.styleArch = st->architecture;
    }
    prepIn.rootPositivePrompt = ComfyUIUtils::stripPromptComments(m_d->generate.editPrompt->toPlainText()).trimmed();
    prepIn.strength0to1 = (m_d->generate.spinStrength ? m_d->generate.spinStrength->value() : 75) / 100.0;
    prepIn.editMode = m_d->generate.checkEditMode && m_d->generate.checkEditMode->isChecked();
    prepIn.activeRegions = comfyActiveRegionEntries(m_d.data());
    return prepIn;
}

void ComfyUIRemoteDock::slotLiveTick()
{
    ComfyLiveRunner::onTick(this);
}

void ComfyUIRemoteDock::beginLiveUploadPipeline()
{
    ComfyLiveRunner::beginUploadPipeline(this);
}

void ComfyUIRemoteDock::continueLiveAfterLoraUploads()
{
    ComfyLiveRunner::continueAfterLoraUploads(this);
}

void ComfyUIRemoteDock::buildLivePreparedPrompts(const quint32 liveSeed)
{
    ComfyLiveRunner::buildPreparedPrompts(this, liveSeed);
}

void ComfyUIRemoteDock::uploadLiveCanvasAndPrompt()
{
    ComfyLiveRunner::uploadCanvasAndPrompt(this);
}

void ComfyUIRemoteDock::continueLiveAfterCanvasUpload()
{
    ComfyLiveRunner::continueAfterCanvasUpload(this);
}

void ComfyUIRemoteDock::continueLiveAfterMaskUpload()
{
    ComfyLiveRunner::continueAfterMaskUpload(this);
}

void ComfyUIRemoteDock::uploadNextLiveRegionMask()
{
    ComfyLiveRunner::uploadNextRegionMask(this);
}

void ComfyUIRemoteDock::continueLiveAfterRegionMaskUpload()
{
    ComfyLiveRunner::continueAfterRegionMaskUpload(this);
}

void ComfyUIRemoteDock::finalizeLiveWorkflowAndSubmit(QJsonObject workflow)
{
    ComfyLiveRunner::finalizeWorkflowAndSubmit(this, workflow);
}

void ComfyUIRemoteDock::submitLiveWorkflow(const QJsonObject &workflow)
{
    ComfyLiveRunner::submitWorkflow(this, workflow);
}

void ComfyUIRemoteDock::slotLivePoll()
{
    ComfyLiveRunner::onPollTimer(this);
}
