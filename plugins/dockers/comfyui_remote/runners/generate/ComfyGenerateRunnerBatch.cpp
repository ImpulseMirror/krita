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

void onBatchSubmitNext(ComfyUIRemoteDock *dock)
{

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotBatchSubmitNext: index=" << dock->m_d->batchSubmitIndex
        << "/" << dock->m_d->batchCountTarget
        << " useCustomWorkflow=" << dock->m_d->batchUseCustomWorkflow
        << " needsPerFrameRef=" << dock->m_d->batchNeedsPerFrameReference
        << " needsPerFrameAnimRefine=" << dock->m_d->batchNeedsPerFrameAnimationRefine;
    if (dock->m_d->batchSubmitIndex >= dock->m_d->batchCountTarget) {
        if (dock->m_d->batchQueueMode == 0) // Back
            dock->m_d->jobQueue.append(dock->m_d->batchCollectIds);
        else if (dock->m_d->batchQueueMode == 1) { // Front
            for (int i = dock->m_d->batchCollectIds.size() - 1; i >= 0; i--)
                dock->m_d->jobQueue.prepend(dock->m_d->batchCollectIds.at(i));
        } else // Replace
            dock->m_d->jobQueue = dock->m_d->batchCollectIds;
        dock->m_d->batchCollectIds.clear();
        dock->m_d->batchCountTarget = 0;
        dock->m_d->batchNeedsPerFrameReference = false;
        dock->m_d->batchNeedsPerFrameAnimationRefine = false;
        clearBatchCaptureStash(dock->m_d.data());
        if (dock->m_d->currentPromptId.isEmpty() && !dock->m_d->jobQueue.isEmpty()) {
            dock->m_d->currentPromptId = dock->m_d->jobQueue.takeFirst();
            dock->m_d->pollCount = 0;
            dock->startPolling();
        }
        dock->updateQueueStatus();
        releaseGenerateActionAfterEnqueue(dock);
        return;
    }

    // §13.74: Full Animation — timeline matches each queued job (canvas, layer tags, future img2img inputs)
    if (dock->m_d->isFullAnimationBatch && dock->m_d->viewManager) {
        KisImageSP img = dock->m_d->viewManager->image();
        if (img && img->animationInterface() && img->animationInterface()->hasAnimation()) {
            const int idx = dock->m_d->batchSubmitIndex;
            int frameTime = idx;
            if (idx >= 0 && idx < dock->m_d->animationBatchFrameTimes.size()) {
                frameTime = dock->m_d->animationBatchFrameTimes.at(idx);
            } else {
                const KisTimeSpan r = img->animationInterface()->activePlaybackRange();
                if (r.isValid() && r.duration() > 0 && idx >= 0 && idx < r.duration())
                    frameTime = r.start() + idx;
            }
            img->animationInterface()->requestTimeSwitchNonGUI(frameTime, false);
            img->waitForDone();
            QCoreApplication::processEvents();
        }
    }

    // P4/D7: built-in Animation img2img — upload canvas (single frame) or target-layer pixels (full batch)
    if (dock->m_d->batchNeedsPerFrameAnimationRefine && !dock->m_d->batchUseCustomWorkflow) {
        QString urlStr = dock->m_d->editServerUrl->text().trimmed();
        KisImageSP image = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
        QImage refImg;
        if (dock->m_d->isFullAnimationBatch) {
            const QString layerId = dock->m_d->comboAnimationTargetLayer
                ? dock->m_d->comboAnimationTargetLayer->currentData().toString().trimmed()
                : QString();
            if (layerId.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Select an animation target layer for img2img batch."), true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                dock->resetProgressBarToIdle();
                clearAnimationBatchState(dock->m_d.data());
                return;
            }
            refImg = ComfyUIUtils::getLayerProjectionByUuid(image, layerId);
            if (refImg.isNull()) {
                dock->setStatusMessage(ComfyTr::tr("Could not read animation target layer pixels."), true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                dock->resetProgressBarToIdle();
                clearAnimationBatchState(dock->m_d.data());
                return;
            }
        } else {
            const QList<KisNodeSP> excludeNodes =
                ComfyUIUtils::collectInpaintExcludeNodes(image, true, dock->m_d->rootControlLayers, dock->m_d->previewLayerId);
            const auto docCapture = ComfyUIUtils::getDocumentImage(image, QRect(), excludeNodes);
            if (!docCapture) {
                dock->setStatusMessage(docCapture.errorMessage.isEmpty()
                                     ? ComfyTr::tr("Could not export canvas for animation refine.")
                                     : docCapture.errorMessage,
                                 true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                dock->resetProgressBarToIdle();
                clearAnimationBatchState(dock->m_d.data());
                return;
            }
            refImg = docCapture.image;
        }
        QTemporaryFile *tmp = new QTemporaryFile(dock);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            dock->setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
            dock->m_d->generate.btnGenerate->setEnabled(true);
            dock->resetProgressBarToIdle();
            clearAnimationBatchState(dock->m_d.data());
            return;
        }
        dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading canvas for animation refine…"));
        dock->setProgressBarKind(true);
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
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"animation_refine.png\"")));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = dock->m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        const int cappedIndex = dock->m_d->batchSubmitIndex;
        QObject::connect(replyUp, &QNetworkReply::finished, dock, [dock, replyUp, tmp, cappedIndex]() {
            Q_UNUSED(tmp);
            replyUp->deleteLater();
            dock->setProgressBarKind(false);
            auto abortAnimBatch = [dock]() {
                dock->m_d->generate.btnGenerate->setEnabled(true);
                dock->resetProgressBarToIdle();
                clearAnimationBatchState(dock->m_d.data());
            };
            if (replyUp->error() != QNetworkReply::NoError) {
                dock->setStatusMessage(ComfyTr::tr("Upload error: %1", replyUp->errorString()), true);
                abortAnimBatch();
                return;
            }
            const QString refName =
                QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (refName.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
                abortAnimBatch();
                return;
            }
            QString styleArch;
            if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
                const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    styleArch = st->architecture;
            }
            const QJsonArray styleLoras = dock->currentStyleLoras();
            const ComfyWorkflowEngine::RefineParams rp = animationRefineParamsFromDock(
                dock->m_d.data(), dock->checkpointForGenerate(), cappedIndex, dock->m_d->batchBaseSeed, dock->m_d->batchSeedStep,
                styleArch, styleLoras, refName);
            QJsonObject workflow = ComfyWorkflowEngine::buildRefine(rp);
            if (workflow.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Animation refine workflow error."), true);
                abortAnimBatch();
                return;
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            dispatchBatchPromptRequest(dock, workflow, cappedIndex);
        });
        return;
    }

    if (dock->m_d->batchUseCustomWorkflow && dock->m_d->batchNeedsPerFrameReference) {
        QString urlStr = dock->m_d->editServerUrl->text().trimmed();
        KisImageSP image = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
        const QList<KisNodeSP> excludeNodes =
            ComfyUIUtils::collectInpaintExcludeNodes(image, true, dock->m_d->rootControlLayers, dock->m_d->previewLayerId);
        const auto docCapture = ComfyUIUtils::getDocumentImage(image, QRect(), excludeNodes);
        if (!docCapture) {
            dock->setStatusMessage(docCapture.errorMessage.isEmpty()
                                 ? ComfyTr::tr("Could not export canvas for reference.")
                                 : docCapture.errorMessage,
                             true);
            dock->m_d->generate.btnGenerate->setEnabled(true);
            dock->resetProgressBarToIdle();
            dock->m_d->batchCountTarget = 0;
            dock->m_d->isFullAnimationBatch = false;
            dock->m_d->animationBatchPromptIdToIndex.clear();
            dock->m_d->animationBatchSourcePathByFrame.clear();
            dock->m_d->animationBatchFrameTimes.clear();
            dock->m_d->animationBatchGroupId.clear();
            dock->m_d->batchNeedsPerFrameReference = false;
            return;
        }
        QImage refImg = docCapture.image;
        QTemporaryFile *tmp = new QTemporaryFile(dock);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            dock->setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
            dock->m_d->generate.btnGenerate->setEnabled(true);
            dock->resetProgressBarToIdle();
            dock->m_d->batchCountTarget = 0;
            dock->m_d->isFullAnimationBatch = false;
            dock->m_d->animationBatchPromptIdToIndex.clear();
            dock->m_d->animationBatchSourcePathByFrame.clear();
            dock->m_d->animationBatchFrameTimes.clear();
            dock->m_d->animationBatchGroupId.clear();
            dock->m_d->batchNeedsPerFrameReference = false;
            return;
        }
        dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading reference image…"));
        dock->setProgressBarKind(true);
        QUrl uploadUrl(urlStr);
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == QLatin1Char('/')) uploadUrl.setPath(QStringLiteral("/upload/image"));
        else if (!up.endsWith(QLatin1Char('/'))) uploadUrl.setPath(up + QStringLiteral("/upload/image"));
        else uploadUrl.setPath(up + QStringLiteral("upload/image"));
        tmp->open();
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"reference.png\"")));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = dock->m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        const int cappedIndex = dock->m_d->batchSubmitIndex;
        QObject::connect(replyUp, &QNetworkReply::finished, dock, [dock, replyUp, tmp, cappedIndex]() {
            Q_UNUSED(tmp);
            replyUp->deleteLater();
            dock->setProgressBarKind(false);
            auto abortAnimBatch = [dock]() {
                dock->m_d->generate.btnGenerate->setEnabled(true);
                dock->resetProgressBarToIdle();
                dock->m_d->batchCountTarget = 0;
                dock->m_d->isFullAnimationBatch = false;
                dock->m_d->animationBatchPromptIdToIndex.clear();
                dock->m_d->animationBatchSourcePathByFrame.clear();
                dock->m_d->animationBatchFrameTimes.clear();
                dock->m_d->animationBatchGroupId.clear();
                dock->m_d->batchNeedsPerFrameReference = false;
            };
            if (replyUp->error() != QNetworkReply::NoError) {
                dock->setStatusMessage(ComfyTr::tr("Upload error: %1", replyUp->errorString()), true);
                abortAnimBatch();
                return;
            }
            const QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (refName.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
                abortAnimBatch();
                return;
            }
            QJsonDocument tmpl(dock->m_d->batchCustomWorkflow);
            QString wt = QString::fromUtf8(tmpl.toJson(QJsonDocument::Compact));
            if (!wt.contains(QStringLiteral("REFERENCE_IMAGE"))) {
                dock->setStatusMessage(ComfyTr::tr("Workflow lost REFERENCE_IMAGE placeholder."), true);
                abortAnimBatch();
                return;
            }
            wt.replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QJsonDocument wdoc = QJsonDocument::fromJson(wt.toUtf8(), &err);
            if (err.error != QJsonParseError::NoError || !wdoc.isObject()) {
                dock->setStatusMessage(ComfyTr::tr("Workflow JSON error after reference replace."), true);
                abortAnimBatch();
                return;
            }
            QJsonObject workflow = wdoc.object();
            if (!dock->tryResolveCustomWorkflowInPlace(&workflow)) {
                abortAnimBatch();
                return;
            }
            const auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
            if (!validation.first) {
                dock->setStatusMessage(validation.second, true);
                abortAnimBatch();
                return;
            }
            if (!validateCustomWorkflowGraphOrShowError(dock, dock->m_d.data(), workflow)) {
                abortAnimBatch();
                return;
            }
            dispatchBatchPromptRequest(dock, workflow, cappedIndex);
        });
        return;
    }

    QJsonObject workflow;
    if (dock->m_d->batchUseCustomWorkflow) {
        workflow = dock->m_d->batchCustomWorkflow;
        if (!dock->tryResolveCustomWorkflowInPlace(&workflow)) {
            dock->reEnableGenerateUi();
            dock->m_d->batchCountTarget = 0;
            return;
        }
    } else {
        QString styleArch;
        if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
            const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                styleArch = st->architecture;
        }
        const QJsonArray styleLoras = dock->currentStyleLoras();
        const ComfyWorkflowEngine::AnimationFrameParams af = animationFrameParamsFromDock(
            dock->m_d.data(), dock->checkpointForGenerate(), dock->m_d->batchSubmitIndex, dock->m_d->batchBaseSeed, dock->m_d->batchSeedStep,
            styleArch, styleLoras);
        workflow = ComfyWorkflowEngine::buildAnimationFrame(af);
        if (workflow.isEmpty())
            return;
    }
    dispatchBatchPromptRequest(dock, workflow, dock->m_d->batchSubmitIndex);

}
void dispatchBatchPromptRequest(ComfyUIRemoteDock *dock, QJsonObject workflow, int submitIndex)
{

    stashBatchCaptureMetadata(dock->m_d.data());
    {
        KisImageSP wfImage = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
        ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, dock->m_d->customWorkflowParamOverrides, wfImage);
    }
    {
        QString expandErr;
        ComfyUIUtils::CustomWorkflowExpandState expandState;
        if (!expandCustomKritaInjectionWorkflow(dock, dock->m_d.data(), &workflow, &expandErr, &expandState)) {
            dock->setStatusMessage(expandErr, true);
            if (dock->m_d->customGraphLiveActive) {
                dock->m_d->customGraphLiveActive = false;
                if (dock->m_d->customGraphLiveTimer)
                    dock->m_d->customGraphLiveTimer->stop();
            }
            dock->m_d->generate.btnGenerate->setEnabled(true);
            dock->m_d->batchCountTarget = 0;
            dock->m_d->batchNeedsPerFrameReference = false;
            dock->m_d->batchNeedsPerFrameAnimationRefine = false;
            return;
        }
        dock->m_d->lastCustomWorkflowExpandState = expandState;
        if (expandState.captureBounds.isValid() || expandState.hasSelectionMask) {
            dock->m_d->batchStashedContextBounds = expandState.captureBounds;
            dock->m_d->batchStashedHasMask = expandState.hasSelectionMask;
        }
        if (dock->m_d->customGraphLiveActive && !expandState.inputFingerprint.isEmpty()
            && expandState.inputFingerprint == dock->m_d->customGraphLiveLastFingerprint) {
            if (dock->m_d->generate.btnGenerate)
                dock->m_d->generate.btnGenerate->setEnabled(true);
            maybeContinueCustomGraphLive(dock);
            return;
        }
        if (dock->m_d->customGraphLiveActive)
            dock->m_d->customGraphLiveLastFingerprint = expandState.inputFingerprint;
    }
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    ComfyWorkflowEngine::applyNsfwFilterToWorkflowOutput(&workflow,
                                                       ComfyUIUtils::settingsNsfwFilterSensitivity());

    // §13.163: ComfyUI prompt API body
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ComfyPromptClient::SubmitRequest submitReq;
    submitReq.workflow = workflow;
    submitReq.clientId = dock->m_d->clientId;
    submitReq.expectedPromptId = expectedPromptId;
    submitReq.dumpPayload = true;
    if (dock->m_d->isFullAnimationBatch && !dock->m_d->animationBatchGroupId.isEmpty()) {
        QJsonObject kid;
        kid.insert(QStringLiteral("animation_id"), dock->m_d->animationBatchGroupId);
        submitReq.extraData.insert(QStringLiteral("krita_ai_diffusion"), kid);
    }
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotBatchSubmitNext POST url=" << ComfyPromptClient::promptEndpointUrl(urlStr).toString()
        << " submitIndex=" << submitIndex
        << " expectedPromptId=" << expectedPromptId;
    ComfyPromptClient::submitPrompt(dock->m_d->nam, urlStr, submitReq, dock,
                                    [dock, submitIndex, expectedPromptId](const ComfyPromptClient::SubmitResult &result) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotBatchSubmitNext REPLY submitIndex=" << submitIndex
            << " httpStatus=" << result.httpStatus
            << " ok=" << result.ok
            << " bodyBytes=" << result.responseBody.size();
        auto abortBatchState = [dock]() {
            if (dock->m_d->customGraphLiveActive) {
                dock->m_d->customGraphLiveActive = false;
                if (dock->m_d->customGraphLiveTimer)
                    dock->m_d->customGraphLiveTimer->stop();
            }
            dock->m_d->generate.btnGenerate->setEnabled(true);
            dock->resetProgressBarToIdle();
            dock->m_d->batchCountTarget = 0;
            dock->m_d->isFullAnimationBatch = false;
            dock->m_d->animationBatchPromptIdToIndex.clear();
            dock->m_d->animationBatchSourcePathByFrame.clear();
            dock->m_d->animationBatchFrameTimes.clear();
            dock->m_d->animationBatchGroupId.clear();
            dock->m_d->batchNeedsPerFrameReference = false;
            dock->m_d->batchNeedsPerFrameAnimationRefine = false;
            clearBatchCaptureStash(dock->m_d.data());
        };
        // FAITHFUL_PORT: dump the raw body (truncated) so the actual ComfyUI
        // rejection is visible in `adb logcat` even if the human-readable
        // extractor fails to find a recognised shape.
        {
            const QByteArray preview = result.responseBody.left(800);
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "slotBatchSubmitNext REPLY body preview (utf8, truncated to 800B): "
                << QString::fromUtf8(preview);
        }
        if (!result.ok) {
            dock->setStatusMessage(ComfyTr::tr("Submit error: %1", result.errorMessage), true);
            abortBatchState();
            return;
        }
        const QString promptId = result.promptId;
        if (promptId != expectedPromptId) {
            dock->setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            abortBatchState();
            return;
        }
        if (dock->m_d->isFullAnimationBatch) {
            int fileFrame = submitIndex;
            if (submitIndex >= 0 && submitIndex < dock->m_d->animationBatchFrameTimes.size())
                fileFrame = dock->m_d->animationBatchFrameTimes.at(submitIndex);
            dock->m_d->animationBatchPromptIdToIndex.insert(promptId, fileFrame);
        }
        ComfyUIRemoteDock::Private::HistoryEntry entry;
        entry.prompt = dock->m_d->generate.editPrompt->toPlainText();
        entry.negative = dock->m_d->generate.editNegative->toPlainText();
        entry.checkpoint = dock->m_d->generate.comboCheckpoint->currentText();
        entry.styleName = dock->m_d->generate.comboPreset ? dock->m_d->generate.comboPreset->currentText() : QString();
        entry.width = dock->m_d->generate.spinWidth->value();
        entry.height = dock->m_d->generate.spinHeight->value();
        entry.steps = dock->m_d->generate.spinSteps->value();
        entry.cfg = dock->m_d->generate.spinCfg->value();
        entry.strength = dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100;
        entry.samplerName = dock->m_d->generate.comboSampler->currentText().trimmed();
        // §13.212: Batch seed = base + index * settings.batch_size; wildcards were re-evaluated per job in workflow build
        const int batchSize = qMax(1, dock->m_d->batchSeedStep);
        entry.seed = dock->m_d->batchBaseSeed + submitIndex * batchSize;
        // §13.74: capture timeline frame for Single Frame mismatch warning after generation
        if (dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3 && dock->m_d->radioSingleFrame
            && dock->m_d->radioSingleFrame->isChecked() && !dock->m_d->isFullAnimationBatch && dock->m_d->viewManager) {
            KisImageSP img = dock->m_d->viewManager->image();
            if (img && img->animationInterface() && img->animationInterface()->hasAnimation())
                entry.animationSubmitTime = img->animationInterface()->currentTime();
        }
        if (dock->m_d->batchUseCustomWorkflow) {
            entry.customWorkflowMetadata = dock->m_d->lastCustomWorkflowExpandState.promptMetadata;
            entry.contextBounds = dock->m_d->lastCustomWorkflowExpandState.captureBounds;
            entry.hasMask = dock->m_d->lastCustomWorkflowExpandState.hasSelectionMask;
            entry.inpaintMode = QStringLiteral("fill");
        }
        applyHistoryCaptureStashToEntry(dock->m_d.data(), &entry);
        dock->m_d->history.pendingHistoryByPromptId.insert(promptId, entry);
        dock->m_d->batchCollectIds.append(promptId);
        dock->m_d->batchSubmitIndex++;
        if (dock->m_d->batchSubmitIndex >= dock->m_d->batchCountTarget)
            clearBatchCaptureStash(dock->m_d.data());
        onBatchSubmitNext(dock);
    });

}

} // namespace ComfyGenerateRunner
