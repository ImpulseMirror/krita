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

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

using namespace ComfyLiveRunnerInternal;
using namespace ComfyInpaintRunnerInternal;

namespace ComfyLiveRunner {

void onPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->liveRt.livePromptId.isEmpty() || !dock->m_d->live.checkLiveMode->isChecked())
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { dock->m_d->liveRt.livePromptId.clear(); dock->stopLiveSpinner(); return; }
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
            dock->m_d->liveRt.livePipelineBusy = false;
            dock->m_d->liveRt.liveScheduler.notifyGenerationFinished(QDateTime::currentMSecsSinceEpoch());
            dock->stopLiveSpinner();
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
            if (r.state == ComfyPromptClient::HistoryState::ExecutionError) {
                qCWarning(KIS_COMFYUI_REMOTE).noquote()
                    << QStringLiteral("COMFY_LIVE generation failed:") << r.errorMessage;
                dock->setStatusMessage(r.errorMessage, true);
            }
            failLive();
        };
        if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
            == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
            return;
        ComfyPromptClient::downloadOutputImage(dock->m_d->nam, urlStr, result.images.first(), dock,
                                               [dock](const QByteArray &data, const QString &errorMessage) {
            if (!dock->m_d->live.checkLiveMode->isChecked()) { dock->m_d->liveRt.livePromptId.clear(); dock->stopLiveSpinner(); return; }
            if (!errorMessage.isEmpty()) { dock->m_d->liveRt.livePromptId.clear(); dock->m_d->liveRt.livePipelineBusy = false; dock->stopLiveSpinner(); return; }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (tmp.open()) {
                tmp.write(data);
                tmp.close();
                QImage resultImg;
                QImage applyImage;
                if (resultImg.load(tmp.fileName())) {
                    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
                    applyImage = resultImg;
                    if (prep.hasMask
                        || prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion) {
                        applyImage = compositeLiveServerResult(resultImg, prep);
                    }
                    if (!applyImage.isNull())
                        applyImage.save(tmp.fileName());

                    const double rawNonBlack = imageNonBlackFraction(resultImg);
                    const double compositeNonBlack = imageNonBlackFraction(applyImage);
                    const bool refineRegion =
                        prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
                    const QString verdict = inpaintFailureVerdict(rawNonBlack,
                                                                  compositeNonBlack,
                                                                  dock->m_d->liveRt.liveDiagLatentPath,
                                                                  dock->m_d->liveRt.liveDiagArchKey,
                                                                  dock->m_d->liveRt.liveDiagDenoise,
                                                                  refineRegion);
                    {
                        const QImage contextImage =
                            prep.nativeContextImage.isNull() ? prep.contextImage : prep.nativeContextImage;
                        const QImage compositingMask = prep.nativeCompositingMask.isNull()
                                                           ? prep.compositingMaskCropped
                                                           : prep.nativeCompositingMask;
                        InpaintDiagSnapshot diag;
                        diag.event = QStringLiteral("composite");
                        diag.pluginVersion = ComfyUIUtils::pluginVersion();
                        diag.workflowKind = refineRegion ? QStringLiteral("refine_region")
                                                         : QStringLiteral("live");
                        diag.archKey = dock->m_d->liveRt.liveDiagArchKey;
                        diag.refineRegion = refineRegion;
                        diag.latentPath = dock->m_d->liveRt.liveDiagLatentPath;
                        diag.denoise = dock->m_d->liveRt.liveDiagDenoise;
                        diag.strength0to1 = prep.strength0to1;
                        diag.contextBounds = prep.contextBounds;
                        diag.targetBoundsRelative = prep.maskPaddedBounds.translated(-prep.contextBounds.topLeft());
                        diag.nativeContextSize = contextImage.size();
                        diag.diffusionExtent = prep.diffusionExtent;
                        diag.rawNonBlack = rawNonBlack;
                        diag.compositeNonBlack = compositeNonBlack;
                        diag.verdict = verdict;
                        diag.serverPixels = describeImagePixels(resultImg, QStringLiteral("rawDownload"));
                        diag.outputPixels = describeImagePixels(applyImage, QStringLiteral("applyImage"));
                        diag.contextPixels = describeImagePixels(contextImage, QStringLiteral("context"));
                        diag.maskPixels = describeImagePixels(compositingMask, QStringLiteral("mask"));
                        logLiveDiag(diag);
                    }
                    if (verdict != QLatin1String("ok")) {
                        qCWarning(KIS_COMFYUI_REMOTE).noquote()
                            << QStringLiteral("LIVE_DIAG FAILURE verdict=%1 rawNonBlack=%2 compositeNonBlack=%3 "
                                              "latentPath=%4 arch=%5 serverSize=%6x%7 contextSize=%8x%9")
                                   .arg(verdict)
                                   .arg(rawNonBlack, 0, 'f', 3)
                                   .arg(compositeNonBlack, 0, 'f', 3)
                                   .arg(dock->m_d->liveRt.liveDiagLatentPath)
                                   .arg(dock->m_d->liveRt.liveDiagArchKey)
                                   .arg(resultImg.width())
                                   .arg(resultImg.height())
                                   .arg(prep.contextImage.width())
                                   .arg(prep.contextImage.height());
                    }
                }
                {
                    const QString cachePath =
                        QDir(ComfyUIUtils::historyCacheDir()).filePath(QStringLiteral("last_live_result.png"));
                    QFile::remove(cachePath);
                    if (QFile::copy(tmp.fileName(), cachePath))
                        dock->m_d->liveRt.lastLiveResultImagePath = cachePath;
                    dock->updateLiveToolbarState();
                }
                if (dock->m_d->viewManager && dock->m_d->viewManager->image() && !resultImg.isNull()) {
                    const ComfyPrepareLiveWorkflow::Result &prep = dock->m_d->liveRt.livePrepared;
                    const bool refineRegion =
                        prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
                    const QRect contextBounds =
                        prep.contextBounds.isValid() ? prep.contextBounds : dock->m_d->viewManager->image()->bounds();
                    const QRect placement =
                        prep.hasMask && prep.maskPaddedBounds.isValid() ? prep.maskPaddedBounds : contextBounds;
                    const QImage contextCapture =
                        prep.nativeContextImage.isNull() ? prep.contextImage : prep.nativeContextImage;
                    QImage dockerPreview;
                    if ((prep.hasMask || refineRegion) && !applyImage.isNull()) {
                        // Masked refine: show client composited merge (same pixels manual apply uses).
                        dockerPreview = applyImage;
                    } else if (!contextCapture.isNull()) {
                        const double rawNonBlack = imageNonBlackFraction(resultImg);
                        if (rawNonBlack >= 0.05) {
                            const QImage highlightResult =
                                prep.hasMask ? cropLiveResultToTarget(resultImg, prep) : resultImg;
                            dockerPreview = ComfyUIUtils::compositeLiveResultPreviewFromContext(
                                contextCapture, contextBounds, placement, highlightResult, true);
                        } else {
                            dockerPreview = resultImg;
                        }
                    }
                    if (!dockerPreview.isNull()) {
                        const QString compPath =
                            QDir(ComfyUIUtils::historyCacheDir()).filePath(QStringLiteral("last_live_composition.png"));
                        QFile::remove(compPath);
                        if (dockerPreview.save(compPath))
                            dock->m_d->liveRt.lastLiveResultCompositionPath = compPath;
                        dock->showLiveDockerPreview(dockerPreview);
                    }
                }
                // FAITHFUL_PORT: upstream LiveWorkspace only updates preview on each frame;
                // apply to canvas is manual (apply / apply-layer toolbar buttons). Auto-apply
                // here raced mergeDown against preview-layer updates → SIGSEGV in MergeMetaData.
                qCWarning(KIS_COMFYUI_REMOTE).noquote()
                    << QStringLiteral("COMFY_LIVE poll result cached preview updated path=")
                    << dock->m_d->liveRt.lastLiveResultImagePath;
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
            }
            dock->m_d->liveRt.livePromptId.clear();
            dock->m_d->liveRt.livePipelineBusy = false;
            dock->m_d->liveRt.liveScheduler.notifyGenerationFinished(QDateTime::currentMSecsSinceEpoch());
            dock->stopLiveSpinner();
        });
    });
}

} // namespace ComfyLiveRunner
