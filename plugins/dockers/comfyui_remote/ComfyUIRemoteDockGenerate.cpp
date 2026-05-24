/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyStyleCollection.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyControlLayer.h"
#include "ComfyUIUtils.h"
#include "ComfyUIWorkflows.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QUuid>
#include <QRandomGenerator>
#include <klocalizedstring.h>
#include <kis_icon_utils.h>
#include <kis_types.h>
#include <kis_image.h>
#include <KisViewManager.h>
#include <KisDocument.h>
#include <kis_selection.h>
#include <kis_image_animation_interface.h>
#include <kis_time_span.h>
#include <KoUpdater.h>
#include <kis_animation_importer.h>

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QCoreApplication>
#include <QImage>
#include <QPixmap>
#include <QLabel>
#include <QUrlQuery>
#include <QSignalBlocker>

#include <KSharedConfig>
#include <KConfigGroup>

#include "ComfyUIIntervalSlider.h"
#include "ComfyUIPoseLayers.h"

#include <kis_layer.h>
#include <kis_shape_layer.h>

#include <algorithm>

// §13.126: End-to-end flow — user action → workflow build (check_color_mode) → queue per QueueMode → POST prompt → poll result → history + UI → Apply
void ComfyUIRemoteDock::slotGenerate()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(i18n("Enter a server URL."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        setStatusMessage(i18n("Invalid URL."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(i18n("Open a document first."), true);
        return;
    }
    auto colorCheck = ComfyUIUtils::checkColorMode(m_d->viewManager->image());
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        return;
    }

    const QJsonObject settingsRoot = ComfyUIUtils::loadSettingsJson();
    int genBatch = m_d->spinBatchCount ? m_d->spinBatchCount->value() : 1;
    double genMul = m_d->resolutionMultiplier <= 0.0 ? 1.0 : m_d->resolutionMultiplier;
    ComfyUIUtils::generationPerformanceBatchResolution(settingsRoot, m_d->lastComfySystemStats, genBatch, genMul,
                                                       &genBatch, &genMul);
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(settingsRoot, &genMul);
    const int genBatchMax = m_d->spinBatchCount ? m_d->spinBatchCount->maximum() : 16;
    genBatch = qBound(1, genBatch, genBatchMax);
    genMul = qMax(0.3, qMin(genMul <= 0.0 ? 1.0 : genMul, 3.0));

    QString customJson = m_d->editCustomWorkflow->toPlainText().trimmed();
    if (!customJson.isEmpty() && m_d->checkUseReferenceImage->isChecked()) {
        // §13.74: Full Animation — reference must be the canvas at each timeline frame (not a single pre-batch upload).
        if (m_d->isFullAnimationBatch) {
            if (!customJson.contains(QLatin1String("REFERENCE_IMAGE"))) {
                setStatusMessage(
                    i18n("Custom workflow must contain REFERENCE_IMAGE when using reference with Full Animation."), true);
                return;
            }
            QJsonParseError err;
            QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(customJson.toUtf8());
            QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                setStatusMessage(i18n("Custom workflow JSON error: %1", err.errorString()), true);
                return;
            }
            QJsonObject workflow = doc.object();
            if (!tryResolveCustomWorkflowInPlace(&workflow))
                return;
            auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
            if (!validation.first) {
                setStatusMessage(validation.second, true);
                return;
            }
            {
                KisImageSP wfImage = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
                ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, m_d->customWorkflowParamOverrides, wfImage);
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            m_d->batchNeedsPerFrameReference = true;

            const int batchCount = genBatch;
            int effW = m_d->spinWidth->value();
            int effH = m_d->spinHeight->value();
            double mul = genMul;
            effW = qBound(64, static_cast<int>(effW * mul), 8192);
            effH = qBound(64, static_cast<int>(effH * mul), 8192);
            ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
            int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
            if (m_d->isFullAnimationBatch && batchCount > 0)
                effectiveBatch = batchCount;
            m_d->batchSeedStep = qMax(1, batchCount); // §13.212: step = settings.batch_size, not capped job count
            int queueMode = m_d->comboQueueMode->currentData().toInt();
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3)
                queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False (no replace/front semantics)
            if (queueMode == 2) {
                m_d->pollTimer->stop();
                for (const QString &id : m_d->jobQueue) m_d->pendingHistoryByPromptId.remove(id);
                if (!m_d->currentPromptId.isEmpty()) m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
                m_d->jobQueue.clear();
                m_d->currentPromptId.clear();
                QUrl interruptUrl(baseUrl);
                QString ip = interruptUrl.path();
                if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
                else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
                else interruptUrl.setPath(ip + "interrupt");
                QNetworkRequest reqInt(interruptUrl);
                ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
                m_d->nam->post(reqInt, QByteArray("{}"));
            }
            m_d->batchCollectIds.clear();
            m_d->batchSubmitIndex = 0;
            m_d->batchCountTarget = qMax(1, effectiveBatch);
            m_d->batchQueueMode = queueMode;
            m_d->batchBaseUrl = baseUrl;
            QString path = baseUrl.path();
            if (path.isEmpty() || path == "/") m_d->batchBaseUrl.setPath("/prompt");
            else if (!path.endsWith('/')) m_d->batchBaseUrl.setPath(path + "/prompt");
            else m_d->batchBaseUrl.setPath(path + "prompt");
            m_d->batchUseCustomWorkflow = true;
            m_d->batchCustomWorkflow = workflow;
            m_d->batchBaseSeed = m_d->checkFixedSeed->isChecked()
                ? static_cast<qint64>(m_d->spinSeed->value())
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!m_d->checkFixedSeed->isChecked())
                m_d->spinSeed->setValue(static_cast<int>(m_d->batchBaseSeed));
            if (m_d->clientId.isEmpty())
                m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

            m_d->labelStatus->setText(i18n("Submitting…"));
            m_d->progressBar->setValue(0);
            m_d->btnGenerate->setEnabled(false);
            slotBatchSubmitNext();
            return;
        }

        KisImageSP image = m_d->viewManager->image();
        QImage refImg = ComfyUIUtils::getCanvasAsQImage(image);
        if (refImg.isNull()) {
            setStatusMessage(i18n("Could not export canvas for reference."), true);
            return;
        }
        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + ".png");
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            setStatusMessage(i18n("Could not save temp image."), true);
            return;
        }
        m_d->labelStatus->setText(i18n("Uploading reference image…"));
        setProgressBarKind(true);  // §13.18
        m_d->btnGenerate->setEnabled(false);
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
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp, urlStr, baseUrl, genBatch, genMul]() {
            replyUp->deleteLater();
            setProgressBarKind(false);  // §13.18: upload finished
            if (replyUp->error() != QNetworkReply::NoError) {
                setStatusMessage(i18n("Upload error: %1", replyUp->errorString()), true);
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value("name").toString();
            if (refName.isEmpty()) {
                setStatusMessage(i18n("Server did not return image name."), true);
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QString workflowText = m_d->editCustomWorkflow->toPlainText().replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(workflowText.toUtf8());
            QJsonDocument wdoc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !wdoc.isObject()) {
                setStatusMessage(i18n("Workflow JSON error after reference replace."), true);
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QJsonObject workflow = wdoc.object();
            if (!tryResolveCustomWorkflowInPlace(&workflow)) {
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            // §13.103: At most one ETN_KritaStyleAndPrompt node
            auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
            if (!validation.first) {
                setStatusMessage(validation.second, true);
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            {
                KisImageSP wfImage = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
                ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, m_d->customWorkflowParamOverrides, wfImage);
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            const int batchCount = genBatch;
            int effW = m_d->spinWidth->value();
            int effH = m_d->spinHeight->value();
            double mul = genMul;
            effW = qBound(64, static_cast<int>(effW * mul), 8192);
            effH = qBound(64, static_cast<int>(effH * mul), 8192);
            ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
            int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
            if (m_d->isFullAnimationBatch && batchCount > 0)
                effectiveBatch = batchCount; // §13.74: one ComfyUI prompt per timeline frame
            m_d->batchSeedStep = qMax(1, batchCount); // §13.212
            int queueMode = m_d->comboQueueMode->currentData().toInt();
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3)
                queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False
            if (queueMode == 2) {
                m_d->pollTimer->stop();
                for (const QString &id : m_d->jobQueue) m_d->pendingHistoryByPromptId.remove(id);
                if (!m_d->currentPromptId.isEmpty()) m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
                m_d->jobQueue.clear();
                m_d->currentPromptId.clear();
                QUrl interruptUrl(baseUrl);
                QString ip = interruptUrl.path();
                if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
                else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
                else interruptUrl.setPath(ip + "interrupt");
                QNetworkRequest reqInt(interruptUrl);
                ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
                m_d->nam->post(reqInt, QByteArray("{}"));
            }
            m_d->batchCollectIds.clear();
            m_d->batchSubmitIndex = 0;
            m_d->batchCountTarget = qMax(1, effectiveBatch);
            m_d->batchQueueMode = queueMode;
            m_d->batchBaseUrl = baseUrl;
            QString path = baseUrl.path();
            if (path.isEmpty() || path == "/") m_d->batchBaseUrl.setPath("/prompt");
            else if (!path.endsWith('/')) m_d->batchBaseUrl.setPath(path + "/prompt");
            else m_d->batchBaseUrl.setPath(path + "prompt");
            m_d->batchUseCustomWorkflow = true;
            m_d->batchCustomWorkflow = workflow;
            m_d->batchNeedsPerFrameReference = false;
            m_d->batchBaseSeed = m_d->checkFixedSeed->isChecked()
                ? static_cast<qint64>(m_d->spinSeed->value())
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!m_d->checkFixedSeed->isChecked())
                m_d->spinSeed->setValue(static_cast<int>(m_d->batchBaseSeed));
            if (m_d->clientId.isEmpty())
                m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_d->progressBar->setValue(0);
            slotBatchSubmitNext();
        });
        return;
    }

    QJsonObject workflow;
    if (!customJson.isEmpty()) {
        QJsonParseError err;
        // §13.135: Strip // line comments for config/workflow JSON (not #)
        QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(customJson.toUtf8());
        QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            setStatusMessage(i18n("Custom workflow JSON error: %1", err.errorString()), true);
            return;
        }
        workflow = doc.object();
        // §13.101: UI workflow (nodes/links) → API via object_info, or pass through API JSON
        if (!tryResolveCustomWorkflowInPlace(&workflow))
            return;
        // §13.103: At most one ETN_KritaStyleAndPrompt node
        auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
        if (!validation.first) {
            setStatusMessage(validation.second, true);
            return;
        }
        {
            KisImageSP wfImage = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
            ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, m_d->customWorkflowParamOverrides, wfImage);
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    } else {
        qint64 seed = m_d->checkFixedSeed->isChecked()
            ? static_cast<qint64>(m_d->spinSeed->value())
            : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        if (!m_d->checkFixedSeed->isChecked()) {
            m_d->spinSeed->setValue(static_cast<int>(seed));
        }
        const ComfyUIUtils::LinkedEditStyleOverride link = ComfyUIUtils::linkedEditStyleOverride(
            m_d->checkEditMode && m_d->checkEditMode->isChecked(),
            m_d->comboCheckpoint->currentText().trimmed(),
            m_d->spinSteps->value(),
            m_d->spinCfg->value(),
            (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0,
            m_d->comboSampler->currentText().trimmed(),
            m_d->ksamplerScheduler);

        QString styleArch;
        if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
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
        int w = m_d->spinWidth->value();
        int h = m_d->spinHeight->value();
        w = static_cast<int>(w * genMul);
        h = static_cast<int>(h * genMul);
        w = qBound(64, w, 8192);
        h = qBound(64, h, 8192);
        ComfyUIUtils::clampExtentToMaxMegapixels(&w, &h);
        genParams.width = w;
        genParams.height = h;
        genParams.batchSize = 1;

        QString userPos = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
        QString promptText = link.active
            ? ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, userPos).trimmed()
            : userPos;
        promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
        ComfyUIUtils::extractLayerPlaceholders(promptText);
        genParams.positivePrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(promptText);
        const QString negSrc =
            link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed();
        genParams.negativePrompt = ComfyUIUtils::evalWildcards(negSrc, static_cast<quint32>(seed & 0xFFFFFFFFu));

        workflow = ComfyWorkflowEngine::buildTextToImage(genParams);
        if (workflow.isEmpty()) {
            setStatusMessage(i18n("Workflow JSON error."), true);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

        bool needsControlUpload = false;
        for (const ComfyControlLayerEntry &ce : m_d->rootControlLayers) {
            if (ce.layerName.isEmpty())
                continue;
            if (ComfyResources::ControlMode::isIpAdapter(ce.mode))
                continue;
            if (ComfyResources::ControlMode::isStructural(ce.mode)) {
                needsControlUpload = true;
                break;
            }
        }
        if (needsControlUpload) {
            m_d->generatePendingBaseWorkflow = workflow;
            m_d->generatePendingArch = genParams.arch;
            m_d->generateStashedCustomJson = customJson;
            m_d->generateStashedBatch = genBatch;
            m_d->generateStashedMul = genMul;
            m_d->generateAwaitingControlUploads = true;
            m_d->generateControlUploadIndex = 0;
            m_d->generateControlUploadedNames.clear();
            m_d->btnGenerate->setEnabled(false);
            uploadNextGenerateControlImage();
            return;
        }
    }

    const int batchCount = genBatch;
    // §13.214: Effective batch from extent (constrain latent samples by capacity)
    int effW = m_d->spinWidth->value();
    int effH = m_d->spinHeight->value();
    double mul = genMul;
    effW = qBound(64, static_cast<int>(effW * mul), 8192);
    effH = qBound(64, static_cast<int>(effH * mul), 8192);
    ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
    int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, batchCount);
    if (m_d->isFullAnimationBatch && batchCount > 0)
        effectiveBatch = batchCount; // §13.74: one ComfyUI prompt per timeline frame
    m_d->batchSeedStep = qMax(1, batchCount); // §13.212: +i * settings.batch_size (performance batch), not effectiveBatch

    int queueMode = m_d->comboQueueMode->currentData().toInt();
    if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3)
        queueMode = 0; // §5.7 / §13.92: Animation — supports_batch=False
    if (queueMode == 2) { // Replace
        m_d->pollTimer->stop();
        for (const QString &id : m_d->jobQueue)
            m_d->pendingHistoryByPromptId.remove(id);
        if (!m_d->currentPromptId.isEmpty())
            m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
        m_d->jobQueue.clear();
        m_d->currentPromptId.clear();
        QUrl interruptUrl(baseUrl);
        QString ip = interruptUrl.path();
        if (ip.isEmpty() || ip == "/") interruptUrl.setPath("/interrupt");
        else if (!ip.endsWith('/')) interruptUrl.setPath(ip + "/interrupt");
        else interruptUrl.setPath(ip + "interrupt");
        QNetworkRequest reqI(interruptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqI);
        reqI.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_d->nam->post(reqI, QByteArray("{}"));
    }

    m_d->batchCollectIds.clear();
    m_d->batchSubmitIndex = 0;
    m_d->batchCountTarget = qMax(1, effectiveBatch);
    m_d->batchQueueMode = queueMode;
    m_d->batchBaseUrl = baseUrl;
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") m_d->batchBaseUrl.setPath("/prompt");
    else if (!path.endsWith('/')) m_d->batchBaseUrl.setPath(path + "/prompt");
    else m_d->batchBaseUrl.setPath(path + "prompt");
    m_d->batchNeedsPerFrameReference = false;
    m_d->batchUseCustomWorkflow = !customJson.isEmpty();
    if (m_d->batchUseCustomWorkflow)
        m_d->batchCustomWorkflow = workflow;
    // §13.212: Set base seed once per batch (seed_i = base + i * batch_size); for non-custom, wildcards re-evaluated per job in slotBatchSubmitNext
    m_d->batchBaseSeed = m_d->checkFixedSeed->isChecked()
        ? static_cast<qint64>(m_d->spinSeed->value())
        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    if (!m_d->checkFixedSeed->isChecked())
        m_d->spinSeed->setValue(static_cast<int>(m_d->batchBaseSeed));
    if (m_d->clientId.isEmpty())
        m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_d->labelStatus->setText(i18n("Submitting…"));
    m_d->progressBar->setValue(0);
    m_d->btnGenerate->setEnabled(false);
    slotBatchSubmitNext();
}

void ComfyUIRemoteDock::slotBatchSubmitNext()
{
    if (m_d->batchSubmitIndex >= m_d->batchCountTarget) {
        if (m_d->batchQueueMode == 0) // Back
            m_d->jobQueue.append(m_d->batchCollectIds);
        else if (m_d->batchQueueMode == 1) { // Front
            for (int i = m_d->batchCollectIds.size() - 1; i >= 0; i--)
                m_d->jobQueue.prepend(m_d->batchCollectIds.at(i));
        } else // Replace
            m_d->jobQueue = m_d->batchCollectIds;
        m_d->batchCollectIds.clear();
        m_d->batchCountTarget = 0;
        m_d->batchNeedsPerFrameReference = false;
        if (m_d->currentPromptId.isEmpty() && !m_d->jobQueue.isEmpty()) {
            m_d->currentPromptId = m_d->jobQueue.takeFirst();
            m_d->pollCount = 0;
            startPolling();
        }
        updateQueueStatus();
        return;
    }

    // §13.74: Full Animation — timeline matches each queued job (canvas, layer tags, future img2img inputs)
    if (m_d->isFullAnimationBatch && m_d->viewManager) {
        KisImageSP img = m_d->viewManager->image();
        if (img && img->animationInterface() && img->animationInterface()->hasAnimation()) {
            const int idx = m_d->batchSubmitIndex;
            int frameTime = idx;
            if (idx >= 0 && idx < m_d->animationBatchFrameTimes.size()) {
                frameTime = m_d->animationBatchFrameTimes.at(idx);
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

    if (m_d->batchUseCustomWorkflow && m_d->batchNeedsPerFrameReference) {
        QString urlStr = m_d->editServerUrl->text().trimmed();
        KisImageSP image = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
        QImage refImg = ComfyUIUtils::getCanvasAsQImage(image);
        if (refImg.isNull()) {
            setStatusMessage(i18n("Could not export canvas for reference."), true);
            m_d->btnGenerate->setEnabled(true);
            m_d->progressBar->setValue(0);
            m_d->batchCountTarget = 0;
            m_d->isFullAnimationBatch = false;
            m_d->animationBatchPromptIdToIndex.clear();
            m_d->animationBatchSourcePathByFrame.clear();
            m_d->animationBatchFrameTimes.clear();
            m_d->animationBatchGroupId.clear();
            m_d->batchNeedsPerFrameReference = false;
            return;
        }
        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            setStatusMessage(i18n("Could not save temp image."), true);
            m_d->btnGenerate->setEnabled(true);
            m_d->progressBar->setValue(0);
            m_d->batchCountTarget = 0;
            m_d->isFullAnimationBatch = false;
            m_d->animationBatchPromptIdToIndex.clear();
            m_d->animationBatchSourcePathByFrame.clear();
            m_d->animationBatchFrameTimes.clear();
            m_d->animationBatchGroupId.clear();
            m_d->batchNeedsPerFrameReference = false;
            return;
        }
        m_d->labelStatus->setText(i18n("Uploading reference image…"));
        setProgressBarKind(true);
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
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        const int cappedIndex = m_d->batchSubmitIndex;
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp, tmp, cappedIndex]() {
            Q_UNUSED(tmp);
            replyUp->deleteLater();
            setProgressBarKind(false);
            auto abortAnimBatch = [this]() {
                m_d->btnGenerate->setEnabled(true);
                m_d->progressBar->setValue(0);
                m_d->batchCountTarget = 0;
                m_d->isFullAnimationBatch = false;
                m_d->animationBatchPromptIdToIndex.clear();
                m_d->animationBatchSourcePathByFrame.clear();
                m_d->animationBatchFrameTimes.clear();
                m_d->animationBatchGroupId.clear();
                m_d->batchNeedsPerFrameReference = false;
            };
            if (replyUp->error() != QNetworkReply::NoError) {
                setStatusMessage(i18n("Upload error: %1", replyUp->errorString()), true);
                abortAnimBatch();
                return;
            }
            const QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (refName.isEmpty()) {
                setStatusMessage(i18n("Server did not return image name."), true);
                abortAnimBatch();
                return;
            }
            QJsonDocument tmpl(m_d->batchCustomWorkflow);
            QString wt = QString::fromUtf8(tmpl.toJson(QJsonDocument::Compact));
            if (!wt.contains(QStringLiteral("REFERENCE_IMAGE"))) {
                setStatusMessage(i18n("Workflow lost REFERENCE_IMAGE placeholder."), true);
                abortAnimBatch();
                return;
            }
            wt.replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QJsonDocument wdoc = QJsonDocument::fromJson(wt.toUtf8(), &err);
            if (err.error != QJsonParseError::NoError || !wdoc.isObject()) {
                setStatusMessage(i18n("Workflow JSON error after reference replace."), true);
                abortAnimBatch();
                return;
            }
            QJsonObject workflow = wdoc.object();
            if (!tryResolveCustomWorkflowInPlace(&workflow)) {
                abortAnimBatch();
                return;
            }
            const auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
            if (!validation.first) {
                setStatusMessage(validation.second, true);
                abortAnimBatch();
                return;
            }
            dispatchBatchPromptRequest(workflow, cappedIndex);
        });
        return;
    }

    QJsonObject workflow;
    if (m_d->batchUseCustomWorkflow) {
        workflow = m_d->batchCustomWorkflow;
        if (!tryResolveCustomWorkflowInPlace(&workflow))
            return;
    } else {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(defaultWorkflow), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
        workflow = doc.object();
        // §13.212: seed for i-th job = base + i * settings.batch_size; wildcards re-evaluated per job with this seed
        const int batchSize = qMax(1, m_d->batchSeedStep);
        qint64 seed = m_d->batchBaseSeed + m_d->batchSubmitIndex * batchSize;
        QJsonObject n3 = workflow["3"].toObject();
        QJsonObject i3 = n3["inputs"].toObject();
        i3["seed"] = static_cast<double>(seed);
        i3["steps"] = m_d->spinSteps->value();
        i3["cfg"] = m_d->spinCfg->value();
        i3["denoise"] = (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0;
        i3["sampler_name"] = m_d->comboSampler->currentText().trimmed().isEmpty()
            ? QString("euler") : m_d->comboSampler->currentText().trimmed();
        i3["scheduler"] = m_d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->ksamplerScheduler;
        n3["inputs"] = i3;
        workflow["3"] = n3;
        QJsonObject n4 = workflow["4"].toObject();
        QJsonObject i4 = n4["inputs"].toObject();
        i4["ckpt_name"] = m_d->comboCheckpoint->currentText().trimmed().isEmpty()
            ? QString("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
        n4["inputs"] = i4;
        workflow["4"] = n4;
        QJsonObject n5 = workflow["5"].toObject();
        QJsonObject i5 = n5["inputs"].toObject();
        int w = m_d->spinWidth->value();
        int h = m_d->spinHeight->value();
        int genBatchTmp = m_d->spinBatchCount ? m_d->spinBatchCount->value() : 1;
        double genMul2 = m_d->resolutionMultiplier <= 0.0 ? 1.0 : m_d->resolutionMultiplier;
        const QJsonObject settingsRootBatch = ComfyUIUtils::loadSettingsJson();
        ComfyUIUtils::generationPerformanceBatchResolution(settingsRootBatch, m_d->lastComfySystemStats, genBatchTmp, genMul2,
                                                           &genBatchTmp, &genMul2);
        ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(settingsRootBatch, &genMul2);
        Q_UNUSED(genBatchTmp);
        double bmul = qMax(0.3, qMin(genMul2 <= 0.0 ? 1.0 : genMul2, 3.0));
        w = qBound(64, static_cast<int>(w * bmul), 8192);
        h = qBound(64, static_cast<int>(h * bmul), 8192);
        ComfyUIUtils::clampExtentToMaxMegapixels(&w, &h);
        i5["width"] = w;
        i5["height"] = h;
        n5["inputs"] = i5;
        workflow["5"] = n5;
        QJsonObject n6 = workflow["6"].toObject();
        QJsonObject i6 = n6["inputs"].toObject();
        QString promptText = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
        promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
        ComfyUIUtils::extractLayerPlaceholders(promptText);  // §13.35: <layer:name> → "Picture {n}"
        promptText = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(promptText);
        i6["text"] = promptText.isEmpty() ? QString("a beautiful painting") : promptText;
        n6["inputs"] = i6;
        workflow["6"] = n6;
        QJsonObject n7 = workflow["7"].toObject();
        QJsonObject i7 = n7["inputs"].toObject();
        i7["text"] = ComfyUIUtils::evalWildcards(ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed(), static_cast<quint32>(seed & 0xFFFFFFFFu));
        n7["inputs"] = i7;
        workflow["7"] = n7;
    }
    dispatchBatchPromptRequest(workflow, m_d->batchSubmitIndex);
}

void ComfyUIRemoteDock::dispatchBatchPromptRequest(QJsonObject workflow, int submitIndex)
{
    {
        KisImageSP wfImage = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
        ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, m_d->customWorkflowParamOverrides, wfImage);
    }
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

    // §13.163: ComfyUI prompt API body — prompt (graph), client_id (UUID), prompt_id (job UUID); response must include prompt_id
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject payload;
    payload["prompt"] = workflow;
    payload["client_id"] = m_d->clientId;
    payload["prompt_id"] = expectedPromptId;
    if (m_d->isFullAnimationBatch && !m_d->animationBatchGroupId.isEmpty()) {
        QJsonObject extraData;
        QJsonObject kid;
        kid.insert(QStringLiteral("animation_id"), m_d->animationBatchGroupId);
        extraData.insert(QStringLiteral("krita_ai_diffusion"), kid);
        payload.insert(QStringLiteral("extra_data"), extraData);
    }
    ComfyUIUtils::dumpComfyPromptPayloadIfEnabled(payload);
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkRequest req(m_d->batchBaseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_d->nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, submitIndex, expectedPromptId]() {
        reply->deleteLater();
        const QByteArray respBody = reply->readAll();
        auto abortBatchState = [this]() {
            m_d->btnGenerate->setEnabled(true);
            m_d->progressBar->setValue(0);
            m_d->batchCountTarget = 0;
            m_d->isFullAnimationBatch = false;
            m_d->animationBatchPromptIdToIndex.clear();
            m_d->animationBatchSourcePathByFrame.clear();
            m_d->animationBatchFrameTimes.clear();
            m_d->animationBatchGroupId.clear();
            m_d->batchNeedsPerFrameReference = false;
        };
        if (reply->error() != QNetworkReply::NoError) {
            // §7.7 / §13.142: Prefer server error body (e.g. LCM message) when present
            QJsonObject obj = QJsonDocument::fromJson(respBody).object();
            if (obj.contains("error")) {
                setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
            } else {
                setStatusMessage(i18n("Submit error: %1", reply->errorString()), true);
            }
            abortBatchState();
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(respBody).object();
        if (obj.contains("error")) {
            setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
            abortBatchState();
            return;
        }
        // §13.181: Message routing — associate response with job via prompt_id (single consumer per job)
        const QString promptId = obj["prompt_id"].toString();
        if (promptId.isEmpty()) {
            setStatusMessage(i18n("No prompt_id in response."), true);
            abortBatchState();
            return;
        }
        if (promptId != expectedPromptId) {
            setStatusMessage(i18n("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            abortBatchState();
            return;
        }
        if (m_d->isFullAnimationBatch) {
            int fileFrame = submitIndex;
            if (submitIndex >= 0 && submitIndex < m_d->animationBatchFrameTimes.size())
                fileFrame = m_d->animationBatchFrameTimes.at(submitIndex);
            m_d->animationBatchPromptIdToIndex.insert(promptId, fileFrame);
        }
        Private::HistoryEntry entry;
        entry.prompt = m_d->editPrompt->toPlainText();
        entry.negative = m_d->editNegative->toPlainText();
        entry.checkpoint = m_d->comboCheckpoint->currentText();
        entry.styleName = m_d->comboPreset ? m_d->comboPreset->currentText() : QString();
        entry.width = m_d->spinWidth->value();
        entry.height = m_d->spinHeight->value();
        entry.steps = m_d->spinSteps->value();
        entry.cfg = m_d->spinCfg->value();
        entry.strength = m_d->spinStrength ? m_d->spinStrength->value() : 100;
        entry.samplerName = m_d->comboSampler->currentText().trimmed();
        // §13.212: Batch seed = base + index * settings.batch_size; wildcards were re-evaluated per job in workflow build
        const int batchSize = qMax(1, m_d->batchSeedStep);
        entry.seed = m_d->batchBaseSeed + submitIndex * batchSize;
        // §13.74: capture timeline frame for Single Frame mismatch warning after generation
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3 && m_d->radioSingleFrame
            && m_d->radioSingleFrame->isChecked() && !m_d->isFullAnimationBatch && m_d->viewManager) {
            KisImageSP img = m_d->viewManager->image();
            if (img && img->animationInterface() && img->animationInterface()->hasAnimation())
                entry.animationSubmitTime = img->animationInterface()->currentTime();
        }
        m_d->pendingHistoryByPromptId.insert(promptId, entry);
        m_d->batchCollectIds.append(promptId);
        m_d->batchSubmitIndex++;
        slotBatchSubmitNext();
    });
}

void ComfyUIRemoteDock::slotGenerateAnimation()
{
    m_d->batchNeedsPerFrameReference = false;
    m_d->animationBatchSourcePathByFrame.clear();
    m_d->animationBatchFrameTimes.clear();
    m_d->animationImportStartFrame = 0;
    m_d->animationBatchRangeStart = 0;
    m_d->animationBatchRangeEnd = 0;
    m_d->animationBatchGroupId.clear();

    // §13.177 / §13.74: frame count from active playback range; per-frame timeline indices for filenames + import
    int frames = m_d->spinAnimationFrames->value();
    if (m_d->viewManager && m_d->viewManager->image()) {
        KisImageSP img = m_d->viewManager->image();
        if (img->animationInterface() && img->animationInterface()->hasAnimation()) {
            const KisTimeSpan range = img->animationInterface()->activePlaybackRange();
            if (range.isValid()) {
                frames = qBound(2, range.duration(), m_d->spinAnimationFrames->maximum());
                m_d->animationImportStartFrame = range.start();
                m_d->animationBatchRangeStart = range.start();
                m_d->animationBatchRangeEnd = range.start() + frames - 1;
                m_d->animationBatchFrameTimes.reserve(frames);
                for (int i = 0; i < frames; ++i)
                    m_d->animationBatchFrameTimes.append(range.start() + i);
            }
        }
    }
    if (m_d->animationBatchFrameTimes.isEmpty()) {
        m_d->animationImportStartFrame = 0;
        m_d->animationBatchRangeStart = 0;
        m_d->animationBatchRangeEnd = qMax(0, frames - 1);
    }
    m_d->spinBatchCount->setValue(frames);
    m_d->checkFixedSeed->setChecked(true);
    // §13.45: Full Animation — .animation/frame-{time}.png, shared animation_id in prompt extra_data
    if (m_d->radioFullAnimation && m_d->radioFullAnimation->isChecked()) {
        m_d->isFullAnimationBatch = true;
        m_d->animationBatchPromptIdToIndex.clear();
        m_d->animationBatchSourcePathByFrame.clear();
        m_d->animationBatchGroupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    slotGenerate();
}

void ComfyUIRemoteDock::slotImportAnimation()
{
    // §13.45: Import frames from .animation (frame-*.png) or .live-frames (frame-*.webp) into document
    if (!m_d->canvas || !m_d->canvas->imageView() || !m_d->canvas->imageView()->document()) {
        setStatusMessage(i18n("No document open."), true);
        return;
    }
    QString docPath = m_d->canvas->imageView()->document()->path();
    if (docPath.isEmpty()) {
        setStatusMessage(i18n("Save the document first to use Import Animation."), true);
        return;
    }
    QList<QPair<int, QString>> indexedFiles;
    QString dirPath = ComfyUIUtils::animationFramesDirectory(docPath);
    QDir dir(dirPath);
    if (dir.exists()) {
        QStringList entries = dir.entryList(QStringList() << QStringLiteral("frame-*.png"), QDir::Files);
        for (const QString &e : entries) {
            int dotPos = e.indexOf(QLatin1Char('.'), 6);
            int n = (dotPos > 6) ? e.mid(6, dotPos - 6).toInt() : 0;
            indexedFiles.append(qMakePair(n, dir.absoluteFilePath(e)));
        }
    }
    if (indexedFiles.isEmpty()) {
        dirPath = ComfyUIUtils::liveFramesDirectory(docPath);
        dir = QDir(dirPath);
        if (dir.exists()) {
            QStringList entries = dir.entryList(QStringList() << QStringLiteral("frame-*.webp"), QDir::Files);
            for (const QString &e : entries) {
                int dotPos = e.indexOf(QLatin1Char('.'), 6);
                int n = (dotPos > 6) ? e.mid(6, dotPos - 6).toInt() : 0;
                indexedFiles.append(qMakePair(n, dir.absoluteFilePath(e)));
            }
        }
    }
    if (indexedFiles.isEmpty()) {
        setStatusMessage(i18n("No frame folder found. Use .animation or .live-frames next to the document."), true);
        return;
    }
    std::sort(indexedFiles.begin(), indexedFiles.end(), [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first < b.first; });
    QStringList pathList;
    for (const auto &p : indexedFiles)
        pathList.append(p.second);
    KisImageSP image = m_d->canvas->image().toStrongRef();
    if (!image) {
        setStatusMessage(i18n("No image in document."), true);
        return;
    }
    KisAnimationImporter importer(image);
    KisImportExportErrorCode result = importer.import(pathList, 0, 1, false, false, 1);
    if (!result.isOk() && !result.isInternalError()) {
        setStatusMessage(result.errorMessage().isEmpty() ? i18n("Import failed.") : result.errorMessage(), true);
        return;
    }
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    setStatusMessage(i18n("Imported %1 frames.", pathList.size()));
}

void ComfyUIRemoteDock::slotCancelQueue()
{
    m_d->pollTimer->stop();
    m_d->batchNeedsPerFrameReference = false;
    m_d->isFullAnimationBatch = false;
    m_d->animationBatchPromptIdToIndex.clear();
    m_d->animationBatchSourcePathByFrame.clear();
    m_d->animationBatchFrameTimes.clear();
    m_d->animationBatchGroupId.clear();
    QStringList toCancel = m_d->jobQueue;
    QString activeId = m_d->currentPromptId;
    for (const QString &id : m_d->jobQueue)
        m_d->pendingHistoryByPromptId.remove(id);
    if (!m_d->currentPromptId.isEmpty())
        m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
    m_d->jobQueue.clear();
    m_d->currentPromptId.clear();
    m_d->progressBar->setValue(0);
    m_d->btnGenerate->setEnabled(true);
    m_d->btnCancelQueue->setEnabled(false);
    setStatusMessage(i18n("Cancelled."));
    updateQueueStatus();
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) return;
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) return;

    auto doInterrupt = [this, baseUrl]() {
        QUrl url = baseUrl;
        QString path = url.path();
        if (path.isEmpty() || path == "/") url.setPath("/interrupt");
        else if (!path.endsWith('/')) url.setPath(path + "/interrupt");
        else url.setPath(path + "interrupt");
        QNetworkRequest req(url);
        ComfyUIUtils::setComfyUIRequestHeaders(req);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_d->nam->post(req, QByteArray("{}"));
    };
    if (!toCancel.isEmpty()) {
        QJsonObject delPayload;
        QJsonArray arr;
        for (const QString &id : toCancel)
            arr.append(id);
        delPayload["delete"] = arr;
        QUrl queueUrl = baseUrl;
        QString p = queueUrl.path();
        if (p.isEmpty() || p == "/") queueUrl.setPath("/queue");
        else if (!p.endsWith('/')) queueUrl.setPath(p + "/queue");
        else queueUrl.setPath(p + "queue");
        QNetworkRequest reqQueue(queueUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqQueue);
        reqQueue.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = m_d->nam->post(reqQueue, QJsonDocument(delPayload).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, doInterrupt, activeId]() {
            reply->deleteLater();
            if (!activeId.isEmpty())
                doInterrupt();
        });
    } else if (!activeId.isEmpty()) {
        doInterrupt();
    }
}

bool ComfyUIRemoteDock::cancelCurrentGenerateJob()
{
    const QString activeId = m_d->currentPromptId;
    if (activeId.isEmpty())
        return false;
    m_d->pollTimer->stop();
    m_d->pendingHistoryByPromptId.remove(activeId);
    m_d->currentPromptId.clear();
    m_d->pollCount = 0;
    if (!m_d->jobQueue.isEmpty()) {
        m_d->currentPromptId = m_d->jobQueue.takeFirst();
        startPolling();
    } else {
        m_d->progressBar->setValue(0);
        if (m_d->btnGenerate)
            m_d->btnGenerate->setEnabled(true);
    }
    updateQueueStatus();
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (!urlStr.isEmpty()) {
        QUrl interruptUrl(urlStr);
        QString ip = interruptUrl.path();
        if (ip.isEmpty() || ip == "/")
            interruptUrl.setPath("/interrupt");
        else if (!ip.endsWith('/'))
            interruptUrl.setPath(ip + "/interrupt");
        else
            interruptUrl.setPath(ip + "interrupt");
        QNetworkRequest reqInt(interruptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
        reqInt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        m_d->nam->post(reqInt, QByteArray("{}"));
    }
    return true;
}

void ComfyUIRemoteDock::cancelQueuedGenerateJobs()
{
    const QStringList toCancel = m_d->jobQueue;
    if (toCancel.isEmpty())
        return;
    for (const QString &id : toCancel)
        m_d->pendingHistoryByPromptId.remove(id);
    m_d->jobQueue.clear();
    updateQueueStatus();
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty())
        return;
    QUrl queueUrl(urlStr);
    QString p = queueUrl.path();
    if (p.isEmpty() || p == "/")
        queueUrl.setPath("/queue");
    else if (!p.endsWith('/'))
        queueUrl.setPath(p + "/queue");
    else
        queueUrl.setPath(p + "queue");
    QJsonObject delPayload;
    QJsonArray arr;
    for (const QString &id : toCancel)
        arr.append(id);
    delPayload["delete"] = arr;
    QNetworkRequest reqQueue(queueUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(reqQueue);
    reqQueue.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_d->nam->post(reqQueue, QJsonDocument(delPayload).toJson(QJsonDocument::Compact));
}

void ComfyUIRemoteDock::stopControlPreviewPolling()
{
    if (m_d->controlPreviewPollTimer)
        m_d->controlPreviewPollTimer->stop();
    m_d->controlPreviewPromptId.clear();
    m_d->controlPreviewPollCount = 0;
    m_d->controlPreviewHandsCompositeBack = false;
    m_d->controlPreviewCompositeLocalRect = QRect();
    m_d->controlPreviewCompositeFullSize = QSize();
    if (m_d->btnControlPreviewRun)
        m_d->btnControlPreviewRun->setEnabled(true);
}

void ComfyUIRemoteDock::syncControlPreviewRangeFromSettings()
{
    if (!m_d->controlPreviewRangeSlider)
        return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const int savedLow = cfg.readEntry(QStringLiteral("control_layer_timing_low_pct"), -1);
    const int savedHigh = cfg.readEntry(QStringLiteral("control_layer_timing_high_pct"), -1);
    if (savedLow >= 0 && savedHigh >= 0) {
        const int lo = qBound(0, savedLow, 100);
        const int hi = qBound(0, savedHigh, 100);
        QSignalBlocker b(m_d->controlPreviewRangeSlider);
        m_d->controlPreviewRangeSlider->setInterval(qMin(lo, hi), qMax(lo, hi));
        return;
    }
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const QJsonObject controlRoot = ComfyUIUtils::builtinControlPresetsRoot();
    const QList<ComfyUIUtils::ControlLayerPreset> cps =
        ComfyUIUtils::controlPresetsForMode(controlRoot, QStringLiteral("default"), QString());
    if (cps.isEmpty()) {
        QSignalBlocker b(m_d->controlPreviewRangeSlider);
        m_d->controlPreviewRangeSlider->setInterval(25, 75);
        return;
    }
    const int idx = qBound(0, s.value(QStringLiteral("control_layer_default_preset_index")).toInt(0), cps.size() - 1);
    const ComfyUIUtils::ControlLayerPreset &p = cps.at(idx);
    const int low = qBound(0, qRound(p.start * 100.0), 100);
    const int high = qBound(0, qRound(p.end * 100.0), 100);
    QSignalBlocker b(m_d->controlPreviewRangeSlider);
    m_d->controlPreviewRangeSlider->setInterval(qMin(low, high), qMax(low, high));
}

void ComfyUIRemoteDock::syncPoseGuidePeopleCountFromSettings()
{
    if (!m_d->spinPoseGuidePeopleCount)
        return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const int n = qBound(1, cfg.readEntry(QStringLiteral("pose_guide_people_count"), 1), 3);
    QSignalBlocker b(m_d->spinPoseGuidePeopleCount);
    m_d->spinPoseGuidePeopleCount->setValue(n);
}

void ComfyUIRemoteDock::slotControlPreviewRun()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(i18n("Enter a server URL."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(i18n("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    const auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        return;
    }
    const QString mode =
        m_d->comboControlPreviewMode ? m_d->comboControlPreviewMode->currentData().toString() : QStringLiteral("depth");
    stopControlPreviewPolling();
    if (m_d->btnControlPreviewRun)
        m_d->btnControlPreviewRun->setEnabled(false);
    if (m_d->labelControlPreviewImage) {
        m_d->labelControlPreviewImage->clear();
        m_d->labelControlPreviewImage->setText(i18n("Uploading…"));
    }

    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull()) {
        setStatusMessage(i18n("Could not export canvas."), true);
        stopControlPreviewPolling();
        return;
    }
    // §13.98: Pose preview prefers tracked vector-layer pose SVG (rasterized) when the active layer is a shape layer.
    if (mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0 && m_d->viewManager) {
        if (KisLayerSP al = m_d->viewManager->activeLayer()) {
            if (auto *sl = qobject_cast<KisShapeLayer *>(al.data())) {
                const QSize docSz = image->bounds().size();
                QImage poseImg = ComfyUIPoseLayers::instance().rasterizedPoseImageForLayer(sl->uuid(), docSz);
                if (!poseImg.isNull()) {
                    if (poseImg.size() != canvasImg.size()) {
                        poseImg = poseImg.scaled(canvasImg.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    }
                    canvasImg = poseImg;
                }
            }
        }
    }
    // §13.53: partial selection → crop to bounds; track rect in full-export coords for hands recomposite.
    const QRect docBounds = image->bounds();
    const QSize fullExportSize = canvasImg.size();
    QRect localCropRect(0, 0, fullExportSize.width(), fullExportSize.height());
    if (m_d->viewManager) {
        if (KisSelectionSP sel = m_d->viewManager->selection()) {
            if (auto ps = sel->pixelSelection()) {
                QRect r = ps->selectedExactRect();
                r &= docBounds;
                if (!r.isEmpty() && r.size() != docBounds.size()) {
                    const QRect local = r.translated(-docBounds.topLeft());
                    if (local.left() >= 0 && local.top() >= 0 && local.right() < canvasImg.width()
                        && local.bottom() < canvasImg.height()) {
                        localCropRect = local;
                        canvasImg = canvasImg.copy(local);
                    }
                }
            }
        }
    }
    m_d->controlPreviewCompositeLocalRect = localCropRect;
    m_d->controlPreviewCompositeFullSize = fullExportSize;
    m_d->controlPreviewHandsCompositeBack =
        (mode.compare(QStringLiteral("hands"), Qt::CaseInsensitive) == 0
         && localCropRect != QRect(QPoint(0, 0), fullExportSize));
    // §13.53: preprocessors use resolution from shortest side of extent (not longest).
    const int resBase = qMin(canvasImg.width(), canvasImg.height());

    QTemporaryFile *tmp = new QTemporaryFile(this);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        setStatusMessage(i18n("Could not save temporary image for upload."), true);
        tmp->deleteLater();
        stopControlPreviewPolling();
        return;
    }
    QUrl uploadUrl(urlStr);
    QString up = uploadUrl.path();
    if (up.isEmpty() || up == QLatin1String("/"))
        uploadUrl.setPath(QStringLiteral("/upload/image"));
    else if (!up.endsWith(QLatin1Char('/')))
        uploadUrl.setPath(up + QStringLiteral("/upload/image"));
    else
        uploadUrl.setPath(up + QStringLiteral("upload/image"));
    tmp->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"krita_control_preview.png\"")));
    part.setBodyDevice(tmp);
    tmp->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(uploadUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, resBase, mode]() {
        reply->deleteLater();
        if (m_d->comboWorkspace->currentIndex() != 0) {
            stopControlPreviewPolling();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Control preview upload failed: %1", reply->errorString()), true);
            if (m_d->labelControlPreviewImage)
                m_d->labelControlPreviewImage->clear();
            stopControlPreviewPolling();
            return;
        }
        const QString uploadedName = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (uploadedName.isEmpty()) {
            setStatusMessage(i18n("Control preview: server did not return an image name."), true);
            stopControlPreviewPolling();
            return;
        }
        QJsonObject workflow = ComfyUIUtils::buildControlImageWorkflow(uploadedName, mode, resBase, false);
        if (workflow.isEmpty()) {
            setStatusMessage(i18n("Unsupported control mode for preprocessor preview."), true);
            stopControlPreviewPolling();
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (m_d->clientId.isEmpty())
            m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject payload;
        payload.insert(QStringLiteral("prompt"), workflow);
        payload.insert(QStringLiteral("client_id"), m_d->clientId);
        payload.insert(QStringLiteral("prompt_id"), expectedPromptId);
        QUrl promptUrl(m_d->editServerUrl->text().trimmed());
        QString p = promptUrl.path();
        if (p.isEmpty() || p == QLatin1String("/"))
            promptUrl.setPath(QStringLiteral("/prompt"));
        else if (!p.endsWith(QLatin1Char('/')))
            promptUrl.setPath(p + QStringLiteral("/prompt"));
        else
            promptUrl.setPath(p + QStringLiteral("prompt"));
        QNetworkRequest reqP(promptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqP);
        reqP.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *replyP = m_d->nam->post(reqP, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyP, &QNetworkReply::finished, this, [this, replyP, expectedPromptId]() {
            replyP->deleteLater();
            if (m_d->comboWorkspace->currentIndex() != 0) {
                stopControlPreviewPolling();
                return;
            }
            if (replyP->error() != QNetworkReply::NoError) {
                setStatusMessage(i18n("Control preview prompt failed: %1", replyP->errorString()), true);
                stopControlPreviewPolling();
                return;
            }
            const QString promptId =
                QJsonDocument::fromJson(replyP->readAll()).object().value(QStringLiteral("prompt_id")).toString();
            if (promptId.isEmpty()) {
                setStatusMessage(i18n("Control preview: empty prompt_id from server."), true);
                stopControlPreviewPolling();
                return;
            }
            if (promptId != expectedPromptId) {
                setStatusMessage(i18n("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                stopControlPreviewPolling();
                return;
            }
            m_d->controlPreviewPromptId = promptId;
            m_d->controlPreviewPollCount = 0;
            if (m_d->labelControlPreviewImage)
                m_d->labelControlPreviewImage->setText(i18n("Running preprocessor…"));
            m_d->controlPreviewPollTimer->start(1000);
        });
    });
}

void ComfyUIRemoteDock::slotControlPreviewPoll()
{
    if (m_d->controlPreviewPromptId.isEmpty() || m_d->comboWorkspace->currentIndex() != 0)
        return;
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        stopControlPreviewPolling();
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    const QString pid = m_d->controlPreviewPromptId;
    if (path.isEmpty() || path == QLatin1String("/"))
        baseUrl.setPath(QStringLiteral("/history/") + pid);
    else if (!path.endsWith(QLatin1Char('/')))
        baseUrl.setPath(path + QStringLiteral("/history/") + pid);
    else
        baseUrl.setPath(path + QStringLiteral("history/") + pid);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_d->comboWorkspace->currentIndex() != 0 || m_d->controlPreviewPromptId.isEmpty()) {
            stopControlPreviewPolling();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Control preview history request failed: %1", reply->errorString()), true);
            stopControlPreviewPolling();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject hist = root.value(m_d->controlPreviewPromptId).toObject();
        const QJsonObject outputs = hist.value(QStringLiteral("outputs")).toObject();
        if (outputs.isEmpty()) {
            m_d->controlPreviewPollCount++;
            constexpr int kMaxPoll = 300;
            if (m_d->controlPreviewPollCount >= kMaxPoll) {
                setStatusMessage(i18n("Control preview timed out waiting for server."), true);
                stopControlPreviewPolling();
                return;
            }
            m_d->controlPreviewPollTimer->start(1000);
            return;
        }
        QString filename;
        QString subfolder;
        for (const QString &nodeId : outputs.keys()) {
            const QJsonArray images = outputs.value(nodeId).toObject().value(QStringLiteral("images")).toArray();
            if (!images.isEmpty()) {
                const QJsonObject img = images.at(0).toObject();
                filename = img.value(QStringLiteral("filename")).toString();
                subfolder = img.value(QStringLiteral("subfolder")).toString();
                break;
            }
        }
        if (filename.isEmpty()) {
            setStatusMessage(i18n("Control preview: no output image in history."), true);
            stopControlPreviewPolling();
            return;
        }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith(QLatin1Char('/')))
            vp += QLatin1Char('/');
        viewUrl.setPath(vp + QStringLiteral("view"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("filename"), filename);
        if (!subfolder.isEmpty())
            q.addQueryItem(QStringLiteral("subfolder"), subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqV(viewUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqV);
        QNetworkReply *replyV = m_d->nam->get(reqV);
        connect(replyV, &QNetworkReply::finished, this, [this, replyV]() {
            replyV->deleteLater();
            if (m_d->comboWorkspace->currentIndex() != 0) {
                stopControlPreviewPolling();
                return;
            }
            m_d->controlPreviewPromptId.clear();
            m_d->controlPreviewPollCount = 0;
            if (m_d->controlPreviewPollTimer)
                m_d->controlPreviewPollTimer->stop();
            if (m_d->btnControlPreviewRun)
                m_d->btnControlPreviewRun->setEnabled(true);
            if (replyV->error() != QNetworkReply::NoError) {
                setStatusMessage(i18n("Control preview image download failed: %1", replyV->errorString()), true);
                if (m_d->labelControlPreviewImage)
                    m_d->labelControlPreviewImage->clear();
                m_d->controlPreviewHandsCompositeBack = false;
                return;
            }
            QImage img;
            if (!img.loadFromData(replyV->readAll())) {
                setStatusMessage(i18n("Control preview: could not decode image."), true);
                if (m_d->labelControlPreviewImage)
                    m_d->labelControlPreviewImage->clear();
                m_d->controlPreviewHandsCompositeBack = false;
                return;
            }
            if (m_d->controlPreviewHandsCompositeBack) {
                img = ComfyUIUtils::compositeControlImageOntoExtent(img, m_d->controlPreviewCompositeFullSize,
                                                                     m_d->controlPreviewCompositeLocalRect);
                m_d->controlPreviewHandsCompositeBack = false;
            }
            if (m_d->labelControlPreviewImage) {
                const QPixmap pm = QPixmap::fromImage(
                    img.scaled(QSize(256, 256), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_d->labelControlPreviewImage->setPixmap(pm);
                m_d->labelControlPreviewImage->setText(QString());
            }
            setStatusMessage(i18n("Control preprocessor preview finished."), false);
        });
    });
}

void ComfyUIRemoteDock::uploadNextGenerateControlImage()
{
    if (!m_d->generateAwaitingControlUploads || !m_d->viewManager) {
        m_d->btnGenerate->setEnabled(true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid() || !image) {
        setStatusMessage(i18n("Invalid server or document."), true);
        m_d->generateAwaitingControlUploads = false;
        m_d->btnGenerate->setEnabled(true);
        return;
    }

    while (m_d->generateControlUploadIndex < m_d->rootControlLayers.size()) {
        const ComfyControlLayerEntry ce = m_d->rootControlLayers.at(m_d->generateControlUploadIndex);
        m_d->generateControlUploadIndex++;
        if (ce.layerName.isEmpty() || ComfyResources::ControlMode::isIpAdapter(ce.mode)
            || !ComfyResources::ControlMode::isStructural(ce.mode))
            continue;

        QImage img = ComfyUIUtils::getLayerProjectionAsQImage(image, ce.layerName);
        if (img.isNull()) {
            setStatusMessage(i18n("Could not export control layer \"%1\".", ce.layerName), true);
            m_d->generateAwaitingControlUploads = false;
            m_d->btnGenerate->setEnabled(true);
            return;
        }
        if (ComfyUIUtils::isControlModeLines(ce.mode)) {
            img = img.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < img.height(); y++) {
                for (int x = 0; x < img.width(); x++) {
                    const QRgb px = img.pixel(x, y);
                    const int a = qAlpha(px);
                    if (a > 0)
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
            setStatusMessage(i18n("Could not save control layer image."), true);
            m_d->generateAwaitingControlUploads = false;
            m_d->btnGenerate->setEnabled(true);
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
        const QString fname =
            QStringLiteral("control_%1.png").arg(m_d->generateControlUploadedNames.size());
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(fname));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        m_d->labelStatus->setText(i18n("Uploading control layer %1…", ce.layerName));
        setProgressBarKind(true);
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp]() {
            replyUp->deleteLater();
            setProgressBarKind(false);
            if (replyUp->error() != QNetworkReply::NoError) {
                setStatusMessage(i18n("Control upload error: %1", replyUp->errorString()), true);
                m_d->generateAwaitingControlUploads = false;
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            const QString name = QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                setStatusMessage(i18n("Server did not return control image name."), true);
                m_d->generateAwaitingControlUploads = false;
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            m_d->generateControlUploadedNames.append(name);
            uploadNextGenerateControlImage();
        });
        return;
    }

    continueGenerateAfterControlUploads();
}

void ComfyUIRemoteDock::continueGenerateAfterControlUploads()
{
    m_d->generateAwaitingControlUploads = false;
    QJsonObject workflow = m_d->generatePendingBaseWorkflow;
    QList<ComfyWorkflowEngine::ControlNetLayerInput> inputs;
    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : m_d->rootControlLayers) {
        if (ce.layerName.isEmpty() || ComfyResources::ControlMode::isIpAdapter(ce.mode)
            || !ComfyResources::ControlMode::isStructural(ce.mode))
            continue;
        if (uploadIdx >= m_d->generateControlUploadedNames.size())
            break;
        ComfyWorkflowEngine::ControlNetLayerInput in;
        in.mode = ce.mode;
        in.imageName = m_d->generateControlUploadedNames.at(uploadIdx++);
        in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
        in.startPercent = ce.start;
        in.endPercent = ce.end;
        inputs.append(in);
    }
    ComfyWorkflowEngine::applyControlNetLayers(&workflow, inputs, m_d->generatePendingArch);
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

    const int genBatch = m_d->generateStashedBatch;
    const double genMul = m_d->generateStashedMul;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    QUrl baseUrl(urlStr);

    int effW = m_d->spinWidth->value();
    int effH = m_d->spinHeight->value();
    effW = qBound(64, static_cast<int>(effW * genMul), 8192);
    effH = qBound(64, static_cast<int>(effH * genMul), 8192);
    ComfyUIUtils::clampExtentToMaxMegapixels(&effW, &effH);
    int effectiveBatch = ComfyUIUtils::computeBatchSize(effW, effH, 512, genBatch);
    if (m_d->isFullAnimationBatch && genBatch > 0)
        effectiveBatch = genBatch;
    m_d->batchSeedStep = qMax(1, genBatch);

    int queueMode = m_d->comboQueueMode->currentData().toInt();
    if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3)
        queueMode = 0;
    if (queueMode == 2) {
        m_d->pollTimer->stop();
        for (const QString &id : m_d->jobQueue)
            m_d->pendingHistoryByPromptId.remove(id);
        if (!m_d->currentPromptId.isEmpty())
            m_d->pendingHistoryByPromptId.remove(m_d->currentPromptId);
        m_d->jobQueue.clear();
        m_d->currentPromptId.clear();
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
        m_d->nam->post(reqI, QByteArray("{}"));
    }

    m_d->batchCollectIds.clear();
    m_d->batchSubmitIndex = 0;
    m_d->batchCountTarget = qMax(1, effectiveBatch);
    m_d->batchQueueMode = queueMode;
    m_d->batchBaseUrl = baseUrl;
    QString path = baseUrl.path();
    if (path.isEmpty() || path == QLatin1Char('/'))
        m_d->batchBaseUrl.setPath(QStringLiteral("/prompt"));
    else if (!path.endsWith(QLatin1Char('/')))
        m_d->batchBaseUrl.setPath(path + QStringLiteral("/prompt"));
    else
        m_d->batchBaseUrl.setPath(path + QStringLiteral("prompt"));
    m_d->batchNeedsPerFrameReference = false;
    m_d->batchUseCustomWorkflow = true;
    m_d->batchCustomWorkflow = workflow;
    m_d->batchBaseSeed = m_d->checkFixedSeed->isChecked()
        ? static_cast<qint64>(m_d->spinSeed->value())
        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    if (!m_d->checkFixedSeed->isChecked())
        m_d->spinSeed->setValue(static_cast<int>(m_d->batchBaseSeed));
    if (m_d->clientId.isEmpty())
        m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_d->labelStatus->setText(i18n("Submitting…"));
    m_d->progressBar->setValue(0);
    slotBatchSubmitNext();
}
