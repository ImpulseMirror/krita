/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlLayer.h"
#include "ComfyOpenPose.h"
#include "ComfyUIPoseLayers.h"
#include "ComfyUIUtils.h"

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
#include <QWebSocket>
#endif

#include <KisDocument.h>
#include <kis_part.h>

#include <QFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QUrlQuery>
#include <QUuid>

#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_node.h>
#include <kis_selection.h>
#include <kis_shape_layer.h>

#include "ComfyUIPoseLayers.h"

#include <klocalizedstring.h>

namespace {

KisNodeSP findNodeByName(KisNodeSP root, const QString &name)
{
    if (!root || name.isEmpty())
        return KisNodeSP();
    if (root->name() == name)
        return root;
    for (int i = 0; i < static_cast<int>(root->childCount()); ++i) {
        KisNodeSP found = findNodeByName(root->at(i), name);
        if (found)
            return found;
    }
    return KisNodeSP();
}

QImage canvasImageForControlLayerJob(KisImageSP image, KisViewManager *viewManager, const QString &mode)
{
    if (!image)
        return QImage();
    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull())
        return QImage();
    if (mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0 && viewManager) {
        if (KisLayerSP al = viewManager->activeLayer()) {
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
    return canvasImg;
}

QRect selectionCropLocalRect(KisImageSP image, KisViewManager *viewManager, const QSize &fullExportSize, QImage *canvasImg)
{
    QRect localCropRect(0, 0, fullExportSize.width(), fullExportSize.height());
    if (!image || !canvasImg || canvasImg->isNull())
        return localCropRect;
    const QRect docBounds = image->bounds();
    if (!viewManager)
        return localCropRect;
    if (KisSelectionSP sel = viewManager->selection()) {
        if (auto ps = sel->pixelSelection()) {
            QRect r = ps->selectedExactRect();
            r &= docBounds;
            if (!r.isEmpty() && r.size() != docBounds.size()) {
                const QRect local = r.translated(-docBounds.topLeft());
                if (local.left() >= 0 && local.top() >= 0 && local.right() < canvasImg->width()
                    && local.bottom() < canvasImg->height()) {
                    localCropRect = local;
                    *canvasImg = canvasImg->copy(local);
                }
            }
        }
    }
    return localCropRect;
}

} // namespace

void ComfyUIRemoteDock::stopControlLayerJobPolling()
{
    if (m_d->controlLayerJobPollTimer)
        m_d->controlLayerJobPollTimer->stop();
    m_d->controlLayerJobPromptId.clear();
    m_d->controlLayerJobPollCount = 0;
    m_d->controlLayerJobMode.clear();
    m_d->controlLayerJobResultLayerName.clear();
    m_d->controlLayerJobAnchorLayerName.clear();
    m_d->controlLayerJobForRegion = false;
    m_d->controlLayerJobRegionRow = -1;
    m_d->controlLayerJobEntryIndex = -1;
    m_d->controlLayerJobHandsCompositeBack = false;
    m_d->controlLayerJobCompositeLocalRect = QRect();
    m_d->controlLayerJobCompositeFullSize = QSize();
    m_d->controlLayerJobOpenPoseJson = QJsonValue();
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (m_d->controlLayerJobWebSocket) {
        m_d->controlLayerJobWebSocket->close();
        m_d->controlLayerJobWebSocket->deleteLater();
        m_d->controlLayerJobWebSocket = nullptr;
    }
#endif
    refreshControlLayerGenerateButtons();
}

void ComfyUIRemoteDock::refreshControlLayerGenerateButtons()
{
    const bool jobIdle = m_d->controlLayerJobPromptId.isEmpty();
    if (m_d->rootControlLayerList)
        m_d->rootControlLayerList->setGenerateEnabled(jobIdle);
    if (m_d->regionControlLayerList)
        m_d->regionControlLayerList->setGenerateEnabled(jobIdle);
}

void ComfyUIRemoteDock::startControlLayerJobWebSocketListen()
{
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (m_d->controlLayerJobMode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) != 0)
        return;
    if (m_d->clientId.isEmpty())
        m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QUrl wsUrl =
        ComfyUIUtils::comfyWebSocketUrlForClient(m_d->editServerUrl->text().trimmed(), m_d->clientId);
    if (!wsUrl.isValid())
        return;
    if (m_d->controlLayerJobWebSocket) {
        m_d->controlLayerJobWebSocket->close();
        m_d->controlLayerJobWebSocket->deleteLater();
        m_d->controlLayerJobWebSocket = nullptr;
    }
    auto *ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_d->controlLayerJobWebSocket = ws;
    connect(ws, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        const QJsonObject msg = QJsonDocument::fromJson(message.toUtf8()).object();
        if (msg.value(QStringLiteral("type")).toString() != QStringLiteral("executed"))
            return;
        const QJsonObject data = msg.value(QStringLiteral("data")).toObject();
        if (data.value(QStringLiteral("prompt_id")).toString() != m_d->controlLayerJobPromptId)
            return;
        const QJsonObject output = data.value(QStringLiteral("output")).toObject();
        const QJsonValue poseJson = ComfyOpenPose::openPoseJsonFromComfyOutputs(output);
        if (!poseJson.isNull())
            m_d->controlLayerJobOpenPoseJson = poseJson;
    });
    ws->open(wsUrl);
#else
    Q_UNUSED(this);
#endif
}

bool ComfyUIRemoteDock::importControlLayerFromOpenPoseJson(const QJsonValue &openPoseJson,
                                                           const QString &resultLayerName,
                                                           const QString &anchorLayerName,
                                                           bool forRegion,
                                                           int regionRow,
                                                           int entryIndex)
{
    if (!m_d->viewManager)
        return false;
    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return false;
    KisDocument *doc = m_d->canvas && m_d->canvas->imageView() ? m_d->canvas->imageView()->document() : nullptr;
    if (!doc) {
        const QList<QPointer<KisDocument>> docs = KisPart::instance()->documents();
        for (const QPointer<KisDocument> &d : docs) {
            if (d && d->image() == image) {
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
        QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
        if (regionRow >= 0 && regionRow < regs.size() && entryIndex >= 0
            && entryIndex < regs[regionRow].controlLayers.size()) {
            regs[regionRow].controlLayers[entryIndex].layerName = newLayerName;
            regs[regionRow].controlLayers[entryIndex].layerId = newLayerId;
        }
        refreshRegionControlLayersList();
    } else if (entryIndex >= 0 && entryIndex < m_d->rootControlLayers.size()) {
        m_d->rootControlLayers[entryIndex].layerName = newLayerName;
        m_d->rootControlLayers[entryIndex].layerId = newLayerId;
        refreshRootControlLayersList();
    }
    scheduleDocumentUiJsonSave();
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    setStatusMessage(ComfyTr::tr("Control vector layer \"%1\" created.", resultLayerName), false);
    return true;
}

void ComfyUIRemoteDock::beginControlLayerGenerateJob(bool forRegion, int entryIndex)
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
    ComfyControlLayerEntry entry;
    if (forRegion) {
        const int regionRow = comfyActiveRegionRow(m_d.data());
        QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
        if (regionRow < 0 || regionRow >= regs.size() || entryIndex < 0
            || entryIndex >= regs[regionRow].controlLayers.size()) {
            setStatusMessage(ComfyTr::tr("Invalid region control layer."), true);
            return;
        }
        entry = regs[regionRow].controlLayers.at(entryIndex);
        if (!ComfyControlLayer::canGenerateJob(entry)) {
            setStatusMessage(ComfyTr::tr("This control mode cannot be generated."), true);
            return;
        }
        m_d->controlLayerJobForRegion = true;
        m_d->controlLayerJobRegionRow = regionRow;
        m_d->controlLayerJobEntryIndex = entryIndex;
    } else {
        if (entryIndex < 0 || entryIndex >= m_d->rootControlLayers.size()) {
            setStatusMessage(ComfyTr::tr("Invalid control layer."), true);
            return;
        }
        entry = m_d->rootControlLayers.at(entryIndex);
        if (!ComfyControlLayer::canGenerateJob(entry)) {
            setStatusMessage(ComfyTr::tr("This control mode cannot be generated."), true);
            return;
        }
        m_d->controlLayerJobForRegion = false;
        m_d->controlLayerJobRegionRow = -1;
        m_d->controlLayerJobEntryIndex = entryIndex;
    }

    KisImageSP image = m_d->viewManager->image();
    const auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        return;
    }

    const QString mode = entry.mode.trimmed().toLower();
    stopControlPreviewPolling();
    stopControlLayerJobPolling();
    m_d->controlLayerJobMode = mode;
    m_d->controlLayerJobAnchorLayerName = entry.layerName;
    m_d->controlLayerJobResultLayerName =
        QStringLiteral("[Control] %1").arg(ComfyControlLayer::modeLabel(entry.mode));
    refreshControlLayerGenerateButtons();
    setStatusMessage(ComfyTr::tr("Generating control layer…"), false);

    QImage canvasImg = canvasImageForControlLayerJob(image, m_d->viewManager, mode);
    if (canvasImg.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
        stopControlLayerJobPolling();
        return;
    }
    const QSize fullExportSize = canvasImg.size();
    const QRect localCropRect =
        selectionCropLocalRect(image, m_d->viewManager, fullExportSize, &canvasImg);
    m_d->controlLayerJobCompositeLocalRect = localCropRect;
    m_d->controlLayerJobCompositeFullSize = fullExportSize;
    m_d->controlLayerJobHandsCompositeBack =
        (mode.compare(QStringLiteral("hands"), Qt::CaseInsensitive) == 0
         && localCropRect != QRect(QPoint(0, 0), fullExportSize));
    m_d->controlLayerJobOpenPoseJson = QJsonValue();
    if (mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0)
        startControlLayerJobWebSocketListen();
    const int resBase = qMin(canvasImg.width(), canvasImg.height());

    QTemporaryFile *tmp = new QTemporaryFile(this);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        setStatusMessage(ComfyTr::tr("Could not save temporary image for upload."), true);
        tmp->deleteLater();
        stopControlLayerJobPolling();
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
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, resBase, mode]() {
        reply->deleteLater();
        if (m_d->controlLayerJobMode.isEmpty()) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Control layer upload failed: %1", reply->errorString()), true);
            stopControlLayerJobPolling();
            return;
        }
        const QString uploadedName =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (uploadedName.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Control layer: server did not return an image name."), true);
            stopControlLayerJobPolling();
            return;
        }
        QJsonObject workflow = ComfyUIUtils::buildControlImageWorkflow(uploadedName, mode, resBase, false);
        if (workflow.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Unsupported control mode for generation."), true);
            stopControlLayerJobPolling();
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
            if (m_d->controlLayerJobMode.isEmpty()) {
                return;
            }
            if (replyP->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Control layer prompt failed: %1", replyP->errorString()), true);
                stopControlLayerJobPolling();
                return;
            }
            const QString promptId =
                QJsonDocument::fromJson(replyP->readAll()).object().value(QStringLiteral("prompt_id")).toString();
            if (promptId.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Control layer: empty prompt_id from server."), true);
                stopControlLayerJobPolling();
                return;
            }
            if (promptId != expectedPromptId) {
                setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                stopControlLayerJobPolling();
                return;
            }
            m_d->controlLayerJobPromptId = promptId;
            m_d->controlLayerJobPollCount = 0;
            if (m_d->controlLayerJobPollTimer)
                m_d->controlLayerJobPollTimer->start(1000);
        });
    });
}

void ComfyUIRemoteDock::slotControlLayerJobPoll()
{
    if (m_d->controlLayerJobPromptId.isEmpty())
        return;
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        stopControlLayerJobPolling();
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    const QString pid = m_d->controlLayerJobPromptId;
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
        if (m_d->controlLayerJobPromptId.isEmpty()) {
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Control layer history request failed: %1", reply->errorString()), true);
            stopControlLayerJobPolling();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject hist = root.value(m_d->controlLayerJobPromptId).toObject();
        const QJsonObject outputs = hist.value(QStringLiteral("outputs")).toObject();
        if (outputs.isEmpty()) {
            m_d->controlLayerJobPollCount++;
            constexpr int kMaxPoll = 300;
            if (m_d->controlLayerJobPollCount >= kMaxPoll) {
                setStatusMessage(ComfyTr::tr("Control layer generation timed out waiting for server."), true);
                stopControlLayerJobPolling();
                return;
            }
            if (m_d->controlLayerJobPollTimer)
                m_d->controlLayerJobPollTimer->start(1000);
            return;
        }
        const QString jobMode = m_d->controlLayerJobMode;
        const QString resultName = m_d->controlLayerJobResultLayerName;
        const QString anchorName = m_d->controlLayerJobAnchorLayerName;
        const bool forRegion = m_d->controlLayerJobForRegion;
        const int regionRow = m_d->controlLayerJobRegionRow;
        const int entryIndex = m_d->controlLayerJobEntryIndex;

        if (jobMode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0) {
            QJsonValue poseJson = m_d->controlLayerJobOpenPoseJson;
            if (poseJson.isNull())
                poseJson = ComfyOpenPose::openPoseJsonFromHistoryOutputs(outputs);
            if (!poseJson.isNull()) {
                stopControlLayerJobPolling();
                if (!importControlLayerFromOpenPoseJson(poseJson, resultName, anchorName, forRegion, regionRow,
                                                        entryIndex)) {
                    setStatusMessage(
                        ComfyTr::tr("Could not create pose vector layer from OpenPose data (invalid or empty keypoints)."),
                        true);
                }
                return;
            }
            setStatusMessage(
                ComfyTr::tr("Pose control layer: server did not return OpenPose JSON (DWPreprocessor openpose_json). "
                     "Connect with Qt WebSockets enabled and retry."),
                true);
            stopControlLayerJobPolling();
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
            setStatusMessage(ComfyTr::tr("Control layer: no output image in history."), true);
            stopControlLayerJobPolling();
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
            const QString resultName = m_d->controlLayerJobResultLayerName;
            const QString anchorName = m_d->controlLayerJobAnchorLayerName;
            const bool forRegion = m_d->controlLayerJobForRegion;
            const int regionRow = m_d->controlLayerJobRegionRow;
            const int entryIndex = m_d->controlLayerJobEntryIndex;
            const bool handsBack = m_d->controlLayerJobHandsCompositeBack;
            const QRect localRect = m_d->controlLayerJobCompositeLocalRect;
            const QSize fullSize = m_d->controlLayerJobCompositeFullSize;
            stopControlLayerJobPolling();
            if (replyV->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Control layer image download failed: %1", replyV->errorString()), true);
                return;
            }
            QImage img;
            if (!img.loadFromData(replyV->readAll())) {
                setStatusMessage(ComfyTr::tr("Control layer: could not decode image."), true);
                return;
            }
            if (handsBack) {
                img = ComfyUIUtils::compositeControlImageOntoExtent(img, fullSize, localRect);
            }
            if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
                setStatusMessage(ComfyTr::tr("No document open."), true);
                return;
            }
            KisImageSP image = m_d->viewManager->image();
            if (!image) {
                setStatusMessage(ComfyTr::tr("No document open."), true);
                return;
            }
            QTemporaryFile tmp;
            tmp.setAutoRemove(true);
            tmp.setFileTemplate(tmp.fileTemplate() + QStringLiteral(".png"));
            if (!tmp.open() || !img.save(tmp.fileName())) {
                setStatusMessage(ComfyTr::tr("Could not save control layer image."), true);
                return;
            }
            const qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()),
                                                                           QStringLiteral("KisPaintLayer"));
            if (n <= 0) {
                setStatusMessage(ComfyTr::tr("Could not import control layer."), true);
                return;
            }
            KisLayerSP imported = m_d->viewManager->activeLayer();
            if (!imported) {
                setStatusMessage(ComfyTr::tr("Could not import control layer."), true);
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
                QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
                if (regionRow >= 0 && regionRow < regs.size() && entryIndex >= 0
                    && entryIndex < regs[regionRow].controlLayers.size()) {
                    regs[regionRow].controlLayers[entryIndex].layerName = newLayerName;
                    regs[regionRow].controlLayers[entryIndex].layerId = newLayerId;
                }
                refreshRegionControlLayersList();
            } else if (entryIndex >= 0 && entryIndex < m_d->rootControlLayers.size()) {
                m_d->rootControlLayers[entryIndex].layerName = newLayerName;
                m_d->rootControlLayers[entryIndex].layerId = newLayerId;
                refreshRootControlLayersList();
            }
            scheduleDocumentUiJsonSave();
            if (m_d->canvas)
                m_d->canvas->updateCanvas();
            setStatusMessage(ComfyTr::tr("Control layer \"%1\" created.", resultName), false);
        });
    });
}
