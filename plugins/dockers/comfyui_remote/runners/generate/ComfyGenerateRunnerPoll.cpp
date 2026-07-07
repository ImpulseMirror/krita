/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyGenerateRunner.h"

#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPromptClient.h"
#include "ComfyHistoryInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSharedPointer>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <QDir>

#include <algorithm>

#include <KoUpdater.h>

#include <kis_animation_importer.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <KisDocument.h>
#include <KisImportExportErrorCode.h>
#include <KisViewManager.h>
#include <kis_node.h>
#include <kis_undo_adapter.h>
#include <commands/KisNodeRenameCommand.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyGenerateRunner {

void maybeContinueCustomGraphLive(ComfyUIRemoteDock *dock);

namespace {

void startNextQueuedGenerateJobIfAny(ComfyUIRemoteDock *dock)
{
    if (!dock->m_d->jobQueue.isEmpty()) {
        dock->m_d->currentPromptId = dock->m_d->jobQueue.takeFirst();
        dock->m_d->pollCount = 0;
        dock->startPolling();
    }
    dock->updateQueueStatus();
}

void advanceGenerateJobQueue(ComfyUIRemoteDock *dock, bool afterSuccess)
{
    if (!dock->m_d->jobQueue.isEmpty()) {
        dock->m_d->currentPromptId = dock->m_d->jobQueue.takeFirst();
        dock->m_d->pollCount = 0;
        dock->startPolling();
    } else if (afterSuccess) {
        maybeContinueCustomGraphLive(dock);
        if (!dock->m_d->customGraphLiveActive)
            dock->reEnableGenerateUi();
    } else {
        dock->reEnableGenerateUi();
    }
    dock->updateQueueStatus();
}

void failGeneratePoll(ComfyUIRemoteDock *dock, const QString &promptId, const QString &msg,
                      bool removePendingHistory = false)
{
    dock->setStatusMessage(msg, true);
    dock->resetProgressBarToIdle();
    dock->m_d->currentPromptId.clear();
    if (removePendingHistory)
        dock->m_d->history.pendingHistoryByPromptId.remove(promptId);
    advanceGenerateJobQueue(dock, false);
}

void failGenerateDownload(ComfyUIRemoteDock *dock, const QString &msg)
{
    dock->setStatusMessage(msg, true);
    dock->resetProgressBarToIdle();
    advanceGenerateJobQueue(dock, false);
}

} // namespace

void onPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->currentPromptId.isEmpty())
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty())
        return;
    const QString promptId = dock->m_d->currentPromptId;
    ComfyPromptClient::fetchHistory(dock->m_d->nam, urlStr, promptId, dock,
                                    [dock, urlStr, promptId](const ComfyPromptClient::HistoryFetchResult &result) {
            ComfyPollRunnerCommon::PollRunningConfig running;
            running.pollCount = &dock->m_d->pollCount;
            running.maxPollCount = ComfyUIRemoteDock::Private::maxPollCount;
            running.pollTimer = dock->m_d->pollTimer;
            running.onTick = [dock]() {
                dock->tickJobProgressBuffer();
                dock->updateQueueStatus();
            };
            running.onTimeout = [dock, promptId]() {
                failGeneratePoll(dock, promptId, ComfyTr::tr("Generation timed out."));
            };
            const auto terminal = [dock, promptId](const ComfyPromptClient::HistoryFetchResult &r) {
                if (r.state == ComfyPromptClient::HistoryState::NetworkError)
                    failGeneratePoll(dock, promptId, ComfyTr::tr("History error: %1", r.errorMessage));
                else if (r.state == ComfyPromptClient::HistoryState::ExecutionError) {
                    qCWarning(KIS_COMFYUI_REMOTE) << "poll: ComfyUI execution error:" << r.errorMessage;
                    failGeneratePoll(dock, promptId, r.errorMessage, true);
                } else if (r.state == ComfyPromptClient::HistoryState::NoImages)
                    failGeneratePoll(dock, promptId, ComfyTr::tr("No image in output."));
            };
            if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
                == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
                return;
            QVector<QPair<QString, QString>> imageInfos;
            for (const ComfyPromptClient::OutputImage &img : result.images)
                imageInfos.append(qMakePair(img.filename, img.subfolder));
            QString completedId = dock->m_d->currentPromptId;
            dock->m_d->currentPromptId.clear();
            if (dock->m_d->jobQueue.isEmpty()) {
                dock->reEnableGenerateUi();
            }
            QUrl baseUrl(dock->m_d->editServerUrl->text().trimmed());
            const int totalImages = imageInfos.size();
            // §13.131: Multi-image result — download all images and store in one entry
            if (totalImages > 1 && dock->m_d->history.pendingHistoryByPromptId.contains(completedId)) {
                ComfyUIRemoteDock::Private::HistoryEntry entry = dock->m_d->history.pendingHistoryByPromptId.take(completedId);
                entry.jobId = completedId;
                QSharedPointer<QMap<int, QString>> pathsByIndex(new QMap<int, QString>());
                for (int i = 0; i < totalImages; i++) {
                    QUrl viewUrl(baseUrl);
                    QString path = viewUrl.path();
                    if (!path.endsWith('/')) path += '/';
                    path += "view";
                    viewUrl.setPath(path);
                    QUrlQuery q;
                    q.addQueryItem("filename", imageInfos.at(i).first);
                    if (!imageInfos.at(i).second.isEmpty()) q.addQueryItem("subfolder", imageInfos.at(i).second);
                    viewUrl.setQuery(q);
                    QNetworkRequest req(viewUrl);
                    ComfyUIUtils::setComfyUIRequestHeaders(req);
                    QNetworkReply *getReply = dock->m_d->nam->get(req);
                    QObject::connect(getReply, &QNetworkReply::finished, dock, [dock, getReply, completedId, entry, totalImages, pathsByIndex, i, baseUrl]() mutable {
                        getReply->deleteLater();
                        if (getReply->error() != QNetworkReply::NoError) {
                            failGenerateDownload(dock, ComfyTr::tr("Download error: %1", getReply->errorString()));
                            return;
                        }
                        QByteArray data = getReply->readAll();
                        QString suffix = "png";
                        if (data.startsWith("\x89PNG")) suffix = "png";
                        else if (data.startsWith("\xff\xd8")) suffix = "jpg";
                        QString cachePath = ComfyUIUtils::historyCacheDir() + QStringLiteral("/") + completedId + QStringLiteral("_") + QString::number(i) + QStringLiteral(".") + suffix;
                        if (QFile::exists(cachePath)) QFile::remove(cachePath);
                        QFile f(cachePath);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(data);
                            f.close();
                            pathsByIndex->insert(i, cachePath);
                            if (dock->m_d->batchStashedHasMask && !dock->m_d->batchStashedCompositingMask.isNull())
                                ComfyHistoryInternal::saveHistoryCompositingMaskSidecar(cachePath,
                                                                                        dock->m_d->batchStashedCompositingMask);
                            if (dock->m_d->history.pendingHistoryByPromptId.contains(completedId)) {
                                const ComfyUIRemoteDock::Private::HistoryEntry &pending =
                                    dock->m_d->history.pendingHistoryByPromptId[completedId];
                                QImage downloaded;
                                if (downloaded.load(cachePath)) {
                                    ComfyHistoryInternal::saveHistoryDisplayThumbnail(
                                        cachePath, pending, downloaded, dock->m_d->batchStashedCompositingMask);
                                }
                            }
                        }
                        if (pathsByIndex->size() == totalImages) {
                            ComfyUIRemoteDock::Private::HistoryEntry e = entry;
                            for (int j = 0; j < totalImages; j++)
                                e.resultImagePaths.append(pathsByIndex->value(j));
                            e.resultImagePath = e.resultImagePaths.isEmpty() ? QString() : e.resultImagePaths.first();
                            e.finishedAt = QDateTime::currentDateTime();
                            dock->finishJobProgress();
                            dock->m_d->history.historyEntries.append(e);
                            while (dock->m_d->history.historyEntries.size() > HistoryState::maxHistoryEntries) {
                                ComfyUIRemoteDock::Private::HistoryEntry old = dock->m_d->history.historyEntries.takeFirst();
                                dock->evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                                QStringList paths = old.resultImagePaths;
                                if (paths.isEmpty() && !old.resultImagePath.isEmpty()) paths << old.resultImagePath;
                                for (const QString &p : paths) { if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p); }
                            }
                            dock->pruneHistoryToStorageLimit();
                            dock->persistTopHistoryEntryToDocument(false);
                            dock->handleGenerationFinished(e.resultImagePath, false);
                            startNextQueuedGenerateJobIfAny(dock);
                        }
                    });
                }
                return;
            }
            // Single image: one GET
            QUrl viewUrl(baseUrl);
            QString path = viewUrl.path();
            if (!path.endsWith('/')) path += '/';
            path += "view";
            viewUrl.setPath(path);
            QUrlQuery q;
            q.addQueryItem("filename", imageInfos.at(0).first);
            if (!imageInfos.at(0).second.isEmpty()) q.addQueryItem("subfolder", imageInfos.at(0).second);
            viewUrl.setQuery(q);
            QNetworkRequest req(viewUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(req);
            QNetworkReply *getReply = dock->m_d->nam->get(req);
            QObject::connect(getReply, &QNetworkReply::finished, dock, [dock, getReply, completedId]() {
                getReply->deleteLater();
                if (getReply->error() != QNetworkReply::NoError) {
                    failGenerateDownload(dock, ComfyTr::tr("Download error: %1", getReply->errorString()));
                    return;
                }
                QByteArray data = getReply->readAll();
                QString suffix = "png";
                if (data.startsWith("\x89PNG")) suffix = "png";
                else if (data.startsWith("\xff\xd8")) suffix = "jpg";
                QTemporaryFile tmp;
                tmp.setFileTemplate(tmp.fileTemplate() + "." + suffix);
                if (!tmp.open()) {
                    failGenerateDownload(dock, ComfyTr::tr("Could not create temp file."));
                    return;
                }
                tmp.write(data);
                tmp.close();
                QString cachePath;
                if (dock->m_d->history.pendingHistoryByPromptId.contains(completedId)) {
                    cachePath = ComfyUIUtils::historyCacheDir() + QStringLiteral("/") + completedId + QStringLiteral(".png");
                    if (QFile::exists(cachePath)) QFile::remove(cachePath);
                    if (QFile::copy(tmp.fileName(), cachePath)) {
                        dock->m_d->history.pendingHistoryByPromptId[completedId].resultImagePath = cachePath;
                        dock->m_d->history.pendingHistoryByPromptId[completedId].resultImagePaths = QStringList() << cachePath;
                        dock->m_d->history.pendingHistoryByPromptId[completedId].jobId = completedId;
                        if (dock->m_d->batchStashedHasMask && !dock->m_d->batchStashedCompositingMask.isNull())
                            ComfyHistoryInternal::saveHistoryCompositingMaskSidecar(cachePath,
                                                                                    dock->m_d->batchStashedCompositingMask);
                        if (dock->m_d->history.pendingHistoryByPromptId.contains(completedId)) {
                            const ComfyUIRemoteDock::Private::HistoryEntry &pending =
                                dock->m_d->history.pendingHistoryByPromptId[completedId];
                            QImage downloaded;
                            if (downloaded.load(cachePath)) {
                                ComfyHistoryInternal::saveHistoryDisplayThumbnail(
                                    cachePath, pending, downloaded, dock->m_d->batchStashedCompositingMask);
                            }
                        }
                    }
                }
                if (!dock->m_d->viewManager || !dock->m_d->viewManager->imageManager()) {
                    failGenerateDownload(dock, ComfyTr::tr("No document open."));
                    return;
                }
                dock->finishJobProgress();
                if (dock->m_d->history.pendingHistoryByPromptId.contains(completedId)) {
                    ComfyUIRemoteDock::Private::HistoryEntry entry = dock->m_d->history.pendingHistoryByPromptId.take(completedId);
                    entry.jobId = completedId;
                    if (entry.resultImagePaths.isEmpty() && !entry.resultImagePath.isEmpty())
                        entry.resultImagePaths = QStringList() << entry.resultImagePath;
                    const bool skipGenFinishedActions =
                        dock->m_d->isFullAnimationBatch && dock->m_d->animationBatchPromptIdToIndex.contains(completedId);
                    // §13.45 / §13.74: Full Animation — collect per-frame results; on last frame, build keyframes list
                    // (cached execution: same bytes as previous frame → reuse previous path in list).
                    if (dock->m_d->isFullAnimationBatch && dock->m_d->animationBatchPromptIdToIndex.contains(completedId)) {
                        const int frameIdx = dock->m_d->animationBatchPromptIdToIndex.take(completedId);
                        const QString docPath = (dock->m_d->canvas && dock->m_d->canvas->imageView() && dock->m_d->canvas->imageView()->document())
                            ? dock->m_d->canvas->imageView()->document()->path() : QString();
                        QString srcPath = entry.resultImagePath;
                        if (srcPath.isEmpty() && !entry.resultImagePaths.isEmpty())
                            srcPath = entry.resultImagePaths.first();
                        if (!srcPath.isEmpty() && QFile::exists(srcPath))
                            dock->m_d->animationBatchSourcePathByFrame.insert(frameIdx, srcPath);
                        if (dock->m_d->animationBatchPromptIdToIndex.isEmpty()) {
                            dock->m_d->isFullAnimationBatch = false;
                            dock->m_d->batchNeedsPerFrameReference = false;
                            dock->m_d->batchNeedsPerFrameAnimationRefine = false;
                            if (!docPath.isEmpty() && dock->m_d->canvas && dock->m_d->canvas->image()) {
                                QVector<int> frameOrder;
                                if (!dock->m_d->animationBatchFrameTimes.isEmpty()) {
                                    frameOrder = dock->m_d->animationBatchFrameTimes;
                                } else {
                                    for (int f = dock->m_d->animationBatchRangeStart; f <= dock->m_d->animationBatchRangeEnd; ++f)
                                        frameOrder.append(f);
                                }
                                if (frameOrder.isEmpty()) {
                                    QList<int> keys = dock->m_d->animationBatchSourcePathByFrame.keys();
                                    std::sort(keys.begin(), keys.end());
                                    for (int k : keys)
                                        frameOrder.append(k);
                                }
                                QStringList keyframePaths;
                                QString prevListPath;
                                QString prevSrcPath;
                                bool batchOk = true;
                                for (int ft : frameOrder) {
                                    const QString src = dock->m_d->animationBatchSourcePathByFrame.value(ft);
                                    if (src.isEmpty() || !QFile::exists(src)) {
                                        batchOk = false;
                                        break;
                                    }
                                    if (!keyframePaths.isEmpty() && !prevSrcPath.isEmpty()
                                        && ComfyUIUtils::filesContentsEqual(src, prevSrcPath)) {
                                        keyframePaths.append(prevListPath);
                                        prevSrcPath = src;
                                        continue;
                                    }
                                    const QString destPath = ComfyUIUtils::animationFramePath(docPath, ft);
                                    QDir().mkpath(QFileInfo(destPath).absolutePath());
                                    if (QFile::exists(destPath))
                                        QFile::remove(destPath);
                                    if (!QFile::copy(src, destPath)) {
                                        batchOk = false;
                                        break;
                                    }
                                    keyframePaths.append(destPath);
                                    prevListPath = destPath;
                                    prevSrcPath = src;
                                }
                                dock->m_d->animationBatchSourcePathByFrame.clear();
                                if (!batchOk || keyframePaths.size() != frameOrder.size()) {
                                    if (!batchOk)
                                        dock->setStatusMessage(ComfyTr::tr("Animation batch incomplete: missing or invalid frame files."), true);
                                } else if (!keyframePaths.isEmpty()) {
                                    KisImageSP img = dock->m_d->canvas->image().toStrongRef();
                                    if (img) {
                                        KisAnimationImporter importer(img);
                                        const int firstFrame = dock->m_d->animationImportStartFrame;
                                        KisImportExportErrorCode impRes =
                                            importer.import(keyframePaths, firstFrame, 1, false, false, 1);
                                        if (dock->m_d->canvas)
                                            dock->m_d->canvas->updateCanvas();
                                        if (impRes.isOk() || impRes.isInternalError()) {
                                            dock->setStatusMessage(ComfyTr::tr("Imported %1 animation frames.", keyframePaths.size()));
                                            // §13.74: rename imported layer "[Generated] {start}-{end}: {params.name}"
                                            if (dock->m_d->viewManager) {
                                                KisNodeSP an = dock->m_d->viewManager->activeNode();
                                                if (an && img->undoAdapter()) {
                                                    QString pfx = ComfyUIUtils::stripPromptComments(
                                                                      dock->m_d->generate.editPrompt->toPlainText())
                                                                      .trimmed();
                                                    if (pfx.length() > 48)
                                                        pfx = pfx.left(48) + QStringLiteral("...");
                                                    if (pfx.isEmpty())
                                                        pfx = ComfyTr::tr("Animation");
                                                    const QString newName = ComfyTr::tr("[Generated] %1-%2: %3",
                                                        dock->m_d->animationBatchRangeStart,
                                                        dock->m_d->animationBatchRangeEnd,
                                                        pfx);
                                                    img->undoAdapter()->addCommand(
                                                        new KisNodeRenameCommand(an, an->name(), newName));
                                                }
                                            }
                                        } else {
                                            dock->setStatusMessage(
                                                impRes.errorMessage().isEmpty()
                                                    ? ComfyTr::tr("Animation import failed.")
                                                    : impRes.errorMessage(),
                                                true);
                                        }
                                        dock->m_d->animationBatchFrameTimes.clear();
                                        dock->m_d->animationBatchGroupId.clear();
                                    }
                                }
                            }
                        }
                    }
                    entry.finishedAt = QDateTime::currentDateTime();
                    dock->m_d->history.historyEntries.append(entry);
                    while (dock->m_d->history.historyEntries.size() > HistoryState::maxHistoryEntries) {
                        ComfyUIRemoteDock::Private::HistoryEntry old = dock->m_d->history.historyEntries.takeFirst();
                        dock->evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                        QStringList paths = old.resultImagePaths;
                        if (paths.isEmpty() && !old.resultImagePath.isEmpty()) paths << old.resultImagePath;
                        for (const QString &p : paths) { if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p); }
                    }
                    dock->pruneHistoryToStorageLimit();
                    dock->persistTopHistoryEntryToDocument(skipGenFinishedActions);
                    const QString finishPath = entry.resultImagePath.isEmpty()
                        ? (entry.resultImagePaths.isEmpty() ? QString() : entry.resultImagePaths.first())
                        : entry.resultImagePath;
                    bool animTimelineMismatch = false;
                    if (!skipGenFinishedActions && dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3
                        && dock->m_d->radioSingleFrame && dock->m_d->radioSingleFrame->isChecked()
                        && entry.animationSubmitTime >= 0 && dock->m_d->viewManager) {
                        KisImageSP ki = dock->m_d->viewManager->image();
                        if (ki && ki->animationInterface() && ki->animationInterface()->hasAnimation())
                            animTimelineMismatch =
                                (ki->animationInterface()->currentTime() != entry.animationSubmitTime);
                    }
                    const bool animTarget = !skipGenFinishedActions
                        && dock->tryApplyAnimationSingleFrameToTargetLayer(finishPath, animTimelineMismatch);
                    if (!skipGenFinishedActions && dock->m_d->comboWorkspace && dock->m_d->comboWorkspace->currentIndex() == 3
                        && dock->m_d->radioSingleFrame && dock->m_d->radioSingleFrame->isChecked()) {
                        dock->updateAnimationResultPreview(finishPath);
                    }
                    if (animTimelineMismatch && !animTarget)
                        dock->setStatusMessage(ComfyTr::tr("Generated frame does not match current time."), false, true);
                    dock->handleGenerationFinished(finishPath, skipGenFinishedActions || animTarget);
                }
                advanceGenerateJobQueue(dock, true);
            });
    });
}

} // namespace ComfyGenerateRunner
