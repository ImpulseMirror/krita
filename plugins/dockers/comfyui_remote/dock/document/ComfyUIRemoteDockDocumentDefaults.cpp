/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyStyleCollection.h"
#include "ComfyFileLibrary.h"
#include "ComfyResources.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfySwitchWidget.h"

#include <QComboBox>
#include <KSharedConfig>

#include <kis_image.h>
#include <kis_annotation.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

QString ComfyUIRemoteDock::encodeStyleIdForDocumentDefaults() const
{
    return encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
}
QString ComfyUIRemoteDock::encodeStyleIdFromPresetCombo(const QComboBox *cb) const
{
    if (!cb)
        return QStringLiteral("none");
    const int idx = cb->currentIndex();
    if (idx <= 0)
        return QStringLiteral("none");
    const QVariant data = cb->itemData(idx);
    if (data.isValid() && !data.toString().isEmpty())
        return data.toString();
    return QStringLiteral("custom:") + cb->currentText();
}
void ComfyUIRemoteDock::applyStyleIdFromDocumentDefaults(const QString &styleId)
{
    if (!m_d->generate.comboPreset)
        return;
    applyStyleIdToPresetCombo(m_d->generate.comboPreset, styleId);
    if (m_d->generate.comboPreset->currentIndex() > 0)
        slotPresetChanged(m_d->generate.comboPreset->currentIndex());
}
QString ComfyUIRemoteDock::checkpointForGenerate() const
{
    QString ckpt;
    if (m_d->generate.comboPreset && m_d->generate.comboPreset->currentIndex() > 0) {
        const int idx = m_d->generate.comboPreset->currentIndex();
        const int firstCustom = firstCustomPresetIndex();
        if (idx >= firstCustom) {
            const QString name = m_d->generate.comboPreset->itemText(idx);
            ckpt = KSharedConfig::openConfig()
                       ->group(QStringLiteral("ComfyUIRemote_Preset_") + name)
                       .readEntry(QStringLiteral("Checkpoint"), QString())
                       .trimmed();
        } else {
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
                if (!st->checkpoints.isEmpty()) {
                    QStringList available;
                    if (m_d->generate.comboCheckpoint) {
                        for (int i = 0; i < m_d->generate.comboCheckpoint->count(); ++i)
                            available.append(m_d->generate.comboCheckpoint->itemText(i));
                    }
                    ckpt = ComfyFileLibrary::preferredCheckpoint(st->checkpoints, available);
                    if (ckpt == QLatin1String("not-found"))
                        ckpt = st->checkpoints.first();
                }
            }
        }
    }
    if (ckpt.isEmpty() && m_d->generate.comboCheckpoint)
        ckpt = m_d->generate.comboCheckpoint->currentText().trimmed();
    if (ckpt.isEmpty())
        ckpt = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    return ckpt;
}
void ComfyUIRemoteDock::syncCheckpointComboFromStyle()
{
    if (!m_d->generate.comboCheckpoint)
        return;
    const QString ckpt = checkpointForGenerate();
    const int ix = m_d->generate.comboCheckpoint->findText(ckpt);
    if (ix >= 0)
        m_d->generate.comboCheckpoint->setCurrentIndex(ix);
    else if (!ckpt.isEmpty())
        m_d->generate.comboCheckpoint->setCurrentText(ckpt);
}
void ComfyUIRemoteDock::applyStyleIdToPresetCombo(QComboBox *cb, const QString &styleId)
{
    if (!cb)
        return;
    const QString id = styleId.trimmed();
    if (id.isEmpty() || id == QLatin1String("none")) {
        cb->setCurrentIndex(0);
        return;
    }
    if (id.startsWith(QLatin1String("custom:"))) {
        const QString name = id.mid(7);
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toString() == id) {
                cb->setCurrentIndex(i);
                return;
            }
        }
        const int fi = cb->findText(name);
        if (fi >= 0)
            cb->setCurrentIndex(fi);
        return;
    }
    for (int i = 0; i < cb->count(); ++i) {
        if (cb->itemData(i).toString() == id) {
            cb->setCurrentIndex(i);
            return;
        }
    }
    const ComfyStyleEntry *legacy = ComfyStyleCollection::instance().findByStyleId(id);
    if (legacy) {
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toString() == legacy->styleId) {
                cb->setCurrentIndex(i);
                return;
            }
        }
    }
}
QString ComfyUIRemoteDock::checkpointNameForUpscaleRefinementPreset() const
{
    if (!m_d->upscale.comboUpscaleRefinementModel)
        return QString();
    const int idx = m_d->upscale.comboUpscaleRefinementModel->currentIndex();
    if (idx <= 0)
        return QString();
    const int fc = firstCustomPresetIndex();
    if (idx < fc)
        return m_d->generate.comboCheckpoint ? m_d->generate.comboCheckpoint->currentText().trimmed() : QString();
    const QString name = m_d->upscale.comboUpscaleRefinementModel->itemText(idx);
    return KSharedConfig::openConfig()
        ->group(QStringLiteral("ComfyUIRemote_Preset_") + name)
        .readEntry(QStringLiteral("Checkpoint"), QString())
        .trimmed();
}
void ComfyUIRemoteDock::readUpscaleRefinementSampling(int *outSteps, double *outCfg, QString *outSampler, QString *outScheduler) const
{
    Q_ASSERT(outSteps && outCfg && outSampler && outScheduler);
    *outSteps = m_d->generate.spinSteps ? m_d->generate.spinSteps->value() : 20;
    *outCfg = m_d->generate.spinCfg ? m_d->generate.spinCfg->value() : 8.0;
    *outSampler = m_d->generate.comboSampler ? m_d->generate.comboSampler->currentText().trimmed() : QStringLiteral("euler");
    *outScheduler = m_d->generateRt.ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->generateRt.ksamplerScheduler;
    if (!m_d->upscale.comboUpscaleRefinementModel)
        return;
    const int idx = m_d->upscale.comboUpscaleRefinementModel->currentIndex();
    const int fc = firstCustomPresetIndex();
    if (idx < fc || idx <= 0)
        return;
    const QString name = m_d->upscale.comboUpscaleRefinementModel->itemText(idx);
    KConfigGroup cfgG = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote_Preset_") + name);
    *outSteps = cfgG.readEntry(QStringLiteral("Steps"), *outSteps);
    *outCfg = cfgG.readEntry(QStringLiteral("Cfg"), *outCfg);
    const QString ps = cfgG.readEntry(QStringLiteral("Sampler"), QString());
    if (!ps.isEmpty())
        *outSampler = ps;
    const QString sch = cfgG.readEntry(QStringLiteral("Scheduler"), QString());
    if (!sch.isEmpty())
        *outScheduler = sch;
}
void ComfyUIRemoteDock::commitPromptEditorsFromUi()
{
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->commitRootPromptEditors();
}
void ComfyUIRemoteDock::applyRecentlyUsedSyncFromSettings()
{
    const ComfyUIUtils::RecentlyUsedSyncSnapshot snap = ComfyUIUtils::recentlyUsedSyncFromSettings();
    if (m_d->inpaint.comboInpaintMode) {
        QSignalBlocker b(m_d->inpaint.comboInpaintMode);
        setComboCurrentItemData(m_d->inpaint.comboInpaintMode, snap.inpaintMode, 0);
    }
    if (m_d->inpaint.comboFillMode) {
        QSignalBlocker b(m_d->inpaint.comboFillMode);
        setComboCurrentItemData(m_d->inpaint.comboFillMode, snap.inpaintFill, 2);
    }
    m_d->inpaintRt.inpaintPersistUseModel = snap.inpaintUseModel;
    m_d->inpaintRt.inpaintPersistUsePromptFocus = snap.inpaintUsePromptFocus;
    m_d->inpaintRt.inpaintContextKey = snap.inpaintContext;
    m_d->inpaintRt.inpaintContextLayerId.clear();
    if (m_d->inpaint.checkInpaintUseModel) {
        QSignalBlocker b(m_d->inpaint.checkInpaintUseModel);
        m_d->inpaint.checkInpaintUseModel->setChecked(snap.inpaintUseModel);
    }
    if (m_d->inpaint.checkInpaintUsePromptFocus) {
        QSignalBlocker b(m_d->inpaint.checkInpaintUsePromptFocus);
        m_d->inpaint.checkInpaintUsePromptFocus->setChecked(snap.inpaintUsePromptFocus);
    }
    if (m_d->inpaint.comboInpaintContext) {
        QSignalBlocker b(m_d->inpaint.comboInpaintContext);
        const int cix = m_d->inpaint.comboInpaintContext->findData(snap.inpaintContext);
        if (cix >= 0)
            m_d->inpaint.comboInpaintContext->setCurrentIndex(cix);
    }
    if (m_d->generate.spinBatchCount) {
        QSignalBlocker b(m_d->generate.spinBatchCount);
        m_d->generate.spinBatchCount->setValue(
            qBound(m_d->generate.spinBatchCount->minimum(), snap.batchCount, m_d->generate.spinBatchCount->maximum()));
    }
}
void ComfyUIRemoteDock::persistDocumentDefaultsToSettings()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QJsonObject dd;
    dd.insert(QStringLiteral("style"), encodeStyleIdForDocumentDefaults());
    dd.insert(QStringLiteral("batch_count"), m_d->generate.spinBatchCount ? m_d->generate.spinBatchCount->value() : 1);
    const QString pt = s.value(QStringLiteral("prompt_translation")).toString();
    const bool transEn = !pt.isEmpty() && pt != QLatin1String("disabled");
    dd.insert(QStringLiteral("translation_enabled"), transEn);
    dd.insert(QStringLiteral("prompt_translation"), transEn ? pt : QStringLiteral("disabled"));
    if (m_d->inpaint.comboInpaintMode)
        dd.insert(QStringLiteral("inpaint_mode"), m_d->inpaint.comboInpaintMode->currentData().toString());
    if (m_d->inpaint.comboFillMode)
        dd.insert(QStringLiteral("inpaint_fill"), m_d->inpaint.comboFillMode->currentData().toString());
    dd.insert(QStringLiteral("inpaint_use_model"), m_d->inpaintRt.inpaintPersistUseModel);
    dd.insert(QStringLiteral("inpaint_use_prompt_focus"), m_d->inpaintRt.inpaintPersistUsePromptFocus);
    if (m_d->inpaint.comboInpaintContext) {
        const QString ctx =
            ComfyUIUtils::inpaintContextForFreshDocumentDefaults(m_d->inpaintRt.inpaintContextKey);
        dd.insert(QStringLiteral("inpaint_context"), ctx);
    }
    if (m_d->generate.comboCheckpoint)
        dd.insert(QStringLiteral("checkpoint"), m_d->generate.comboCheckpoint->currentText().trimmed());
    dd.insert(QStringLiteral("upscale_model"), QStringLiteral("default"));
    s.insert(QStringLiteral("document_defaults"), dd);
    ComfyUIUtils::saveSettingsJson(s);
}
void ComfyUIRemoteDock::tryApplyDocumentDefaultsForNewDocument(KisImageSP image)
{
    if (!image)
        return;
    const QString key = ComfyUIUtils::documentIdAnnotationKey();
    KisAnnotationSP idAnn = image->annotation(key);
    const QString docId = idAnn ? QString::fromUtf8(idAnn->annotation()).trimmed() : QString();
    if (docId.isEmpty())
        return;
    if (m_d->documentDefaultsAppliedDocIds.contains(docId))
        return;
    if (ComfyUIUtils::documentHasStoredUiJsonPayload(image)) {
        m_d->documentDefaultsAppliedDocIds.insert(docId);
        return;
    }
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QJsonObject def = ComfyUIUtils::documentDefaultsFromSettingsRoot(s);
    m_d->documentDefaultsAppliedDocIds.insert(docId);
    if (def.isEmpty())
        return;

    applyStyleIdFromDocumentDefaults(def.value(QStringLiteral("style")).toString());
    if (m_d->generate.spinBatchCount && def.contains(QStringLiteral("batch_count"))) {
        const int bc = def.value(QStringLiteral("batch_count")).toInt(1);
        m_d->generate.spinBatchCount->setValue(qBound(m_d->generate.spinBatchCount->minimum(), bc, m_d->generate.spinBatchCount->maximum()));
    }
    const QString ck = def.value(QStringLiteral("checkpoint")).toString().trimmed();
    if (!ck.isEmpty() && m_d->generate.comboCheckpoint) {
        const int ci = m_d->generate.comboCheckpoint->findText(ck);
        if (ci >= 0)
            m_d->generate.comboCheckpoint->setCurrentIndex(ci);
        else
            m_d->generate.comboCheckpoint->setCurrentText(ck);
    }
    if (def.contains(QStringLiteral("translation_enabled"))) {
        const bool tEn = def.value(QStringLiteral("translation_enabled")).toBool(false);
        QString ptx = def.value(QStringLiteral("prompt_translation")).toString();
        if (!tEn || ptx == QLatin1String("disabled"))
            ptx = QStringLiteral("disabled");
        if (ptx.isEmpty() && tEn)
            ptx = QStringLiteral("en");
        s.insert(QStringLiteral("prompt_translation"), ptx);
        ComfyUIUtils::saveSettingsJson(s);
        applyInterfaceAppearanceSettings();
    }
    if (m_d->inpaint.comboInpaintMode) {
        const QString im = def.value(QStringLiteral("inpaint_mode")).toString();
        if (!im.isEmpty()) {
            QSignalBlocker b(m_d->inpaint.comboInpaintMode);
            setComboCurrentItemData(m_d->inpaint.comboInpaintMode, im, 0);
        }
    }
    if (m_d->inpaint.comboFillMode) {
        const QString fl = def.value(QStringLiteral("inpaint_fill")).toString();
        if (!fl.isEmpty()) {
            QSignalBlocker b(m_d->inpaint.comboFillMode);
            setComboCurrentItemData(m_d->inpaint.comboFillMode, fl, 2);
        }
    }
    if (def.contains(QStringLiteral("inpaint_use_model")))
        m_d->inpaintRt.inpaintPersistUseModel = def.value(QStringLiteral("inpaint_use_model")).toBool(true);
    if (def.contains(QStringLiteral("inpaint_use_prompt_focus")))
        m_d->inpaintRt.inpaintPersistUsePromptFocus = def.value(QStringLiteral("inpaint_use_prompt_focus")).toBool(false);
    if (m_d->inpaint.checkInpaintUseModel) {
        QSignalBlocker b(m_d->inpaint.checkInpaintUseModel);
        m_d->inpaint.checkInpaintUseModel->setChecked(m_d->inpaintRt.inpaintPersistUseModel);
    }
    if (m_d->inpaint.checkInpaintUsePromptFocus) {
        QSignalBlocker b(m_d->inpaint.checkInpaintUsePromptFocus);
        m_d->inpaint.checkInpaintUsePromptFocus->setChecked(m_d->inpaintRt.inpaintPersistUsePromptFocus);
    }
    if (m_d->inpaint.comboInpaintContext) {
        QString ctx = def.value(QStringLiteral("inpaint_context")).toString();
        ctx = ComfyUIUtils::inpaintContextForFreshDocumentDefaults(ctx);
        m_d->inpaintRt.inpaintContextKey = ctx;
        m_d->inpaintRt.inpaintContextLayerId.clear();
        QSignalBlocker b(m_d->inpaint.comboInpaintContext);
        const int cix = m_d->inpaint.comboInpaintContext->findData(ctx);
        if (cix >= 0)
            m_d->inpaint.comboInpaintContext->setCurrentIndex(cix);
    }
    updateNegativePromptAlertVisibility();
    persistDocumentDefaultsToSettings();
}
