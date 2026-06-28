/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyInpaintRunner.h"
#include "ComfyInpaintRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionLink.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyHistoryInternal.h"
#include "ComfyUiLayoutDiagnostics.h"
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

#include <KisViewManager.h>
#include <kis_image.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)


using namespace ComfyInpaintRunnerInternal;

namespace ComfyInpaintRunner {

void onPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->inpaintRt.inpaintPromptId.isEmpty())
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->m_d->inpaintRt.inpaintPromptId.clear();
        dock->reEnableGenerateUi();
        dock->m_d->progressBar->setValue(0);
        return;
    }
    const QString promptId = dock->m_d->inpaintRt.inpaintPromptId;
    ComfyPromptClient::fetchHistory(dock->m_d->nam, urlStr, promptId, dock,
                                    [dock, urlStr, promptId](const ComfyPromptClient::HistoryFetchResult &result) {
        const auto failInpaint = [dock]() {
            dock->m_d->inpaintRt.inpaintPromptId.clear();
            dock->reEnableGenerateUi();
            dock->m_d->progressBar->setValue(0);
        };
        ComfyPollRunnerCommon::PollRunningConfig running;
        running.pollCount = &dock->m_d->inpaintRt.inpaintPollCount;
        running.maxPollCount = InpaintRuntime::inpaintMaxPollCount;
        running.pollTimer = dock->m_d->inpaintRt.inpaintPollTimer;
        running.onTimeout = [dock, failInpaint]() {
            dock->setStatusMessage(ComfyTr::tr("Inpaint timed out."), true);
            failInpaint();
        };
        const auto terminal = [dock, failInpaint](const ComfyPromptClient::HistoryFetchResult &r) {
            if (r.state == ComfyPromptClient::HistoryState::NetworkError)
                dock->setStatusMessage(ComfyTr::tr("History error: %1", r.errorMessage), true);
            else if (r.state == ComfyPromptClient::HistoryState::ExecutionError)
                dock->setStatusMessage(r.errorMessage, true);
            failInpaint();
        };
        if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
            == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
            return;

        if (result.images.isEmpty()) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaintPoll: history done but no output images for" << promptId;
            failInpaint();
            return;
        }
        const ComfyPromptClient::OutputImage outputPick = result.images.first();
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotInpaintPoll: downloading history output file=" << outputPick.filename
            << " subfolder=" << outputPick.subfolder << " promptId=" << promptId
            << " workflow="
            << (dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow ? "refine_region" : "inpaint");

        ComfyPromptClient::downloadOutputImage(dock->m_d->nam, urlStr, outputPick, dock,
                                               [dock, promptId](const QByteArray &data, const QString &errorMessage) {
            if (!errorMessage.isEmpty()) {
                qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaintPoll: download failed:" << errorMessage;
                dock->m_d->inpaintRt.inpaintPromptId.clear();
                dock->reEnableGenerateUi();
                dock->m_d->progressBar->setValue(0);
                return;
            }
            QImage resultImg;
            resultImg.loadFromData(data);
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "slotInpaintPoll: downloaded bytes=" << data.size() << " "
                << describeImagePixels(resultImg, QStringLiteral("rawDownload"));

            const QImage contextImage = dock->m_d->inpaintRt.inpaintNativeContextImage.isNull()
                                            ? dock->m_d->inpaintRt.inpaintCurrentImage
                                            : dock->m_d->inpaintRt.inpaintNativeContextImage;
            const QImage compositingMask = dock->m_d->inpaintRt.inpaintNativeCompositingMask.isNull()
                                               ? dock->m_d->inpaintRt.inpaintCompositingMaskCropped
                                               : dock->m_d->inpaintRt.inpaintNativeCompositingMask;

            InpaintCompositeParams compositeParams;
            compositeParams.serverResult = resultImg;
            compositeParams.contextImage = contextImage;
            compositeParams.compositingMask = compositingMask;
            compositeParams.contextBounds = dock->m_d->inpaintRt.inpaintContextBounds;
            compositeParams.targetBounds = dock->m_d->inpaintRt.inpaintTargetBounds;
            compositeParams.preprocessGrow = dock->m_d->inpaintRt.inpaintPreprocessGrow;
            compositeParams.preprocessFeather = dock->m_d->inpaintRt.inpaintPreprocessFeather;
            compositeParams.preprocessBlend = dock->m_d->inpaintRt.inpaintPreprocessBlend;
            compositeParams.diffusionExtent = dock->m_d->inpaintRt.inpaintDiffusionExtent;
            compositeParams.refineRegionWorkflow = dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow;
            compositeParams.serverPreMasked =
                dock->m_d->inpaintRt.inpaintServerMaskedOutput && !compositeParams.refineRegionWorkflow;

            const InpaintCompositeResult composite = compositeInpaintServerOntoContext(compositeParams);
            const QImage outputImage = composite.output;

            const double rawNonBlack = imageNonBlackFraction(resultImg);
            const double compositeNonBlack = imageNonBlackFraction(outputImage);
            const QString verdict = inpaintFailureVerdict(rawNonBlack,
                                                          compositeNonBlack,
                                                          dock->m_d->inpaintRt.inpaintDiagLatentPath,
                                                          dock->m_d->inpaintRt.inpaintDiagArchKey,
                                                          dock->m_d->inpaintRt.inpaintDiagDenoise,
                                                          compositeParams.refineRegionWorkflow);
            {
                InpaintDiagSnapshot diag;
                diag.event = QStringLiteral("composite");
                diag.pluginVersion = ComfyUIUtils::pluginVersion();
                diag.workflowKind =
                    compositeParams.refineRegionWorkflow ? QStringLiteral("refine_region") : QStringLiteral("inpaint");
                diag.archKey = dock->m_d->inpaintRt.inpaintDiagArchKey;
                diag.refineRegion = compositeParams.refineRegionWorkflow;
                diag.serverPreMasked = compositeParams.serverPreMasked;
                diag.latentPath = dock->m_d->inpaintRt.inpaintDiagLatentPath;
                diag.denoise = dock->m_d->inpaintRt.inpaintDiagDenoise;
                diag.contextBounds = compositeParams.contextBounds;
                diag.targetBoundsRelative =
                    compositeParams.targetBounds.translated(-compositeParams.contextBounds.topLeft());
                diag.nativeContextSize = contextImage.size();
                diag.diffusionExtent = compositeParams.diffusionExtent;
                diag.rawNonBlack = rawNonBlack;
                diag.compositeNonBlack = compositeNonBlack;
                diag.compositePath = composite.pathTaken;
                diag.verdict = verdict;
                diag.serverPixels = describeImagePixels(resultImg, QStringLiteral("rawDownload"));
                diag.outputPixels = describeImagePixels(outputImage, QStringLiteral("output"));
                logInpaintDiag(diag);
            }
            if (verdict != QLatin1String("ok")) {
                qCWarning(KIS_COMFYUI_REMOTE).noquote()
                    << QStringLiteral("INPAINT_DIAG FAILURE verdict=%1 rawNonBlack=%2 compositeNonBlack=%3 "
                                      "latentPath=%4 arch=%5 serverSize=%6x%7 contextSize=%8x%9")
                           .arg(verdict)
                           .arg(rawNonBlack, 0, 'f', 3)
                           .arg(compositeNonBlack, 0, 'f', 3)
                           .arg(dock->m_d->inpaintRt.inpaintDiagLatentPath)
                           .arg(dock->m_d->inpaintRt.inpaintDiagArchKey)
                           .arg(resultImg.width())
                           .arg(resultImg.height())
                           .arg(contextImage.width())
                           .arg(contextImage.height());
            }

            const QString cachePath = ComfyUIUtils::historyCacheDir() + QStringLiteral("/")
                + (promptId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : promptId)
                + QStringLiteral(".png");
            if (QFile::exists(cachePath))
                QFile::remove(cachePath);
            if (!outputImage.save(cachePath)) {
                qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaintPoll: failed to save cache" << cachePath;
                dock->m_d->inpaintRt.inpaintPromptId.clear();
                dock->m_d->inpaintRt.inpaintPendingEntry = ComfyUIRemoteDock::Private::HistoryEntry();
                dock->reEnableGenerateUi();
                dock->m_d->progressBar->setValue(0);
                return;
            }
            {
                QImage maskSidecar = dock->m_d->inpaintRt.inpaintNativeCompositingMask;
                if (maskSidecar.isNull())
                    maskSidecar = dock->m_d->inpaintRt.inpaintCompositingMaskCropped;
                ComfyHistoryInternal::saveHistoryCompositingMaskSidecar(cachePath, maskSidecar);
            }
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "slotInpaintPoll: saved cache=" << cachePath << " path=" << composite.pathTaken
                << " contextBounds=" << dock->m_d->inpaintRt.inpaintContextBounds
                << " targetBounds=" << dock->m_d->inpaintRt.inpaintTargetBounds;

            dock->rememberHistoryPreviewImage(cachePath, outputImage);
            ComfyUIRemoteDock::Private::HistoryEntry entry = dock->m_d->inpaintRt.inpaintPendingEntry;
            entry.jobId = promptId;
            entry.resultImagePath = cachePath;
            entry.resultImagePaths = QStringList() << cachePath;
            entry.width = outputImage.width();
            entry.height = outputImage.height();
            entry.finishedAt = QDateTime::currentDateTime();
            {
                QImage maskSidecar = dock->m_d->inpaintRt.inpaintNativeCompositingMask;
                if (maskSidecar.isNull())
                    maskSidecar = dock->m_d->inpaintRt.inpaintCompositingMaskCropped;
                ComfyHistoryInternal::saveHistoryDisplayThumbnail(cachePath, entry, outputImage, maskSidecar);
                qCWarning(KIS_COMFYUI_REMOTE).noquote()
                    << QStringLiteral("COMFY_UI_DIAG inpaintPoll.historySaved jobId=") << promptId
                    << QStringLiteral("cache=") << cachePath
                    << QStringLiteral("contextBounds=") << entry.contextBounds
                    << QStringLiteral("targetBounds=") << entry.targetBounds
                    << QStringLiteral("hasMask=") << entry.hasMask
                    << QStringLiteral("maskNull=") << maskSidecar.isNull()
                    << QStringLiteral("thumbExists=")
                    << QFile::exists(ComfyHistoryInternal::historyThumbnailSidecarPath(cachePath));
            }
            dock->m_d->history.historyEntries.append(entry);
            while (dock->m_d->history.historyEntries.size() > HistoryState::maxHistoryEntries) {
                ComfyUIRemoteDock::Private::HistoryEntry old = dock->m_d->history.historyEntries.takeFirst();
                dock->evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                QStringList paths = old.resultImagePaths;
                if (paths.isEmpty() && !old.resultImagePath.isEmpty())
                    paths << old.resultImagePath;
                for (const QString &p : paths) {
                    if (!p.isEmpty() && QFile::exists(p))
                        QFile::remove(p);
                }
            }
            dock->pruneHistoryToStorageLimit();
            dock->persistTopHistoryEntryToDocument(false);
            dock->handleGenerationFinished(cachePath, false);
            dock->m_d->inpaintRt.inpaintPendingEntry = ComfyUIRemoteDock::Private::HistoryEntry();
            dock->m_d->inpaintRt.inpaintFullCanvasImage = QImage();
            dock->m_d->inpaintRt.inpaintCompositingMaskCropped = QImage();
            dock->m_d->inpaintRt.inpaintNativeContextImage = QImage();
            dock->m_d->inpaintRt.inpaintNativeCompositingMask = QImage();
            dock->m_d->inpaintRt.inpaintNativeContextSize = QSize();
            dock->m_d->inpaintRt.inpaintDiffusionExtent = QSize();
            dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow = false;
            dock->m_d->inpaintRt.inpaintServerMaskedOutput = false;
            dock->m_d->inpaintRt.inpaintDiagLatentPath.clear();
            dock->m_d->inpaintRt.inpaintDiagArchKey.clear();
            dock->m_d->inpaintRt.inpaintDiagDenoise = -1.0;
            dock->m_d->inpaintRt.inpaintContextBounds = QRect();
            dock->m_d->inpaintRt.inpaintTargetBounds = QRect();
            dock->m_d->inpaintRt.inpaintPreprocessGrow = 0;
            dock->m_d->inpaintRt.inpaintPreprocessFeather = 0;
            dock->m_d->inpaintRt.inpaintPreprocessBlend = 0;
            dock->m_d->inpaintRt.inpaintFromRegionLayer = false;
            dock->m_d->labelStatus->setText(ComfyTr::tr("Inpaint done."));
            dock->m_d->progressBar->setValue(100);
            dock->m_d->inpaintRt.inpaintPromptId.clear();
            dock->reEnableGenerateUi();
        });
    });
}

} // namespace ComfyInpaintRunner
