/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyControlRunner.h"
#include "ComfyControlRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyControlLayerListWidget.h"
#include "ComfyLocalization.h"
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
#include <kis_group_layer.h>
#include <kis_selection.h>
#include <kis_shape_layer.h>


using namespace ComfyControlRunnerInternal;

namespace ComfyControlRunner {

void stopLayerJobPolling(ComfyUIRemoteDock *dock)
{

    if (dock->m_d->generateRt.controlLayerJobPollTimer)
        dock->m_d->generateRt.controlLayerJobPollTimer->stop();
    dock->m_d->generateRt.controlLayerJobPromptId.clear();
    dock->m_d->generateRt.controlLayerJobPollCount = 0;
    dock->m_d->generateRt.controlLayerJobMode.clear();
    dock->m_d->generateRt.controlLayerJobResultLayerName.clear();
    dock->m_d->generateRt.controlLayerJobAnchorLayerName.clear();
    dock->m_d->generateRt.controlLayerJobForRegion = false;
    dock->m_d->generateRt.controlLayerJobRegionRow = -1;
    dock->m_d->generateRt.controlLayerJobEntryIndex = -1;
    dock->m_d->generateRt.controlLayerJobHandsCompositeBack = false;
    dock->m_d->generateRt.controlLayerJobCompositeLocalRect = QRect();
    dock->m_d->generateRt.controlLayerJobCompositeFullSize = QSize();
    dock->m_d->generateRt.controlLayerJobOpenPoseJson = QJsonValue();
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (dock->m_d->generateRt.controlLayerJobWebSocket) {
        dock->m_d->generateRt.controlLayerJobWebSocket->close();
        dock->m_d->generateRt.controlLayerJobWebSocket->deleteLater();
        dock->m_d->generateRt.controlLayerJobWebSocket = nullptr;
    }
#endif
    refreshLayerJobGenerateButtons(dock);

}
void refreshLayerJobGenerateButtons(ComfyUIRemoteDock *dock)
{

    const bool jobIdle = dock->m_d->generateRt.controlLayerJobPromptId.isEmpty();
    if (dock->m_d->generate.rootControlLayerList)
        dock->m_d->generate.rootControlLayerList->setGenerateEnabled(jobIdle);
    if (dock->m_d->generate.regionControlLayerList)
        dock->m_d->generate.regionControlLayerList->setGenerateEnabled(jobIdle);

}
void startLayerJobWebSocketListen(ComfyUIRemoteDock *dock)
{

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (dock->m_d->generateRt.controlLayerJobMode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) != 0)
        return;
    if (dock->m_d->clientId.isEmpty())
        dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QUrl wsUrl =
        ComfyUIUtils::comfyWebSocketUrlForClient(dock->m_d->editServerUrl->text().trimmed(), dock->m_d->clientId);
    if (!wsUrl.isValid())
        return;
    if (dock->m_d->generateRt.controlLayerJobWebSocket) {
        dock->m_d->generateRt.controlLayerJobWebSocket->close();
        dock->m_d->generateRt.controlLayerJobWebSocket->deleteLater();
        dock->m_d->generateRt.controlLayerJobWebSocket = nullptr;
    }
    auto *ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, dock);
    dock->m_d->generateRt.controlLayerJobWebSocket = ws;
    QObject::connect(ws, &QWebSocket::textMessageReceived, dock, [dock](const QString &message) {
        const QJsonObject msg = QJsonDocument::fromJson(message.toUtf8()).object();
        if (msg.value(QStringLiteral("type")).toString() != QStringLiteral("executed"))
            return;
        const QJsonObject data = msg.value(QStringLiteral("data")).toObject();
        if (data.value(QStringLiteral("prompt_id")).toString() != dock->m_d->generateRt.controlLayerJobPromptId)
            return;
        const QJsonObject output = data.value(QStringLiteral("output")).toObject();
        const QJsonValue poseJson = ComfyOpenPose::openPoseJsonFromComfyOutputs(output);
        if (!poseJson.isNull())
            dock->m_d->generateRt.controlLayerJobOpenPoseJson = poseJson;
    });
    ws->open(wsUrl);
#else
    Q_UNUSED(dock);
#endif

}
bool importLayerFromOpenPoseJson(ComfyUIRemoteDock *dock, const QJsonValue &openPoseJson, const QString &resultLayerName, const QString &anchorLayerName, bool forRegion, int regionRow, int entryIndex)
{

    if (!dock->m_d->viewManager)
        return false;
    KisImageSP image = dock->m_d->viewManager->image();
    if (!image)
        return false;
    KisDocument *doc = dock->m_d->canvas && dock->m_d->canvas->imageView() ? dock->m_d->canvas->imageView()->document() : nullptr;
    if (!doc) {
        const QList<QPointer<KisDocument>> docs = KisPart::instance()->documents();
        for (const QPointer<KisDocument> &d : docs) {
            if (d && KisImageSP(d->image()) == image) {
                doc = d.data();
                break;
            }
        }
    }
    if (!doc)
        return false;

    ComfyOpenPose::Pose pose = ComfyOpenPose::Pose::fromOpenPoseJson(openPoseJson);
    if (pose.joints.isEmpty())
        return false;
    pose.scaleToExtent(image->bounds().size());
    const QString svg = pose.toSvg();
    KisNodeSP anchor = findNodeByName(image->rootLayer(), anchorLayerName);
    if (!ComfyUIPoseLayers::instance().createVectorLayerFromSvg(image, doc, resultLayerName, svg, anchor))
        return false;

    const QString newLayerName = resultLayerName;
    const QString newLayerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (forRegion) {
        QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(dock->m_d.data());
        if (regionRow >= 0 && regionRow < regs.size() && entryIndex >= 0
            && entryIndex < regs[regionRow].controlLayers.size()) {
            regs[regionRow].controlLayers[entryIndex].layerName = newLayerName;
            regs[regionRow].controlLayers[entryIndex].layerId = newLayerId;
        }
        dock->refreshRegionControlLayersList();
    } else if (entryIndex >= 0 && entryIndex < dock->m_d->rootControlLayers.size()) {
        dock->m_d->rootControlLayers[entryIndex].layerName = newLayerName;
        dock->m_d->rootControlLayers[entryIndex].layerId = newLayerId;
        dock->refreshRootControlLayersList();
    }
    dock->scheduleDocumentUiJsonSave();
    if (dock->m_d->canvas)
        dock->m_d->canvas->updateCanvas();
    dock->setStatusMessage(ComfyTr::tr("Control vector layer \"%1\" created.", resultLayerName), false);
    return true;

}
void onLayerJobRun(ComfyUIRemoteDock *dock, bool forRegion, int entryIndex)
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
    ComfyControlLayerEntry entry;
    if (forRegion) {
        const int regionRow = comfyActiveRegionRow(dock->m_d.data());
        QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(dock->m_d.data());
        if (regionRow < 0 || regionRow >= regs.size() || entryIndex < 0
            || entryIndex >= regs[regionRow].controlLayers.size()) {
            dock->setStatusMessage(ComfyTr::tr("Invalid region control layer."), true);
            return;
        }
        entry = regs[regionRow].controlLayers.at(entryIndex);
        if (!ComfyControlLayer::canGenerateJob(entry)) {
            dock->setStatusMessage(ComfyTr::tr("This control mode cannot be generated."), true);
            return;
        }
        dock->m_d->generateRt.controlLayerJobForRegion = true;
        dock->m_d->generateRt.controlLayerJobRegionRow = regionRow;
        dock->m_d->generateRt.controlLayerJobEntryIndex = entryIndex;
    } else {
        if (entryIndex < 0 || entryIndex >= dock->m_d->rootControlLayers.size()) {
            dock->setStatusMessage(ComfyTr::tr("Invalid control layer."), true);
            return;
        }
        entry = dock->m_d->rootControlLayers.at(entryIndex);
        if (!ComfyControlLayer::canGenerateJob(entry)) {
            dock->setStatusMessage(ComfyTr::tr("This control mode cannot be generated."), true);
            return;
        }
        dock->m_d->generateRt.controlLayerJobForRegion = false;
        dock->m_d->generateRt.controlLayerJobRegionRow = -1;
        dock->m_d->generateRt.controlLayerJobEntryIndex = entryIndex;
    }

    KisImageSP image = dock->m_d->viewManager->image();
    const auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        dock->setStatusMessage(colorCheck.second, true);
        return;
    }

    const QString mode = entry.mode.trimmed().toLower();
    stopPreviewPolling(dock);
    stopLayerJobPolling(dock);
    dock->m_d->generateRt.controlLayerJobMode = mode;
    dock->m_d->generateRt.controlLayerJobAnchorLayerName = entry.layerName;
    dock->m_d->generateRt.controlLayerJobResultLayerName =
        QStringLiteral("[Control] %1").arg(ComfyControlLayer::modeLabel(entry.mode));
    refreshLayerJobGenerateButtons(dock);
    dock->setStatusMessage(ComfyTr::tr("Generating control layer…"), false);

    QImage canvasImg = canvasImageForControlLayerJob(image, dock->m_d->viewManager, mode);
    if (canvasImg.isNull()) {
        dock->setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
        stopLayerJobPolling(dock);
        return;
    }
    const QSize fullExportSize = canvasImg.size();
    const QRect localCropRect =
        selectionCropLocalRect(image, dock->m_d->viewManager, fullExportSize, &canvasImg);
    dock->m_d->generateRt.controlLayerJobCompositeLocalRect = localCropRect;
    dock->m_d->generateRt.controlLayerJobCompositeFullSize = fullExportSize;
    dock->m_d->generateRt.controlLayerJobHandsCompositeBack =
        (mode.compare(QStringLiteral("hands"), Qt::CaseInsensitive) == 0
         && localCropRect != QRect(QPoint(0, 0), fullExportSize));
    dock->m_d->generateRt.controlLayerJobOpenPoseJson = QJsonValue();
    if (mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0)
        startLayerJobWebSocketListen(dock);
    const int resBase = qMin(canvasImg.width(), canvasImg.height());

    QTemporaryFile *tmp = new QTemporaryFile(dock);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        dock->setStatusMessage(ComfyTr::tr("Could not save temporary image for upload."), true);
        tmp->deleteLater();
        stopLayerJobPolling(dock);
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
                   QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"krita_control_layer.png\"")));
    part.setBodyDevice(tmp);
    tmp->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(uploadUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);

    QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply, resBase, mode]() {
        reply->deleteLater();
        if (dock->m_d->generateRt.controlLayerJobMode.isEmpty()) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            dock->setStatusMessage(ComfyTr::tr("Control layer upload failed: %1", reply->errorString()), true);
            stopLayerJobPolling(dock);
            return;
        }
        const QString uploadedName =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (uploadedName.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Control layer: server did not return an image name."), true);
            stopLayerJobPolling(dock);
            return;
        }
        QJsonObject workflow = ComfyUIUtils::buildControlImageWorkflow(uploadedName, mode, resBase, false);
        if (workflow.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Unsupported control mode for generation."), true);
            stopLayerJobPolling(dock);
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
            if (dock->m_d->generateRt.controlLayerJobMode.isEmpty()) {
                return;
            }
            if (!result.ok) {
                dock->setStatusMessage(ComfyTr::tr("Control layer prompt failed: %1", result.errorMessage), true);
                stopLayerJobPolling(dock);
                return;
            }
            if (result.promptId != expectedPromptId) {
                dock->setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                stopLayerJobPolling(dock);
                return;
            }
            dock->m_d->generateRt.controlLayerJobPromptId = result.promptId;
            dock->m_d->generateRt.controlLayerJobPollCount = 0;
            if (dock->m_d->generateRt.controlLayerJobPollTimer)
                dock->m_d->generateRt.controlLayerJobPollTimer->start(1000);
        });
    });

}

void onLayerJobPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->generateRt.controlLayerJobPromptId.isEmpty())
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        stopLayerJobPolling(dock);
        return;
    }
    const QString pid = dock->m_d->generateRt.controlLayerJobPromptId;
    ComfyPromptClient::fetchHistory(dock->m_d->nam, urlStr, pid, dock,
                                    [dock, urlStr, pid](const ComfyPromptClient::HistoryFetchResult &result) {
        if (dock->m_d->generateRt.controlLayerJobPromptId.isEmpty()) {
            return;
        }
        if (result.state != ComfyPromptClient::HistoryState::Done) {
            ComfyPollRunnerCommon::PollRunningConfig running;
            running.pollCount = &dock->m_d->generateRt.controlLayerJobPollCount;
            running.maxPollCount = 300;
            running.pollTimer = dock->m_d->generateRt.controlLayerJobPollTimer;
            running.onTimeout = [dock]() {
                dock->setStatusMessage(ComfyTr::tr("Control layer generation timed out waiting for server."), true);
                stopLayerJobPolling(dock);
            };
            const auto terminal = [dock](const ComfyPromptClient::HistoryFetchResult &r) {
                if (r.state == ComfyPromptClient::HistoryState::NetworkError)
                    dock->setStatusMessage(ComfyTr::tr("Control layer history request failed: %1", r.errorMessage), true);
                else if (r.state == ComfyPromptClient::HistoryState::ExecutionError)
                    dock->setStatusMessage(r.errorMessage, true);
                stopLayerJobPolling(dock);
            };
            if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
                == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
                return;
        }
        const QJsonObject outputs = result.historyEntry.value(QStringLiteral("outputs")).toObject();
        const QString jobMode = dock->m_d->generateRt.controlLayerJobMode;
        const QString resultName = dock->m_d->generateRt.controlLayerJobResultLayerName;
        const QString anchorName = dock->m_d->generateRt.controlLayerJobAnchorLayerName;
        const bool forRegion = dock->m_d->generateRt.controlLayerJobForRegion;
        const int regionRow = dock->m_d->generateRt.controlLayerJobRegionRow;
        const int entryIndex = dock->m_d->generateRt.controlLayerJobEntryIndex;

        if (jobMode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0) {
            QJsonValue poseJson = dock->m_d->generateRt.controlLayerJobOpenPoseJson;
            if (poseJson.isNull())
                poseJson = ComfyOpenPose::openPoseJsonFromHistoryOutputs(outputs);
            if (!poseJson.isNull()) {
                stopLayerJobPolling(dock);
                if (!importLayerFromOpenPoseJson(dock, poseJson, resultName, anchorName, forRegion, regionRow,
                                                        entryIndex)) {
                    dock->setStatusMessage(
                        ComfyTr::tr("Could not create pose vector layer from OpenPose data (invalid or empty keypoints)."),
                        true);
                }
                return;
            }
            dock->setStatusMessage(
                ComfyTr::tr("Pose control layer: server did not return OpenPose JSON (DWPreprocessor openpose_json). "
                     "Connect with Qt WebSockets enabled and retry."),
                true);
            stopLayerJobPolling(dock);
            return;
        }

        if (result.images.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Control layer: no output image in history."), true);
            stopLayerJobPolling(dock);
            return;
        }
        ComfyPromptClient::downloadOutputImage(dock->m_d->nam, urlStr, result.images.first(), dock,
                                               [dock](const QByteArray &data, const QString &errorMessage) {
            const QString resultName = dock->m_d->generateRt.controlLayerJobResultLayerName;
            const QString anchorName = dock->m_d->generateRt.controlLayerJobAnchorLayerName;
            const bool forRegion = dock->m_d->generateRt.controlLayerJobForRegion;
            const int regionRow = dock->m_d->generateRt.controlLayerJobRegionRow;
            const int entryIndex = dock->m_d->generateRt.controlLayerJobEntryIndex;
            const bool handsBack = dock->m_d->generateRt.controlLayerJobHandsCompositeBack;
            const QRect localRect = dock->m_d->generateRt.controlLayerJobCompositeLocalRect;
            const QSize fullSize = dock->m_d->generateRt.controlLayerJobCompositeFullSize;
            stopLayerJobPolling(dock);
            if (!errorMessage.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Control layer image download failed: %1", errorMessage), true);
                return;
            }
            QImage img;
            if (!img.loadFromData(data)) {
                dock->setStatusMessage(ComfyTr::tr("Control layer: could not decode image."), true);
                return;
            }
            if (handsBack) {
                img = ComfyUIUtils::compositeControlImageOntoExtent(img, fullSize, localRect);
            }
            if (!dock->m_d->viewManager || !dock->m_d->viewManager->imageManager()) {
                dock->setStatusMessage(ComfyTr::tr("No document open."), true);
                return;
            }
            KisImageSP image = dock->m_d->viewManager->image();
            if (!image) {
                dock->setStatusMessage(ComfyTr::tr("No document open."), true);
                return;
            }
            QTemporaryFile tmp;
            tmp.setAutoRemove(true);
            tmp.setFileTemplate(tmp.fileTemplate() + QStringLiteral(".png"));
            if (!tmp.open() || !img.save(tmp.fileName())) {
                dock->setStatusMessage(ComfyTr::tr("Could not save control layer image."), true);
                return;
            }
            const qint32 n = dock->m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()),
                                                                           QStringLiteral("KisPaintLayer"));
            if (n <= 0) {
                dock->setStatusMessage(ComfyTr::tr("Could not import control layer."), true);
                return;
            }
            KisLayerSP imported = dock->m_d->viewManager->activeLayer();
            if (!imported) {
                dock->setStatusMessage(ComfyTr::tr("Could not import control layer."), true);
                return;
            }
            imported->setName(resultName);
            KisNodeSP anchor = findNodeByName(image->rootLayer(), anchorName);
            if (anchor) {
                KisNodeSP importedNode = imported;
                image->removeNode(importedNode);
                image->addNode(importedNode, anchor->parent(), anchor);
            }
            QString newLayerName = imported->name();
            QString newLayerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (forRegion) {
                QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(dock->m_d.data());
                if (regionRow >= 0 && regionRow < regs.size() && entryIndex >= 0
                    && entryIndex < regs[regionRow].controlLayers.size()) {
                    regs[regionRow].controlLayers[entryIndex].layerName = newLayerName;
                    regs[regionRow].controlLayers[entryIndex].layerId = newLayerId;
                }
                dock->refreshRegionControlLayersList();
            } else if (entryIndex >= 0 && entryIndex < dock->m_d->rootControlLayers.size()) {
                dock->m_d->rootControlLayers[entryIndex].layerName = newLayerName;
                dock->m_d->rootControlLayers[entryIndex].layerId = newLayerId;
                dock->refreshRootControlLayersList();
            }
            dock->scheduleDocumentUiJsonSave();
            if (dock->m_d->canvas)
                dock->m_d->canvas->updateCanvas();
            dock->setStatusMessage(ComfyTr::tr("Control layer \"%1\" created.", resultName), false);
        });
    });
}

} // namespace ComfyControlRunner
