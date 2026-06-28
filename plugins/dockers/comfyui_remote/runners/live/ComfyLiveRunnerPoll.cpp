/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLiveRunner.h"
#include "ComfyLiveRunnerInternal.h"

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

#include <QDir>
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
#include <KisDocument.h>


using namespace ComfyLiveRunnerInternal;

namespace ComfyLiveRunner {

void onPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->liveRt.livePromptId.isEmpty() || !dock->m_d->live.checkLiveMode->isChecked())
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { dock->m_d->liveRt.livePromptId.clear(); dock->stopLiveSpinner(); dock->m_d->liveRt.liveTimer->start(30000); return; }
    const QString promptId = dock->m_d->liveRt.livePromptId;
    ComfyPromptClient::fetchHistory(dock->m_d->nam, urlStr, promptId, dock,
                                    [dock, urlStr](const ComfyPromptClient::HistoryFetchResult &result) {
        if (!dock->m_d->live.checkLiveMode->isChecked()) {
            dock->m_d->liveRt.livePromptId.clear();
            dock->stopLiveSpinner();
            return;
        }
        const auto failLive = [dock]() {
            dock->m_d->liveRt.livePromptId.clear();
            dock->stopLiveSpinner();
            dock->m_d->liveRt.liveTimer->start(30000);
        };
        ComfyPollRunnerCommon::PollRunningConfig running;
        running.pollCount = &dock->m_d->liveRt.livePollCount;
        running.maxPollCount = LiveRuntime::liveMaxPollCount;
        running.pollTimer = dock->m_d->liveRt.livePollTimer;
        running.onTick = [dock]() {
            dock->setLiveProgress((dock->m_d->liveRt.livePollCount * 100)
                                  / LiveRuntime::liveMaxPollCount);
        };
        running.onTimeout = failLive;
        const auto terminal = [dock, failLive](const ComfyPromptClient::HistoryFetchResult &r) {
            if (r.state == ComfyPromptClient::HistoryState::ExecutionError)
                dock->setStatusMessage(r.errorMessage, true);
            failLive();
        };
        if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
            == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
            return;
        ComfyPromptClient::downloadOutputImage(dock->m_d->nam, urlStr, result.images.first(), dock,
                                               [dock](const QByteArray &data, const QString &errorMessage) {
            if (!dock->m_d->live.checkLiveMode->isChecked()) { dock->m_d->liveRt.livePromptId.clear(); dock->stopLiveSpinner(); return; }
            if (!errorMessage.isEmpty()) { dock->m_d->liveRt.livePromptId.clear(); dock->stopLiveSpinner(); dock->m_d->liveRt.liveTimer->start(30000); return; }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (tmp.open()) {
                tmp.write(data);
                tmp.close();
                QImage resultImg;
                if (resultImg.load(tmp.fileName())) {
                    resultImg = cropLiveResultToTarget(resultImg, dock->m_d->liveRt.livePrepared);
                    resultImg.save(tmp.fileName());
                }
                {
                    const QString cachePath =
                        QDir(ComfyUIUtils::historyCacheDir()).filePath(QStringLiteral("last_live_result.png"));
                    QFile::remove(cachePath);
                    if (QFile::copy(tmp.fileName(), cachePath))
                        dock->m_d->liveRt.lastLiveResultImagePath = cachePath;
                }
                if (dock->m_d->viewManager && dock->m_d->viewManager->image()) {
                    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
                    const QRect contextBounds =
                        prep.contextBounds.isValid() ? prep.contextBounds : dock->m_d->viewManager->image()->bounds();
                    const QRect placement =
                        prep.hasMask && prep.maskPaddedBounds.isValid() ? prep.maskPaddedBounds : contextBounds;
                    const QImage composition = ComfyUIUtils::compositeLiveResultPreview(
                        dock->m_d->viewManager->image(), contextBounds, placement, resultImg, true);
                    if (!composition.isNull()) {
                        const QString compPath =
                            QDir(ComfyUIUtils::historyCacheDir()).filePath(QStringLiteral("last_live_composition.png"));
                        QFile::remove(compPath);
                        if (composition.save(compPath))
                            dock->m_d->liveRt.lastLiveResultCompositionPath = compPath;
                        dock->updateLiveResultPreview(composition, contextBounds.topLeft());
                    }
                }
                // §13.45: When Record is on, save frame to .live-frames/frame-N.webp
                if (dock->m_d->live.checkLiveRecord && dock->m_d->live.checkLiveRecord->isChecked() && dock->m_d->canvas && dock->m_d->canvas->imageView() && dock->m_d->canvas->imageView()->document()) {
                    QString docPath = dock->m_d->canvas->imageView()->document()->path();
                    if (!docPath.isEmpty()) {
                        QString framePath = ComfyUIUtils::liveFramePath(docPath, dock->m_d->liveRt.liveFrameIndex);
                        QDir().mkpath(QFileInfo(framePath).absolutePath());
                        QImage img;
                        if (img.load(tmp.fileName()) && img.save(framePath, "webp"))
                            dock->m_d->liveRt.liveFrameIndex++;
                    }
                }
                if (dock->m_d->viewManager->imageManager()) {
                    QJsonObject ls = ComfyUIUtils::loadSettingsJson();
                    QString regionBeh = ls.value(QStringLiteral("apply_region_behavior_live")).toString();
                    if (regionBeh.isEmpty())
                        regionBeh = QStringLiteral("replace");
                    QStringList regionLayerNames;
                    if (!dock->m_d->liveRt.liveRegionalInputs.isEmpty()) {
                        for (const ComfyUIRemoteDock::ComfyUIRemoteDock::Private::RegionEntry &re : comfyActiveRegionEntries(dock->m_d.data())) {
                            if (re.maskSource.startsWith(QLatin1String("layer:")))
                                regionLayerNames.append(re.maskSource.mid(6));
                        }
                    }
                    bool applied = false;
                    if (!regionLayerNames.isEmpty() && regionBeh != QLatin1String("none"))
                        applied = dock->applyResultToNamedRegionLayers(tmp.fileName(), regionLayerNames, regionBeh);
                    if (!applied) {
                        QString liveBeh = ls.value(QStringLiteral("apply_behavior_live")).toString();
                        if (liveBeh.isEmpty())
                            liveBeh = QStringLiteral("replace");
                        QRect resultBounds;
                        if (dock->m_d->liveRt.livePrepared.hasMask && !dock->m_d->liveRt.livePrepared.maskPaddedBounds.isEmpty())
                            resultBounds = dock->m_d->liveRt.livePrepared.maskPaddedBounds;
                        applied = dock->applyResultFileWithBehavior(tmp.fileName(), liveBeh, QString(), resultBounds);
                    }
                    if (applied && ls.value(QStringLiteral("new_seed_after_apply")).toBool(false) && dock->m_d->generate.spinSeed) {
                        dock->m_d->generate.spinSeed->setValue(
                            static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
                    }
                }
            }
            dock->m_d->liveRt.livePromptId.clear();
            dock->stopLiveSpinner();
            if (dock->m_d->live.checkLiveMode->isChecked()) dock->m_d->liveRt.liveTimer->start(30000);
        });
    });
}

} // namespace ComfyLiveRunner
