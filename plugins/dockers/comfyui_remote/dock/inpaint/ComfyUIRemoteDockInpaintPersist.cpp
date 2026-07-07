/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyGenerateUi.h"
#include "ComfyTheme.h"
#include "ComfySwitchWidget.h"

#include <QTimer>
#include <QJsonDocument>
#include <KSharedConfig>

#include <kis_image.h>
#include <kis_selection.h>
#include <kis_annotation.h>
#include <kis_group_layer.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

void ComfyUIRemoteDock::onGenerateStrengthChanged(int strengthPercent)
{
    const int v = qBound(1, strengthPercent, 100);
    if (m_d->generate.spinStrength && m_d->generate.spinStrength->value() != v) {
        QSignalBlocker b(m_d->generate.spinStrength);
        m_d->generate.spinStrength->setValue(v);
    }
    if (m_d->inpaint.sliderStrength && m_d->inpaint.sliderStrength->value() != v) {
        QSignalBlocker b(m_d->inpaint.sliderStrength);
        m_d->inpaint.sliderStrength->setValue(v);
    }
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    const bool onLiveWorkspace = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
    if (!onLiveWorkspace)
        cfg.writeEntry("GenerateStrength", v);
    if (onLiveWorkspace) {
        KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
        if (img) {
            const QString liveKey = ComfyUIUtils::liveWorkspaceAnnotationKey();
            QJsonObject o;
            o.insert(QStringLiteral("strength"), v / 100.0);
            img->removeAnnotation(liveKey);
            img->addAnnotation(KisAnnotationSP(new KisAnnotation(
                liveKey, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("live")),
                QJsonDocument(o).toJson(QJsonDocument::Compact))));
            scheduleDocumentUiJsonSave();
        }
    }
    updateInpaintControlsForArch();
}
void ComfyUIRemoteDock::updateInpaintControlsForArch()
{
    if (!m_d->generate.comboCheckpoint)
        return;
    const QString ckpt = checkpointForGenerate();
    const ComfyResources::Arch arch =
        ComfyResources::archFromKey(ComfyUIUtils::classifyCheckpointArch(ckpt));
    const bool editUi = m_d->generate.checkEditMode && m_d->generate.checkEditMode->isChecked();
    QString linkedEditStyleId;
    if (m_d->generate.comboPreset && m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            linkedEditStyleId = st->linkedEditStyle;
    }
    const bool canToggleEdit = ComfyUIUtils::canToggleEditMode(ckpt, linkedEditStyleId);
    if (m_d->generate.checkEditMode)
        m_d->generate.checkEditMode->setEnabled(canToggleEdit);
    if (m_d->inpaint.editModeSwitch)
        m_d->inpaint.editModeSwitch->setEnabled(canToggleEdit);
    if (m_d->inpaint.checkInpaintUseModel)
        m_d->inpaint.checkInpaintUseModel->setEnabled(ComfyResources::isSdxlLike(arch) || ComfyResources::hasControlnetInpaint(arch));
    if (m_d->inpaint.checkInpaintUsePromptFocus) {
        const bool showFocus = arch == ComfyResources::Arch::Sd15 || ComfyResources::isSdxlLike(arch);
        m_d->inpaint.checkInpaintUsePromptFocus->setVisible(showFocus);
    }
    if (m_d->inpaint.comboFillMode) {
        const bool strengthFull =
            (m_d->generate.spinStrength && m_d->generate.spinStrength->value() >= 100)
            || arch == ComfyResources::Arch::QwenL;
        m_d->inpaint.comboFillMode->setEnabled(strengthFull && !editUi);
    }
    updateGenerateOptions();
    refreshInlineControlLayersList();
}
void ComfyUIRemoteDock::syncInpaintContextComboSelection()
{
    if (!m_d->inpaint.comboInpaintContext)
        return;
    QSignalBlocker blocker(m_d->inpaint.comboInpaintContext);
    int index = -1;
    if (m_d->inpaintRt.inpaintContextKey == QLatin1String("layer_bounds") && !m_d->inpaintRt.inpaintContextLayerId.isEmpty()) {
        index = m_d->inpaint.comboInpaintContext->findData(m_d->inpaintRt.inpaintContextLayerId);
        if (index < 0)
            index = m_d->inpaint.comboInpaintContext->findData(
                QUuid(m_d->inpaintRt.inpaintContextLayerId).toString(QUuid::WithoutBraces));
        if (index < 0)
            index = m_d->inpaint.comboInpaintContext->findData(QUuid(m_d->inpaintRt.inpaintContextLayerId).toString(QUuid::WithBraces));
    } else {
        index = m_d->inpaint.comboInpaintContext->findData(m_d->inpaintRt.inpaintContextKey);
    }
    if (index >= 0)
        m_d->inpaint.comboInpaintContext->setCurrentIndex(index);
}
void ComfyUIRemoteDock::refreshInpaintContextLayers()
{
    if (!m_d->inpaint.comboInpaintContext || !m_d->viewManager)
        return;
    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return;

    const QVariant current = m_d->inpaint.comboInpaintContext->currentData();
    QSignalBlocker blocker(m_d->inpaint.comboInpaintContext);
    while (m_d->inpaint.comboInpaintContext->count() > 3)
        m_d->inpaint.comboInpaintContext->removeItem(m_d->inpaint.comboInpaintContext->count() - 1);

    const QIcon layerIcon = ComfyTheme::icon(QStringLiteral("context-layer"));
    QList<KisNodeSP> stack;
    if (KisNodeSP root = image->rootLayer())
        stack.append(root);
    while (!stack.isEmpty()) {
        KisNodeSP node = stack.takeFirst();
        for (int i = 0; i < static_cast<int>(node->childCount()); ++i)
            stack.append(node->at(i));
        if (!ComfyUIUtils::isInpaintContextMaskNode(node))
            continue;
        const QString layerId = node->uuid().toString(QUuid::WithoutBraces);
        if (layerId.isEmpty())
            continue;
        m_d->inpaint.comboInpaintContext->addItem(layerIcon, node->name(), layerId);
    }

    const int restore = m_d->inpaint.comboInpaintContext->findData(current);
    if (restore >= 0)
        m_d->inpaint.comboInpaintContext->setCurrentIndex(restore);
    else
        syncInpaintContextComboSelection();
}
void ComfyUIRemoteDock::saveInpaintWorkspaceToDocument()
{
    if (!m_d->canvas)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    ComfyUIUtils::InpaintWorkspaceSnapshot snap;
    snap.mode = m_d->inpaint.comboInpaintMode ? m_d->inpaint.comboInpaintMode->currentData().toString() : QStringLiteral("automatic");
    snap.fill = m_d->inpaint.comboFillMode ? m_d->inpaint.comboFillMode->currentData().toString() : QStringLiteral("blur");
    snap.useInpaint = m_d->inpaintRt.inpaintPersistUseModel;
    snap.usePromptFocus = m_d->inpaintRt.inpaintPersistUsePromptFocus;
    snap.context = m_d->inpaintRt.inpaintContextKey;
    snap.contextLayerId = m_d->inpaintRt.inpaintContextLayerId;
    const QJsonObject o = ComfyUIUtils::inpaintWorkspaceToJson(snap);
    const QString key = ComfyUIUtils::inpaintWorkspaceAnnotationKey();
    img->removeAnnotation(key);
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(
        key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("inpaint")),
        QJsonDocument(o).toJson(QJsonDocument::Compact))));
    scheduleDocumentUiJsonSave();
}
void ComfyUIRemoteDock::loadInpaintWorkspaceFromDocument()
{
    if (!m_d->canvas)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    QJsonObject root;
    if (KisAnnotationSP ann = img->annotation(ComfyUIUtils::inpaintWorkspaceAnnotationKey())) {
        if (!ann->annotation().isEmpty())
            root = QJsonDocument::fromJson(ann->annotation()).object();
    }
    if (root.isEmpty()) {
        const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
        root = ui.value(QStringLiteral("inpaint")).toObject();
    }
    if (root.isEmpty())
        return;
    ComfyUIUtils::InpaintWorkspaceSnapshot snap;
    if (!ComfyUIUtils::inpaintWorkspaceFromJson(root, &snap))
        return;
    if (m_d->inpaint.comboInpaintMode) {
        QSignalBlocker b(m_d->inpaint.comboInpaintMode);
        setComboCurrentItemData(m_d->inpaint.comboInpaintMode, snap.mode, 0);
    }
    if (m_d->inpaint.comboFillMode) {
        QSignalBlocker b(m_d->inpaint.comboFillMode);
        setComboCurrentItemData(m_d->inpaint.comboFillMode, snap.fill, 2);
    }
    m_d->inpaintRt.inpaintPersistUseModel = snap.useInpaint;
    m_d->inpaintRt.inpaintPersistUsePromptFocus = snap.usePromptFocus;
    m_d->inpaintRt.inpaintContextKey = snap.context;
    m_d->inpaintRt.inpaintContextLayerId = snap.contextLayerId;
    if (m_d->inpaint.checkInpaintUseModel) {
        QSignalBlocker b(m_d->inpaint.checkInpaintUseModel);
        m_d->inpaint.checkInpaintUseModel->setChecked(m_d->inpaintRt.inpaintPersistUseModel);
    }
    if (m_d->inpaint.checkInpaintUsePromptFocus) {
        QSignalBlocker b(m_d->inpaint.checkInpaintUsePromptFocus);
        m_d->inpaint.checkInpaintUsePromptFocus->setChecked(m_d->inpaintRt.inpaintPersistUsePromptFocus);
    }
    refreshInpaintContextLayers();
    syncInpaintContextComboSelection();
    updateInpaintControlsForArch();
}
void ComfyUIRemoteDock::schedulePersistDocumentDefaults()
{
    if (!m_d->documentDefaultsSaveTimer)
        return;
    m_d->documentDefaultsSaveTimer->start(350);
}
