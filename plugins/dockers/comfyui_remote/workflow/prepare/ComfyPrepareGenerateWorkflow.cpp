/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPrepareGenerateWorkflow.h"

#include "ComfyControlLayer.h"
#include "ComfyRegionProcess.h"
#include "ComfyStyleCollection.h"
#include "ComfyUIUtils.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"

namespace ComfyPrepareGenerateWorkflow {

namespace {

QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForGenerate(const ComfyUIRemoteDock::Private *d)
{
    QList<ComfyUIRemoteDock::Private::RegionEntry> regs = comfyActiveRegionEntries(d);
    if (d->generate.checkRegionOnly && d->generate.checkRegionOnly->isChecked()) {
        const int row = comfyActiveRegionRow(d);
        if (row >= 0 && row < regs.size())
            return {regs.at(row)};
    }
    return regs;
}

} // namespace

Result prepare(const Input &input)
{
    ComfyPrepareWorkflow::PrepareFlags flags;
    flags.requireMask = input.requireMask;
    flags.captureImage = input.captureImage;
    flags.isLive = false;
    return ComfyPrepareWorkflow::prepare(input, flags);
}

Input inputFromDock(const ComfyUIRemoteDock *dock, PrepareFlags flags)
{
    Input prepIn;
    if (!dock->m_d->viewManager)
        return prepIn;

    prepIn.image = dock->m_d->viewManager->image();
    prepIn.viewManager = dock->m_d->viewManager;
    prepIn.requireMask = flags.requireMask;
    prepIn.captureImage = flags.captureImage;

    prepIn.modifierMode = QStringLiteral("automatic");
    if (dock->m_d->inpaint.comboInpaintMode && dock->m_d->inpaint.comboInpaintMode->currentData().isValid()) {
        const QString modeData = dock->m_d->inpaint.comboInpaintMode->currentData().toString();
        if (!modeData.isEmpty())
            prepIn.modifierMode = modeData;
    }
    prepIn.contextKey = QStringLiteral("automatic");
    prepIn.contextLayerId.clear();
    if (prepIn.modifierMode == QLatin1String("custom")) {
        prepIn.contextKey = dock->m_d->inpaintRt.inpaintContextKey.trimmed().isEmpty()
                                ? QStringLiteral("automatic")
                                : dock->m_d->inpaintRt.inpaintContextKey.trimmed();
        prepIn.contextLayerId = dock->m_d->inpaintRt.inpaintContextLayerId;
        if (dock->m_d->inpaint.comboInpaintContext) {
            ComfyUIUtils::decodeInpaintContextComboData(dock->m_d->inpaint.comboInpaintContext->currentData(),
                                                        &prepIn.contextKey, &prepIn.contextLayerId);
        }
    }
    if (prepIn.modifierMode == QLatin1String("custom") && dock->m_d->inpaint.comboFillMode
        && dock->m_d->inpaint.comboFillMode->currentData().isValid())
        prepIn.customFillKind = dock->m_d->inpaint.comboFillMode->currentData().toString();

    prepIn.checkpoint = dock->checkpointForGenerate();
    if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            prepIn.styleArch = st->architecture;
    }
    prepIn.rootPositivePrompt =
        ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed();
    prepIn.strength0to1 = (dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100) / 100.0;
    prepIn.editMode = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    prepIn.regionOnly = dock->m_d->generate.checkRegionOnly && dock->m_d->generate.checkRegionOnly->isChecked();
    prepIn.activeRegionRow = comfyActiveRegionRow(dock->m_d.data());
    prepIn.activeRegions = regionsForGenerate(dock->m_d.data());
    prepIn.rootControlLayers = dock->m_d->rootControlLayers;
    prepIn.jobControlLayers =
        mergedJobControlLayers(dock->m_d->rootControlLayers, comfyActiveRegionEntries(dock->m_d.data()));
    prepIn.previewLayerId = dock->m_d->previewLayerId;
    prepIn.persistUseInpaintModel = dock->m_d->inpaintRt.inpaintPersistUseModel;
    prepIn.persistUsePromptFocus = dock->m_d->inpaintRt.inpaintPersistUsePromptFocus;
    return prepIn;
}

} // namespace ComfyPrepareGenerateWorkflow
