/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyStyleCollection.h"
#include "ComfyResources.h"
#include "ComfyRegionProcess.h"
#include "ComfyControlLayer.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QUuid>

#include <klocalizedstring.h>
#include <kis_image_manager.h>

namespace {

QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForUpscale(const ComfyUIRemoteDock::Private *d)
{
    QList<ComfyUIRemoteDock::Private::RegionEntry> regs = comfyActiveRegionEntries(d);
    if (d->checkRegionOnly && d->checkRegionOnly->isChecked()) {
        const int row = comfyActiveRegionRow(d);
        if (row >= 0 && row < regs.size())
            return {regs.at(row)};
    }
    return regs;
}

QList<ComfyControlLayerEntry> controlLayersForUpscale(const ComfyUIRemoteDock::Private *d)
{
    return mergedJobControlLayers(d->rootControlLayers, regionsForUpscale(d));
}

QImage maskPngForComfyUpload(const QImage &maskGray, int targetW, int targetH)
{
    QImage scaled = maskGray;
    if (targetW > 0 && targetH > 0
        && (scaled.width() != targetW || scaled.height() != targetH)) {
        scaled = scaled.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage maskPng(scaled.size(), QImage::Format_ARGB32);
    for (int y = 0; y < scaled.height(); y++) {
        for (int x = 0; x < scaled.width(); x++) {
            const int g = qGray(scaled.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    }
    return maskPng;
}

} // namespace

void ComfyUIRemoteDock::slotUpscale()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    auto colorCheck = ComfyUIUtils::checkColorMode(m_d->viewManager->image());
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
        return;
    }
    int w = canvasImg.width();
    int h = canvasImg.height();
    int w2 = qRound(w * m_d->upscaleFactor);
    int h2 = qRound(h * m_d->upscaleFactor);
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!canvasImg.save(tmpImage->fileName())) {
        setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }
    m_d->btnUpscale->setEnabled(false);
    m_d->progressBar->setValue(0);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/upload/image");
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/upload/image");
    else baseUrl.setPath(path + "upload/image");
    tmpImage->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_upscale.png\""));
    imagePart.setBodyDevice(tmpImage);
    tmpImage->setParent(multiPart);
    multiPart->append(imagePart);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(ComfyTr::tr("Uploading for upscale…"));
    setProgressBarKind(true);  // §13.18
    connect(reply, &QNetworkReply::finished, this, [this, reply, w, h, w2, h2]() {
        reply->deleteLater();
        setProgressBarKind(false);  // §13.18: upload finished
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->upscaleUploadedImageName = obj.value("name").toString();
        if (m_d->upscaleUploadedImageName.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        continueUpscaleAfterCanvasUpload(w, h, w2, h2);
    });
}

void ComfyUIRemoteDock::continueUpscaleAfterCanvasUpload(int canvasW, int canvasH, int w2, int h2)
{
        const bool wantRefine = m_d->checkUpscaleRefine && m_d->checkUpscaleRefine->isChecked();
        const QString scaleMethod = ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(
            ComfyUIUtils::normalizeDiffusionScaleMode(
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("diffusion_scale_mode")).toString()));
        const int overlapPx = m_d->tileOverlapMode == 1 ? m_d->tileOverlap : -1;

        QJsonObject workflow;
        bool useTiledRefine = false;
        if (wantRefine) {
            if (!m_d->comboUpscaleRefinementModel || m_d->comboUpscaleRefinementModel->currentIndex() <= 0) {
                setStatusMessage(
                    ComfyTr::tr("Select a refinement model (a style preset other than \"None\"), or disable \"Refine upscaled image\"."),
                    true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QString ckpt = checkpointNameForUpscaleRefinementPreset();
            if (ckpt.isEmpty())
                ckpt = m_d->comboCheckpoint ? m_d->comboCheckpoint->currentText().trimmed() : QString();
            if (ckpt.isEmpty())
                ckpt = QStringLiteral("v1-5-pruned-emaonly.safetensors");
            QString styleArch;
            if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
                const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    styleArch = st->architecture;
            }
            int baseSteps = 20;
            double baseCfg = 8.0;
            QString sam = QStringLiteral("euler");
            QString sch = QStringLiteral("normal");
            readUpscaleRefinementSampling(&baseSteps, &baseCfg, &sam, &sch);
            const ComfyUIUtils::ResolvedSamplerInputs rsi = ComfyUIUtils::resolveSamplerForLive(
                ComfyUIUtils::loadSettingsJson(), sam, baseSteps, baseCfg);
            const int strengthPct = m_d->sliderUpscaleRefineStrength ? m_d->sliderUpscaleRefineStrength->value() : 30;
            const int guidancePct = m_d->sliderUpscaleRefineGuidance ? m_d->sliderUpscaleRefineGuidance->value() : 50;
            const double denoise = qBound(0.05, strengthPct / 100.0, 1.0);
            const double kcfg = qBound(1.0, 1.0 + (guidancePct / 100.0) * 14.0, 20.0);
            int stylePreferredResolution = 0;
            const ComfyResources::Arch archForTiles =
                ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
            if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
                const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    stylePreferredResolution = st->preferredResolution;
            }
            const ComfyUIUtils::UpscaleTiledLayoutSpec tileLayout = ComfyUIUtils::computeUpscaleTiledLayoutSpec(
                w2, h2, archForTiles, stylePreferredResolution, denoise, overlapPx);
            const int tileEstimate = tileLayout.totalTiles;
            qint64 seed = m_d->checkFixedSeed && m_d->checkFixedSeed->isChecked()
                ? static_cast<qint64>(m_d->spinSeed ? m_d->spinSeed->value() : 0)
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!m_d->checkFixedSeed || !m_d->checkFixedSeed->isChecked()) {
                if (m_d->spinSeed)
                    m_d->spinSeed->setValue(static_cast<int>(qBound<qint64>(0, seed, 2147483647)));
            }
            const bool usePrompt = m_d->checkUpscaleUsePrompt && m_d->checkUpscaleUsePrompt->isChecked() && m_d->editPrompt;
            QString pos;
            QString neg;
            if (usePrompt) {
                pos = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
                pos = ComfyUIUtils::evalWildcards(pos, static_cast<quint32>(seed));
                ComfyUIUtils::extractLayerPlaceholders(pos);
                pos = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(pos);
                neg = ComfyUIUtils::evalWildcards(
                    ComfyUIUtils::stripPromptComments(m_d->editNegative ? m_d->editNegative->toPlainText() : QString()).trimmed(),
                    static_cast<quint32>(seed));
            }
            if (pos.isEmpty())
                pos = QStringLiteral("high quality, detailed");

            if (tileEstimate > 1) {
                useTiledRefine = true;
                ComfyWorkflowEngine::UpscaleTiledParams tp;
                tp.imageName = m_d->upscaleUploadedImageName;
                tp.checkpoint = ckpt;
                tp.arch = archForTiles;
                tp.stylePreferredResolution = stylePreferredResolution;
                tp.seed = seed;
                tp.positivePrompt = pos;
                tp.negativePrompt = neg;
                tp.sampler = rsi.sampler;
                tp.scheduler = rsi.scheduler;
                tp.steps = rsi.steps;
                tp.cfg = kcfg;
                tp.denoise = denoise;
                tp.scaledWidth = w2;
                tp.scaledHeight = h2;
                tp.targetWidth = w2;
                tp.targetHeight = h2;
                tp.tileOverlapPx = overlapPx;
                tp.minTileSize = tileLayout.minTileSize;
                tp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
                tp.upscaleFactor = canvasW > 0 ? static_cast<double>(w2) / static_cast<double>(canvasW) : 1.0;
                tp.editReference = ComfyResources::supportsEditInstructions(archForTiles);

                m_d->upscaleStashedTiledParams = tp;
                m_d->upscaleStashedW2 = w2;
                m_d->upscaleStashedH2 = h2;
                m_d->upscaleStashedCanvasW = canvasW;
                m_d->upscaleStashedArch = archForTiles;
                m_d->upscalePendingIsTiled = true;
                m_d->upscalePendingWantRefine = wantRefine;

                m_d->upscaleRegionalInputs.clear();
                m_d->upscaleProcessedRegions.clear();
                if (usePrompt && ComfyResources::supportsRegions(archForTiles) && m_d->viewManager) {
                    const ComfyRegionProcess::ProcessRegionsResult processed = ComfyRegionProcess::processRegions(
                        regionsForUpscale(m_d.data()), m_d->viewManager->image(), m_d->viewManager, pos);
                    if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion) {
                        m_d->upscaleProcessedRegions = processed.regions;
                        m_d->upscaleRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
                            m_d->upscaleProcessedRegions, tp.promptTranslationLanguage);
                        const double factor = tp.upscaleFactor;
                        for (int ri = 0; ri < m_d->upscaleRegionalInputs.size(); ++ri) {
                            if (ri >= m_d->upscaleProcessedRegions.size()
                                || m_d->upscaleProcessedRegions.at(ri).isBackground)
                                continue;
                            QRect b = ComfyRegionProcess::maskNonZeroBounds(
                                m_d->upscaleProcessedRegions.at(ri).maskGray);
                            if (b.isEmpty())
                                continue;
                            if (qAbs(factor - 1.0) > 0.001) {
                                b = QRect(qRound(b.x() * factor),
                                          qRound(b.y() * factor),
                                          qMax(1, qRound(b.width() * factor)),
                                          qMax(1, qRound(b.height() * factor)));
                            }
                            m_d->upscaleRegionalInputs[ri].maskBoundsUpscaled = b;
                        }
                        m_d->upscaleAwaitingRegionMaskUploads = true;
                        m_d->upscaleRegionMaskUploadIndex = 0;
                        uploadNextUpscaleRegionMask();
                        return;
                    }
                }
                beginUpscaleConditioningUploadPipeline();
                return;
            } else {
                ComfyWorkflowEngine::UpscaleRefineParams rp;
                rp.imageName = m_d->upscaleUploadedImageName;
                rp.checkpoint = ckpt;
                rp.arch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
                rp.scaleWidth = qMax(64, (w2 / 8) * 8);
                rp.scaleHeight = qMax(64, (h2 / 8) * 8);
                rp.upscaleMethod = scaleMethod;
                rp.seed = seed;
                rp.positivePrompt = pos;
                rp.negativePrompt = neg;
                rp.sampler = rsi.sampler;
                rp.scheduler = rsi.scheduler;
                rp.steps = rsi.steps;
                rp.cfg = kcfg;
                rp.denoise = denoise;
                rp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
                workflow = ComfyWorkflowEngine::buildUpscaleRefine(rp);
            }
            if (workflow.isEmpty()) {
                setStatusMessage(useTiledRefine ? ComfyTr::tr("Tiled upscale workflow error.") : ComfyTr::tr("Upscale refine workflow error."),
                                 true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
        } else {
            ComfyWorkflowEngine::UpscaleSimpleParams sp;
            sp.imageName = m_d->upscaleUploadedImageName;
            sp.targetWidth = w2;
            sp.targetHeight = h2;
            sp.upscaleMethod = scaleMethod;
            workflow = ComfyWorkflowEngine::buildUpscaleSimple(sp);
            if (workflow.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Upscale workflow error."), true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (wantRefine && !useTiledRefine) {
            ComfyUIUtils::applyUpscaleRefineVaedecodeTiling(
                workflow,
                QStringLiteral("8"),
                m_d->tileOverlapMode,
                m_d->tileOverlap,
                ComfyUIUtils::loadSettingsJson());
        }
        submitUpscaleWorkflow(workflow, wantRefine, useTiledRefine);
}

void ComfyUIRemoteDock::beginUpscaleConditioningUploadPipeline()
{
    m_d->upscaleControlLayersActive = controlLayersForUpscale(m_d.data());
    if (ComfyControlLayer::anyNeedsGenerateUpload(m_d->upscaleControlLayersActive)) {
        m_d->upscaleAwaitingControlUploads = true;
        m_d->upscaleControlUploadIndex = 0;
        m_d->upscaleControlUploadedNames.clear();
        uploadNextUpscaleControlImage();
        return;
    }
    finalizeUpscaleWorkflowAndSubmit();
}

void ComfyUIRemoteDock::uploadNextUpscaleControlImage()
{
    if (!m_d->upscaleAwaitingControlUploads || !m_d->viewManager || !m_d->nam) {
        m_d->btnUpscale->setEnabled(true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    if (!image) {
        m_d->upscaleAwaitingControlUploads = false;
        m_d->btnUpscale->setEnabled(true);
        return;
    }
    while (m_d->upscaleControlUploadIndex < m_d->upscaleControlLayersActive.size()) {
        const ComfyControlLayerEntry ce = m_d->upscaleControlLayersActive.at(m_d->upscaleControlUploadIndex);
        m_d->upscaleControlUploadIndex++;
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;

        QImage img = ComfyUIUtils::getLayerProjectionAsQImage(image, ce.layerName);
        if (img.isNull()) {
            setStatusMessage(ComfyTr::tr("Could not export control layer \"%1\".", ce.layerName), true);
            m_d->upscaleAwaitingControlUploads = false;
            m_d->btnUpscale->setEnabled(true);
            return;
        }
        if (m_d->upscaleStashedW2 > 0 && m_d->upscaleStashedH2 > 0
            && (img.width() != m_d->upscaleStashedW2 || img.height() != m_d->upscaleStashedH2)) {
            img = img.scaled(m_d->upscaleStashedW2, m_d->upscaleStashedH2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (ComfyUIUtils::isControlModeLines(ce.mode)) {
            img = img.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < img.height(); y++) {
                for (int x = 0; x < img.width(); x++) {
                    const QRgb px = img.pixel(x, y);
                    if (qAlpha(px) > 0)
                        img.setPixel(x, y, qRgb(255, 255, 255));
                    else
                        img.setPixel(x, y, qRgb(0, 0, 0));
                }
            }
        }

        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!img.save(tmp->fileName())) {
            setStatusMessage(ComfyTr::tr("Could not save control layer image."), true);
            m_d->upscaleAwaitingControlUploads = false;
            m_d->btnUpscale->setEnabled(true);
            return;
        }

        QUrl uploadUrl(urlStr);
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == QLatin1Char('/'))
            uploadUrl.setPath(QStringLiteral("/upload/image"));
        else if (!up.endsWith(QLatin1Char('/')))
            uploadUrl.setPath(up + QStringLiteral("/upload/image"));
        else
            uploadUrl.setPath(up + QStringLiteral("upload/image"));

        tmp->open();
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"upscale_control_%1.png\"")
                                    .arg(m_d->upscaleControlUploadedNames.size())));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        m_d->labelStatus->setText(ComfyTr::tr("Uploading control layer %1…", ce.layerName));
        setProgressBarKind(true);
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp]() {
            replyUp->deleteLater();
            setProgressBarKind(false);
            if (replyUp->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Control upload error: %1", replyUp->errorString()), true);
                m_d->upscaleAwaitingControlUploads = false;
                m_d->btnUpscale->setEnabled(true);
                return;
            }
            const QString name =
                QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return control image name."), true);
                m_d->upscaleAwaitingControlUploads = false;
                m_d->btnUpscale->setEnabled(true);
                return;
            }
            m_d->upscaleControlUploadedNames.append(name);
            uploadNextUpscaleControlImage();
        });
        return;
    }
    m_d->upscaleAwaitingControlUploads = false;
    finalizeUpscaleWorkflowAndSubmit();
}

void ComfyUIRemoteDock::uploadNextUpscaleRegionMask()
{
    if (!m_d->upscaleAwaitingRegionMaskUploads || !m_d->nam) {
        m_d->btnUpscale->setEnabled(true);
        return;
    }
    while (m_d->upscaleRegionMaskUploadIndex < m_d->upscaleProcessedRegions.size()) {
        const int inputIdx = m_d->upscaleRegionMaskUploadIndex;
        m_d->upscaleRegionMaskUploadIndex++;
        if (inputIdx >= m_d->upscaleRegionalInputs.size())
            continue;

        const ComfyRegionProcess::ProcessedRegionEntry &region = m_d->upscaleProcessedRegions.at(inputIdx);
        if (region.maskGray.isNull()) {
            setStatusMessage(ComfyTr::tr("Region mask is empty."), true);
            m_d->upscaleAwaitingRegionMaskUploads = false;
            m_d->btnUpscale->setEnabled(true);
            return;
        }
        const QString regionLabel =
            region.isBackground ? ComfyTr::tr("background") : QString::number(inputIdx);
        const QImage maskPng =
            maskPngForComfyUpload(region.maskGray, m_d->upscaleStashedW2, m_d->upscaleStashedH2);

        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!maskPng.save(tmp->fileName())) {
            setStatusMessage(ComfyTr::tr("Could not save region mask."), true);
            m_d->upscaleAwaitingRegionMaskUploads = false;
            m_d->btnUpscale->setEnabled(true);
            return;
        }

        QString urlStr = m_d->editServerUrl->text().trimmed();
        QUrl uploadUrl(urlStr);
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == QLatin1Char('/'))
            uploadUrl.setPath(QStringLiteral("/upload/image"));
        else if (!up.endsWith(QLatin1Char('/')))
            uploadUrl.setPath(up + QStringLiteral("/upload/image"));
        else
            uploadUrl.setPath(up + QStringLiteral("upload/image"));

        tmp->open();
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"upscale_region_mask_%1.png\"")
                                    .arg(inputIdx)));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        m_d->labelStatus->setText(ComfyTr::tr("Uploading region mask %1…", regionLabel));
        setProgressBarKind(true);
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp, inputIdx]() {
            replyUp->deleteLater();
            setProgressBarKind(false);
            if (replyUp->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Region mask upload error: %1", replyUp->errorString()), true);
                m_d->upscaleAwaitingRegionMaskUploads = false;
                m_d->btnUpscale->setEnabled(true);
                return;
            }
            const QString name =
                QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty() || inputIdx >= m_d->upscaleRegionalInputs.size()) {
                setStatusMessage(ComfyTr::tr("Server did not return region mask name."), true);
                m_d->upscaleAwaitingRegionMaskUploads = false;
                m_d->btnUpscale->setEnabled(true);
                return;
            }
            m_d->upscaleRegionalInputs[inputIdx].maskImageName = name;
            uploadNextUpscaleRegionMask();
        });
        return;
    }
    m_d->upscaleAwaitingRegionMaskUploads = false;
    beginUpscaleConditioningUploadPipeline();
}

void ComfyUIRemoteDock::finalizeUpscaleWorkflowAndSubmit()
{
    if (!m_d->upscalePendingIsTiled) {
        m_d->btnUpscale->setEnabled(true);
        return;
    }

    ComfyWorkflowEngine::UpscaleTiledParams tp = m_d->upscaleStashedTiledParams;
    tp.regionalPrompts = m_d->upscaleRegionalInputs;

    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : m_d->upscaleControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= m_d->upscaleControlUploadedNames.size())
            break;
        const QString imageName = m_d->upscaleControlUploadedNames.at(uploadIdx++);
        if (ComfyResources::ControlMode::isIpAdapter(ce.mode)) {
            ComfyWorkflowEngine::IpAdapterLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            tp.ipAdapterLayers.append(in);
        } else {
            ComfyWorkflowEngine::ControlNetLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            tp.controlLayers.append(in);
        }
    }

    QJsonObject workflow = ComfyWorkflowEngine::buildUpscaleTiled(tp);
    if (workflow.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Tiled upscale workflow error."), true);
        m_d->btnUpscale->setEnabled(true);
        m_d->progressBar->setValue(0);
        m_d->upscalePendingIsTiled = false;
        m_d->upscaleRegionalInputs.clear();
        m_d->upscaleProcessedRegions.clear();
        return;
    }
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    const bool wantRefine = m_d->upscalePendingWantRefine;
    m_d->upscalePendingIsTiled = false;
    m_d->upscaleRegionalInputs.clear();
    m_d->upscaleProcessedRegions.clear();
    m_d->upscaleControlLayersActive.clear();
    m_d->upscaleControlUploadedNames.clear();
    submitUpscaleWorkflow(workflow, wantRefine, true);
}

void ComfyUIRemoteDock::submitUpscaleWorkflow(const QJsonObject &workflow, bool wantRefine, bool useTiledRefine)
{
    if (m_d->clientId.isEmpty())
        m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject payload;
    payload.insert(QStringLiteral("prompt"), workflow);
    payload.insert(QStringLiteral("client_id"), m_d->clientId);
    payload.insert(QStringLiteral("prompt_id"), expectedPromptId);
    QUrl promptUrl(m_d->editServerUrl->text().trimmed());
    QString p = promptUrl.path();
    if (p.isEmpty() || p == QLatin1Char('/'))
        promptUrl.setPath(QStringLiteral("/prompt"));
    else if (!p.endsWith(QLatin1Char('/')))
        promptUrl.setPath(p + QStringLiteral("/prompt"));
    else
        promptUrl.setPath(p + QStringLiteral("prompt"));
    QNetworkRequest reqPrompt(promptUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(reqPrompt);
    reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt, expectedPromptId, wantRefine, useTiledRefine]() {
        replyPrompt->deleteLater();
        const QByteArray body = replyPrompt->readAll();
        if (replyPrompt->error() != QNetworkReply::NoError) {
            const QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (obj.contains(QStringLiteral("error")))
                setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj.value(QStringLiteral("error")).toString()),
                                 true);
            else
                setStatusMessage(ComfyTr::tr("Submit error: %1", replyPrompt->errorString()), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.contains(QStringLiteral("error"))) {
            setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj.value(QStringLiteral("error")).toString()), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const QString promptId = obj.value(QStringLiteral("prompt_id")).toString();
        if (promptId.isEmpty()) {
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        if (promptId != expectedPromptId) {
            setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        m_d->upscalePromptId = promptId;
        m_d->upscalePollCount = 0;
        m_d->upscaleLastSubmitUsedRefine = wantRefine;
        m_d->labelStatus->setText(useTiledRefine ? ComfyTr::tr("Tiled upscale…")
                                                 : (wantRefine ? ComfyTr::tr("Upscaling and refining…")
                                                               : ComfyTr::tr("Upscaling…")));
        m_d->upscalePollTimer->start(1000);
    });
}

void ComfyUIRemoteDock::slotUpscalePoll()
{
    if (m_d->upscalePromptId.isEmpty()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->upscalePromptId.clear();
        m_d->btnUpscale->setEnabled(true);
        m_d->progressBar->setValue(0);
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->upscalePromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->upscalePromptId);
    else baseUrl.setPath(path + "history/" + m_d->upscalePromptId);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("History error: %1", reply->errorString()), true);
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->upscalePromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->upscalePollCount++;
            if (m_d->upscalePollCount >= Private::upscaleMaxPollCount) {
                setStatusMessage(ComfyTr::tr("Upscale timed out."), true);
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->upscalePollTimer->start(1000);
            return;
        }
        QString filename, subfolder;
        for (const QString &nodeId : outputs.keys()) {
            QJsonArray images = outputs.value(nodeId).toObject().value("images").toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images.at(0).toObject();
                filename = img.value("filename").toString();
                subfolder = img.value("subfolder").toString();
                break;
            }
        }
        if (filename.isEmpty()) {
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        vp += "view";
        viewUrl.setPath(vp);
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqView(viewUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqView);
        QNetworkReply *replyView = m_d->nam->get(reqView);
        connect(replyView, &QNetworkReply::finished, this, [this, replyView]() {
            replyView->deleteLater();
            if (replyView->error() != QNetworkReply::NoError) {
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (!tmp.open()) {
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            tmp.write(replyView->readAll());
            tmp.close();
            if (m_d->viewManager->imageManager()) {
                m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                if (m_d->canvas) m_d->canvas->updateCanvas();
            }
            m_d->labelStatus->setText(m_d->upscaleLastSubmitUsedRefine
                ? ComfyTr::tr("Upscale and refine done. Result added as new layer.")
                : ComfyTr::tr("Upscale done. Result added as new layer."));
            m_d->progressBar->setValue(100);
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
        });
    });
}
