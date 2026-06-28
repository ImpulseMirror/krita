/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyControlRunner.h"
#include "ComfyControlRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyControlLayerListWidget.h"
#include "ComfyLocalization.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyOpenPose.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPromptClient.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIPoseLayers.h"
#include "ComfyUIUtils.h"

#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QPixmap>
#include <QRect>
#include <QSignalBlocker>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
#include <QWebSocket>
#endif

#include <KConfigGroup>
#include <KSharedConfig>

#include <KisDocument.h>
#include <KisPart.h>
#include <KisViewManager.h>

#include <kis_image.h>
#include <kis_image_manager.h>
#include <kis_layer.h>
#include <kis_node.h>
#include <kis_selection.h>
#include <kis_shape_layer.h>


using namespace ComfyControlRunnerInternal;

namespace ComfyControlRunner {

void stopPreviewPolling(ComfyUIRemoteDock *dock)
{

    if (dock->m_d->generateRt.controlPreviewPollTimer)
        dock->m_d->generateRt.controlPreviewPollTimer->stop();
    dock->m_d->generateRt.controlPreviewPromptId.clear();
    dock->m_d->generateRt.controlPreviewPollCount = 0;
    dock->m_d->generateRt.controlPreviewHandsCompositeBack = false;
    dock->m_d->generateRt.controlPreviewCompositeLocalRect = QRect();
    dock->m_d->generateRt.controlPreviewCompositeFullSize = QSize();
    if (dock->m_d->generate.btnControlPreviewRun)
        dock->m_d->generate.btnControlPreviewRun->setEnabled(true);

}
void syncPreviewRangeFromSettings(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->generate.controlPreviewRangeSlider)
        return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const int savedLow = cfg.readEntry(QStringLiteral("control_layer_timing_low_pct"), -1);
    const int savedHigh = cfg.readEntry(QStringLiteral("control_layer_timing_high_pct"), -1);
    if (savedLow >= 0 && savedHigh >= 0) {
        const int lo = qBound(0, savedLow, 100);
        const int hi = qBound(0, savedHigh, 100);
        QSignalBlocker b(dock->m_d->generate.controlPreviewRangeSlider);
        dock->m_d->generate.controlPreviewRangeSlider->setInterval(qMin(lo, hi), qMax(lo, hi));
        return;
    }
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const QJsonObject controlRoot = ComfyUIUtils::builtinControlPresetsRoot();
    const QList<ComfyUIUtils::ControlLayerPreset> cps =
        ComfyUIUtils::controlPresetsForMode(controlRoot, QStringLiteral("default"), QString());
    if (cps.isEmpty()) {
        QSignalBlocker b(dock->m_d->generate.controlPreviewRangeSlider);
        dock->m_d->generate.controlPreviewRangeSlider->setInterval(25, 75);
        return;
    }
    const int idx = qBound(0, s.value(QStringLiteral("control_layer_default_preset_index")).toInt(0), cps.size() - 1);
    const ComfyUIUtils::ControlLayerPreset &p = cps.at(idx);
    const int low = qBound(0, qRound(p.start * 100.0), 100);
    const int high = qBound(0, qRound(p.end * 100.0), 100);
    QSignalBlocker b(dock->m_d->generate.controlPreviewRangeSlider);
    dock->m_d->generate.controlPreviewRangeSlider->setInterval(qMin(low, high), qMax(low, high));

}
void syncPoseGuidePeopleCountFromSettings(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->generate.spinPoseGuidePeopleCount)
        return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const int n = qBound(1, cfg.readEntry(QStringLiteral("pose_guide_people_count"), 1), 3);
    QSignalBlocker b(dock->m_d->generate.spinPoseGuidePeopleCount);
    dock->m_d->generate.spinPoseGuidePeopleCount->setValue(n);

}
void onPreviewRun(ComfyUIRemoteDock *dock)
{

    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->setStatusMessage(ComfyTr::tr("Enter a server URL."), true);
        return;
    }
    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        dock->setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = dock->m_d->viewManager->image();
    const auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        dock->setStatusMessage(colorCheck.second, true);
        return;
    }
    const QString mode =
        dock->m_d->generate.comboControlPreviewMode ? dock->m_d->generate.comboControlPreviewMode->currentData().toString() : QStringLiteral("depth");
    stopPreviewPolling(dock);
    stopLayerJobPolling(dock);
    if (dock->m_d->generate.btnControlPreviewRun)
        dock->m_d->generate.btnControlPreviewRun->setEnabled(false);
    if (dock->m_d->generate.labelControlPreviewImage) {
        dock->m_d->generate.labelControlPreviewImage->clear();
        dock->m_d->generate.labelControlPreviewImage->setText(ComfyTr::tr("Uploading…"));
    }

    QImage canvasImg;
    {
        const auto docCapture = ComfyUIUtils::getDocumentImage(image, QRect(), {});
        if (!docCapture) {
            dock->setStatusMessage(docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas.")
                                                               : docCapture.errorMessage,
                             true);
            stopPreviewPolling(dock);
            return;
        }
        canvasImg = docCapture.image;
    }
    // §13.98: Pose preview prefers tracked vector-layer pose SVG (rasterized) when the active layer is a shape layer.
    if (mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0 && dock->m_d->viewManager) {
        if (KisLayerSP al = dock->m_d->viewManager->activeLayer()) {
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
    if (dock->m_d->viewManager) {
        if (KisSelectionSP sel = dock->m_d->viewManager->selection()) {
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
    dock->m_d->generateRt.controlPreviewCompositeLocalRect = localCropRect;
    dock->m_d->generateRt.controlPreviewCompositeFullSize = fullExportSize;
    dock->m_d->generateRt.controlPreviewHandsCompositeBack =
        (mode.compare(QStringLiteral("hands"), Qt::CaseInsensitive) == 0
         && localCropRect != QRect(QPoint(0, 0), fullExportSize));
    // §13.53: preprocessors use resolution from shortest side of extent (not longest).
    const int resBase = qMin(canvasImg.width(), canvasImg.height());

    QTemporaryFile *tmp = new QTemporaryFile(dock);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        dock->setStatusMessage(ComfyTr::tr("Could not save temporary image for upload."), true);
        tmp->deleteLater();
        stopPreviewPolling(dock);
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
    QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);

    QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply, resBase, mode]() {
        reply->deleteLater();
        if (dock->m_d->comboWorkspace->currentIndex() != 0) {
            stopPreviewPolling(dock);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            dock->setStatusMessage(ComfyTr::tr("Control preview upload failed: %1", reply->errorString()), true);
            if (dock->m_d->generate.labelControlPreviewImage)
                dock->m_d->generate.labelControlPreviewImage->clear();
            stopPreviewPolling(dock);
            return;
        }
        const QString uploadedName = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (uploadedName.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Control preview: server did not return an image name."), true);
            stopPreviewPolling(dock);
            return;
        }
        QJsonObject workflow = ComfyUIUtils::buildControlImageWorkflow(uploadedName, mode, resBase, false);
        if (workflow.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Unsupported control mode for preprocessor preview."), true);
            stopPreviewPolling(dock);
            return;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (dock->m_d->clientId.isEmpty())
            dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        ComfyPromptClient::SubmitRequest submitReq;
        submitReq.workflow = workflow;
        submitReq.clientId = dock->m_d->clientId;
        submitReq.expectedPromptId = expectedPromptId;
        const QString serverUrl = dock->m_d->editServerUrl->text().trimmed();
        ComfyPromptClient::submitPrompt(dock->m_d->nam, serverUrl, submitReq, dock,
                                        [dock, expectedPromptId](const ComfyPromptClient::SubmitResult &result) {
            if (dock->m_d->comboWorkspace->currentIndex() != 0) {
                stopPreviewPolling(dock);
                return;
            }
            if (!result.ok) {
                dock->setStatusMessage(ComfyTr::tr("Control preview prompt failed: %1", result.errorMessage), true);
                stopPreviewPolling(dock);
                return;
            }
            if (result.promptId != expectedPromptId) {
                dock->setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                stopPreviewPolling(dock);
                return;
            }
            dock->m_d->generateRt.controlPreviewPromptId = result.promptId;
            dock->m_d->generateRt.controlPreviewPollCount = 0;
            if (dock->m_d->generate.labelControlPreviewImage)
                dock->m_d->generate.labelControlPreviewImage->setText(ComfyTr::tr("Running preprocessor…"));
            dock->m_d->generateRt.controlPreviewPollTimer->start(1000);
        });
    });

}

void onPreviewPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->generateRt.controlPreviewPromptId.isEmpty() || dock->m_d->comboWorkspace->currentIndex() != 0)
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        stopPreviewPolling(dock);
        return;
    }
    const QString pid = dock->m_d->generateRt.controlPreviewPromptId;
    ComfyPromptClient::fetchHistory(dock->m_d->nam, urlStr, pid, dock,
                                    [dock, urlStr, pid](const ComfyPromptClient::HistoryFetchResult &result) {
        if (dock->m_d->comboWorkspace->currentIndex() != 0 || dock->m_d->generateRt.controlPreviewPromptId.isEmpty()) {
            stopPreviewPolling(dock);
            return;
        }
        ComfyPollRunnerCommon::PollRunningConfig running;
        running.pollCount = &dock->m_d->generateRt.controlPreviewPollCount;
        running.maxPollCount = 300;
        running.pollTimer = dock->m_d->generateRt.controlPreviewPollTimer;
        running.onTimeout = [dock]() {
            dock->setStatusMessage(ComfyTr::tr("Control preview timed out waiting for server."), true);
            stopPreviewPolling(dock);
        };
        const auto terminal = [dock](const ComfyPromptClient::HistoryFetchResult &r) {
            if (r.state == ComfyPromptClient::HistoryState::NetworkError)
                dock->setStatusMessage(ComfyTr::tr("Control preview history request failed: %1", r.errorMessage), true);
            else if (r.state == ComfyPromptClient::HistoryState::ExecutionError)
                dock->setStatusMessage(r.errorMessage, true);
            else if (r.state == ComfyPromptClient::HistoryState::NoImages)
                dock->setStatusMessage(ComfyTr::tr("Control preview: no output image in history."), true);
            stopPreviewPolling(dock);
        };
        if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
            == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
            return;
        ComfyPromptClient::downloadOutputImage(dock->m_d->nam, urlStr, result.images.first(), dock,
                                               [dock](const QByteArray &data, const QString &errorMessage) {
            if (dock->m_d->comboWorkspace->currentIndex() != 0) {
                stopPreviewPolling(dock);
                return;
            }
            dock->m_d->generateRt.controlPreviewPromptId.clear();
            dock->m_d->generateRt.controlPreviewPollCount = 0;
            if (dock->m_d->generateRt.controlPreviewPollTimer)
                dock->m_d->generateRt.controlPreviewPollTimer->stop();
            if (dock->m_d->generate.btnControlPreviewRun)
                dock->m_d->generate.btnControlPreviewRun->setEnabled(true);
            if (!errorMessage.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Control preview image download failed: %1", errorMessage), true);
                if (dock->m_d->generate.labelControlPreviewImage)
                    dock->m_d->generate.labelControlPreviewImage->clear();
                dock->m_d->generateRt.controlPreviewHandsCompositeBack = false;
                return;
            }
            QImage img;
            if (!img.loadFromData(data)) {
                dock->setStatusMessage(ComfyTr::tr("Control preview: could not decode image."), true);
                if (dock->m_d->generate.labelControlPreviewImage)
                    dock->m_d->generate.labelControlPreviewImage->clear();
                dock->m_d->generateRt.controlPreviewHandsCompositeBack = false;
                return;
            }
            if (dock->m_d->generateRt.controlPreviewHandsCompositeBack) {
                img = ComfyUIUtils::compositeControlImageOntoExtent(img, dock->m_d->generateRt.controlPreviewCompositeFullSize,
                                                                     dock->m_d->generateRt.controlPreviewCompositeLocalRect);
                dock->m_d->generateRt.controlPreviewHandsCompositeBack = false;
            }
            if (dock->m_d->generate.labelControlPreviewImage) {
                const QPixmap pm = QPixmap::fromImage(
                    img.scaled(QSize(256, 256), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                dock->m_d->generate.labelControlPreviewImage->setPixmap(pm);
                dock->m_d->generate.labelControlPreviewImage->setText(QString());
            }
            dock->setStatusMessage(ComfyTr::tr("Control preprocessor preview finished."), false);
        });
    });
}

} // namespace ComfyControlRunner
