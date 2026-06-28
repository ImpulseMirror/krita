/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyTheme.h"

#include "ComfyRegionProcess.h"
#include "ComfyUIPoseLayers.h"
#include "ComfyGenerateUi.h"

#include <KSharedConfig>

#include <kis_annotation.h>
#include <kis_shape_layer.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

void ComfyUIRemoteDock::syncPerformanceFromAutoPreset()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    if (s.value(QStringLiteral("performance_preset")).toString() != QLatin1String("auto"))
        return;
    int b = m_d->generate.spinBatchCount ? m_d->generate.spinBatchCount->value() : 1;
    double m = m_d->generate.resolutionMultiplier <= 0.0 ? 1.0 : m_d->generate.resolutionMultiplier;
    ComfyUIUtils::generationPerformanceBatchResolution(s, m_d->lastComfySystemStats, b, m, &b, &m);
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(s, &m);
    b = qBound(1, b, 16);
    m = qMax(0.3, qMin(m <= 0.0 ? 1.0 : m, 3.0));
    if (m_d->generate.spinBatchCount)
        m_d->generate.spinBatchCount->setValue(qBound(m_d->generate.spinBatchCount->minimum(), b, m_d->generate.spinBatchCount->maximum()));
    m_d->generate.resolutionMultiplier = m;
    if (m_d->generate.sliderResolutionMultiplier) {
        const int sv = qRound(m * 10.0);
        m_d->generate.sliderResolutionMultiplier->setValue(
            qBound(m_d->generate.sliderResolutionMultiplier->minimum(), sv, m_d->generate.sliderResolutionMultiplier->maximum()));
    }
    if (m_d->generate.labelResolutionMultiplier)
        m_d->generate.labelResolutionMultiplier->setText(QString::number(m, 'f', 1) + QLatin1String("×"));
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("ResolutionMultiplier", m_d->generate.resolutionMultiplier);
    schedulePersistDocumentDefaults();
    refreshQueueResolutionRowVisibility();
}
void ComfyUIRemoteDock::applyPromptHeader()
{
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->setPromptHeaderMode(qBound(0, m_d->promptHeaderMode, 2));
    if (!m_d->generate.regionsGroupBox || !m_d->generate.regionHeaderLabel) return;
    const int mode = qBound(0, m_d->promptHeaderMode, 2);
    if (mode == 0) {
        m_d->generate.regionsGroupBox->setTitle(ComfyTr::tr("Regions"));
        m_d->generate.regionHeaderLabel->setPixmap(QPixmap());
        m_d->generate.regionHeaderLabel->setText(ComfyTr::tr("Different prompt per area (layer or selection):"));
        m_d->generate.regionHeaderLabel->show();
    } else if (mode == 1) {
        m_d->generate.regionsGroupBox->setTitle(QString());
        m_d->generate.regionHeaderLabel->setText(QString());
        m_d->generate.regionHeaderLabel->setPixmap(
            ComfyTheme::icon(QStringLiteral("region-prompt")).pixmap(16, 16));
        m_d->generate.regionHeaderLabel->show();
    } else {
        m_d->generate.regionsGroupBox->setTitle(QString());
        m_d->generate.regionHeaderLabel->setText(QString());
        m_d->generate.regionHeaderLabel->setPixmap(QPixmap());
        m_d->generate.regionHeaderLabel->hide();
    }
}
void ComfyUIRemoteDock::refreshRegionsList()
{
    if (m_d->generate.regionPromptWidget) {
        m_d->generate.regionPromptWidget->bind(&comfyActiveRegionEntries(m_d.data()), &m_d->activeRegionIndex);
        m_d->generate.regionPromptWidget->refresh();
    }
    refreshRegionControlLayersList();
    updateGenerateOptions();
}
void ComfyUIRemoteDock::loadRegionsFromConfig()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    int n = cfg.readEntry("RegionsCount", 0);
    m_d->regionEntries.clear();
    for (int i = 0; i < n; i++) {
        Private::RegionEntry e;
        e.name = cfg.readEntry(QString("Region_%1_Name").arg(i), QString());
        e.prompt = cfg.readEntry(QString("Region_%1_Prompt").arg(i), QString());
        e.maskSource = cfg.readEntry(QString("Region_%1_MaskSource").arg(i), "selection");
        e.layerIds = cfg.readEntry(QString("Region_%1_LayerIds").arg(i), QString());
        if (!e.name.isEmpty())
            m_d->regionEntries.append(e);
    }
    int ne = cfg.readEntry("EditRegionsCount", 0);
    m_d->editRegionEntries.clear();
    for (int i = 0; i < ne; i++) {
        Private::RegionEntry e;
        e.name = cfg.readEntry(QString("EditRegion_%1_Name").arg(i), QString());
        e.prompt = cfg.readEntry(QString("EditRegion_%1_Prompt").arg(i), QString());
        e.maskSource = cfg.readEntry(QString("EditRegion_%1_MaskSource").arg(i), QStringLiteral("selection"));
        e.layerIds = cfg.readEntry(QString("EditRegion_%1_LayerIds").arg(i), QString());
        if (!e.name.isEmpty())
            m_d->editRegionEntries.append(e);
    }
}
void ComfyUIRemoteDock::saveRegionsToConfig()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", m_d->regionEntries.size());
    for (int i = 0; i < m_d->regionEntries.size(); i++) {
        const Private::RegionEntry &e = m_d->regionEntries.at(i);
        cfg.writeEntry(QString("Region_%1_Name").arg(i), e.name);
        cfg.writeEntry(QString("Region_%1_Prompt").arg(i), e.prompt);
        cfg.writeEntry(QString("Region_%1_MaskSource").arg(i), e.maskSource);
        cfg.writeEntry(QString("Region_%1_LayerIds").arg(i), e.layerIds);
    }
    cfg.writeEntry("EditRegionsCount", m_d->editRegionEntries.size());
    for (int i = 0; i < m_d->editRegionEntries.size(); i++) {
        const Private::RegionEntry &e = m_d->editRegionEntries.at(i);
        cfg.writeEntry(QString("EditRegion_%1_Name").arg(i), e.name);
        cfg.writeEntry(QString("EditRegion_%1_Prompt").arg(i), e.prompt);
        cfg.writeEntry(QString("EditRegion_%1_MaskSource").arg(i), e.maskSource);
        cfg.writeEntry(QString("EditRegion_%1_LayerIds").arg(i), e.layerIds);
    }
    cfg.config()->sync();
    scheduleDocumentUiJsonSave();
}
void ComfyUIRemoteDock::savePreviewLayerIdToDocument(const QString &layerId)
{
    if (!m_d->canvas) return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img) return;
    const QString key = ComfyUIUtils::previewLayerAnnotationKey();
    if (layerId.isEmpty()) {
        img->removeAnnotation(key);
    } else {
        img->removeAnnotation(key);
        img->addAnnotation(KisAnnotationSP(new KisAnnotation(key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("preview_layer")), layerId.toUtf8())));
    }
    m_d->previewLayerId = layerId;
}
void ComfyUIRemoteDock::slotAddPoseGuideToVectorLayer()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisLayerSP al = m_d->viewManager->activeLayer();
    KisShapeLayerSP sl(qobject_cast<KisShapeLayer *>(al.data()));
    if (!sl) {
        setStatusMessage(ComfyTr::tr("Select a vector layer, then add a pose guide."), true);
        return;
    }
    KisDocument *doc = m_d->canvas && m_d->canvas->imageView() ? m_d->canvas->imageView()->document() : nullptr;
    if (!doc) {
        setStatusMessage(ComfyTr::tr("Could not access the document to edit vector shapes."), true);
        return;
    }
    const int people = m_d->generate.spinPoseGuidePeopleCount ? m_d->generate.spinPoseGuidePeopleCount->value() : 1;
    if (ComfyUIPoseLayers::instance().addPoseCharacter(m_d->viewManager->image(), sl, doc, people))
        setStatusMessage(ComfyTr::tr("Pose guide added. Pose SVG is refreshed every 500 ms while the layer exists."), false);
    else
        setStatusMessage(ComfyTr::tr("Could not add pose guide (check that the layer is a vector layer)."), true);
}
