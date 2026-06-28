/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUpscaleRunner.h"
#include "ComfyUpscaleRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyLocalization.h"
#include "ComfySwitchWidget.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionLink.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#include <kis_image.h>
#include <kis_image_manager.h>


using namespace ComfyUpscaleRunnerInternal;

namespace ComfyUpscaleRunner {

void onUpscale(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        dock->setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    auto colorCheck = ComfyUIUtils::checkColorMode(dock->m_d->viewManager->image());
    if (!colorCheck.first) {
        dock->setStatusMessage(colorCheck.second, true);
        return;
    }
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        dock->setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    KisImageSP image = dock->m_d->viewManager->image();
    const auto docCapture = ComfyUIUtils::getDocumentImage(image, QRect(), {});
    if (!docCapture) {
        dock->setStatusMessage(docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas.")
                                                           : docCapture.errorMessage,
                         true);
        return;
    }
    QImage canvasImg = docCapture.image;
    int w = canvasImg.width();
    int h = canvasImg.height();
    int w2 = qRound(w * dock->m_d->upscaleRt.upscaleFactor);
    int h2 = qRound(h * dock->m_d->upscaleRt.upscaleFactor);
    QTemporaryFile *tmpImage = new QTemporaryFile(dock);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!canvasImg.save(tmpImage->fileName())) {
        dock->setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }
    dock->m_d->upscale.btnUpscale->setEnabled(false);
    dock->m_d->progressBar->setValue(0);
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
    QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading for upscale…"));
    dock->setProgressBarKind(true);  // §13.18
    QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply, w, h, w2, h2]() {
        reply->deleteLater();
        dock->setProgressBarKind(false);  // §13.18: upload finished
        if (reply->error() != QNetworkReply::NoError) {
            dock->setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            dock->m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        dock->m_d->upscaleRt.upscaleUploadedImageName = obj.value("name").toString();
        if (dock->m_d->upscaleRt.upscaleUploadedImageName.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            dock->m_d->progressBar->setValue(0);
            return;
        }
        continueAfterCanvasUpload(dock, w, h, w2, h2);
    });

}

void continueAfterCanvasUpload(ComfyUIRemoteDock *dock, int canvasW, int canvasH, int w2, int h2)
{

        const bool wantRefine = dock->m_d->upscale.checkUpscaleRefine && dock->m_d->upscale.checkUpscaleRefine->isChecked();
        const QString scaleMethod = ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(
            ComfyUIUtils::normalizeDiffusionScaleMode(
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("diffusion_scale_mode")).toString()));
        const int overlapPx = dock->m_d->upscaleRt.tileOverlapMode == 1 ? dock->m_d->upscaleRt.tileOverlap : -1;

        QJsonObject workflow;
        bool useTiledRefine = false;
        if (wantRefine) {
            if (!dock->m_d->upscale.comboUpscaleRefinementModel || dock->m_d->upscale.comboUpscaleRefinementModel->currentIndex() <= 0) {
                dock->setStatusMessage(
                    ComfyTr::tr("Select a refinement model (a style preset other than \"None\"), or disable \"Refine upscaled image\"."),
                    true);
                dock->m_d->upscale.btnUpscale->setEnabled(true);
                dock->m_d->progressBar->setValue(0);
                return;
            }
            QString ckpt = dock->checkpointNameForUpscaleRefinementPreset();
            if (ckpt.isEmpty())
                ckpt = dock->checkpointForGenerate();
            QString styleArch;
            if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
                const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    styleArch = st->architecture;
            }
            int baseSteps = 20;
            double baseCfg = 8.0;
            QString sam = QStringLiteral("euler");
            QString sch = QStringLiteral("normal");
            dock->readUpscaleRefinementSampling(&baseSteps, &baseCfg, &sam, &sch);
            const ComfyUIUtils::ResolvedSamplerInputs rsi = ComfyUIUtils::resolveSamplerForLive(
                dock->currentJsonStyleEntry(),
                ComfyUIUtils::loadSettingsJson(), sam, baseSteps, baseCfg);
            const int strengthPct = dock->m_d->upscale.sliderUpscaleRefineStrength ? dock->m_d->upscale.sliderUpscaleRefineStrength->value() : 30;
            const int guidancePct = dock->m_d->upscale.sliderUpscaleRefineGuidance ? dock->m_d->upscale.sliderUpscaleRefineGuidance->value() : 50;
            const double denoise = qBound(0.05, strengthPct / 100.0, 1.0);
            const double kcfg = qBound(1.0, 1.0 + (guidancePct / 100.0) * 14.0, 20.0);
            int stylePreferredResolution = 0;
            const ComfyResources::Arch archForTiles =
                ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
            if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
                const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    stylePreferredResolution = st->preferredResolution;
            }
            const ComfyUIUtils::UpscaleTiledLayoutSpec tileLayout = ComfyUIUtils::computeUpscaleTiledLayoutSpec(
                w2, h2, archForTiles, stylePreferredResolution, denoise, overlapPx);
            const int tileEstimate = tileLayout.totalTiles;
            qint64 seed = dock->m_d->generate.checkFixedSeed && dock->m_d->generate.checkFixedSeed->isChecked()
                ? static_cast<qint64>(dock->m_d->generate.spinSeed ? dock->m_d->generate.spinSeed->value() : 0)
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!dock->m_d->generate.checkFixedSeed || !dock->m_d->generate.checkFixedSeed->isChecked()) {
                if (dock->m_d->generate.spinSeed)
                    dock->m_d->generate.spinSeed->setValue(static_cast<int>(qBound<qint64>(0, seed, 2147483647)));
            }
            const bool usePrompt = dock->m_d->upscale.checkUpscaleUsePrompt && dock->m_d->upscale.checkUpscaleUsePrompt->isChecked() && dock->m_d->generate.editPrompt;
            QString pos;
            QString neg;
            if (usePrompt) {
                pos = ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed();
                pos = ComfyUIUtils::evalWildcards(pos, static_cast<quint32>(seed));
                ComfyUIUtils::extractLayerPlaceholders(pos);
                pos = ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(pos, dock->currentStyleLoras());
                neg = ComfyUIUtils::evalWildcards(
                    ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative ? dock->m_d->generate.editNegative->toPlainText() : QString()).trimmed(),
                    static_cast<quint32>(seed));
            }
            if (pos.isEmpty())
                pos = QStringLiteral("high quality, detailed");

            if (tileEstimate > 1) {
                useTiledRefine = true;
                ComfyWorkflowEngine::UpscaleTiledParams tp;
                tp.imageName = dock->m_d->upscaleRt.upscaleUploadedImageName;
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

                dock->m_d->upscaleRt.upscaleStashedTiledParams = tp;
                dock->m_d->upscaleRt.upscaleStashedW2 = w2;
                dock->m_d->upscaleRt.upscaleStashedH2 = h2;
                dock->m_d->upscaleRt.upscaleStashedCanvasW = canvasW;
                dock->m_d->upscaleRt.upscaleStashedArch = archForTiles;
                dock->m_d->upscaleRt.upscalePendingIsTiled = true;
                dock->m_d->upscaleRt.upscalePendingWantRefine = wantRefine;

                dock->m_d->upscaleRt.upscaleRegionalInputs.clear();
                dock->m_d->upscaleRt.upscaleProcessedRegions.clear();
                if (usePrompt && ComfyResources::supportsRegions(archForTiles) && dock->m_d->viewManager) {
                    const ComfyRegionProcess::ProcessRegionsResult processed = ComfyRegionProcess::processRegions(
                        regionsForUpscale(dock->m_d.data()), dock->m_d->viewManager->image(), dock->m_d->viewManager, pos);
                    if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion) {
                        dock->m_d->upscaleRt.upscaleProcessedRegions = processed.regions;
                        dock->m_d->upscaleRt.upscaleRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
                            dock->m_d->upscaleRt.upscaleProcessedRegions, tp.promptTranslationLanguage);
                        const double factor = tp.upscaleFactor;
                        for (int ri = 0; ri < dock->m_d->upscaleRt.upscaleRegionalInputs.size(); ++ri) {
                            if (ri >= dock->m_d->upscaleRt.upscaleProcessedRegions.size()
                                || dock->m_d->upscaleRt.upscaleProcessedRegions.at(ri).isBackground)
                                continue;
                            QRect b = ComfyRegionProcess::maskNonZeroBounds(
                                dock->m_d->upscaleRt.upscaleProcessedRegions.at(ri).maskGray);
                            if (b.isEmpty())
                                continue;
                            if (qAbs(factor - 1.0) > 0.001) {
                                b = QRect(qRound(b.x() * factor),
                                          qRound(b.y() * factor),
                                          qMax(1, qRound(b.width() * factor)),
                                          qMax(1, qRound(b.height() * factor)));
                            }
                            dock->m_d->upscaleRt.upscaleRegionalInputs[ri].maskBoundsUpscaled = b;
                        }
                        dock->m_d->upscaleRt.upscaleAwaitingRegionMaskUploads = true;
                        dock->m_d->upscaleRt.upscaleRegionMaskUploadIndex = 0;
                        uploadNextRegionMask(dock);
                        return;
                    }
                }
                beginConditioningUploadPipeline(dock);
                return;
            } else {
                ComfyWorkflowEngine::UpscaleRefineParams rp;
                rp.imageName = dock->m_d->upscaleRt.upscaleUploadedImageName;
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
                dock->setStatusMessage(useTiledRefine ? ComfyTr::tr("Tiled upscale workflow error.") : ComfyTr::tr("Upscale refine workflow error."),
                                 true);
                dock->m_d->upscale.btnUpscale->setEnabled(true);
                dock->m_d->progressBar->setValue(0);
                return;
            }
        } else {
            ComfyWorkflowEngine::UpscaleSimpleParams sp;
            sp.imageName = dock->m_d->upscaleRt.upscaleUploadedImageName;
            sp.targetWidth = w2;
            sp.targetHeight = h2;
            sp.upscaleMethod = scaleMethod;
            workflow = ComfyWorkflowEngine::buildUpscaleSimple(sp);
            if (workflow.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Upscale workflow error."), true);
                dock->m_d->upscale.btnUpscale->setEnabled(true);
                dock->m_d->progressBar->setValue(0);
                return;
            }
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (wantRefine && !useTiledRefine) {
            ComfyUIUtils::applyUpscaleRefineVaedecodeTiling(
                workflow,
                QStringLiteral("8"),
                dock->m_d->upscaleRt.tileOverlapMode,
                dock->m_d->upscaleRt.tileOverlap,
                ComfyUIUtils::loadSettingsJson());
        }
        submitWorkflow(dock, workflow, wantRefine, useTiledRefine);

}


} // namespace ComfyUpscaleRunner
