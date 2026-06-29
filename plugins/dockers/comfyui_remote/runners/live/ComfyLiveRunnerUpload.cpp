/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLiveRunner.h"
#include "ComfyLiveRunnerInternal.h"
#include "ComfyInpaintRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRandomGenerator>
#include <QRect>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#include <kis_image.h>
#include <kis_image_manager.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

using namespace ComfyLiveRunnerInternal;
using namespace ComfyInpaintRunnerInternal;

namespace ComfyLiveRunner {

void beginUploadPipeline(ComfyUIRemoteDock *dock)
{
    if (livePipelineBusy(dock->m_d.data()))
        return;

    const ComfyStyleEntry *styleEntry = dock->currentJsonStyleEntry();
    const QString livePreset =
        ComfyUIUtils::liveSamplerPresetName(styleEntry, ComfyUIUtils::loadSettingsJson());
    const QString ckpt = dock->checkpointForGenerate();
    const ComfyResources::Arch arch =
        ComfyWorkflowEngine::resolveArch(ckpt, styleEntry ? styleEntry->architecture : QString());
    const ComfyUIUtils::SamplerPresetLoraResult samplerLora =
        ComfyUIUtils::resolveSamplerPresetLora(
            livePreset,
            arch,
            ComfyUIUtils::mergedServerLoraFilenames(dock->m_d->comfyServerLoraFilenames));
    if (!samplerLora.ok) {
        if (!samplerLora.errorMessage.isEmpty())
            dock->setStatusMessage(samplerLora.errorMessage, true);
        return;
    }

    dock->m_d->liveRt.livePipelineBusy = true;
    dock->startLiveSpinner();
    dock->setLiveProgress(0);
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE beginUploadPipeline strength=")
        << (dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : -1)
        << QStringLiteral("seed=") << (dock->m_d->generate.spinSeed ? dock->m_d->generate.spinSeed->value() : -1)
        << QStringLiteral("editMode=")
        << (dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked());

    const QStringList loraPaths =
        dock->m_d->isConnected && dock->m_d->nam
            ? ComfyUploadPipeline::collectMissingLoraUploadPaths(
                  ComfyUIUtils::mergedServerLoraFilenames(dock->m_d->comfyServerLoraFilenames),
                  samplerLora.loraFilename.isEmpty() ? QStringList() : QStringList { samplerLora.loraFilename })
            : QStringList();

    if (loraPaths.isEmpty()) {
        continueAfterLoraUploads(dock);
        return;
    }

    dock->m_d->liveRt.liveAwaitingLoraUploads = true;
    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.setProgressKind = [dock](bool isUpload) { dock->setProgressBarKind(isUpload); };
    handlers.setStatusText = [dock](const QString &text) {
        if (dock->m_d->labelStatus)
            dock->m_d->labelStatus->setText(text);
    };
    handlers.setStatusMessage = [dock](const QString &msg, bool isError) { dock->setStatusMessage(msg, isError); };
    handlers.onLoraUploaded = [dock](const QString &) {
        ComfyFileLibrary::instance().init();
        ComfyFileLibrary::instance().updateRemoteLoras(dock->m_d->comfyServerLoraFilenames);
    };
    handlers.shouldContinue = [dock]() {
        return dock->m_d->live.checkLiveMode && dock->m_d->live.checkLiveMode->isChecked();
    };
    handlers.onCancelled = [dock]() {
        dock->m_d->liveRt.liveAwaitingLoraUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
        dock->stopLiveSpinner();
    };
    handlers.onComplete = [dock](const ComfyUploadPipeline::Result &) {
        dock->m_d->liveRt.liveAwaitingLoraUploads = false;
        continueAfterLoraUploads(dock);
    };
    handlers.onAbort = [dock]() {
        dock->m_d->liveRt.liveAwaitingLoraUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
        dock->stopLiveSpinner();
    };
    run->start(dock->m_d->editServerUrl->text().trimmed(), loraPaths, {}, std::move(handlers), &dock->m_d->comfyServerLoraFilenames);

}

void continueAfterLoraUploads(ComfyUIRemoteDock *dock)
{

    dock->m_d->liveRt.liveAwaitingLoraUploads = false;
    if (!dock->m_d->live.checkLiveMode->isChecked()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        dock->stopLiveSpinner();
        return;
    }

    dock->commitPromptEditorsFromUi();
    dock->m_d->liveRt.liveRegionalInputs.clear();
    dock->m_d->liveRt.livePrepared = ComfyPrepareLiveWorkflow::prepare(dock->prepareLiveWorkflowInput());
    if (!dock->m_d->liveRt.livePrepared.ok) {
        if (!dock->m_d->liveRt.livePrepared.errorMessage.isEmpty())
            dock->setStatusMessage(dock->m_d->liveRt.livePrepared.errorMessage, true);
        dock->m_d->liveRt.livePipelineBusy = false;
        dock->stopLiveSpinner();
        return;
    }

    const quint32 liveSeed =
        static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    dock->m_d->liveRt.livePreparedSeed = liveSeed;
    buildPreparedPrompts(dock, liveSeed);

    dock->m_d->liveRt.liveControlLayersActive = dock->m_d->rootControlLayers;
    dock->m_d->liveRt.liveControlLayersActive.append(dock->m_d->liveRt.livePromptReferenceLayers);

    KisImageSP image = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
    ComfyUploadPipeline::ControlUploadOptions controlOpts;
    controlOpts.filenamePrefix = QStringLiteral("live_control_");
    const QList<ComfyUploadPipeline::ImageItem> controlItems =
        ComfyControlLayer::anyNeedsGenerateUpload(dock->m_d->liveRt.liveControlLayersActive)
            ? ComfyUploadPipeline::buildControlUploadItems(image, dock->m_d->liveRt.liveControlLayersActive, controlOpts)
            : QList<ComfyUploadPipeline::ImageItem>();

    if (controlItems.isEmpty()) {
        uploadCanvasAndPrompt(dock);
        return;
    }

    dock->m_d->liveRt.liveAwaitingControlUploads = true;
    dock->m_d->liveRt.liveControlUploadedNames.clear();
    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.shouldContinue = [dock]() {
        return dock->m_d->live.checkLiveMode && dock->m_d->live.checkLiveMode->isChecked();
    };
    handlers.onCancelled = [dock]() {
        dock->m_d->liveRt.liveAwaitingControlUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
    };
    handlers.onComplete = [dock](const ComfyUploadPipeline::Result &result) {
        dock->m_d->liveRt.liveAwaitingControlUploads = false;
        dock->m_d->liveRt.liveControlUploadedNames = result.uploadedImageNames;
        uploadCanvasAndPrompt(dock);
    };
    handlers.onAbort = [dock]() {
        dock->m_d->liveRt.liveAwaitingControlUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
    };
    run->start(dock->m_d->editServerUrl->text().trimmed(), {}, controlItems, std::move(handlers));

}

void buildPreparedPrompts(ComfyUIRemoteDock *dock, const quint32 liveSeed)
{

    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
    QString livePos = prep.useProcessedPositive && !prep.effectivePositivePrompt.isEmpty()
        ? prep.effectivePositivePrompt
        : ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed();
    livePos = ComfyUIUtils::evalWildcards(livePos, liveSeed);
    const QString layerTemplate = ComfyUIUtils::layerPlaceholderReplacementForArch(prep.arch);
    const QStringList refLayerNames = ComfyUIUtils::extractLayerPlaceholders(livePos, layerTemplate);
    dock->m_d->liveRt.livePromptReferenceLayers = ComfyControlLayer::referenceLayersFromPromptTags(refLayerNames);
    if (prep.hasMask && prep.strength0to1 >= 1.0) {
        const QString archKey = ComfyResources::archToKey(prep.arch);
        livePos = ComfyUIUtils::prependInpaintPromptInstructions(livePos, QStringLiteral("fill"), archKey);
    }
    livePos = ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(livePos, dock->currentStyleLorasForLive());
    dock->m_d->liveRt.livePreparedPositive = livePos;

    const ComfyUIUtils::ResolvedSamplingInputs sampling = ComfyUIUtils::resolveSamplingFromStyle(
        dock->currentJsonStyleEntry(),
        ComfyUIUtils::loadSettingsJson(),
        dock->m_d->generate.comboSampler ? dock->m_d->generate.comboSampler->currentText() : QString(),
        dock->m_d->generate.spinSteps ? dock->m_d->generate.spinSteps->value() : 20,
        dock->m_d->generate.spinCfg ? dock->m_d->generate.spinCfg->value() : 8.0,
        prep.strength0to1,
        true);
  QString liveNeg =
        ComfyUIUtils::evalWildcards(ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative->toPlainText()).trimmed(),
                                   liveSeed);
    if (sampling.cfg <= 1.0 + 1e-6)
        liveNeg.clear();
    dock->m_d->liveRt.livePreparedNegative = liveNeg;

}

void uploadCanvasAndPrompt(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->live.checkLiveMode->isChecked() || !dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    dock->m_d->liveRt.liveUploadedImageName.clear();
    dock->m_d->liveRt.liveUploadedMaskName.clear();
    if (!dock->m_d->liveRt.livePrepared.ok) {
        dock->m_d->liveRt.livePrepared = ComfyPrepareLiveWorkflow::prepare(dock->prepareLiveWorkflowInput());
        if (!dock->m_d->liveRt.livePrepared.ok) {
            if (!dock->m_d->liveRt.livePrepared.errorMessage.isEmpty())
                dock->setStatusMessage(dock->m_d->liveRt.livePrepared.errorMessage, true);
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        const quint32 liveSeed =
            static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        dock->m_d->liveRt.livePreparedSeed = liveSeed;
        buildPreparedPrompts(dock, liveSeed);
    }

    const bool needsImage =
        dock->m_d->liveRt.livePrepared.hasMask
        || dock->m_d->liveRt.livePrepared.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::Refine;
    if (!needsImage) {
        continueAfterCanvasUpload(dock);
        return;
    }

    const QImage canvasImg = dock->m_d->liveRt.livePrepared.contextImage;
    if (canvasImg.isNull()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    QTemporaryFile *tmp = new QTemporaryFile(dock);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    tmp->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"krita_live.png\"")));
    part.setBodyDevice(tmp);
    tmp->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(comfyImageUploadUrl(urlStr));
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply]() {
        reply->deleteLater();
        if (!dock->m_d->live.checkLiveMode->isChecked() || reply->error() != QNetworkReply::NoError) {
            if (dock->m_d->live.checkLiveMode->isChecked())
                dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        dock->m_d->liveRt.liveUploadedImageName =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (dock->m_d->liveRt.liveUploadedImageName.isEmpty()) {
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        continueAfterCanvasUpload(dock);
    });

}

void continueAfterCanvasUpload(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->live.checkLiveMode->isChecked()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    if (dock->m_d->liveRt.livePrepared.hasMask) {
        const QImage maskPng =
            ComfyUIUtils::maskPngForComfyUpload(dock->m_d->liveRt.livePrepared.compositingMaskCropped);
        if (maskPng.isNull()) {
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        QTemporaryFile *tmpMask = new QTemporaryFile(dock);
        tmpMask->setFileTemplate(tmpMask->fileTemplate() + QStringLiteral(".png"));
        tmpMask->open();
        tmpMask->close();
        if (!maskPng.save(tmpMask->fileName())) {
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }

        QString urlStr = dock->m_d->editServerUrl->text().trimmed();
        tmpMask->open();
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"krita_live_mask.png\"")));
        part.setBodyDevice(tmpMask);
        tmpMask->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest req(comfyImageUploadUrl(urlStr));
        ComfyUIUtils::setComfyUIRequestHeaders(req);
        QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
        multiPart->setParent(reply);
        QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply]() {
            reply->deleteLater();
            if (!dock->m_d->live.checkLiveMode->isChecked() || reply->error() != QNetworkReply::NoError) {
                if (dock->m_d->live.checkLiveMode->isChecked())
                    dock->m_d->liveRt.livePipelineBusy = false;
                return;
            }
            dock->m_d->liveRt.liveUploadedMaskName =
                QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
            if (dock->m_d->liveRt.liveUploadedMaskName.isEmpty()) {
                dock->m_d->liveRt.livePipelineBusy = false;
                return;
            }
            continueAfterMaskUpload(dock);
        });
        return;
    }

    continueAfterMaskUpload(dock);

}

void continueAfterMaskUpload(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->live.checkLiveMode->isChecked()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
    if (prep.processedRegions.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion
        && ComfyResources::supportsRegions(prep.arch)) {
        dock->m_d->liveRt.liveRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
            prep.processedRegions.regions, ComfyUIUtils::activePromptTranslationLanguage());
        dock->m_d->liveRt.liveAwaitingRegionMaskUploads = true;
        dock->m_d->liveRt.liveRegionMaskUploadIndex = 0;
        uploadNextRegionMask(dock);
        return;
    }

    continueAfterRegionMaskUpload(dock);

}

void uploadNextRegionMask(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->liveRt.liveAwaitingRegionMaskUploads || !dock->m_d->viewManager || !dock->m_d->live.checkLiveMode->isChecked()) {
        dock->m_d->liveRt.liveAwaitingRegionMaskUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    QList<ComfyUploadPipeline::ImageItem> maskItems;
    for (int inputIdx = 0; inputIdx < dock->m_d->liveRt.livePrepared.processedRegions.regions.size(); ++inputIdx) {
        if (inputIdx >= dock->m_d->liveRt.liveRegionalInputs.size())
            continue;

        const ComfyRegionProcess::ProcessedRegionEntry &region =
            dock->m_d->liveRt.livePrepared.processedRegions.regions.at(inputIdx);
        if (region.maskGray.isNull()) {
            dock->setStatusMessage(ComfyTr::tr("Region mask is empty."), true);
            dock->m_d->liveRt.liveAwaitingRegionMaskUploads = false;
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }

        ComfyUploadPipeline::ImageItem item;
        item.filenameHint = QStringLiteral("live_region_mask_%1.png").arg(inputIdx);
        const QImage maskGray = region.maskGray;
        item.prepareImage = [maskGray]() { return ComfyUIUtils::maskPngForComfyUpload(maskGray); };
        item.onUploaded = [dock, inputIdx](const QString &name) {
            if (inputIdx < dock->m_d->liveRt.liveRegionalInputs.size())
                dock->m_d->liveRt.liveRegionalInputs[inputIdx].maskImageName = name;
        };
        maskItems.append(item);
    }

    auto finishRegionMasks = [dock]() {
        dock->m_d->liveRt.liveAwaitingRegionMaskUploads = false;
        continueAfterRegionMaskUpload(dock);
    };

    if (maskItems.isEmpty()) {
        finishRegionMasks();
        return;
    }

    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.shouldContinue = [dock]() {
        return dock->m_d->live.checkLiveMode && dock->m_d->live.checkLiveMode->isChecked();
    };
    handlers.onCancelled = [dock]() {
        dock->m_d->liveRt.liveAwaitingRegionMaskUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
    };
    handlers.onComplete = [finishRegionMasks](const ComfyUploadPipeline::Result &) { finishRegionMasks(); };
    handlers.onAbort = [dock]() {
        dock->m_d->liveRt.liveAwaitingRegionMaskUploads = false;
        dock->m_d->liveRt.livePipelineBusy = false;
    };
    run->start(dock->m_d->editServerUrl->text().trimmed(), {}, maskItems, std::move(handlers));

}

void continueAfterRegionMaskUpload(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->live.checkLiveMode->isChecked()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
    const QString ckptName = dock->checkpointForGenerate();
    QString styleArch;
    if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            styleArch = st->architecture;
    }

    const quint32 liveSeed = dock->m_d->liveRt.livePreparedSeed != 0
        ? dock->m_d->liveRt.livePreparedSeed
        : static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    const QString livePos = dock->m_d->liveRt.livePreparedPositive.isEmpty()
        ? ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(
              ComfyUIUtils::evalWildcards(
                  ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed(), liveSeed),
              dock->currentStyleLorasForLive())
        : dock->m_d->liveRt.livePreparedPositive;
    const ComfyUIUtils::ResolvedSamplingInputs sampling = ComfyUIUtils::resolveSamplingFromStyle(
        dock->currentJsonStyleEntry(),
        ComfyUIUtils::loadSettingsJson(),
        dock->m_d->generate.comboSampler ? dock->m_d->generate.comboSampler->currentText() : QString(),
        dock->m_d->generate.spinSteps ? dock->m_d->generate.spinSteps->value() : 20,
        dock->m_d->generate.spinCfg ? dock->m_d->generate.spinCfg->value() : 8.0,
        prep.strength0to1,
        true);
    const QString liveNeg = dock->m_d->liveRt.livePreparedNegative.isEmpty()
        ? ComfyUIUtils::evalWildcards(
              ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative->toPlainText()).trimmed(), liveSeed)
        : dock->m_d->liveRt.livePreparedNegative;

    const ComfyResources::Arch workflowArch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
    {
        const bool refineRegion =
            prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
        QImage uploadMask = prep.compositingMaskCropped;
        if (uploadMask.format() != QImage::Format_Grayscale8)
            uploadMask = uploadMask.convertToFormat(QImage::Format_Grayscale8);
        InpaintDiagSnapshot diag;
        diag.event = QStringLiteral("upload");
        diag.pluginVersion = ComfyUIUtils::pluginVersion();
        diag.workflowKind = refineRegion ? QStringLiteral("refine_region") : QStringLiteral("live");
        diag.archKey = ComfyResources::archToKey(workflowArch);
        diag.refineRegion = refineRegion;
        diag.strength0to1 = prep.strength0to1;
        diag.denoise = sampling.denoiseStrength;
        diag.useInpaintModel = prep.inpaintParams.useInpaintModel;
        diag.selectionOriginal = prep.selectionOriginalBounds;
        diag.maskPaddedBounds = prep.maskPaddedBounds;
        diag.contextBounds = prep.contextBounds;
        diag.targetBoundsRelative = prep.targetBoundsRelative;
        diag.nativeContextSize = prep.nativeContextSize;
        diag.uploadContextSize = prep.contextImage.size();
        diag.diffusionExtent = prep.diffusionExtent;
        diag.grow = prep.preprocess.grow;
        diag.feather = prep.preprocess.feather;
        diag.blend = prep.preprocess.blend;
        diag.imageUploadName = dock->m_d->liveRt.liveUploadedImageName;
        diag.maskUploadName = dock->m_d->liveRt.liveUploadedMaskName;
        diag.contextPixels = describeImagePixels(prep.contextImage, QStringLiteral("uploadContext"));
        diag.maskPixels = describeImagePixels(uploadMask, QStringLiteral("uploadMask"));
        logLiveDiag(diag);
    }

    QJsonObject workflow;
    if (prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::Generate) {
        KisImageSP image = dock->m_d->viewManager->image();
        ComfyWorkflowEngine::TextToImageParams gen;
        gen.checkpoint = ckptName;
        gen.arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
        gen.seed = static_cast<qint64>(liveSeed);
        gen.styleLoras = dock->currentStyleLorasForLive();
        gen.positivePrompt = livePos;
        gen.negativePrompt = liveNeg;
        gen.denoise = 1.0;
        gen.sampler = sampling.sampler;
        gen.scheduler = sampling.scheduler;
        gen.steps = sampling.totalSteps;
        gen.cfg = sampling.cfg;
        gen.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
        gen.batchSize = 1;
        gen.layerCount = dock->m_d->generate.spinLayerCount ? dock->m_d->generate.spinLayerCount->value() : 1;
        if (image) {
            const QRect doc = image->bounds();
            gen.width = qBound(64, doc.width(), 8192);
            gen.height = qBound(64, doc.height(), 8192);
            ComfyUIUtils::clampExtentToMaxMegapixels(&gen.width, &gen.height);
        } else {
            gen.width = dock->m_d->generate.spinWidth ? dock->m_d->generate.spinWidth->value() : 512;
            gen.height = dock->m_d->generate.spinHeight ? dock->m_d->generate.spinHeight->value() : 512;
        }
        workflow = ComfyWorkflowEngine::buildTextToImage(gen);
    } else if (prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion) {
        ComfyWorkflowEngine::RefineRegionParams rrp;
        rrp.refine.checkpoint = ckptName;
        rrp.refine.imageName = dock->m_d->liveRt.liveUploadedImageName;
        rrp.maskImageName = dock->m_d->liveRt.liveUploadedMaskName;
        rrp.refine.arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
        rrp.refine.seed = static_cast<qint64>(liveSeed);
        rrp.refine.styleLoras = dock->currentStyleLorasForLive();
        rrp.refine.positivePrompt = livePos;
        rrp.refine.negativePrompt = liveNeg;
        rrp.refine.denoise = sampling.denoiseStrength;
        rrp.refine.sampler = sampling.sampler;
        rrp.refine.scheduler = sampling.scheduler;
        rrp.refine.steps = sampling.totalSteps;
        rrp.refine.cfg = sampling.cfg;
        rrp.refine.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
        rrp.growMaskBy = prep.preprocess.grow;
        rrp.featherMaskBy = prep.preprocess.feather;
        rrp.blendMaskBy = prep.preprocess.blend;
        rrp.targetBoundsRelative = prep.targetBoundsRelative;
        rrp.nativeTargetBoundsRelative = prep.nativeTargetBoundsRelative.isValid()
                                               ? prep.nativeTargetBoundsRelative
                                               : prep.targetBoundsRelative;
        rrp.contextExtentWidth = qMax(64, prep.nativeContextSize.width());
        rrp.contextExtentHeight = qMax(64, prep.nativeContextSize.height());
        rrp.extentWidth = qMax(64, prep.diffusionExtent.width());
        rrp.extentHeight = qMax(64, prep.diffusionExtent.height());
        rrp.colorMatch = ComfyUIUtils::settingsColorMatchEnabled();
        rrp.useInpaintModel = prep.inpaintParams.useInpaintModel;
        rrp.nsfwFilterSensitivity = ComfyUIUtils::settingsNsfwFilterSensitivity();
        const ComfyUIUtils::ResolvedInpaintServerModels inpaintModels =
            ComfyUIUtils::resolveInpaintServerModels(dock->m_d->lastObjectInfoRoot,
                                                     rrp.refine.arch,
                                                     prep.inpaintParams.useInpaintModel);
        rrp.controlNetInpaintFile = inpaintModels.controlNetInpaintFile;
        rrp.fooocusInpaintHead = inpaintModels.fooocusInpaintHead;
        rrp.fooocusInpaintPatch = inpaintModels.fooocusInpaintPatch;
        const ComfyStyleEntry *styleEntry = dock->currentJsonStyleEntry();
        // Match inpaint refine_region: quality sampler preset, not live preset.
        ComfyUIUtils::applyStrengthResolvedSamplingToRefine(
            &rrp.refine,
            styleEntry,
            ComfyUIUtils::loadSettingsJson(),
            rrp.refine.sampler,
            rrp.refine.steps,
            rrp.refine.cfg,
            prep.strength0to1);
        workflow = ComfyWorkflowEngine::buildRefineRegion(rrp);
    } else {
        ComfyWorkflowEngine::LiveParams lp;
        lp.checkpoint = ckptName;
        lp.imageName = dock->m_d->liveRt.liveUploadedImageName;
        lp.arch = workflowArch;
        lp.seed = static_cast<qint64>(liveSeed);
        lp.styleLoras = dock->currentStyleLorasForLive();
        lp.positivePrompt = livePos;
        lp.negativePrompt = liveNeg;
        lp.denoise = sampling.denoiseStrength;
        lp.sampler = sampling.sampler;
        lp.scheduler = sampling.scheduler;
        lp.steps = sampling.totalSteps;
        lp.cfg = sampling.cfg;
        lp.nsfwFilterSensitivity = ComfyUIUtils::settingsNsfwFilterSensitivity();
        lp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
        workflow = ComfyWorkflowEngine::buildLive(lp);
    }

    if (workflow.isEmpty()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }
    finalizeWorkflowAndSubmit(dock, workflow);

}

void finalizeWorkflowAndSubmit(ComfyUIRemoteDock *dock, QJsonObject workflow)
{

    if (!dock->m_d->live.checkLiveMode->isChecked()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
    const QString ckptName = dock->checkpointForGenerate();
    QString styleArch;
    if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            styleArch = st->architecture;
    }
    const ComfyResources::Arch arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);

    ComfyWorkflowEngine::applyCheckpointStyleOptions(
        &workflow, dock->m_d->generateRt.generateStyleVae, dock->m_d->generateRt.generateStyleClipSkip, arch);

    QList<ComfyWorkflowEngine::IpAdapterLayerInput> ipInputs;
    QList<ComfyWorkflowEngine::ControlNetLayerInput> cnInputs;
    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : dock->m_d->liveRt.liveControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= dock->m_d->liveRt.liveControlUploadedNames.size())
            break;
        const QString imageName = dock->m_d->liveRt.liveControlUploadedNames.at(uploadIdx++);
        if (ComfyResources::ControlMode::isIpAdapter(ce.mode)) {
            ComfyWorkflowEngine::IpAdapterLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            ipInputs.append(in);
        } else {
            ComfyWorkflowEngine::ControlNetLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            cnInputs.append(in);
        }
    }

    const bool img2imgRefine = ComfyWorkflowEngine::isImg2imgRefineWorkflow(workflow);
    const bool inpaintWorkflow = ComfyWorkflowEngine::isInpaintingTemplateWorkflow(workflow);
    const bool hasExtraConditioning = !ipInputs.isEmpty() || !cnInputs.isEmpty()
        || dock->m_d->liveRt.liveRegionalInputs.size() >= 2;

    if (inpaintWorkflow) {
        // buildRefineRegion / buildInpaint already finish sampler + NSFW (same as inpaint submit).
        ComfyWorkflowEngine::applyIpAdapterLayers(&workflow, ipInputs, arch);
        ComfyWorkflowEngine::applyControlNetLayers(&workflow, cnInputs, arch);
    } else {
        ComfyWorkflowEngine::GenerationConditioningParams conditioning;
        conditioning.ipLayers = ipInputs;
        conditioning.controlLayers = cnInputs;
        conditioning.regions = dock->m_d->liveRt.liveRegionalInputs;
        conditioning.editReference =
            !img2imgRefine
            && (ComfyResources::supportsEditInstructions(arch)
                || (dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked()));

        ComfyWorkflowEngine::WorkflowGraphContext ctx = ComfyWorkflowEngine::discoverWorkflowGraphContext(workflow);
        if (!img2imgRefine || hasExtraConditioning)
            ComfyWorkflowEngine::applyGenerationConditioning(&workflow, conditioning, ctx, arch);

        if (ComfyWorkflowEngine::usesSamplerCustomAdvanced(arch)) {
            double denoise = prep.strength0to1;
            if (workflow.contains(ctx.samplerNodeId)) {
                denoise = workflow.value(ctx.samplerNodeId)
                              .toObject()
                              .value(QStringLiteral("inputs"))
                              .toObject()
                              .value(QStringLiteral("denoise"))
                              .toDouble(denoise);
            }
            ComfyWorkflowEngine::finishWorkflowWithSamplerCustom(
                &workflow, ctx.samplerNodeId, arch, ctx.extentWidth, ctx.extentHeight, denoise);
        }

        ComfyWorkflowEngine::applyNsfwFilterToWorkflowOutput(&workflow, ComfyUIUtils::settingsNsfwFilterSensitivity());
    }

    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

    {
        QString latentPath;
        const QString graphSummary = summarizeWorkflowGraph(workflow, &latentPath);
        dock->m_d->liveRt.liveDiagLatentPath = latentPath;
        dock->m_d->liveRt.liveDiagArchKey = ComfyResources::archToKey(arch);
        double workflowDenoise = prep.strength0to1;
        if (workflow.contains(QStringLiteral("8"))) {
            workflowDenoise = workflow.value(QStringLiteral("8"))
                                .toObject()
                                .value(QStringLiteral("inputs"))
                                .toObject()
                                .value(QStringLiteral("denoise"))
                                .toDouble(workflowDenoise);
        }
        dock->m_d->liveRt.liveDiagDenoise = workflowDenoise;
        const bool refineRegion =
            prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
        InpaintDiagSnapshot diag;
        diag.event = QStringLiteral("build");
        diag.pluginVersion = ComfyUIUtils::pluginVersion();
        diag.workflowKind = refineRegion ? QStringLiteral("refine_region") : QStringLiteral("live");
        diag.archKey = dock->m_d->liveRt.liveDiagArchKey;
        diag.checkpoint = ckptName;
        diag.strength0to1 = prep.strength0to1;
        diag.denoise = workflowDenoise;
        diag.useInpaintModel = prep.inpaintParams.useInpaintModel;
        diag.refineRegion = refineRegion;
        diag.contextBounds = prep.contextBounds;
        diag.targetBoundsRelative = prep.targetBoundsRelative;
        diag.nativeContextSize = prep.nativeContextSize;
        diag.diffusionExtent = prep.diffusionExtent;
        diag.imageUploadName = dock->m_d->liveRt.liveUploadedImageName;
        diag.maskUploadName = dock->m_d->liveRt.liveUploadedMaskName;
        diag.graphSummary = graphSummary;
        diag.latentPath = latentPath;
        logLiveDiag(diag);
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "COMFY_LIVE workflow latentPath=" << latentPath << " graph=" << graphSummary;
    }

    submitWorkflow(dock, workflow);

}

void submitWorkflow(ComfyUIRemoteDock *dock, const QJsonObject &workflow)
{

    if (!dock->m_d->live.checkLiveMode->isChecked() || !dock->m_d->nam) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->m_d->liveRt.livePipelineBusy = false;
        return;
    }

    if (dock->m_d->clientId.isEmpty())
        dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ComfyPromptClient::SubmitRequest submitReq;
    submitReq.workflow = workflow;
    submitReq.clientId = dock->m_d->clientId;
    submitReq.expectedPromptId = expectedPromptId;
    ComfyPromptClient::submitPrompt(dock->m_d->nam, urlStr, submitReq, dock,
                                    [dock, expectedPromptId](const ComfyPromptClient::SubmitResult &result) {
        if (!dock->m_d->live.checkLiveMode->isChecked()) {
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        if (!result.ok) {
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        if (result.promptId != expectedPromptId) {
            dock->setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            dock->m_d->liveRt.livePipelineBusy = false;
            return;
        }
        dock->m_d->liveRt.liveScheduler.notifyGenerationStarted(QDateTime::currentMSecsSinceEpoch());
        dock->m_d->liveRt.livePromptId = result.promptId;
        dock->m_d->liveRt.livePollCount = 0;
        dock->startLiveSpinner();
        dock->setLiveProgress(0);
        dock->m_d->liveRt.livePollTimer->start(1000);
    });

}

} // namespace ComfyLiveRunner
