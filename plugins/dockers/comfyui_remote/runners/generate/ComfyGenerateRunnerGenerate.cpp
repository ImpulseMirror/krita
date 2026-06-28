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

void onGenerate(ComfyUIRemoteDock *dock)
{
    dock->commitPromptEditorsFromUi();
    if (dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 4 && dock->m_d->checkCustomGraphLive)
        dock->m_d->customGraphLiveActive = dock->m_d->checkCustomGraphLive->isChecked();
    // FAITHFUL_PORT/DEBUG: snapshot every decision input at the top so logcat
    // shows exactly which precondition tripped when "nothing happens" on click.
    const QString dbgUrl = dock->m_d->editServerUrl ? dock->m_d->editServerUrl->text().trimmed() : QStringLiteral("<null editServerUrl>");
    const int dbgStrength = dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : -1;
    const bool dbgEditMode = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    const int dbgWorkspace = dock->m_d->comboWorkspace ? dock->m_d->comboWorkspace->currentIndex() : -1;
    const int dbgCustomLen = dock->m_d->editCustomWorkflow ? dock->m_d->editCustomWorkflow->toPlainText().trimmed().size() : -1;
    const bool dbgHasImage = dock->m_d->viewManager && dock->m_d->viewManager->image();
    const bool dbgBtnEnabled = dock->m_d->generate.btnGenerate && dock->m_d->generate.btnGenerate->isEnabled();
    const int dbgQueueDepth = dock->m_d->jobQueue.size();
    const QString dbgCurrent = dock->m_d->currentPromptId;
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotGenerate ENTER url=" << dbgUrl
        << " strength=" << dbgStrength
        << " editMode=" << dbgEditMode
        << " workspace=" << dbgWorkspace
        << " customWorkflowLen=" << dbgCustomLen
        << " hasImage=" << dbgHasImage
        << " btnGenerateEnabled=" << dbgBtnEnabled
        << " queueDepth=" << dbgQueueDepth
        << " currentPromptId=" << dbgCurrent;

    // FAITHFUL_PORT/CRASH-FREE FIX #3: recover from a stuck-disabled Generate
    // button. Previously, if a previous upload reply was dropped mid-flight
    // (e.g. the device sleeping during canvas upload to an unreachable
    // 127.0.0.1:8188), the button stayed disabled forever and every subsequent
    // click was eaten by Qt with no log / no status. The compact UI hides the
    // disabled state, so the user only sees "nothing happens". Detect the
    // wedge — disabled button with no current prompt and no jobs queued — and
    // forcibly re-enable before processing the click.
    if (dock->m_d->generate.btnGenerate && !dock->m_d->generate.btnGenerate->isEnabled()
        && dock->m_d->currentPromptId.isEmpty() && dock->m_d->jobQueue.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotGenerate: btnGenerate was stuck-disabled with no in-flight job; re-enabling";
        dock->m_d->generate.btnGenerate->setEnabled(true);
    }

    QString urlStr = dock->m_d->editServerUrl ? dock->m_d->editServerUrl->text().trimmed() : QString();
    if (urlStr.isEmpty()) {
        dock->setStatusMessage(ComfyTr::tr("Enter a server URL in Settings → Connection."), true);
        return;
    }

    dock->syncCheckpointComboFromStyle();
    const QString resolvedCheckpoint = dock->checkpointForGenerate();
    qCWarning(KIS_COMFYUI_REMOTE).nospace() << "slotGenerate checkpoint=" << resolvedCheckpoint;

    // FAITHFUL_PORT: when the Size row is hidden (compact / Android view) the
    // user never sees the W/H spinners, so they keep their construction-time
    // defaults of 512×512 and every generation comes out at 512×512 regardless
    // of how big the document is. Upstream krita-ai-diffusion derives the
    // generation extent from the document bounds (or the selection bounds if
    // one exists). Mirror that here just-in-time so the workflow build path
    // below picks up the correct dimensions via spinWidth / spinHeight.
    if (dock->m_d->viewManager && dock->m_d->viewManager->image()
        && dock->m_d->generate.sizeRowWidget && !dock->m_d->generate.sizeRowWidget->isVisible()) {
        KisImageSP img = dock->m_d->viewManager->image();
        QRect targetRect = img->bounds();
        if (KisSelectionSP sel = dock->m_d->viewManager->selection()) {
            if (auto ps = sel->pixelSelection()) {
                const QRect selRect = ps->selectedExactRect().intersected(img->bounds());
                if (!selRect.isEmpty() && selRect.size() != img->bounds().size())
                    targetRect = selRect;
            }
        }
        const int tw = qBound(64, targetRect.width(), 8192);
        const int th = qBound(64, targetRect.height(), 8192);
        if (dock->m_d->generate.spinWidth && dock->m_d->generate.spinWidth->value() != tw)
            dock->m_d->generate.spinWidth->setValue(tw);
        if (dock->m_d->generate.spinHeight && dock->m_d->generate.spinHeight->value() != th)
            dock->m_d->generate.spinHeight->setValue(th);
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotGenerate: compact-UI size override w=" << tw << " h=" << th
            << " docBounds=" << img->bounds() << " targetRect=" << targetRect;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        dock->setStatusMessage(ComfyTr::tr("Invalid server URL: %1", urlStr), true);
        return;
    }
    // FAITHFUL_PORT: on Android the device-local 127.0.0.1 is the tablet
    // itself, not the dev machine. Catch the common misconfiguration here
    // instead of letting the request silently time out and look like
    // "nothing happens".
    if (baseUrl.host() == QLatin1String("127.0.0.1") || baseUrl.host() == QLatin1String("localhost")) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotGenerate: server URL points at device loopback" << baseUrl.toString();
        dock->setStatusMessage(
            ComfyTr::tr("Server URL is localhost (%1) — this is the tablet itself. Use your computer's LAN IP in Settings → Connection.",
                        baseUrl.host()), true);
        return;
    }
    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        dock->setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    const bool graphWorkspace = dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 4;
    if (graphWorkspace) {
        const QString graphJson = dock->m_d->editCustomWorkflow ? dock->m_d->editCustomWorkflow->toPlainText().trimmed() : QString();
        if (graphJson.isEmpty()) {
            dock->setStatusMessage(
                ComfyTr::tr("Graph workspace: paste workflow JSON in Settings → Workflow, then click Generate."), true);
            return;
        }
    }
    auto colorCheck = ComfyUIUtils::checkColorMode(dock->m_d->viewManager->image());
    if (!colorCheck.first) {
        dock->setStatusMessage(colorCheck.second, true);
        return;
    }

    const QJsonObject settingsRoot = ComfyUIUtils::loadSettingsJson();
    int genBatch = dock->m_d->generate.spinBatchCount ? dock->m_d->generate.spinBatchCount->value() : 1;
    double genMul = dock->m_d->generate.resolutionMultiplier <= 0.0 ? 1.0 : dock->m_d->generate.resolutionMultiplier;
    ComfyUIUtils::generationPerformanceBatchResolution(settingsRoot, dock->m_d->lastComfySystemStats, genBatch, genMul,
                                                       &genBatch, &genMul);
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(settingsRoot, &genMul);
    const int genBatchMax = dock->m_d->generate.spinBatchCount ? dock->m_d->generate.spinBatchCount->maximum() : 16;
    genBatch = qBound(1, genBatch, genBatchMax);
    genMul = qMax(0.3, qMin(genMul <= 0.0 ? 1.0 : genMul, 3.0));

    QString customJson = dock->m_d->editCustomWorkflow->toPlainText().trimmed();
    if (!customJson.isEmpty() && dock->m_d->live.checkUseReferenceImage->isChecked()) {
        // §13.74: Full Animation — reference must be the canvas at each timeline frame (not a single pre-batch upload).
        if (dock->m_d->isFullAnimationBatch) {
            if (!customJson.contains(QLatin1String("REFERENCE_IMAGE"))) {
                dock->setStatusMessage(
                    ComfyTr::tr("Custom workflow must contain REFERENCE_IMAGE when using reference with Full Animation."), true);
                return;
            }
            QJsonParseError err;
            QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(customJson.toUtf8());
            QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                dock->setStatusMessage(ComfyTr::tr("Custom workflow JSON error: %1", err.errorString()), true);
                return;
            }
            QJsonObject workflow = doc.object();
            if (!dock->tryResolveCustomWorkflowInPlace(&workflow))
                return;
            auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
            if (!validation.first) {
                dock->setStatusMessage(validation.second, true);
                return;
            }
            if (!validateCustomWorkflowGraphOrShowError(dock, dock->m_d.data(), workflow))
                return;
            {
                KisImageSP wfImage = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
                ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, dock->m_d->customWorkflowParamOverrides, wfImage);
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            dock->m_d->batchNeedsPerFrameReference = true;

            const int batchCount = genBatch;
            int effW = dock->m_d->generate.spinWidth->value();
            int effH = dock->m_d->generate.spinHeight->value();
            double mul = genMul;
            effW = qBound(64, static_cast<int>(effW * mul), 8192);
            effH = qBound(64, static_cast<int>(effH * mul), 8192);
            ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
            int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
            if (dock->m_d->isFullAnimationBatch && batchCount > 0)
                effectiveBatch = batchCount;
            dock->m_d->batchSeedStep = qMax(1, batchCount); // §13.212: step = settings.batch_size, not capped job count
            int queueMode = takeGenerateQueueMode(dock->m_d.data());
            if (dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3)
                queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False (no replace/front semantics)
            if (queueMode == 2) {
                dock->m_d->pollTimer->stop();
                for (const QString &id : dock->m_d->jobQueue) dock->m_d->history.pendingHistoryByPromptId.remove(id);
                if (!dock->m_d->currentPromptId.isEmpty()) dock->m_d->history.pendingHistoryByPromptId.remove(dock->m_d->currentPromptId);
                dock->m_d->jobQueue.clear();
                dock->m_d->currentPromptId.clear();
                QUrl interruptUrl(baseUrl);
                QString ip = interruptUrl.path();
                if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
                else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
                else interruptUrl.setPath(ip + "interrupt");
                QNetworkRequest reqInt(interruptUrl);
                ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
                dock->m_d->nam->post(reqInt, QByteArray("{}"));
            }
            dock->m_d->batchCollectIds.clear();
            dock->m_d->batchSubmitIndex = 0;
            dock->m_d->batchCountTarget = qMax(1, effectiveBatch);
            dock->m_d->batchQueueMode = queueMode;
            dock->m_d->batchBaseUrl = baseUrl;
            QString path = baseUrl.path();
            if (path.isEmpty() || path == "/") dock->m_d->batchBaseUrl.setPath("/prompt");
            else if (!path.endsWith('/')) dock->m_d->batchBaseUrl.setPath(path + "/prompt");
            else dock->m_d->batchBaseUrl.setPath(path + "prompt");
            dock->m_d->batchUseCustomWorkflow = true;
            dock->m_d->batchCustomWorkflow = workflow;
            dock->m_d->batchBaseSeed = dock->m_d->generate.checkFixedSeed->isChecked()
                ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!dock->m_d->generate.checkFixedSeed->isChecked())
                dock->m_d->generate.spinSeed->setValue(static_cast<int>(dock->m_d->batchBaseSeed));
            if (dock->m_d->clientId.isEmpty())
                dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

            dock->m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
            dock->m_d->progressBar->setValue(0);
            dock->m_d->generate.btnGenerate->setEnabled(false);
            onBatchSubmitNext(dock);
            return;
        }

        KisImageSP image = dock->m_d->viewManager->image();
        const QList<KisNodeSP> excludeNodes =
            ComfyUIUtils::collectInpaintExcludeNodes(image, true, dock->m_d->rootControlLayers, dock->m_d->previewLayerId);
        const auto docCapture = ComfyUIUtils::getDocumentImage(image, QRect(), excludeNodes);
        if (!docCapture) {
            dock->setStatusMessage(docCapture.errorMessage.isEmpty()
                                 ? ComfyTr::tr("Could not export canvas for reference.")
                                 : docCapture.errorMessage,
                             true);
            return;
        }
        QImage refImg = docCapture.image;
        QTemporaryFile *tmp = new QTemporaryFile(dock);
        tmp->setFileTemplate(tmp->fileTemplate() + ".png");
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            dock->setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
            return;
        }
        dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading reference image…"));
        dock->setProgressBarKind(true);  // §13.18
        dock->m_d->generate.btnGenerate->setEnabled(false);
        QUrl uploadUrl(urlStr);
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == "/") uploadUrl.setPath("/upload/image");
        else if (!up.endsWith('/')) uploadUrl.setPath(up + "/upload/image");
        else uploadUrl.setPath(up + "upload/image");
        tmp->open();
        // §13.139: Images sent via upload path (not inline base64 in graph); node inputs receive server filename
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"image\"; filename=\"reference.png\""));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = dock->m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        QObject::connect(replyUp, &QNetworkReply::finished, dock, [dock, replyUp, urlStr, baseUrl, genBatch, genMul]() {
            replyUp->deleteLater();
            dock->setProgressBarKind(false);  // §13.18: upload finished
            if (replyUp->error() != QNetworkReply::NoError) {
                dock->setStatusMessage(ComfyTr::tr("Upload error: %1", replyUp->errorString()), true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value("name").toString();
            if (refName.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            QString workflowText = dock->m_d->editCustomWorkflow->toPlainText().replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(workflowText.toUtf8());
            QJsonDocument wdoc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !wdoc.isObject()) {
                dock->setStatusMessage(ComfyTr::tr("Workflow JSON error after reference replace."), true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            QJsonObject workflow = wdoc.object();
            if (!dock->tryResolveCustomWorkflowInPlace(&workflow)) {
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            // §13.103: At most one ETN_KritaStyleAndPrompt node
            auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
            if (!validation.first) {
                dock->setStatusMessage(validation.second, true);
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            if (!validateCustomWorkflowGraphOrShowError(dock, dock->m_d.data(), workflow)) {
                dock->m_d->generate.btnGenerate->setEnabled(true);
                return;
            }
            {
                KisImageSP wfImage = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
                ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, dock->m_d->customWorkflowParamOverrides, wfImage);
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            const int batchCount = genBatch;
            int effW = dock->m_d->generate.spinWidth->value();
            int effH = dock->m_d->generate.spinHeight->value();
            double mul = genMul;
            effW = qBound(64, static_cast<int>(effW * mul), 8192);
            effH = qBound(64, static_cast<int>(effH * mul), 8192);
            ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
            int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
            if (dock->m_d->isFullAnimationBatch && batchCount > 0)
                effectiveBatch = batchCount; // §13.74: one ComfyUI prompt per timeline frame
            dock->m_d->batchSeedStep = qMax(1, batchCount); // §13.212
            int queueMode = takeGenerateQueueMode(dock->m_d.data());
            if (dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3)
                queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False
            if (queueMode == 2) {
                dock->m_d->pollTimer->stop();
                for (const QString &id : dock->m_d->jobQueue) dock->m_d->history.pendingHistoryByPromptId.remove(id);
                if (!dock->m_d->currentPromptId.isEmpty()) dock->m_d->history.pendingHistoryByPromptId.remove(dock->m_d->currentPromptId);
                dock->m_d->jobQueue.clear();
                dock->m_d->currentPromptId.clear();
                QUrl interruptUrl(baseUrl);
                QString ip = interruptUrl.path();
                if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
                else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
                else interruptUrl.setPath(ip + "interrupt");
                QNetworkRequest reqInt(interruptUrl);
                ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
                dock->m_d->nam->post(reqInt, QByteArray("{}"));
            }
            dock->m_d->batchCollectIds.clear();
            dock->m_d->batchSubmitIndex = 0;
            dock->m_d->batchCountTarget = qMax(1, effectiveBatch);
            dock->m_d->batchQueueMode = queueMode;
            dock->m_d->batchBaseUrl = baseUrl;
            QString path = baseUrl.path();
            if (path.isEmpty() || path == "/") dock->m_d->batchBaseUrl.setPath("/prompt");
            else if (!path.endsWith('/')) dock->m_d->batchBaseUrl.setPath(path + "/prompt");
            else dock->m_d->batchBaseUrl.setPath(path + "prompt");
            dock->m_d->batchUseCustomWorkflow = true;
            dock->m_d->batchCustomWorkflow = workflow;
            dock->m_d->batchNeedsPerFrameReference = false;
            dock->m_d->batchBaseSeed = dock->m_d->generate.checkFixedSeed->isChecked()
                ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!dock->m_d->generate.checkFixedSeed->isChecked())
                dock->m_d->generate.spinSeed->setValue(static_cast<int>(dock->m_d->batchBaseSeed));
            if (dock->m_d->clientId.isEmpty())
                dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            dock->m_d->progressBar->setValue(0);
            onBatchSubmitNext(dock);
        });
        return;
    }

    // P4/D7: built-in Animation workspace — per-frame batch (buildAnimationFrame or buildRefine)
    if (customJson.isEmpty() && dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3) {
        if (dock->m_d->isFullAnimationBatch) {
            const QString docPath = (dock->m_d->canvas && dock->m_d->canvas->imageView() && dock->m_d->canvas->imageView()->document())
                ? dock->m_d->canvas->imageView()->document()->path()
                : QString();
            if (docPath.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Save the document first to generate a full animation batch."), true);
                return;
            }
        }
        const int batchCount = genBatch;
        int effW = dock->m_d->generate.spinWidth->value();
        int effH = dock->m_d->generate.spinHeight->value();
        double mul = genMul;
        effW = qBound(64, static_cast<int>(effW * mul), 8192);
        effH = qBound(64, static_cast<int>(effH * mul), 8192);
        ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
        int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
        if (dock->m_d->isFullAnimationBatch && batchCount > 0)
            effectiveBatch = batchCount;
        dock->m_d->batchSeedStep = qMax(1, batchCount);

        int queueMode = takeGenerateQueueMode(dock->m_d.data());
        queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False
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
        dock->m_d->batchNeedsPerFrameAnimationRefine = animationRequiresCanvasImage(dock->m_d.data());
        dock->m_d->batchUseCustomWorkflow = false;
        dock->m_d->batchBaseSeed = dock->m_d->generate.checkFixedSeed->isChecked()
            ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
            : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        if (!dock->m_d->generate.checkFixedSeed->isChecked())
            dock->m_d->generate.spinSeed->setValue(static_cast<int>(dock->m_d->batchBaseSeed));
        if (dock->m_d->clientId.isEmpty())
            dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        dock->m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
        dock->m_d->progressBar->setValue(0);
        dock->m_d->generate.btnGenerate->setEnabled(false);
        onBatchSubmitNext(dock);
        return;
    }

    QJsonObject workflow;
    if (!customJson.isEmpty()) {
        QJsonParseError err;
        // §13.135: Strip // line comments for config/workflow JSON (not #)
        QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(customJson.toUtf8());
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            dock->setStatusMessage(ComfyTr::tr("Custom workflow JSON error: %1", err.errorString()), true);
            return;
        }
        workflow = doc.object();
        // §13.101: UI workflow (nodes/links) → API via object_info, or pass through API JSON
        if (!dock->tryResolveCustomWorkflowInPlace(&workflow))
            return;
        // §13.103: At most one ETN_KritaStyleAndPrompt node
        auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
        if (!validation.first) {
            dock->setStatusMessage(validation.second, true);
            return;
        }
        if (!validateCustomWorkflowGraphOrShowError(dock, dock->m_d.data(), workflow))
            return;
        {
            KisImageSP wfImage = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
            ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, dock->m_d->customWorkflowParamOverrides, wfImage);
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    } else {
        if (graphWorkspace) {
            dock->setStatusMessage(
                ComfyTr::tr("Graph workspace requires a custom workflow JSON in Settings → Workflow."), true);
            return;
        }
        if (tryStartRefineFromGenerate(dock)) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotGenerate: tryStartRefineFromGenerate took over (Refine path); returning";
            return;
        }
        qint64 seed = dock->m_d->generate.checkFixedSeed->isChecked()
            ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
            : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        if (!dock->m_d->generate.checkFixedSeed->isChecked()) {
            dock->m_d->generate.spinSeed->setValue(static_cast<int>(seed));
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
            resolvedCheckpoint,
            dock->m_d->generate.spinSteps->value(),
            dock->m_d->generate.spinCfg->value(),
            (dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100) / 100.0,
            dock->m_d->generate.comboSampler->currentText().trimmed(),
            dock->m_d->generateRt.ksamplerScheduler);

        QString styleArch;
        if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
            const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                styleArch = st->architecture;
        }

        ComfyWorkflowEngine::TextToImageParams genParams;
        genParams.checkpoint = link.checkpoint;
        genParams.arch =
            ComfyWorkflowEngine::resolveArch(link.checkpoint, styleArch);
        genParams.seed = seed;
        genParams.steps = link.steps;
        genParams.cfg = link.cfg;
        genParams.denoise = link.denoise;
        genParams.sampler = link.sampler;
        genParams.scheduler = link.scheduler;
        int w = dock->m_d->generate.spinWidth->value();
        int h = dock->m_d->generate.spinHeight->value();
        w = static_cast<int>(w * genMul);
        h = static_cast<int>(h * genMul);
        w = qBound(64, w, 8192);
        h = qBound(64, h, 8192);
        ComfyUIUtils::clampExtentToMaxMegapixels(&w, &h);
        genParams.width = w;
        genParams.height = h;
        genParams.batchSize = 1;
        genParams.layerCount = dock->m_d->generate.spinLayerCount ? dock->m_d->generate.spinLayerCount->value() : 1;
        if (genParams.arch == ComfyResources::Arch::QwenL) {
            genParams.denoise = 1.0;
            dock->m_d->generateRt.generatePendingLayerCount = qMax(1, genParams.layerCount);
        } else {
            dock->m_d->generateRt.generatePendingLayerCount = 1;
        }

        QString userPos = ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed();
        const QList<ComfyUIRemoteDock::Private::RegionEntry> regsForGen = regionsForGenerate(dock->m_d.data());
        QString promptText = link.active
            ? ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, userPos).trimmed()
            : userPos;
        promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
        ComfyUIUtils::extractLayerPlaceholders(promptText);
        genParams.styleLoras = dock->currentStyleLoras();
        genParams.positivePrompt =
            ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(promptText, genParams.styleLoras);

        KisImageSP genImage = dock->m_d->viewManager->image();
        const ComfyRegionProcess::ProcessRegionsResult processed =
            ComfyRegionProcess::processRegions(regsForGen, genImage, dock->m_d->viewManager, genParams.positivePrompt);
        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion)
            genParams.positivePrompt = processed.effectivePositive;
        const QString negSrc =
            link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative->toPlainText()).trimmed();
        genParams.negativePrompt = ComfyUIUtils::evalWildcards(negSrc, static_cast<quint32>(seed & 0xFFFFFFFFu));
        genParams.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();

        workflow = ComfyWorkflowEngine::buildTextToImage(genParams);
        if (workflow.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Workflow JSON error."), true);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

        dock->m_d->generateRt.generatePendingBaseWorkflow = workflow;
        dock->m_d->generateRt.generatePendingArch = genParams.arch;
        dock->m_d->generateRt.generateStashedCustomJson = customJson;
        dock->m_d->generateRt.generateStashedBatch = genBatch;
        dock->m_d->generateRt.generateStashedMul = genMul;

        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion
            && ComfyResources::supportsRegions(genParams.arch)) {
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "slotGenerate: MultiRegion path, regions=" << processed.regions.size()
                << " arch=" << static_cast<int>(genParams.arch);
            dock->m_d->generateRt.generateProcessedRegions = processed.regions;
            dock->m_d->generateRt.generateRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
                dock->m_d->generateRt.generateProcessedRegions, genParams.promptTranslationLanguage);
            dock->m_d->generateRt.generateAwaitingRegionMaskUploads = true;
            dock->m_d->generateRt.generateRegionMaskUploadIndex = 0;
            dock->m_d->generate.btnGenerate->setEnabled(false);
            uploadNextRegionMask(dock);
            return;
        }

        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotGenerate: SingleRegion / no-region path, dispatching upload pipeline w=" << genParams.width
            << " h=" << genParams.height
            << " steps=" << genParams.steps
            << " arch=" << static_cast<int>(genParams.arch)
            << " posLen=" << genParams.positivePrompt.size()
            << " negLen=" << genParams.negativePrompt.size();
        dock->m_d->generate.btnGenerate->setEnabled(false);
        beginUploadPipeline(dock);
        return;
    }

    const int batchCount = genBatch;
    // §13.214: Effective batch from extent (constrain latent samples by capacity)
    int effW = dock->m_d->generate.spinWidth->value();
    int effH = dock->m_d->generate.spinHeight->value();
    double mul = genMul;
    effW = qBound(64, static_cast<int>(effW * mul), 8192);
    effH = qBound(64, static_cast<int>(effH * mul), 8192);
    ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
    int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
    if (dock->m_d->isFullAnimationBatch && batchCount > 0)
        effectiveBatch = batchCount; // §13.74: one ComfyUI prompt per timeline frame
    if (dock->m_d->customGraphLiveActive && dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 4)
        effectiveBatch = 1;
    dock->m_d->batchSeedStep = qMax(1, batchCount); // §13.212: +i * settings.batch_size (performance batch), not effectiveBatch

    int queueMode = takeGenerateQueueMode(dock->m_d.data());
    if (dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3)
        queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False
    if (queueMode == 2) { // Replace
        dock->m_d->pollTimer->stop();
        for (const QString &id : dock->m_d->jobQueue)
            dock->m_d->history.pendingHistoryByPromptId.remove(id);
        if (!dock->m_d->currentPromptId.isEmpty())
            dock->m_d->history.pendingHistoryByPromptId.remove(dock->m_d->currentPromptId);
        dock->m_d->jobQueue.clear();
        dock->m_d->currentPromptId.clear();
        QUrl interruptUrl(baseUrl);
        QString ip = interruptUrl.path();
        if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
        else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
        else interruptUrl.setPath(ip + "interrupt");
        QNetworkRequest reqI(interruptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqI);
        reqI.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        dock->m_d->nam->post(reqI, QByteArray("{}"));
    }

    dock->m_d->batchCollectIds.clear();
    dock->m_d->batchSubmitIndex = 0;
    dock->m_d->batchCountTarget = qMax(1, effectiveBatch);
    dock->m_d->batchQueueMode = queueMode;
    dock->m_d->batchBaseUrl = baseUrl;
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") dock->m_d->batchBaseUrl.setPath("/prompt");
    else if (!path.endsWith('/')) dock->m_d->batchBaseUrl.setPath(path + "/prompt");
    else dock->m_d->batchBaseUrl.setPath(path + "prompt");
    dock->m_d->batchNeedsPerFrameReference = false;
    dock->m_d->batchNeedsPerFrameAnimationRefine = false;
    dock->m_d->batchUseCustomWorkflow = !customJson.isEmpty();
    if (dock->m_d->batchUseCustomWorkflow)
        dock->m_d->batchCustomWorkflow = workflow;
    // §13.212: Set base seed once per batch (seed_i = base + i * batch_size); for non-custom, wildcards re-evaluated per job in slotBatchSubmitNext
    dock->m_d->batchBaseSeed = dock->m_d->generate.checkFixedSeed->isChecked()
        ? static_cast<qint64>(dock->m_d->generate.spinSeed->value())
        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    if (!dock->m_d->generate.checkFixedSeed->isChecked())
        dock->m_d->generate.spinSeed->setValue(static_cast<int>(dock->m_d->batchBaseSeed));
    if (dock->m_d->clientId.isEmpty())
        dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotGenerate: dispatching batch submitIndex=0/" << dock->m_d->batchCountTarget
        << " queueMode=" << dock->m_d->batchQueueMode
        << " baseUrl=" << dock->m_d->batchBaseUrl.toString()
        << " useCustomWorkflow=" << dock->m_d->batchUseCustomWorkflow;
    dock->m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
    dock->m_d->progressBar->setValue(0);
    dock->m_d->generate.btnGenerate->setEnabled(false);
    onBatchSubmitNext(dock);
}


} // namespace ComfyGenerateRunner
