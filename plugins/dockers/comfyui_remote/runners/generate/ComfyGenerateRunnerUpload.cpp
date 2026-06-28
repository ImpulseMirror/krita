/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyGenerateRunner.h"
#include "ComfyGenerateRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRandomGenerator>
#include <QSharedPointer>
#include <QTimer>
#include <QUuid>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <algorithm>

#include <KSharedConfig>

#include <KoUpdater.h>
#include <kis_animation_importer.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <kis_node.h>
#include <kis_selection.h>
#include <kis_time_span.h>
#include <commands/KisNodeRenameCommand.h>
#include <KisDocument.h>
#include <KisViewManager.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyGenerateRunner {

using namespace ComfyGenerateRunnerInternal;

void beginUploadPipeline(ComfyUIRemoteDock *dock)
{

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "beginGenerateUploadPipeline ENTER isConnected=" << dock->m_d->isConnected
        << " hasNam=" << (dock->m_d->nam != nullptr);

    const QStringList loraPaths =
        dock->m_d->isConnected && dock->m_d->nam
            ? ComfyUploadPipeline::collectMissingLoraUploadPaths(dock->m_d->comfyServerLoraFilenames)
            : QStringList();

    dock->m_d->generateRt.generateControlLayersActive = controlLayersForGenerate(dock->m_d.data());
    dock->m_d->generateRt.generateControlUploadedNames.clear();
    dock->m_d->generateRt.generateAwaitingLoraUploads = !loraPaths.isEmpty();
    dock->m_d->generateRt.generateAwaitingControlUploads =
        ComfyControlLayer::anyNeedsGenerateUpload(dock->m_d->generateRt.generateControlLayersActive);

    KisImageSP image = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
    const QList<ComfyUploadPipeline::ImageItem> controlItems =
        dock->m_d->generateRt.generateAwaitingControlUploads
            ? ComfyUploadPipeline::buildControlUploadItems(image, dock->m_d->generateRt.generateControlLayersActive)
            : QList<ComfyUploadPipeline::ImageItem>();

    if (!loraPaths.isEmpty() || !controlItems.isEmpty()) {
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
        handlers.onComplete = [dock](const ComfyUploadPipeline::Result &result) {
            dock->m_d->generateRt.generateAwaitingLoraUploads = false;
            dock->m_d->generateRt.generateAwaitingControlUploads = false;
            dock->m_d->generateRt.generateControlUploadedNames = result.uploadedImageNames;
            finalizeWorkflowAndSubmit(dock, dock->m_d->generateRt.generatePendingBaseWorkflow);
        };
        handlers.onAbort = [dock]() {
            dock->m_d->generateRt.generateAwaitingLoraUploads = false;
            dock->m_d->generateRt.generateAwaitingControlUploads = false;
            dock->m_d->generate.btnGenerate->setEnabled(true);
        };
        run->start(dock->m_d->editServerUrl->text().trimmed(),
                   loraPaths,
                   controlItems,
                   std::move(handlers),
                   &dock->m_d->comfyServerLoraFilenames);
        return;
    }

    finalizeWorkflowAndSubmit(dock, dock->m_d->generateRt.generatePendingBaseWorkflow);

}
void uploadNextRegionMask(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->generateRt.generateAwaitingRegionMaskUploads || !dock->m_d->viewManager) {
        dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }

    QList<ComfyUploadPipeline::ImageItem> maskItems;
    for (int inputIdx = 0; inputIdx < dock->m_d->generateRt.generateProcessedRegions.size(); ++inputIdx) {
        if (inputIdx >= dock->m_d->generateRt.generateRegionalInputs.size())
            continue;

        const ComfyRegionProcess::ProcessedRegionEntry &region = dock->m_d->generateRt.generateProcessedRegions.at(inputIdx);
        if (region.maskGray.isNull()) {
            dock->setStatusMessage(ComfyTr::tr("Region mask is empty."), true);
            dock->m_d->generateRt.generateAwaitingRegionMaskUploads = false;
            dock->m_d->generate.btnGenerate->setEnabled(true);
            return;
        }
        const QString regionLabel =
            region.isBackground ? ComfyTr::tr("background") : QString::number(inputIdx);
        ComfyUploadPipeline::ImageItem item;
        item.statusText = ComfyTr::tr("Uploading region mask %1…", regionLabel);
        item.filenameHint = QStringLiteral("region_mask_%1.png").arg(inputIdx);
        const QImage maskGray = region.maskGray;
        item.prepareImage = [maskGray]() { return ComfyUIUtils::maskPngForComfyUpload(maskGray); };
        item.onUploaded = [dock, inputIdx](const QString &name) {
            if (inputIdx < dock->m_d->generateRt.generateRegionalInputs.size())
                dock->m_d->generateRt.generateRegionalInputs[inputIdx].maskImageName = name;
        };
        maskItems.append(item);
    }

    auto finishAfterRegionMasks = [dock]() {
        dock->m_d->generateRt.generateAwaitingRegionMaskUploads = false;
        if (dock->m_d->generateRt.generateRefineAfterRegions) {
            QJsonObject workflow;
            QString regionMask;
            for (const ComfyWorkflowEngine::RegionalPromptInput &ri : dock->m_d->generateRt.generateRegionalInputs) {
                if (!ri.isBackground && !ri.maskImageName.isEmpty()) {
                    regionMask = ri.maskImageName;
                    break;
                }
            }
            if (!regionMask.isEmpty()) {
                ComfyWorkflowEngine::RefineRegionParams rrp;
                rrp.refine = dock->m_d->generateRt.generateStashedRefineParams;
                rrp.maskImageName = regionMask;
                rrp.growMaskBy = ComfyUIUtils::clampInpaintGrowFeather(
                    ComfyUIUtils::loadSettingsJson().value(QStringLiteral("selection_grow_offset")).toInt(4));
                rrp.colorMatch = ComfyUIUtils::settingsColorMatchEnabled();
                workflow = ComfyWorkflowEngine::buildRefineRegion(rrp);
            }
            if (workflow.isEmpty())
                workflow = ComfyWorkflowEngine::buildRefine(dock->m_d->generateRt.generateStashedRefineParams);
            if (workflow.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Refine workflow error."), true);
                dock->m_d->generateRt.generateRefineAfterRegions = false;
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            dock->m_d->generateRt.generatePendingBaseWorkflow = workflow;
            dock->m_d->generateRt.generatePendingArch = dock->m_d->generateRt.generateStashedRefineParams.arch;
            dock->m_d->generateRt.generateRefineAfterRegions = false;
        }
        beginUploadPipeline(dock);
    };

    if (maskItems.isEmpty()) {
        finishAfterRegionMasks();
        return;
    }

    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.setProgressKind = [dock](bool isUpload) { dock->setProgressBarKind(isUpload); };
    handlers.setStatusText = [dock](const QString &text) {
        if (dock->m_d->labelStatus)
            dock->m_d->labelStatus->setText(text);
    };
    handlers.setStatusMessage = [dock](const QString &msg, bool isError) { dock->setStatusMessage(msg, isError); };
    handlers.onComplete = [finishAfterRegionMasks](const ComfyUploadPipeline::Result &) { finishAfterRegionMasks(); };
    handlers.onAbort = [dock]() {
        dock->m_d->generateRt.generateAwaitingRegionMaskUploads = false;
        dock->m_d->generate.btnGenerate->setEnabled(true);
    };
    run->start(dock->m_d->editServerUrl->text().trimmed(), {}, maskItems, std::move(handlers));

}
void finalizeWorkflowAndSubmit(ComfyUIRemoteDock *dock, QJsonObject workflow)
{

    ComfyWorkflowEngine::applyCheckpointStyleOptions(
        &workflow, dock->m_d->generateRt.generateStyleVae, dock->m_d->generateRt.generateStyleClipSkip, dock->m_d->generateRt.generateStyleArch);

    QList<ComfyWorkflowEngine::IpAdapterLayerInput> ipInputs;
    QList<ComfyWorkflowEngine::ControlNetLayerInput> cnInputs;
    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : dock->m_d->generateRt.generateControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= dock->m_d->generateRt.generateControlUploadedNames.size())
            break;
        const QString imageName = dock->m_d->generateRt.generateControlUploadedNames.at(uploadIdx++);
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

    ComfyWorkflowEngine::GenerationConditioningParams conditioning;
    conditioning.ipLayers = ipInputs;
    conditioning.controlLayers = cnInputs;
    conditioning.regions = dock->m_d->generateRt.generateRegionalInputs;
    const bool img2imgRefine = ComfyWorkflowEngine::isImg2imgRefineWorkflow(workflow);
    const bool hasExtraConditioning = !ipInputs.isEmpty() || !cnInputs.isEmpty()
        || dock->m_d->generateRt.generateRegionalInputs.size() >= 2;
    conditioning.editReference =
        !img2imgRefine && ComfyResources::supportsEditInstructions(dock->m_d->generateRt.generatePendingArch);

    ComfyWorkflowEngine::WorkflowGraphContext ctx = ComfyWorkflowEngine::discoverWorkflowGraphContext(workflow);
    if (!img2imgRefine || hasExtraConditioning)
        ComfyWorkflowEngine::applyGenerationConditioning(&workflow, conditioning, ctx, dock->m_d->generateRt.generatePendingArch);

    if (ComfyWorkflowEngine::usesSamplerCustomAdvanced(dock->m_d->generateRt.generatePendingArch)) {
        double denoise = 1.0;
        if (workflow.contains(ctx.samplerNodeId)) {
            denoise = workflow.value(ctx.samplerNodeId)
                          .toObject()
                          .value(QStringLiteral("inputs"))
                          .toObject()
                          .value(QStringLiteral("denoise"))
                          .toDouble(1.0);
        }
        int extentW = ctx.extentWidth;
        int extentH = ctx.extentHeight;
        if (dock->m_d->generateRt.generatePendingRefineWidth > 0 && dock->m_d->generateRt.generatePendingRefineHeight > 0) {
            extentW = dock->m_d->generateRt.generatePendingRefineWidth;
            extentH = dock->m_d->generateRt.generatePendingRefineHeight;
        }
        ComfyWorkflowEngine::finishWorkflowWithSamplerCustom(
            &workflow, ctx.samplerNodeId, dock->m_d->generateRt.generatePendingArch, extentW, extentH, denoise);
    }

    ComfyWorkflowEngine::applyNsfwFilterToWorkflowOutput(&workflow,
                                                       ComfyUIUtils::settingsNsfwFilterSensitivity());

    if (dock->m_d->generateRt.generatePendingArch == ComfyResources::Arch::QwenL && dock->m_d->generateRt.generatePendingLayerCount > 1) {
        ComfyWorkflowEngine::packLatentLayersAfterSampler(
            &workflow, dock->m_d->generateRt.generatePendingLayerCount, qMax(1, dock->m_d->generateRt.generatePendingLayerCount));
    }

    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

    const int genBatch = dock->m_d->generateRt.generateStashedBatch;
    const double genMul = dock->m_d->generateRt.generateStashedMul;
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    QUrl baseUrl(urlStr);

    int effW = dock->m_d->generate.spinWidth->value();
    int effH = dock->m_d->generate.spinHeight->value();
    effW = qBound(64, static_cast<int>(effW * genMul), 8192);
    effH = qBound(64, static_cast<int>(effH * genMul), 8192);
    ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
    int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, genBatch);
    if (dock->m_d->isFullAnimationBatch && genBatch > 0)
        effectiveBatch = genBatch;
    dock->m_d->batchSeedStep = qMax(1, genBatch);

    int queueMode = takeGenerateQueueMode(dock->m_d.data());
    if (dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3)
        queueMode = 0;
    if (queueMode == 2) {
        dock->m_d->pollTimer->stop();
        for (const QString &id : dock->m_d->jobQueue)
            dock->m_d->history.pendingHistoryByPromptId.remove(id);
        if (!dock->m_d->currentPromptId.isEmpty())
            dock->m_d->history.pendingHistoryByPromptId.remove(dock->m_d->currentPromptId);
        dock->m_d->jobQueue.clear();
        dock->m_d->currentPromptId.clear();
        QUrl interruptUrl(baseUrl);
        QString ip = interruptUrl.path();
        if (ip.isEmpty() || ip == QLatin1Char('/'))
            interruptUrl.setPath(QStringLiteral("/interrupt"));
        else if (!ip.endsWith(QLatin1Char('/')))
            interruptUrl.setPath(ip + QStringLiteral("/interrupt"));
        else
            interruptUrl.setPath(ip + QStringLiteral("interrupt"));
        QNetworkRequest reqI(interruptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqI);
        reqI.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        dock->m_d->nam->post(reqI, QByteArray("{}"));
    }

    dock->m_d->batchCollectIds.clear();
    dock->m_d->batchSubmitIndex = 0;
    dock->m_d->batchCountTarget = qMax(1, effectiveBatch);
    dock->m_d->batchQueueMode = queueMode;
    dock->m_d->batchBaseUrl = baseUrl;
    QString path = baseUrl.path();
    if (path.isEmpty() || path == QLatin1Char('/'))
        dock->m_d->batchBaseUrl.setPath(QStringLiteral("/prompt"));
    else if (!path.endsWith(QLatin1Char('/')))
        dock->m_d->batchBaseUrl.setPath(path + QStringLiteral("/prompt"));
    else
        dock->m_d->batchBaseUrl.setPath(path + QStringLiteral("prompt"));
    dock->m_d->batchNeedsPerFrameReference = false;
    dock->m_d->batchNeedsPerFrameAnimationRefine = false;
    dock->m_d->batchUseCustomWorkflow = true;
    dock->m_d->batchCustomWorkflow = workflow;
    dock->m_d->batchBaseSeed = dock->m_d->generate.checkFixedSeed->isChecked()
        ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    if (!dock->m_d->generate.checkFixedSeed->isChecked())
        dock->m_d->generate.spinSeed->setValue(static_cast<int>(dock->m_d->batchBaseSeed));
    if (dock->m_d->clientId.isEmpty())
        dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    stashBatchCaptureMetadata(dock->m_d.data());
    dock->m_d->generateRt.generateRegionalInputs.clear();
    dock->m_d->generateRt.generateProcessedRegions.clear();
    dock->m_d->generateRt.generateControlLayersActive.clear();
    dock->m_d->generateRt.generateRefineAfterRegions = false;
    dock->m_d->generateRt.generatePendingRefineWidth = 0;
    dock->m_d->generateRt.generatePendingRefineHeight = 0;

    dock->m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
    dock->m_d->progressBar->setValue(0);
    dock->m_d->generate.btnGenerate->setEnabled(false);
    onBatchSubmitNext(dock);

}
bool tryStartRefineFromGenerate(ComfyUIRemoteDock *dock)
{

    const int strengthPct = dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100;
    const bool editMode = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "tryStartRefineFromGenerate strengthPct=" << strengthPct
        << " editMode=" << editMode
        << " customWorkflowLen="
        << (dock->m_d->editCustomWorkflow ? dock->m_d->editCustomWorkflow->toPlainText().trimmed().size() : -1);

    if (dock->m_d->editCustomWorkflow && !dock->m_d->editCustomWorkflow->toPlainText().trimmed().isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "tryStartRefineFromGenerate: custom workflow present → returning false";
        return false;
    }

    ComfyPrepareGenerateWorkflow::PrepareFlags dryRun;
    dryRun.requireMask = true;
    dryRun.captureImage = false;
    const ComfyPrepareGenerateWorkflow::Result maskedPrep =
        ComfyPrepareGenerateWorkflow::prepare(dock->prepareGenerateWorkflowInput(dryRun));
    if (maskedPrep.ok && maskedPrep.hasMask) {
        qCWarning(KIS_COMFYUI_REMOTE) << "tryStartRefineFromGenerate: masked workflow via prepare → dock->slotInpaint()";
        dock->slotInpaint();
        return true;
    }

    if (strengthPct >= 100 && !editMode) {
        qCWarning(KIS_COMFYUI_REMOTE)
            << "tryStartRefineFromGenerate: strength=100 && !editMode && no masked workflow → normal Generate";
        return false;
    }

    uploadCanvasForRefine(dock);
    return true;

}
void uploadCanvasForRefine(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        dock->setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }

    const bool editMode = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    QString linkedEditStyleId;
    if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            linkedEditStyleId = st->linkedEditStyle;
    }
    const ComfyUIUtils::LinkedEditStyleOverride link = ComfyUIUtils::linkedEditStyleOverride(
        editMode,
        linkedEditStyleId,
        dock->checkpointForGenerate(),
        dock->m_d->generate.spinSteps->value(),
        dock->m_d->generate.spinCfg->value(),
        (dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100) / 100.0,
        dock->m_d->generate.comboSampler->currentText().trimmed(),
        dock->m_d->generateRt.ksamplerScheduler);

    qint64 seed = dock->m_d->generate.checkFixedSeed->isChecked()
        ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    if (!dock->m_d->generate.checkFixedSeed->isChecked())
        dock->m_d->generate.spinSeed->setValue(static_cast<int>(seed));

    QString userPos = ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed();
    QString promptText = link.active
        ? ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, userPos).trimmed()
        : userPos;
    promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
    ComfyUIUtils::extractLayerPlaceholders(promptText);

    ComfyPrepareGenerateWorkflow::PrepareFlags prepFlags;
    prepFlags.requireMask = false;
    prepFlags.captureImage = true;
    ComfyPrepareGenerateWorkflow::Input prepIn = dock->prepareGenerateWorkflowInput(prepFlags);
    prepIn.rootPositivePrompt = promptText;
    const ComfyPrepareGenerateWorkflow::Result prep = ComfyPrepareGenerateWorkflow::prepare(prepIn);
    if (!prep.ok) {
        dock->setStatusMessage(prep.errorMessage, true);
        return;
    }
    if (prep.hasMask || prep.workflowKind != ComfyPrepareGenerateWorkflow::WorkflowKind::Refine) {
        dock->setStatusMessage(ComfyTr::tr("Could not prepare canvas refine workflow."), true);
        return;
    }
    dock->m_d->generateRt.generateRefinePrepared = prep;

    const QImage canvasImg = prep.contextImage;
    if (canvasImg.isNull()) {
        dock->setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
        return;
    }

    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    QUrl uploadUrl(urlStr);
    QString up = uploadUrl.path();
    if (up.isEmpty() || up == QLatin1Char('/'))
        uploadUrl.setPath(QStringLiteral("/upload/image"));
    else if (!up.endsWith(QLatin1Char('/')))
        uploadUrl.setPath(up + QStringLiteral("/upload/image"));
    else
        uploadUrl.setPath(up + QStringLiteral("upload/image"));

    QTemporaryFile *tmp = new QTemporaryFile(dock);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        dock->setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }

    dock->m_d->generate.btnGenerate->setEnabled(false);
    dock->m_d->progressBar->setValue(0);
    tmp->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"krita_refine.png\"")));
    part.setBodyDevice(tmp);
    tmp->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(uploadUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading canvas for refine…"));
    dock->setProgressBarKind(true);
    QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply, link, seed]() {
        reply->deleteLater();
        dock->setProgressBarKind(false);
        if (reply->error() != QNetworkReply::NoError) {
            dock->setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            dock->m_d->generate.btnGenerate->setEnabled(true);
            return;
        }
        dock->m_d->generateRt.generateRefineUploadedImageName =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (dock->m_d->generateRt.generateRefineUploadedImageName.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            dock->m_d->generate.btnGenerate->setEnabled(true);
            return;
        }

        const QJsonObject settingsRoot = ComfyUIUtils::loadSettingsJson();
        int genBatch = dock->m_d->generate.spinBatchCount ? dock->m_d->generate.spinBatchCount->value() : 1;
        double genMul = dock->m_d->generate.resolutionMultiplier <= 0.0 ? 1.0 : dock->m_d->generate.resolutionMultiplier;
        ComfyUIUtils::generationPerformanceBatchResolution(settingsRoot, dock->m_d->lastComfySystemStats, genBatch, genMul,
                                                           &genBatch, &genMul);
        const int genBatchMax = dock->m_d->generate.spinBatchCount ? dock->m_d->generate.spinBatchCount->maximum() : 16;
        genBatch = qBound(1, genBatch, genBatchMax);

        QString styleArch;
        const ComfyStyleEntry *styleEntry = nullptr;
        if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
            const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
                styleEntry = st;
                styleArch = st->architecture;
            }
        }

        ComfyWorkflowEngine::RefineParams rp;
        rp.checkpoint = link.checkpoint;
        rp.imageName = dock->m_d->generateRt.generateRefineUploadedImageName;
        rp.arch = ComfyWorkflowEngine::resolveArch(link.checkpoint, styleArch);
        rp.seed = seed;
        rp.steps = link.steps;
        rp.cfg = link.cfg;
        rp.denoise = link.denoise;
        rp.sampler = link.sampler;
        rp.scheduler = link.scheduler;
        rp.styleLoras = dock->currentStyleLoras();
        rp.positivePrompt =
            ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(dock->m_d->generateRt.generateRefinePrepared.effectivePositivePrompt,
                                                                   rp.styleLoras);
        const QString negSrc =
            link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative->toPlainText()).trimmed();
        rp.negativePrompt = ComfyUIUtils::evalWildcards(negSrc, static_cast<quint32>(seed & 0xFFFFFFFFu));
        rp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
        rp.nsfwFilterSensitivity = ComfyUIUtils::settingsNsfwFilterSensitivity();
        ComfyUIUtils::applyStrengthResolvedSamplingToRefine(
            &rp, styleEntry, settingsRoot, rp.sampler, rp.steps, rp.cfg, link.denoise);

        dock->m_d->generateRt.generateStashedCustomJson.clear();
        dock->m_d->generateRt.generateStashedBatch = genBatch;
        dock->m_d->generateRt.generateStashedMul = 1.0;
        dock->m_d->generateRt.generateRefineAfterRegions = false;

        const ComfyRegionProcess::ProcessRegionsResult processed = dock->m_d->generateRt.generateRefinePrepared.processedRegions;
        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion)
            rp.positivePrompt = ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(processed.effectivePositive,
                                                                                       rp.styleLoras);

        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion
            && ComfyResources::supportsRegions(rp.arch)) {
            dock->m_d->generateRt.generateStashedRefineParams = rp;
            dock->m_d->generateRt.generateRefineAfterRegions = true;
            dock->m_d->generateRt.generateProcessedRegions = processed.regions;
            dock->m_d->generateRt.generateRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
                dock->m_d->generateRt.generateProcessedRegions, rp.promptTranslationLanguage);
            dock->m_d->generateRt.generateAwaitingRegionMaskUploads = true;
            dock->m_d->generateRt.generateRegionMaskUploadIndex = 0;
            uploadNextRegionMask(dock);
            return;
        }

        QJsonObject workflow = ComfyWorkflowEngine::buildRefine(rp);
        if (workflow.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Refine workflow error."), true);
            dock->m_d->generate.btnGenerate->setEnabled(true);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

        dock->m_d->generateRt.generatePendingBaseWorkflow = workflow;
        dock->m_d->generateRt.generatePendingArch = rp.arch;
        dock->m_d->generateRt.generatePendingRefineWidth = dock->m_d->generateRt.generateRefinePrepared.contextImage.width();
        dock->m_d->generateRt.generatePendingRefineHeight = dock->m_d->generateRt.generateRefinePrepared.contextImage.height();
        dock->m_d->generateRt.generateRegionalInputs.clear();
        dock->m_d->generateRt.generateProcessedRegions.clear();
        beginUploadPipeline(dock);
    });

}


} // namespace ComfyGenerateRunner
