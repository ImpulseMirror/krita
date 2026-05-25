/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyStyleCollection.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyControlLayer.h"
#include "ComfyRegionProcess.h"
#include "ComfyUIUtils.h"
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
#include <QLoggingCategory>
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

// Defined in ComfyUIRemoteDock.cpp - shared logging category for the comfyui_remote
// docker. Brought in via `extern` so slotGenerate() can log to the same "krita.comfyui_remote"
// channel and the entire generate path is traceable via `adb logcat`.
Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

#include "ComfyUIIntervalSlider.h"
#include "ComfyUIPoseLayers.h"

#include <kis_layer.h>
#include <kis_shape_layer.h>

#include <algorithm>

namespace {

bool validateCustomWorkflowGraphOrShowError(ComfyUIRemoteDock *dock,
                                            ComfyUIRemoteDock::Private *d,
                                            const QJsonObject &workflow)
{
    const auto apiCheck = ComfyUIUtils::validateCustomWorkflowApiGraph(workflow, d->lastObjectInfoRoot);
    if (apiCheck.first)
        return true;
    dock->setStatusMessage(apiCheck.second, true);
    return false;
}

QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForGenerate(const ComfyUIRemoteDock::Private *d)
{
    QList<ComfyUIRemoteDock::Private::RegionEntry> regs = comfyActiveRegionEntries(d);
    if (d->checkRegionOnly && d->checkRegionOnly->isChecked()) {
        const int row = comfyActiveRegionRow(d);
        if (row >= 0 && row < regs.size())
            return {regs.at(row)};
    }
    return regs;
}

QList<ComfyControlLayerEntry> controlLayersForGenerate(const ComfyUIRemoteDock::Private *d)
{
    return mergedJobControlLayers(d->rootControlLayers, regionsForGenerate(d));
}

QImage maskPngForComfyUpload(const QImage &maskGray)
{
    QImage maskPng(maskGray.size(), QImage::Format_ARGB32);
    for (int y = 0; y < maskGray.height(); y++) {
        for (int x = 0; x < maskGray.width(); x++) {
            const int g = qGray(maskGray.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    }
    return maskPng;
}

QString samplerModelNodeId(const QJsonObject &workflow)
{
    const QJsonArray refineLatent = workflow.value(QStringLiteral("6"))
                                        .toObject()
                                        .value(QStringLiteral("inputs"))
                                        .toObject()
                                        .value(QStringLiteral("latent_image"))
                                        .toArray();
    if (refineLatent.size() >= 1 && refineLatent.at(0).toString() == QLatin1String("2"))
        return QStringLiteral("3");

    const QJsonArray model = workflow.value(QStringLiteral("3"))
                               .toObject()
                               .value(QStringLiteral("inputs"))
                               .toObject()
                               .value(QStringLiteral("model"))
                               .toArray();
    if (model.size() >= 1)
        return model.at(0).toString();
    return QStringLiteral("4");
}

QString samplerPositiveNodeId(const QJsonObject &workflow)
{
    const QJsonArray pos = workflow.value(QStringLiteral("3"))
                               .toObject()
                               .value(QStringLiteral("inputs"))
                               .toObject()
                               .value(QStringLiteral("positive"))
                               .toArray();
    if (pos.size() >= 1)
        return pos.at(0).toString();
    return QStringLiteral("6");
}

ComfyWorkflowEngine::AnimationFrameParams animationFrameParamsFromDock(const ComfyUIRemoteDock::Private *d,
                                                                       int frameIndex,
                                                                       qint64 batchBaseSeed,
                                                                       int batchSeedStep,
                                                                       const QString &styleArch)
{
    ComfyWorkflowEngine::AnimationFrameParams af;
    const QString ckpt = d->comboCheckpoint->currentText().trimmed().isEmpty()
        ? QStringLiteral("v1-5-pruned-emaonly.safetensors")
        : d->comboCheckpoint->currentText().trimmed();
    af.base.arch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
    af.base.checkpoint = ckpt;
    const qint64 seed = ComfyWorkflowEngine::animationFrameSeed(batchBaseSeed, frameIndex, batchSeedStep);
    QString promptText = ComfyUIUtils::stripPromptComments(d->editPrompt->toPlainText()).trimmed();
    promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
    ComfyUIUtils::extractLayerPlaceholders(promptText);
    af.base.positivePrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(promptText);
    af.base.negativePrompt = ComfyUIUtils::evalWildcards(
        ComfyUIUtils::stripPromptComments(d->editNegative->toPlainText()).trimmed(), static_cast<quint32>(seed & 0xFFFFFFFFu));
    af.base.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
    af.base.denoise = (d->spinStrength ? d->spinStrength->value() : 100) / 100.0;
    af.base.sampler = d->comboSampler->currentText().trimmed().isEmpty() ? QStringLiteral("euler")
                                                                         : d->comboSampler->currentText().trimmed();
    af.base.scheduler = d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : d->ksamplerScheduler;
    af.base.steps = d->spinSteps->value();
    af.base.cfg = d->spinCfg->value();
    const bool animationFastSampling =
        d->comboWorkspace && d->comboWorkspace->currentIndex() == 3 && d->comboQuality
        && d->comboQuality->currentIndex() == 0;
    if (animationFastSampling) {
        const ComfyUIUtils::ResolvedSamplerInputs si = ComfyUIUtils::resolveSamplerForLive(
            ComfyUIUtils::loadSettingsJson(),
            d->comboSampler ? d->comboSampler->currentText() : QString(),
            d->spinSteps ? d->spinSteps->value() : 20,
            d->spinCfg ? d->spinCfg->value() : 8.0);
        af.base.sampler = si.sampler;
        af.base.scheduler = si.scheduler;
        af.base.steps = si.steps;
        af.base.cfg = si.cfg;
    }
    int w = d->spinWidth->value();
    int h = d->spinHeight->value();
    int genBatchTmp = d->spinBatchCount ? d->spinBatchCount->value() : 1;
    double genMul2 = d->resolutionMultiplier <= 0.0 ? 1.0 : d->resolutionMultiplier;
    const QJsonObject settingsRootBatch = ComfyUIUtils::loadSettingsJson();
    ComfyUIUtils::generationPerformanceBatchResolution(settingsRootBatch, d->lastComfySystemStats, genBatchTmp, genMul2,
                                                       &genBatchTmp, &genMul2);
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(settingsRootBatch, &genMul2);
    const double bmul = qMax(0.3, qMin(genMul2 <= 0.0 ? 1.0 : genMul2, 3.0));
    w = qBound(64, static_cast<int>(w * bmul), 8192);
    h = qBound(64, static_cast<int>(h * bmul), 8192);
    ComfyUIUtils::clampExtentToMaxMegapixels(&w, &h);
    af.base.width = w;
    af.base.height = h;
    af.base.batchSize = 1;
    af.batchBaseSeed = batchBaseSeed;
    af.frameIndex = frameIndex;
    af.batchSeedStep = batchSeedStep;
    return af;
}

} // namespace

// §13.126: End-to-end flow — user action → workflow build (check_color_mode) → queue per QueueMode → POST prompt → poll result → history + UI → Apply
void ComfyUIRemoteDock::slotGenerate()
{
    // FAITHFUL_PORT/DEBUG: snapshot every decision input at the top so logcat
    // shows exactly which precondition tripped when "nothing happens" on click.
    const QString dbgUrl = m_d->editServerUrl ? m_d->editServerUrl->text().trimmed() : QStringLiteral("<null editServerUrl>");
    const int dbgStrength = m_d->spinStrength ? m_d->spinStrength->value() : -1;
    const bool dbgEditMode = m_d->checkEditMode && m_d->checkEditMode->isChecked();
    const int dbgWorkspace = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
    const int dbgCustomLen = m_d->editCustomWorkflow ? m_d->editCustomWorkflow->toPlainText().trimmed().size() : -1;
    const bool dbgHasImage = m_d->viewManager && m_d->viewManager->image();
    const bool dbgBtnEnabled = m_d->btnGenerate && m_d->btnGenerate->isEnabled();
    const int dbgQueueDepth = m_d->jobQueue.size();
    const QString dbgCurrent = m_d->currentPromptId;
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
    if (m_d->btnGenerate && !m_d->btnGenerate->isEnabled()
        && m_d->currentPromptId.isEmpty() && m_d->jobQueue.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotGenerate: btnGenerate was stuck-disabled with no in-flight job; re-enabling";
        m_d->btnGenerate->setEnabled(true);
    }

    QString urlStr = m_d->editServerUrl ? m_d->editServerUrl->text().trimmed() : QString();
    if (urlStr.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Enter a server URL in Settings → Connection."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        setStatusMessage(ComfyTr::tr("Invalid server URL: %1", urlStr), true);
        return;
    }
    // FAITHFUL_PORT: on Android the device-local 127.0.0.1 is the tablet
    // itself, not the dev machine. Catch the common misconfiguration here
    // instead of letting the request silently time out and look like
    // "nothing happens".
    if (baseUrl.host() == QLatin1String("127.0.0.1") || baseUrl.host() == QLatin1String("localhost")) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotGenerate: server URL points at device loopback" << baseUrl.toString();
        setStatusMessage(
            ComfyTr::tr("Server URL is localhost (%1) — this is the tablet itself. Use your computer's LAN IP in Settings → Connection.",
                        baseUrl.host()), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    const bool graphWorkspace = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 4;
    if (graphWorkspace) {
        const QString graphJson = m_d->editCustomWorkflow ? m_d->editCustomWorkflow->toPlainText().trimmed() : QString();
        if (graphJson.isEmpty()) {
            setStatusMessage(
                ComfyTr::tr("Graph workspace: paste workflow JSON in Settings → Workflow, then click Generate."), true);
            return;
        }
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
                    ComfyTr::tr("Custom workflow must contain REFERENCE_IMAGE when using reference with Full Animation."), true);
                return;
            }
            QJsonParseError err;
            QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(customJson.toUtf8());
            QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                setStatusMessage(ComfyTr::tr("Custom workflow JSON error: %1", err.errorString()), true);
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
            if (!validateCustomWorkflowGraphOrShowError(this, m_d.data(), workflow))
                return;
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

            m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
            m_d->progressBar->setValue(0);
            m_d->btnGenerate->setEnabled(false);
            slotBatchSubmitNext();
            return;
        }

        KisImageSP image = m_d->viewManager->image();
        QImage refImg = ComfyUIUtils::getCanvasAsQImage(image);
        if (refImg.isNull()) {
            setStatusMessage(ComfyTr::tr("Could not export canvas for reference."), true);
            return;
        }
        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + ".png");
        tmp->open();
        tmp->close();
        if (!refImg.save(tmp->fileName())) {
            setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
            return;
        }
        m_d->labelStatus->setText(ComfyTr::tr("Uploading reference image…"));
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
                setStatusMessage(ComfyTr::tr("Upload error: %1", replyUp->errorString()), true);
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value("name").toString();
            if (refName.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            QString workflowText = m_d->editCustomWorkflow->toPlainText().replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(workflowText.toUtf8());
            QJsonDocument wdoc = QJsonDocument::fromJson(jsonBytes, &err);
            if (err.error != QJsonParseError::NoError || !wdoc.isObject()) {
                setStatusMessage(ComfyTr::tr("Workflow JSON error after reference replace."), true);
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
            if (!validateCustomWorkflowGraphOrShowError(this, m_d.data(), workflow)) {
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
            setStatusMessage(ComfyTr::tr("Custom workflow JSON error: %1", err.errorString()), true);
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
        if (!validateCustomWorkflowGraphOrShowError(this, m_d.data(), workflow))
            return;
        {
            KisImageSP wfImage = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
            ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, m_d->customWorkflowParamOverrides, wfImage);
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    } else {
        if (graphWorkspace) {
            setStatusMessage(
                ComfyTr::tr("Graph workspace requires a custom workflow JSON in Settings → Workflow."), true);
            return;
        }
        if (tryStartRefineFromGenerate()) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotGenerate: tryStartRefineFromGenerate took over (Refine path); returning";
            return;
        }
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
        const QList<Private::RegionEntry> regsForGen = regionsForGenerate(m_d.data());
        QString promptText = link.active
            ? ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, userPos).trimmed()
            : userPos;
        promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
        ComfyUIUtils::extractLayerPlaceholders(promptText);
        genParams.positivePrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(promptText);

        KisImageSP genImage = m_d->viewManager->image();
        const ComfyRegionProcess::ProcessRegionsResult processed =
            ComfyRegionProcess::processRegions(regsForGen, genImage, m_d->viewManager, genParams.positivePrompt);
        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion)
            genParams.positivePrompt = processed.effectivePositive;
        const QString negSrc =
            link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed();
        genParams.negativePrompt = ComfyUIUtils::evalWildcards(negSrc, static_cast<quint32>(seed & 0xFFFFFFFFu));
        genParams.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();

        workflow = ComfyWorkflowEngine::buildTextToImage(genParams);
        if (workflow.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Workflow JSON error."), true);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

        m_d->generatePendingBaseWorkflow = workflow;
        m_d->generatePendingArch = genParams.arch;
        m_d->generateStashedCustomJson = customJson;
        m_d->generateStashedBatch = genBatch;
        m_d->generateStashedMul = genMul;

        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion
            && ComfyResources::supportsRegions(genParams.arch)) {
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "slotGenerate: MultiRegion path, regions=" << processed.regions.size()
                << " arch=" << static_cast<int>(genParams.arch);
            m_d->generateProcessedRegions = processed.regions;
            m_d->generateRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
                m_d->generateProcessedRegions, genParams.promptTranslationLanguage);
            m_d->generateAwaitingRegionMaskUploads = true;
            m_d->generateRegionMaskUploadIndex = 0;
            m_d->btnGenerate->setEnabled(false);
            uploadNextGenerateRegionMask();
            return;
        }

        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotGenerate: SingleRegion / no-region path, dispatching upload pipeline w=" << genParams.width
            << " h=" << genParams.height
            << " steps=" << genParams.steps
            << " arch=" << static_cast<int>(genParams.arch)
            << " posLen=" << genParams.positivePrompt.size()
            << " negLen=" << genParams.negativePrompt.size();
        m_d->btnGenerate->setEnabled(false);
        beginGenerateUploadPipeline();
        return;
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

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotGenerate: dispatching batch submitIndex=0/" << m_d->batchCountTarget
        << " queueMode=" << m_d->batchQueueMode
        << " baseUrl=" << m_d->batchBaseUrl.toString()
        << " useCustomWorkflow=" << m_d->batchUseCustomWorkflow;
    m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
    m_d->progressBar->setValue(0);
    m_d->btnGenerate->setEnabled(false);
    slotBatchSubmitNext();
}

void ComfyUIRemoteDock::slotBatchSubmitNext()
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotBatchSubmitNext: index=" << m_d->batchSubmitIndex
        << "/" << m_d->batchCountTarget
        << " useCustomWorkflow=" << m_d->batchUseCustomWorkflow
        << " needsPerFrameRef=" << m_d->batchNeedsPerFrameReference;
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
            setStatusMessage(ComfyTr::tr("Could not export canvas for reference."), true);
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
            setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
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
        m_d->labelStatus->setText(ComfyTr::tr("Uploading reference image…"));
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
                setStatusMessage(ComfyTr::tr("Upload error: %1", replyUp->errorString()), true);
                abortAnimBatch();
                return;
            }
            const QString refName = QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (refName.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
                abortAnimBatch();
                return;
            }
            QJsonDocument tmpl(m_d->batchCustomWorkflow);
            QString wt = QString::fromUtf8(tmpl.toJson(QJsonDocument::Compact));
            if (!wt.contains(QStringLiteral("REFERENCE_IMAGE"))) {
                setStatusMessage(ComfyTr::tr("Workflow lost REFERENCE_IMAGE placeholder."), true);
                abortAnimBatch();
                return;
            }
            wt.replace(QStringLiteral("REFERENCE_IMAGE"), refName);
            QJsonParseError err;
            QJsonDocument wdoc = QJsonDocument::fromJson(wt.toUtf8(), &err);
            if (err.error != QJsonParseError::NoError || !wdoc.isObject()) {
                setStatusMessage(ComfyTr::tr("Workflow JSON error after reference replace."), true);
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
            if (!validateCustomWorkflowGraphOrShowError(this, m_d.data(), workflow)) {
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
        QString styleArch;
        if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                styleArch = st->architecture;
        }
        const ComfyWorkflowEngine::AnimationFrameParams af = animationFrameParamsFromDock(
            m_d.data(), m_d->batchSubmitIndex, m_d->batchBaseSeed, m_d->batchSeedStep, styleArch);
        workflow = ComfyWorkflowEngine::buildAnimationFrame(af);
        if (workflow.isEmpty())
            return;
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
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotBatchSubmitNext POST url=" << m_d->batchBaseUrl.toString()
        << " bodyBytes=" << body.size()
        << " submitIndex=" << submitIndex
        << " expectedPromptId=" << expectedPromptId;
    QNetworkReply *reply = m_d->nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, submitIndex, expectedPromptId]() {
        reply->deleteLater();
        const QByteArray respBody = reply->readAll();
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotBatchSubmitNext REPLY submitIndex=" << submitIndex
            << " httpStatus=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            << " err=" << reply->error()
            << " errStr=" << (reply->error() == QNetworkReply::NoError ? QString() : reply->errorString())
            << " bodyBytes=" << respBody.size();
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
                setStatusMessage(ComfyTr::tr("Submit error: %1", reply->errorString()), true);
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
            setStatusMessage(ComfyTr::tr("No prompt_id in response."), true);
            abortBatchState();
            return;
        }
        if (promptId != expectedPromptId) {
            setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
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
        setStatusMessage(ComfyTr::tr("No document open."), true);
        return;
    }
    QString docPath = m_d->canvas->imageView()->document()->path();
    if (docPath.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Save the document first to use Import Animation."), true);
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
        setStatusMessage(ComfyTr::tr("No frame folder found. Use .animation or .live-frames next to the document."), true);
        return;
    }
    std::sort(indexedFiles.begin(), indexedFiles.end(), [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first < b.first; });
    QStringList pathList;
    for (const auto &p : indexedFiles)
        pathList.append(p.second);
    KisImageSP image = m_d->canvas->image().toStrongRef();
    if (!image) {
        setStatusMessage(ComfyTr::tr("No image in document."), true);
        return;
    }
    KisAnimationImporter importer(image);
    KisImportExportErrorCode result = importer.import(pathList, 0, 1, false, false, 1);
    if (!result.isOk() && !result.isInternalError()) {
        setStatusMessage(result.errorMessage().isEmpty() ? ComfyTr::tr("Import failed.") : result.errorMessage(), true);
        return;
    }
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    setStatusMessage(ComfyTr::tr("Imported %1 frames.", pathList.size()));
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
    setStatusMessage(ComfyTr::tr("Cancelled."));
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
        setStatusMessage(ComfyTr::tr("Enter a server URL."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
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
    stopControlLayerJobPolling();
    if (m_d->btnControlPreviewRun)
        m_d->btnControlPreviewRun->setEnabled(false);
    if (m_d->labelControlPreviewImage) {
        m_d->labelControlPreviewImage->clear();
        m_d->labelControlPreviewImage->setText(ComfyTr::tr("Uploading…"));
    }

    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
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
        setStatusMessage(ComfyTr::tr("Could not save temporary image for upload."), true);
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
            setStatusMessage(ComfyTr::tr("Control preview upload failed: %1", reply->errorString()), true);
            if (m_d->labelControlPreviewImage)
                m_d->labelControlPreviewImage->clear();
            stopControlPreviewPolling();
            return;
        }
        const QString uploadedName = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (uploadedName.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Control preview: server did not return an image name."), true);
            stopControlPreviewPolling();
            return;
        }
        QJsonObject workflow = ComfyUIUtils::buildControlImageWorkflow(uploadedName, mode, resBase, false);
        if (workflow.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Unsupported control mode for preprocessor preview."), true);
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
                setStatusMessage(ComfyTr::tr("Control preview prompt failed: %1", replyP->errorString()), true);
                stopControlPreviewPolling();
                return;
            }
            const QString promptId =
                QJsonDocument::fromJson(replyP->readAll()).object().value(QStringLiteral("prompt_id")).toString();
            if (promptId.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Control preview: empty prompt_id from server."), true);
                stopControlPreviewPolling();
                return;
            }
            if (promptId != expectedPromptId) {
                setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                stopControlPreviewPolling();
                return;
            }
            m_d->controlPreviewPromptId = promptId;
            m_d->controlPreviewPollCount = 0;
            if (m_d->labelControlPreviewImage)
                m_d->labelControlPreviewImage->setText(ComfyTr::tr("Running preprocessor…"));
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
            setStatusMessage(ComfyTr::tr("Control preview history request failed: %1", reply->errorString()), true);
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
                setStatusMessage(ComfyTr::tr("Control preview timed out waiting for server."), true);
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
            setStatusMessage(ComfyTr::tr("Control preview: no output image in history."), true);
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
                setStatusMessage(ComfyTr::tr("Control preview image download failed: %1", replyV->errorString()), true);
                if (m_d->labelControlPreviewImage)
                    m_d->labelControlPreviewImage->clear();
                m_d->controlPreviewHandsCompositeBack = false;
                return;
            }
            QImage img;
            if (!img.loadFromData(replyV->readAll())) {
                setStatusMessage(ComfyTr::tr("Control preview: could not decode image."), true);
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
            setStatusMessage(ComfyTr::tr("Control preprocessor preview finished."), false);
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
        setStatusMessage(ComfyTr::tr("Invalid server or document."), true);
        m_d->generateAwaitingControlUploads = false;
        m_d->btnGenerate->setEnabled(true);
        return;
    }

    while (m_d->generateControlUploadIndex < m_d->generateControlLayersActive.size()) {
        const ComfyControlLayerEntry ce = m_d->generateControlLayersActive.at(m_d->generateControlUploadIndex);
        m_d->generateControlUploadIndex++;
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;

        QImage img = ComfyUIUtils::getLayerProjectionAsQImage(image, ce.layerName);
        if (img.isNull()) {
            setStatusMessage(ComfyTr::tr("Could not export control layer \"%1\".", ce.layerName), true);
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
            setStatusMessage(ComfyTr::tr("Could not save control layer image."), true);
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
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(fname)));
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
                m_d->generateAwaitingControlUploads = false;
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            const QString name = QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return control image name."), true);
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

void ComfyUIRemoteDock::uploadNextGenerateRegionMask()
{
    if (!m_d->generateAwaitingRegionMaskUploads || !m_d->viewManager) {
        m_d->btnGenerate->setEnabled(true);
        return;
    }
    while (m_d->generateRegionMaskUploadIndex < m_d->generateProcessedRegions.size()) {
        const int inputIdx = m_d->generateRegionMaskUploadIndex;
        m_d->generateRegionMaskUploadIndex++;
        if (inputIdx >= m_d->generateRegionalInputs.size())
            continue;

        const ComfyRegionProcess::ProcessedRegionEntry &region = m_d->generateProcessedRegions.at(inputIdx);
        if (region.maskGray.isNull()) {
            setStatusMessage(ComfyTr::tr("Region mask is empty."), true);
            m_d->generateAwaitingRegionMaskUploads = false;
            m_d->btnGenerate->setEnabled(true);
            return;
        }
        const QString regionLabel =
            region.isBackground ? ComfyTr::tr("background") : QString::number(inputIdx);
        const QImage maskPng = maskPngForComfyUpload(region.maskGray);
        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!maskPng.save(tmp->fileName())) {
            setStatusMessage(ComfyTr::tr("Could not save region mask."), true);
            m_d->generateAwaitingRegionMaskUploads = false;
            m_d->btnGenerate->setEnabled(true);
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
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"region_mask_%1.png\"")
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
                m_d->generateAwaitingRegionMaskUploads = false;
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            const QString name =
                QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty() || inputIdx >= m_d->generateRegionalInputs.size()) {
                setStatusMessage(ComfyTr::tr("Server did not return region mask name."), true);
                m_d->generateAwaitingRegionMaskUploads = false;
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            m_d->generateRegionalInputs[inputIdx].maskImageName = name;
            uploadNextGenerateRegionMask();
        });
        return;
    }

    m_d->generateAwaitingRegionMaskUploads = false;
    if (m_d->generateRefineAfterRegions) {
        QJsonObject workflow;
        QString regionMask;
        for (const ComfyWorkflowEngine::RegionalPromptInput &ri : m_d->generateRegionalInputs) {
            if (!ri.isBackground && !ri.maskImageName.isEmpty()) {
                regionMask = ri.maskImageName;
                break;
            }
        }
        if (!regionMask.isEmpty()) {
            ComfyWorkflowEngine::RefineRegionParams rrp;
            rrp.refine = m_d->generateStashedRefineParams;
            rrp.maskImageName = regionMask;
            rrp.growMaskBy = ComfyUIUtils::clampInpaintGrowFeather(
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("selection_grow_offset")).toInt(4));
            rrp.colorMatch = ComfyUIUtils::settingsColorMatchEnabled();
            workflow = ComfyWorkflowEngine::buildRefineRegion(rrp);
        }
        if (workflow.isEmpty())
            workflow = ComfyWorkflowEngine::buildRefine(m_d->generateStashedRefineParams);
        if (workflow.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Refine workflow error."), true);
            m_d->generateRefineAfterRegions = false;
            m_d->btnGenerate->setEnabled(true);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        m_d->generatePendingBaseWorkflow = workflow;
        m_d->generatePendingArch = m_d->generateStashedRefineParams.arch;
        m_d->generateRefineAfterRegions = false;
    }
    beginGenerateUploadPipeline();
}

void ComfyUIRemoteDock::beginGenerateUploadPipeline()
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "beginGenerateUploadPipeline ENTER isConnected=" << m_d->isConnected
        << " hasNam=" << (m_d->nam != nullptr);
    m_d->generateLoraUploadPaths.clear();
    if (m_d->isConnected && m_d->nam) {
        ComfyFileLibrary::instance().init();
        for (const ComfyFileRecord *rec :
             ComfyFileLibrary::instance().localLorasMissingOnServer(m_d->comfyServerLoraFilenames)) {
            if (rec && !rec->path.isEmpty())
                m_d->generateLoraUploadPaths.append(rec->path);
        }
    }
    if (!m_d->generateLoraUploadPaths.isEmpty()) {
        m_d->generateAwaitingLoraUploads = true;
        m_d->generateLoraUploadIndex = 0;
        uploadNextGenerateLoraFile();
        return;
    }
    m_d->generateControlLayersActive = controlLayersForGenerate(m_d.data());
    if (ComfyControlLayer::anyNeedsGenerateUpload(m_d->generateControlLayersActive)) {
        m_d->generateAwaitingControlUploads = true;
        m_d->generateControlUploadIndex = 0;
        m_d->generateControlUploadedNames.clear();
        uploadNextGenerateControlImage();
        return;
    }
    finalizeGenerateWorkflowAndSubmit(m_d->generatePendingBaseWorkflow);
}

void ComfyUIRemoteDock::uploadNextGenerateLoraFile()
{
    if (!m_d->generateAwaitingLoraUploads || !m_d->nam) {
        m_d->btnGenerate->setEnabled(true);
        return;
    }
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        m_d->generateAwaitingLoraUploads = false;
        m_d->btnGenerate->setEnabled(true);
        return;
    }
    while (m_d->generateLoraUploadIndex < m_d->generateLoraUploadPaths.size()) {
        const QString path = m_d->generateLoraUploadPaths.at(m_d->generateLoraUploadIndex++);
        const QString baseName = QFileInfo(path).fileName();
        if (baseName.isEmpty() || !QFile::exists(path))
            continue;

        m_d->labelStatus->setText(ComfyTr::tr("Uploading LoRA %1…", baseName));
        setProgressBarKind(true);
        QNetworkReply *reply = ComfyUIUtils::tryUploadLoraFileViaEtnApi(m_d->nam, urlStr, path, this);
        if (!reply) {
            setProgressBarKind(false);
            setStatusMessage(ComfyTr::tr("Could not read LoRA file %1 for upload.", baseName), true);
            m_d->generateAwaitingLoraUploads = false;
            m_d->btnGenerate->setEnabled(true);
            return;
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, baseName]() {
            reply->deleteLater();
            setProgressBarKind(false);
            const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code = codeVar.isValid() ? codeVar.toInt() : 0;
            const bool ok =
                (reply->error() == QNetworkReply::NoError && (code == 200 || code == 201 || code == 204));
            if (!ok) {
                const QString codeStr = code > 0 ? QString::number(code) : QStringLiteral("—");
                setStatusMessage(
                    ComfyTr::tr("LoRA upload failed for %1 (HTTP %2). Install the file on the server or use Styles → Upload.",
                                baseName,
                                codeStr),
                    true);
                m_d->generateAwaitingLoraUploads = false;
                m_d->btnGenerate->setEnabled(true);
                return;
            }
            if (!m_d->comfyServerLoraFilenames.contains(baseName, Qt::CaseInsensitive)) {
                m_d->comfyServerLoraFilenames.append(baseName);
                m_d->comfyServerLoraFilenames.sort(Qt::CaseInsensitive);
                ComfyFileLibrary::instance().init();
                ComfyFileLibrary::instance().updateRemoteLoras(m_d->comfyServerLoraFilenames);
            }
            uploadNextGenerateLoraFile();
        });
        return;
    }
    continueGenerateAfterLoraUploads();
}

void ComfyUIRemoteDock::continueGenerateAfterLoraUploads()
{
    m_d->generateAwaitingLoraUploads = false;
    m_d->generateControlLayersActive = controlLayersForGenerate(m_d.data());
    if (ComfyControlLayer::anyNeedsGenerateUpload(m_d->generateControlLayersActive)) {
        m_d->generateAwaitingControlUploads = true;
        m_d->generateControlUploadIndex = 0;
        m_d->generateControlUploadedNames.clear();
        uploadNextGenerateControlImage();
        return;
    }
    finalizeGenerateWorkflowAndSubmit(m_d->generatePendingBaseWorkflow);
}

void ComfyUIRemoteDock::continueGenerateAfterControlUploads()
{
    m_d->generateAwaitingControlUploads = false;
    finalizeGenerateWorkflowAndSubmit(m_d->generatePendingBaseWorkflow);
}

void ComfyUIRemoteDock::finalizeGenerateWorkflowAndSubmit(QJsonObject workflow)
{
    ComfyWorkflowEngine::applyCheckpointStyleOptions(
        &workflow, m_d->generateStyleVae, m_d->generateStyleClipSkip, m_d->generateStyleArch);

    QList<ComfyWorkflowEngine::IpAdapterLayerInput> ipInputs;
    QList<ComfyWorkflowEngine::ControlNetLayerInput> cnInputs;
    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : m_d->generateControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= m_d->generateControlUploadedNames.size())
            break;
        const QString imageName = m_d->generateControlUploadedNames.at(uploadIdx++);
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
    conditioning.regions = m_d->generateRegionalInputs;
    conditioning.editReference = ComfyResources::supportsEditInstructions(m_d->generatePendingArch);

    ComfyWorkflowEngine::WorkflowGraphContext ctx = ComfyWorkflowEngine::discoverWorkflowGraphContext(workflow);
    ComfyWorkflowEngine::applyGenerationConditioning(&workflow, conditioning, ctx, m_d->generatePendingArch);

    if (ComfyWorkflowEngine::usesSamplerCustomAdvanced(m_d->generatePendingArch)) {
        double denoise = 1.0;
        if (workflow.contains(ctx.samplerNodeId)) {
            denoise = workflow.value(ctx.samplerNodeId)
                          .toObject()
                          .value(QStringLiteral("inputs"))
                          .toObject()
                          .value(QStringLiteral("denoise"))
                          .toDouble(1.0);
        }
        ComfyWorkflowEngine::finishWorkflowWithSamplerCustom(
            &workflow, ctx.samplerNodeId, m_d->generatePendingArch, ctx.extentWidth, ctx.extentHeight, denoise);
    }

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

    m_d->generateRegionalInputs.clear();
    m_d->generateProcessedRegions.clear();
    m_d->generateControlLayersActive.clear();
    m_d->generateRefineAfterRegions = false;

    m_d->labelStatus->setText(ComfyTr::tr("Submitting…"));
    m_d->progressBar->setValue(0);
    m_d->btnGenerate->setEnabled(false);
    slotBatchSubmitNext();
}

bool ComfyUIRemoteDock::tryStartRefineFromGenerate()
{
    const int strengthPct = m_d->spinStrength ? m_d->spinStrength->value() : 100;
    const bool editMode = m_d->checkEditMode && m_d->checkEditMode->isChecked();
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "tryStartRefineFromGenerate strengthPct=" << strengthPct
        << " editMode=" << editMode
        << " customWorkflowLen="
        << (m_d->editCustomWorkflow ? m_d->editCustomWorkflow->toPlainText().trimmed().size() : -1);
    if (strengthPct >= 100 && !editMode) {
        qCWarning(KIS_COMFYUI_REMOTE) << "tryStartRefineFromGenerate: strength=100 && !editMode → returning false (normal Generate path)";
        return false;
    }
    if (!m_d->editCustomWorkflow->toPlainText().trimmed().isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "tryStartRefineFromGenerate: custom workflow present → returning false";
        return false;
    }

    KisImageSP image = m_d->viewManager->image();
    KisSelectionSP sel = m_d->viewManager->selection();
    const bool hasSelection =
        sel && sel->pixelSelection() && !sel->pixelSelection()->selectedExactRect().isEmpty();
    if (hasSelection && !ComfyUIUtils::isSelectionEntireDocument(image, m_d->viewManager)) {
        slotInpaint();
        return true;
    }

    uploadCanvasForRefineGenerate();
    return true;
}

void ComfyUIRemoteDock::uploadCanvasForRefineGenerate()
{
    KisImageSP image = m_d->viewManager->image();
    const QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image).convertToFormat(QImage::Format_ARGB32);
    if (canvasImg.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
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

    QTemporaryFile *tmp = new QTemporaryFile(this);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }

    m_d->btnGenerate->setEnabled(false);
    m_d->progressBar->setValue(0);
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
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(ComfyTr::tr("Uploading canvas for refine…"));
    setProgressBarKind(true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, image]() {
        reply->deleteLater();
        setProgressBarKind(false);
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            m_d->btnGenerate->setEnabled(true);
            return;
        }
        m_d->generateRefineUploadedImageName =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (m_d->generateRefineUploadedImageName.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            m_d->btnGenerate->setEnabled(true);
            return;
        }

        const QJsonObject settingsRoot = ComfyUIUtils::loadSettingsJson();
        int genBatch = m_d->spinBatchCount ? m_d->spinBatchCount->value() : 1;
        double genMul = m_d->resolutionMultiplier <= 0.0 ? 1.0 : m_d->resolutionMultiplier;
        ComfyUIUtils::generationPerformanceBatchResolution(settingsRoot, m_d->lastComfySystemStats, genBatch, genMul,
                                                           &genBatch, &genMul);
        const int genBatchMax = m_d->spinBatchCount ? m_d->spinBatchCount->maximum() : 16;
        genBatch = qBound(1, genBatch, genBatchMax);

        qint64 seed = m_d->checkFixedSeed->isChecked()
            ? static_cast<qint64>(m_d->spinSeed->value())
            : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        if (!m_d->checkFixedSeed->isChecked())
            m_d->spinSeed->setValue(static_cast<int>(seed));

        const bool editMode = m_d->checkEditMode && m_d->checkEditMode->isChecked();
        const ComfyUIUtils::LinkedEditStyleOverride link = ComfyUIUtils::linkedEditStyleOverride(
            editMode,
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

        QString userPos = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
        QString promptText = link.active
            ? ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, userPos).trimmed()
            : userPos;
        promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
        ComfyUIUtils::extractLayerPlaceholders(promptText);

        ComfyWorkflowEngine::RefineParams rp;
        rp.checkpoint = link.checkpoint;
        rp.imageName = m_d->generateRefineUploadedImageName;
        rp.arch = ComfyWorkflowEngine::resolveArch(link.checkpoint, styleArch);
        rp.seed = seed;
        rp.steps = link.steps;
        rp.cfg = link.cfg;
        rp.denoise = link.denoise;
        rp.sampler = link.sampler;
        rp.scheduler = link.scheduler;
        rp.positivePrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(promptText);
        const QString negSrc =
            link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed();
        rp.negativePrompt = ComfyUIUtils::evalWildcards(negSrc, static_cast<quint32>(seed & 0xFFFFFFFFu));
        rp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();

        m_d->generateStashedCustomJson.clear();
        m_d->generateStashedBatch = genBatch;
        m_d->generateStashedMul = 1.0;
        m_d->generateRefineAfterRegions = false;

        const QList<Private::RegionEntry> regsForRefine = regionsForGenerate(m_d.data());
        const ComfyRegionProcess::ProcessRegionsResult processed =
            ComfyRegionProcess::processRegions(regsForRefine, image, m_d->viewManager, promptText);
        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion)
            rp.positivePrompt =
                ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(processed.effectivePositive);

        if (processed.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::MultiRegion
            && ComfyResources::supportsRegions(rp.arch)) {
            m_d->generateStashedRefineParams = rp;
            m_d->generateRefineAfterRegions = true;
            m_d->generateProcessedRegions = processed.regions;
            m_d->generateRegionalInputs = ComfyRegionProcess::toRegionalWorkflowInputs(
                m_d->generateProcessedRegions, rp.promptTranslationLanguage);
            m_d->generateAwaitingRegionMaskUploads = true;
            m_d->generateRegionMaskUploadIndex = 0;
            uploadNextGenerateRegionMask();
            return;
        }

        QJsonObject workflow = ComfyWorkflowEngine::buildRefine(rp);
        if (workflow.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Refine workflow error."), true);
            m_d->btnGenerate->setEnabled(true);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);

        m_d->generatePendingBaseWorkflow = workflow;
        m_d->generatePendingArch = rp.arch;
        m_d->generateRegionalInputs.clear();
        m_d->generateProcessedRegions.clear();
        beginGenerateUploadPipeline();
    });
}
