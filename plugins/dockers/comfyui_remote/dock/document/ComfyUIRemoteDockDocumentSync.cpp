/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyRegionProcess.h"
#include "ComfyRegionLink.h"
#include "ComfyGenerateUi.h"
#include "ComfyHistoryInternal.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfySwitchWidget.h"

#include <QTimer>
#include <QJsonDocument>
#include <QJsonParseError>

#include <kis_image.h>
#include <kis_annotation.h>
#include <kis_selection.h>
#include <kis_image_animation_interface.h>
#include <KisPart.h>
#include <KisDocument.h>
#include <kis_group_layer.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

using ComfyUIUtils::regionUiStateEntriesFromJsonArray;
using ComfyUIUtils::regionUiStateEntriesToJsonArray;
using ComfyUIUtils::rootRegionUiWrapFromJson;
using ComfyUIUtils::rootRegionUiWrapToJson;
using ComfyUIUtils::ComfyRegionUiStateEntry;

namespace {

ComfyRegionUiStateEntry comfyRegionEntryToUi(const ComfyUIRemoteDock::Private::RegionEntry &e)
{
    ComfyRegionUiStateEntry u;
    u.name = e.name;
    u.positive = e.prompt;
    u.maskSource = e.maskSource;
    u.layerIds = e.layerIds;
    u.controlLayers = e.controlLayers;
    return u;
}

ComfyUIRemoteDock::Private::RegionEntry comfyRegionEntryFromUi(const ComfyRegionUiStateEntry &u)
{
    ComfyUIRemoteDock::Private::RegionEntry e;
    e.name = u.name;
    e.prompt = u.positive;
    e.maskSource = u.maskSource;
    e.layerIds = u.layerIds;
    e.controlLayers = u.controlLayers;
    return e;
}

QList<ComfyRegionUiStateEntry> comfyRegionEntriesToUiList(const QList<ComfyUIRemoteDock::Private::RegionEntry> &entries)
{
    QList<ComfyRegionUiStateEntry> out;
    for (const ComfyUIRemoteDock::Private::RegionEntry &e : entries)
        out.append(comfyRegionEntryToUi(e));
    return out;
}

static int comfyWorkspaceIndexFromUiJson(const QString &w)
{
    if (w == QLatin1String("upscaling"))
        return 1;
    if (w == QLatin1String("live"))
        return 2;
    if (w == QLatin1String("animation"))
        return 3;
    if (w == QLatin1String("custom"))
        return 4;
    return 0;
}

} // namespace


void ComfyUIRemoteDock::updateUpscaleTargetSize()
{
    if (!m_d->upscale.labelUpscaleTargetSize) return;
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->upscale.labelUpscaleTargetSize->setText(ComfyTr::tr("Target size: — × —"));
        return;
    }
    QSize size = m_d->viewManager->image()->size();
    const int w = qRound(size.width() * m_d->upscaleRt.upscaleFactor);
    const int h = qRound(size.height() * m_d->upscaleRt.upscaleFactor);
    m_d->upscale.labelUpscaleTargetSize->setText(ComfyTr::tr("Target size: %1 × %2", w, h));
}
void ComfyUIRemoteDock::slotDocumentSyncPoll()
{
    if (!m_d->documentSyncPoller)
        return;
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        return;
    }
    KisDocument *doc = m_d->canvas && m_d->canvas->imageView() ? m_d->canvas->imageView()->document() : nullptr;
    if (doc) {
        bool open = false;
        const QList<QPointer<KisDocument>> docs = KisPart::instance()->documents();
        for (const QPointer<KisDocument> &d : docs) {
            if (d.data() == doc) {
                open = true;
                break;
            }
        }
        if (!open) {
            m_d->documentSyncPoller->stop();
            m_d->documentPollInitialized = false;
            return;
        }
    }

    KisImageSP image = m_d->viewManager->image();
    if (!image) {
        m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        return;
    }

    bool curHasSel = false;
    QRect curSelRect;
    if (KisSelectionSP sel = m_d->viewManager->selection()) {
        if (sel->pixelSelection()) {
            curSelRect = sel->pixelSelection()->selectedExactRect();
            curHasSel = !curSelRect.isEmpty();
        }
    }

    int curTime = (std::numeric_limits<int>::min)();
    if (image->animationInterface() && image->animationInterface()->hasAnimation())
        curTime = image->animationInterface()->currentTime();

    if (!m_d->documentPollInitialized) {
        m_d->lastPolledHadSelection = curHasSel;
        m_d->lastPolledSelectionBounds = curSelRect;
        m_d->lastPolledCurrentTime = curTime;
        if (KisLayerSP layer = m_d->viewManager->activeLayer())
            m_d->lastPolledActiveLayerId = layer->uuid().toString();
        m_d->documentPollInitialized = true;
        return;
    }

    const bool selChanged =
        (curHasSel != m_d->lastPolledHadSelection) || (curHasSel && curSelRect != m_d->lastPolledSelectionBounds);
    QString curActiveLayerId;
    if (KisLayerSP layer = m_d->viewManager->activeLayer())
        curActiveLayerId = layer->uuid().toString();
    const bool activeLayerChanged = curActiveLayerId != m_d->lastPolledActiveLayerId;
    if (selChanged) {
        m_d->lastPolledHadSelection = curHasSel;
        m_d->lastPolledSelectionBounds = curSelRect;
        updateGenerateOptions();
    }
    if (activeLayerChanged) {
        m_d->lastPolledActiveLayerId = curActiveLayerId;
        updateGenerateOptions();
    }

    if (curTime != m_d->lastPolledCurrentTime) {
        m_d->lastPolledCurrentTime = curTime;
        const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
        if (ws == 3 && m_d->radioSingleFrame && m_d->radioSingleFrame->isChecked() && m_d->animationPreviewRow
            && m_d->animationPreviewRow->isVisible() && m_d->animationPreviewDebounce) {
            m_d->animationPreviewDebounce->stop();
            m_d->animationPreviewDebounce->start();
        }
    }

    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->onActiveLayerChanged();
    refreshInpaintContextLayers();
}
void ComfyUIRemoteDock::slotDebouncedAnimationTargetPreview()
{
    refreshAnimationTargetLayerLivePreview();
}
void ComfyUIRemoteDock::mergeDocumentModelIntoUiJson(QJsonObject *ui, KisImageSP img) const
{
    if (!ui)
        return;

    QJsonObject upscale;
    upscale.insert(QStringLiteral("upscaler"), selectedUpscalerModelName());
    upscale.insert(QStringLiteral("factor"), m_d->upscaleRt.upscaleFactor);
    const bool useDiffusion = m_d->upscale.checkUpscaleRefine && m_d->upscale.checkUpscaleRefine->isChecked();
    upscale.insert(QStringLiteral("use_diffusion"), useDiffusion);
    const int strengthPct = m_d->upscale.sliderUpscaleRefineStrength ? m_d->upscale.sliderUpscaleRefineStrength->value() : 0;
    const int guidancePct = m_d->upscale.sliderUpscaleRefineGuidance ? m_d->upscale.sliderUpscaleRefineGuidance->value() : 0;
    upscale.insert(QStringLiteral("strength"), strengthPct / 100.0);
    upscale.insert(QStringLiteral("unblur_strength"), guidancePct / 100.0);
    upscale.insert(QStringLiteral("tile_overlap_mode"), m_d->upscaleRt.tileOverlapMode);
    upscale.insert(QStringLiteral("tile_overlap"), m_d->upscaleRt.tileOverlap);
    upscale.insert(QStringLiteral("use_prompt"), m_d->upscale.checkUpscaleUsePrompt && m_d->upscale.checkUpscaleUsePrompt->isChecked());
    upscale.insert(QStringLiteral("refinement_style"), encodeStyleIdFromPresetCombo(m_d->upscale.comboUpscaleRefinementModel));
    ui->insert(QStringLiteral("upscale"), upscale);

    QJsonObject custom;
    if (img) {
        if (KisAnnotationSP cw = img->annotation(ComfyUIUtils::customWorkflowAnnotationKey())) {
            if (!cw->annotation().isEmpty()) {
                const QByteArray raw = cw->annotation();
                QJsonParseError err{};
                const QJsonDocument wd = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(raw), &err);
                if (err.error == QJsonParseError::NoError && wd.isObject())
                    custom.insert(QStringLiteral("workflow"), wd.object());
                else
                    custom.insert(QStringLiteral("workflow_text"), QString::fromUtf8(raw));
            }
        }
    }
    if (!m_d->customWorkflowParamOverrides.isEmpty()) {
        QJsonObject pparams;
        for (auto it = m_d->customWorkflowParamOverrides.constBegin(); it != m_d->customWorkflowParamOverrides.constEnd(); ++it)
            pparams.insert(it.key(), QJsonValue::fromVariant(it.value()));
        custom.insert(QStringLiteral("params"), pparams);
    }
    if (!custom.isEmpty())
        ui->insert(QStringLiteral("custom"), custom);

    const QString rootPositive = m_d->generate.editPrompt ? m_d->generate.editPrompt->toPlainText() : QString();
    const QString rootNegative = m_d->generate.editNegative ? m_d->generate.editNegative->toPlainText() : QString();
    const QList<ComfyRegionUiStateEntry> rootUi = comfyRegionEntriesToUiList(m_d->regionEntries);
    ui->insert(QStringLiteral("root"), rootRegionUiWrapToJson(rootPositive, rootNegative, rootUi));
    ui->insert(QStringLiteral("edit"),
               rootRegionUiWrapToJson(rootPositive, rootNegative, comfyRegionEntriesToUiList(m_d->editRegionEntries)));
    ui->insert(QStringLiteral("regions"), regionUiStateEntriesToJsonArray(rootUi));
    ui->insert(QStringLiteral("control"), ComfyControlLayer::toJsonArray(m_d->rootControlLayers));

    static const QStringList wsIds = { QStringLiteral("generation"), QStringLiteral("upscaling"), QStringLiteral("live"),
                                       QStringLiteral("animation"), QStringLiteral("custom") };
    const int wix = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : 0;
    if (wix >= 0 && wix < wsIds.size())
        ui->insert(QStringLiteral("workspace"), wsIds.at(wix));
    ui->insert(QStringLiteral("style"), encodeStyleIdForDocumentDefaults());
    if (m_d->generate.spinStrength)
        ui->insert(QStringLiteral("strength"), m_d->generate.spinStrength->value());
    if (m_d->generate.checkRegionOnly)
        ui->insert(QStringLiteral("region_only"), m_d->generate.checkRegionOnly->isChecked());
    if (m_d->generate.checkEditMode)
        ui->insert(QStringLiteral("edit_mode"), m_d->generate.checkEditMode->isChecked());
    if (m_d->generate.spinBatchCount)
        ui->insert(QStringLiteral("batch_count"), m_d->generate.spinBatchCount->value());
    if (m_d->generate.spinSeed)
        ui->insert(QStringLiteral("seed"), static_cast<double>(m_d->generate.spinSeed->value()));
    if (m_d->generate.checkFixedSeed)
        ui->insert(QStringLiteral("fixed_seed"), m_d->generate.checkFixedSeed->isChecked());
    ui->insert(QStringLiteral("resolution_multiplier"), m_d->generate.resolutionMultiplier);
    if (m_d->generate.comboQueueMode) {
        const int qix = m_d->generate.comboQueueMode->currentIndex();
        QString qm = QStringLiteral("back");
        if (qix == 1)
            qm = QStringLiteral("front");
        else if (qix == 2)
            qm = QStringLiteral("replace");
        ui->insert(QStringLiteral("queue_mode"), qm);
    }
    {
        QJsonObject sset = ComfyUIUtils::loadSettingsJson();
        const QString pt = sset.value(QStringLiteral("prompt_translation")).toString();
        const bool transEn = !pt.isEmpty() && pt != QLatin1String("disabled");
        ui->insert(QStringLiteral("translation_enabled"), transEn);
    }
    if (m_d->generate.spinLayerCount)
        ui->insert(QStringLiteral("layer_count"), m_d->generate.spinLayerCount->value());
}
void ComfyUIRemoteDock::loadRegionsPersistedForDocument(KisImageSP img)
{
    if (!img)
        return;
    const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);

    auto loadRootFromKConfig = [this]() {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int n = cfg.readEntry("RegionsCount", 0);
        m_d->regionEntries.clear();
        for (int i = 0; i < n; i++) {
            Private::RegionEntry e;
            e.name = cfg.readEntry(QStringLiteral("Region_%1_Name").arg(i), QString());
            e.prompt = cfg.readEntry(QStringLiteral("Region_%1_Prompt").arg(i), QString());
            e.maskSource = cfg.readEntry(QStringLiteral("Region_%1_MaskSource").arg(i), QStringLiteral("selection"));
            if (!e.name.isEmpty())
                m_d->regionEntries.append(e);
        }
    };
    auto loadEditFromKConfig = [this]() {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int ne = cfg.readEntry("EditRegionsCount", 0);
        m_d->editRegionEntries.clear();
        for (int i = 0; i < ne; i++) {
            Private::RegionEntry e;
            e.name = cfg.readEntry(QStringLiteral("EditRegion_%1_Name").arg(i), QString());
            e.prompt = cfg.readEntry(QStringLiteral("EditRegion_%1_Prompt").arg(i), QString());
            e.maskSource = cfg.readEntry(QStringLiteral("EditRegion_%1_MaskSource").arg(i), QStringLiteral("selection"));
            if (!e.name.isEmpty())
                m_d->editRegionEntries.append(e);
        }
    };

    bool rootFromDoc = false;
    const QJsonArray ra = ComfyUIUtils::readRegionUiArrayFromDocumentUi(ui, &rootFromDoc);
    if (rootFromDoc) {
        m_d->regionEntries.clear();
        for (const ComfyRegionUiStateEntry &u : regionUiStateEntriesFromJsonArray(ra))
            m_d->regionEntries.append(comfyRegionEntryFromUi(u));
    } else {
        loadRootFromKConfig();
    }

    QString rootPos;
    QString rootNeg;
    if (rootRegionUiWrapFromJson(ui.value(QStringLiteral("root")).toObject(), &rootPos, &rootNeg, nullptr)) {
        if (m_d->generate.editPrompt && !rootPos.isEmpty())
            m_d->generate.editPrompt->setPlainText(rootPos);
        if (m_d->generate.editNegative && !rootNeg.isEmpty())
            m_d->generate.editNegative->setPlainText(rootNeg);
    }

    const QJsonObject editObj = ui.value(QStringLiteral("edit")).toObject();
    QList<ComfyRegionUiStateEntry> editRegs;
    const bool editFromDoc =
        rootRegionUiWrapFromJson(editObj, nullptr, nullptr, &editRegs) && editObj.contains(QStringLiteral("regions"));
    if (editFromDoc) {
        m_d->editRegionEntries.clear();
        for (const ComfyRegionUiStateEntry &u : editRegs)
            m_d->editRegionEntries.append(comfyRegionEntryFromUi(u));
    } else {
        loadEditFromKConfig();
    }

    m_d->rootControlLayers = ComfyControlLayer::fromJsonArray(ui.value(QStringLiteral("control")).toArray());
    refreshRootControlLayersList();

    refreshRegionsList();
}
void ComfyUIRemoteDock::applyModelFieldsFromUiJson(const QJsonObject &ui)
{
    if (ui.contains(QStringLiteral("workspace")) && m_d->comboWorkspace) {
        const int ix = comfyWorkspaceIndexFromUiJson(ui.value(QStringLiteral("workspace")).toString());
        if (ix >= 0 && ix < m_d->comboWorkspace->count())
            m_d->comboWorkspace->setCurrentIndex(ix);
    }
    if (ui.contains(QStringLiteral("style")))
        applyStyleIdFromDocumentDefaults(ui.value(QStringLiteral("style")).toString());
    if (ui.contains(QStringLiteral("strength")) && m_d->generate.spinStrength) {
        const QJsonValue sv = ui.value(QStringLiteral("strength"));
        int pct = 100;
        if (sv.isDouble()) {
            const double d = sv.toDouble();
            pct = (d <= 1.0001) ? qBound(1, qRound(d * 100.0), 100) : qBound(1, qRound(d), 100);
        } else {
            pct = qBound(1, sv.toInt(100), 100);
        }
        m_d->generate.spinStrength->setValue(pct);
    }
    if (ui.contains(QStringLiteral("region_only")) && m_d->generate.checkRegionOnly) {
        QSignalBlocker b(m_d->generate.checkRegionOnly);
        m_d->generate.checkRegionOnly->setChecked(ui.value(QStringLiteral("region_only")).toBool());
    }
    if (ui.contains(QStringLiteral("edit_mode")) && m_d->generate.checkEditMode) {
        QSignalBlocker b(m_d->generate.checkEditMode);
        m_d->generate.checkEditMode->setChecked(ui.value(QStringLiteral("edit_mode")).toBool());
    }
    if (ui.contains(QStringLiteral("batch_count")) && m_d->generate.spinBatchCount) {
        m_d->generate.spinBatchCount->setValue(qBound(m_d->generate.spinBatchCount->minimum(),
                                            ui.value(QStringLiteral("batch_count")).toInt(1),
                                            m_d->generate.spinBatchCount->maximum()));
    }
    if (ui.contains(QStringLiteral("seed")) && m_d->generate.spinSeed) {
        const qint64 s = static_cast<qint64>(ui.value(QStringLiteral("seed")).toDouble());
        m_d->generate.spinSeed->setValue(int(qBound<qint64>(0, s, 2147483647)));
    }
    if (ui.contains(QStringLiteral("fixed_seed")) && m_d->generate.checkFixedSeed) {
        QSignalBlocker b(m_d->generate.checkFixedSeed);
        m_d->generate.checkFixedSeed->setChecked(ui.value(QStringLiteral("fixed_seed")).toBool());
    }
    if (ui.contains(QStringLiteral("resolution_multiplier"))) {
        double m = ui.value(QStringLiteral("resolution_multiplier")).toDouble(1.0);
        if (m <= 0.0)
            m = 1.0;
        m_d->generate.resolutionMultiplier = m;
        if (m_d->generate.sliderResolutionMultiplier && m_d->generate.labelResolutionMultiplier) {
            int sliderValue = qRound(m * 10.0);
            sliderValue = qBound(3, sliderValue, 15);
            QSignalBlocker bs(m_d->generate.sliderResolutionMultiplier);
            m_d->generate.sliderResolutionMultiplier->setValue(sliderValue);
            m_d->generate.labelResolutionMultiplier->setText(QString::number(m_d->generate.resolutionMultiplier, 'f', 1) + QStringLiteral("×"));
        }
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ResolutionMultiplier", m_d->generate.resolutionMultiplier);
    }
    if (ui.contains(QStringLiteral("queue_mode")) && m_d->generate.comboQueueMode) {
        const QString qm = ui.value(QStringLiteral("queue_mode")).toString();
        int ix = 0;
        if (qm == QStringLiteral("front"))
            ix = 1;
        else if (qm == QStringLiteral("replace"))
            ix = 2;
        QSignalBlocker b(m_d->generate.comboQueueMode);
        if (ix >= 0 && ix < m_d->generate.comboQueueMode->count())
            m_d->generate.comboQueueMode->setCurrentIndex(ix);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("QueueMode", ix);
    }
    if (ui.contains(QStringLiteral("upscale"))) {
        const QJsonObject us = ui.value(QStringLiteral("upscale")).toObject();
        syncUpscaleRefinementModelFromPresetCombo();
        if (us.contains(QStringLiteral("upscaler")) && m_d->upscale.comboUpscaleModel) {
            const QString upscaler = us.value(QStringLiteral("upscaler")).toString();
            m_d->upscaleRt.upscalerModel = upscaler;
            const int idx = m_d->upscale.comboUpscaleModel->findData(upscaler);
            if (idx >= 0) {
                QSignalBlocker b(m_d->upscale.comboUpscaleModel);
                m_d->upscale.comboUpscaleModel->setCurrentIndex(idx);
            }
        }
        auto readStrengthPct = [](const QJsonValue &v, int defVal) {
            if (v.isDouble()) {
                const double d = v.toDouble();
                return (d <= 1.0001) ? qBound(0, qRound(d * 100.0), 100) : qBound(0, qRound(d), 100);
            }
            return qBound(0, v.toInt(defVal), 100);
        };
        if (us.contains(QStringLiteral("factor"))) {
            double f = us.value(QStringLiteral("factor")).toDouble(m_d->upscaleRt.upscaleFactor);
            f = qBound(1.0, f, 4.0);
            m_d->upscaleRt.upscaleFactor = f;
            if (m_d->upscale.sliderUpscaleFactor) {
                QSignalBlocker bs(m_d->upscale.sliderUpscaleFactor);
                m_d->upscale.sliderUpscaleFactor->setValue(qRound(f * 10.0));
            }
            if (m_d->upscale.spinUpscaleFactor) {
                QSignalBlocker bsp(m_d->upscale.spinUpscaleFactor);
                m_d->upscale.spinUpscaleFactor->setValue(f);
            }
        }
        if (us.contains(QStringLiteral("use_diffusion")) && m_d->upscale.checkUpscaleRefine) {
            QSignalBlocker b(m_d->upscale.checkUpscaleRefine);
            m_d->upscale.checkUpscaleRefine->setChecked(us.value(QStringLiteral("use_diffusion")).toBool());
            syncUpscaleRefineControlsEnabled(m_d->upscale.checkUpscaleRefine->isChecked());
        }
        if (us.contains(QStringLiteral("strength")) && m_d->upscale.sliderUpscaleRefineStrength) {
            const int pct = readStrengthPct(us.value(QStringLiteral("strength")), 30);
            QSignalBlocker b(m_d->upscale.sliderUpscaleRefineStrength);
            m_d->upscale.sliderUpscaleRefineStrength->setValue(pct);
            if (m_d->upscale.labelUpscaleRefineStrength)
                m_d->upscale.labelUpscaleRefineStrength->setText(QString::number(pct) + QLatin1Char('%'));
        }
        if (us.contains(QStringLiteral("unblur_strength")) && m_d->upscale.sliderUpscaleRefineGuidance) {
            const int pct = readStrengthPct(us.value(QStringLiteral("unblur_strength")), 50);
            QSignalBlocker b(m_d->upscale.sliderUpscaleRefineGuidance);
            m_d->upscale.sliderUpscaleRefineGuidance->setValue(pct);
            if (m_d->upscale.labelUpscaleRefineGuidance)
                m_d->upscale.labelUpscaleRefineGuidance->setText(QString::number(pct) + QLatin1Char('%'));
        }
        if (us.contains(QStringLiteral("tile_overlap_mode")) && m_d->upscale.comboTileOverlapMode) {
            const int tom = qBound(0, us.value(QStringLiteral("tile_overlap_mode")).toInt(0), m_d->upscale.comboTileOverlapMode->count() - 1);
            m_d->upscaleRt.tileOverlapMode = tom;
            QSignalBlocker b(m_d->upscale.comboTileOverlapMode);
            m_d->upscale.comboTileOverlapMode->setCurrentIndex(tom);
            if (m_d->upscale.spinTileOverlap)
                m_d->upscale.spinTileOverlap->setVisible(tom == 1);
        }
        if (us.contains(QStringLiteral("tile_overlap")) && m_d->upscale.spinTileOverlap) {
            m_d->upscaleRt.tileOverlap = us.value(QStringLiteral("tile_overlap")).toInt(m_d->upscaleRt.tileOverlap);
            QSignalBlocker b(m_d->upscale.spinTileOverlap);
            m_d->upscale.spinTileOverlap->setValue(m_d->upscaleRt.tileOverlap);
        }
        if (us.contains(QStringLiteral("use_prompt")) && m_d->upscale.checkUpscaleUsePrompt) {
            QSignalBlocker b(m_d->upscale.checkUpscaleUsePrompt);
            m_d->upscale.checkUpscaleUsePrompt->setChecked(us.value(QStringLiteral("use_prompt")).toBool());
        }
        if (us.contains(QStringLiteral("refinement_style")) && m_d->upscale.comboUpscaleRefinementModel)
            applyStyleIdToPresetCombo(m_d->upscale.comboUpscaleRefinementModel, us.value(QStringLiteral("refinement_style")).toString());
        KConfigGroup ucfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
        if (m_d->upscale.checkUpscaleRefine)
            ucfg.writeEntry(QStringLiteral("UpscaleRefineEnabled"), m_d->upscale.checkUpscaleRefine->isChecked());
        if (m_d->upscale.sliderUpscaleRefineStrength)
            ucfg.writeEntry(QStringLiteral("UpscaleRefineStrength"), m_d->upscale.sliderUpscaleRefineStrength->value());
        if (m_d->upscale.sliderUpscaleRefineGuidance)
            ucfg.writeEntry(QStringLiteral("UpscaleRefineGuidance"), m_d->upscale.sliderUpscaleRefineGuidance->value());
        if (m_d->upscale.checkUpscaleUsePrompt)
            ucfg.writeEntry(QStringLiteral("UpscaleUsePrompt"), m_d->upscale.checkUpscaleUsePrompt->isChecked());
        ucfg.writeEntry(QStringLiteral("TileOverlapMode"), m_d->upscaleRt.tileOverlapMode);
        ucfg.writeEntry(QStringLiteral("TileOverlap"), m_d->upscaleRt.tileOverlap);
        if (m_d->upscale.comboUpscaleRefinementModel)
            ucfg.writeEntry(QStringLiteral("UpscaleRefinementModelIndex"), m_d->upscale.comboUpscaleRefinementModel->currentIndex());
    }
    if (ui.contains(QStringLiteral("translation_enabled"))) {
        QJsonObject s = ComfyUIUtils::loadSettingsJson();
        const bool tEn = ui.value(QStringLiteral("translation_enabled")).toBool(false);
        QString ptx = s.value(QStringLiteral("prompt_translation")).toString();
        if (!tEn)
            ptx = QStringLiteral("disabled");
        else if (ptx.isEmpty() || ptx == QLatin1String("disabled"))
            ptx = QStringLiteral("en");
        s.insert(QStringLiteral("prompt_translation"), ptx);
        ComfyUIUtils::saveSettingsJson(s);
        applyInterfaceAppearanceSettings();
    }
    if (ui.contains(QStringLiteral("layer_count")) && m_d->generate.spinLayerCount) {
        const int lc = qBound(m_d->generate.spinLayerCount->minimum(),
                              ui.value(QStringLiteral("layer_count")).toInt(1),
                              m_d->generate.spinLayerCount->maximum());
        m_d->generate.spinLayerCount->setValue(lc);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("LayerCount", lc);
    }
    if (ui.contains(QStringLiteral("seed")) || ui.contains(QStringLiteral("fixed_seed")))
        persistSeedToConfig();
    if (ui.contains(QStringLiteral("edit_mode")) || ui.contains(QStringLiteral("workspace")))
        refreshRegionsList();
    updateUpscaleTargetSize();
}
