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

#include <KoUpdater.h>
#include <KSharedConfig>

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

void onGenerateAnimation(ComfyUIRemoteDock *dock)
{

    dock->m_d->batchNeedsPerFrameReference = false;
    dock->m_d->batchNeedsPerFrameAnimationRefine = false;
    dock->m_d->animationBatchSourcePathByFrame.clear();
    dock->m_d->animationBatchFrameTimes.clear();
    dock->m_d->animationImportStartFrame = 0;
    dock->m_d->animationBatchRangeStart = 0;
    dock->m_d->animationBatchRangeEnd = 0;
    dock->m_d->animationBatchGroupId.clear();

    // §13.177 / §13.74: frame count from active playback range; per-frame timeline indices for filenames + import
    int frames = dock->m_d->spinAnimationFrames->value();
    if (dock->m_d->viewManager && dock->m_d->viewManager->image()) {
        KisImageSP img = dock->m_d->viewManager->image();
        if (img->animationInterface() && img->animationInterface()->hasAnimation()) {
            const KisTimeSpan range = img->animationInterface()->activePlaybackRange();
            if (range.isValid()) {
                frames = qBound(2, range.duration(), dock->m_d->spinAnimationFrames->maximum());
                dock->m_d->animationImportStartFrame = range.start();
                dock->m_d->animationBatchRangeStart = range.start();
                dock->m_d->animationBatchRangeEnd = range.start() + frames - 1;
                dock->m_d->animationBatchFrameTimes.reserve(frames);
                for (int i = 0; i < frames; ++i)
                    dock->m_d->animationBatchFrameTimes.append(range.start() + i);
            }
        }
    }
    if (dock->m_d->animationBatchFrameTimes.isEmpty()) {
        dock->m_d->animationImportStartFrame = 0;
        dock->m_d->animationBatchRangeStart = 0;
        dock->m_d->animationBatchRangeEnd = qMax(0, frames - 1);
    }
    dock->m_d->generate.spinBatchCount->setValue(frames);
    dock->m_d->generate.checkFixedSeed->setChecked(true);
    // §13.45: Full Animation — .animation/frame-{time}.png, shared animation_id in prompt extra_data
    if (dock->m_d->radioFullAnimation && dock->m_d->radioFullAnimation->isChecked()) {
        dock->m_d->isFullAnimationBatch = true;
        dock->m_d->animationBatchPromptIdToIndex.clear();
        dock->m_d->animationBatchSourcePathByFrame.clear();
        dock->m_d->animationBatchGroupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    onGenerate(dock);

}
void onImportAnimation(ComfyUIRemoteDock *dock)
{

    // §13.45: Import frames from .animation (frame-*.png) or .live-frames (frame-*.webp) into document
    if (!dock->m_d->canvas || !dock->m_d->canvas->imageView() || !dock->m_d->canvas->imageView()->document()) {
        dock->setStatusMessage(ComfyTr::tr("No document open."), true);
        return;
    }
    QString docPath = dock->m_d->canvas->imageView()->document()->path();
    if (docPath.isEmpty()) {
        dock->setStatusMessage(ComfyTr::tr("Save the document first to use Import Animation."), true);
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
        dock->setStatusMessage(ComfyTr::tr("No frame folder found. Use .animation or .live-frames next to the document."), true);
        return;
    }
    std::sort(indexedFiles.begin(), indexedFiles.end(), [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first < b.first; });
    QStringList pathList;
    for (const auto &p : indexedFiles)
        pathList.append(p.second);
    KisImageSP image = dock->m_d->canvas->image().toStrongRef();
    if (!image) {
        dock->setStatusMessage(ComfyTr::tr("No image in document."), true);
        return;
    }
    KisAnimationImporter importer(image);
    KisImportExportErrorCode result = importer.import(pathList, 0, 1, false, false, 1);
    if (!result.isOk() && !result.isInternalError()) {
        dock->setStatusMessage(result.errorMessage().isEmpty() ? ComfyTr::tr("Import failed.") : result.errorMessage(), true);
        return;
    }
    if (dock->m_d->canvas)
        dock->m_d->canvas->updateCanvas();
    dock->setStatusMessage(ComfyTr::tr("Imported %1 frames.", pathList.size()));

}
void onCancelQueue(ComfyUIRemoteDock *dock)
{

    QStringList localPromptIds;
    const auto trackId = [&localPromptIds](const QString &id) {
        if (!id.isEmpty() && !localPromptIds.contains(id))
            localPromptIds.append(id);
    };

    for (const QString &id : dock->m_d->jobQueue)
        trackId(id);
    trackId(dock->m_d->currentPromptId);
    for (const QString &id : dock->m_d->batchCollectIds)
        trackId(id);
    for (auto it = dock->m_d->animationBatchPromptIdToIndex.constBegin();
         it != dock->m_d->animationBatchPromptIdToIndex.constEnd();
         ++it)
        trackId(it.key());
    for (auto it = dock->m_d->history.pendingHistoryByPromptId.constBegin();
         it != dock->m_d->history.pendingHistoryByPromptId.constEnd();
         ++it)
        trackId(it.key());
    trackId(dock->m_d->inpaintRt.inpaintPromptId);
    trackId(dock->m_d->upscaleRt.upscalePromptId);
    trackId(dock->m_d->liveRt.livePromptId);
    trackId(dock->m_d->generateRt.controlPreviewPromptId);
    trackId(dock->m_d->generateRt.controlLayerJobPromptId);

    dock->m_d->pollTimer->stop();
    dock->m_d->inpaintRt.inpaintPollTimer->stop();
    dock->m_d->upscaleRt.upscalePollTimer->stop();
    dock->m_d->liveRt.livePollTimer->stop();
    if (dock->m_d->generateRt.controlPreviewPollTimer)
        dock->m_d->generateRt.controlPreviewPollTimer->stop();
    if (dock->m_d->generateRt.controlLayerJobPollTimer)
        dock->m_d->generateRt.controlLayerJobPollTimer->stop();
    if (dock->m_d->customGraphLiveTimer)
        dock->m_d->customGraphLiveTimer->stop();
    dock->m_d->customGraphLiveActive = false;

    dock->m_d->batchNeedsPerFrameReference = false;
    dock->m_d->batchNeedsPerFrameAnimationRefine = false;
    dock->m_d->isFullAnimationBatch = false;
    dock->m_d->animationBatchPromptIdToIndex.clear();
    dock->m_d->animationBatchSourcePathByFrame.clear();
    dock->m_d->animationBatchFrameTimes.clear();
    dock->m_d->animationBatchGroupId.clear();
    dock->m_d->batchCollectIds.clear();
    dock->m_d->batchSubmitIndex = 0;
    dock->m_d->batchCountTarget = 0;
    clearBatchCaptureStash(dock->m_d.data());

    dock->m_d->history.pendingHistoryByPromptId.clear();
    dock->m_d->jobQueue.clear();
    dock->m_d->currentPromptId.clear();
    dock->m_d->pollCount = 0;

    dock->m_d->inpaintRt.inpaintPromptId.clear();
    dock->m_d->upscaleRt.upscalePromptId.clear();
    dock->m_d->liveRt.livePromptId.clear();
    dock->m_d->generateRt.controlPreviewPromptId.clear();
    dock->m_d->generateRt.controlPreviewPollCount = 0;
    dock->m_d->generateRt.controlLayerJobPromptId.clear();

    dock->resetProgressBarToIdle();
    dock->reEnableGenerateUi();
    if (dock->m_d->inpaint.btnInpaint)
        dock->m_d->inpaint.btnInpaint->setEnabled(true);
    if (dock->m_d->upscale.btnUpscale)
        dock->m_d->upscale.btnUpscale->setEnabled(true);
    if (dock->m_d->generate.btnControlPreviewRun)
        dock->m_d->generate.btnControlPreviewRun->setEnabled(true);
    dock->m_d->generate.btnCancelQueue->setEnabled(false);
    dock->setStatusMessage(ComfyTr::tr("Cancelled."));
    dock->updateQueueStatus();

    const QString urlStr = dock->m_d->editServerUrl ? dock->m_d->editServerUrl->text().trimmed() : QString();
    if (urlStr.isEmpty() || !dock->m_d->nam)
        return;
    ComfyUIUtils::requestComfyClearAllQueueJobs(dock->m_d->nam, urlStr, localPromptIds, dock);

}
bool cancelCurrentJob(ComfyUIRemoteDock *dock)
{

    const QString activeId = dock->m_d->currentPromptId;
    if (activeId.isEmpty())
        return false;
    dock->m_d->pollTimer->stop();
    dock->m_d->history.pendingHistoryByPromptId.remove(activeId);
    dock->m_d->currentPromptId.clear();
    dock->m_d->pollCount = 0;
    if (!dock->m_d->jobQueue.isEmpty()) {
        dock->m_d->currentPromptId = dock->m_d->jobQueue.takeFirst();
        dock->startPolling();
    } else {
        dock->resetProgressBarToIdle();
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
    }
    dock->updateQueueStatus();
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
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
        dock->m_d->nam->post(reqInt, QByteArray("{}"));
    }
    return true;

}
void cancelQueuedJobs(ComfyUIRemoteDock *dock)
{

    const QStringList toCancel = dock->m_d->jobQueue;
    if (toCancel.isEmpty())
        return;
    for (const QString &id : toCancel)
        dock->m_d->history.pendingHistoryByPromptId.remove(id);
    dock->m_d->jobQueue.clear();
    dock->updateQueueStatus();
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
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
    dock->m_d->nam->post(reqQueue, QJsonDocument(delPayload).toJson(QJsonDocument::Compact));

}

} // namespace ComfyGenerateRunner
