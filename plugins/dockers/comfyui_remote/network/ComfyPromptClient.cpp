/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPromptClient.h"

#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyPromptClient {

QUrl promptEndpointUrl(const QString &serverBaseUrl)
{
    return ComfyUIUtils::comfyResolveApiUrl(serverBaseUrl.trimmed(), QStringLiteral("prompt"));
}

QUrl historyEndpointUrl(const QString &serverBaseUrl, const QString &promptId)
{
    const QString rel = QStringLiteral("history/") + promptId.trimmed();
    return ComfyUIUtils::comfyResolveApiUrl(serverBaseUrl.trimmed(), rel);
}

QUrl viewImageUrl(const QString &serverBaseUrl, const OutputImage &image)
{
    QUrl url = ComfyUIUtils::comfyResolveApiUrl(serverBaseUrl.trimmed(), QStringLiteral("view"));
    if (!url.isValid() || image.filename.isEmpty())
        return url;
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("filename"), image.filename);
    if (!image.subfolder.isEmpty())
        q.addQueryItem(QStringLiteral("subfolder"), image.subfolder);
    url.setQuery(q);
    return url;
}

QList<OutputImage> extractOutputImages(const QJsonObject &historyEntry)
{
    QList<OutputImage> images;
    const QJsonObject outputs = historyEntry.value(QStringLiteral("outputs")).toObject();
    if (outputs.isEmpty())
        return images;

    const auto imagesFromNode = [&](const QString &nodeId) -> QList<OutputImage> {
        QList<OutputImage> nodeImages;
        const QJsonArray raw =
            outputs.value(nodeId).toObject().value(QStringLiteral("images")).toArray();
        for (const QJsonValue &val : raw) {
            const QJsonObject img = val.toObject();
            const QString fn = img.value(QStringLiteral("filename")).toString();
            if (fn.isEmpty())
                continue;
            OutputImage out;
            out.filename = fn;
            out.subfolder = img.value(QStringLiteral("subfolder")).toString();
            nodeImages.append(out);
        }
        return nodeImages;
    };

    // Inpaint / refine graphs use SaveImage node "10"; prefer it when multiple nodes emit images
    // (mask previews can appear first in outputs and look like a black silhouette).
    if (outputs.contains(QStringLiteral("10"))) {
        images = imagesFromNode(QStringLiteral("10"));
        if (!images.isEmpty()) {
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "extractOutputImages: picked SaveImage node 10 file=" << images.first().filename;
            return images;
        }
    }

    QString bestNodeId;
    int bestNodeNumeric = -1;
    for (auto it = outputs.constBegin(); it != outputs.constEnd(); ++it) {
        const QList<OutputImage> nodeImages = imagesFromNode(it.key());
        if (nodeImages.isEmpty())
            continue;
        bool ok = false;
        const int numeric = it.key().toInt(&ok);
        if (!bestNodeId.isEmpty() && ok && numeric <= bestNodeNumeric)
            continue;
        bestNodeId = it.key();
        bestNodeNumeric = ok ? numeric : bestNodeNumeric;
        images = nodeImages;
    }
    if (!images.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "extractOutputImages: picked fallback node" << bestNodeId << " file=" << images.first().filename;
    }
    return images;
}

HistoryFetchResult parseHistoryResponse(const QByteArray &responseBody,
                                        const QString &promptId,
                                        QNetworkReply::NetworkError networkError,
                                        const QString &networkErrorString)
{
    HistoryFetchResult out;
    if (networkError != QNetworkReply::NoError) {
        out.state = HistoryState::NetworkError;
        out.errorMessage = networkErrorString;
        return out;
    }

    const QJsonObject root = QJsonDocument::fromJson(responseBody).object();
    out.historyEntry = root.value(promptId).toObject();
    if (const QString execErr = ComfyUIUtils::comfyHistoryExecutionError(out.historyEntry); !execErr.isEmpty()) {
        out.state = HistoryState::ExecutionError;
        out.errorMessage = execErr;
        return out;
    }

    const QJsonObject outputs = out.historyEntry.value(QStringLiteral("outputs")).toObject();
    if (outputs.isEmpty()) {
        out.state = HistoryState::Running;
        return out;
    }

    out.images = extractOutputImages(out.historyEntry);
    out.state = out.images.isEmpty() ? HistoryState::NoImages : HistoryState::Done;
    return out;
}

void fetchHistory(QNetworkAccessManager *nam,
                  const QString &serverBaseUrl,
                  const QString &promptId,
                  QObject *context,
                  std::function<void(const HistoryFetchResult &)> onResult)
{
    if (!nam || serverBaseUrl.trimmed().isEmpty() || promptId.isEmpty()) {
        HistoryFetchResult out;
        out.state = HistoryState::NetworkError;
        out.errorMessage = ComfyTr::tr("Invalid history request.");
        if (onResult)
            onResult(out);
        return;
    }

    const QUrl url = historyEndpointUrl(serverBaseUrl, promptId);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = nam->get(req);
    if (context)
        reply->setParent(context);
    QObject::connect(reply, &QNetworkReply::finished, context ? context : nam, [reply, promptId, onResult = std::move(onResult)]() {
        const HistoryFetchResult result = parseHistoryResponse(
            reply->readAll(), promptId, reply->error(), reply->errorString());
        reply->deleteLater();
        if (onResult)
            onResult(result);
    });
}

void downloadOutputImage(QNetworkAccessManager *nam,
                         const QString &serverBaseUrl,
                         const OutputImage &image,
                         QObject *context,
                         std::function<void(const QByteArray &data, const QString &errorMessage)> onResult)
{
    if (!nam || image.filename.isEmpty()) {
        if (onResult)
            onResult(QByteArray(), ComfyTr::tr("Invalid image download request."));
        return;
    }

    const QUrl url = viewImageUrl(serverBaseUrl, image);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = nam->get(req);
    if (context)
        reply->setParent(context);
    QObject::connect(reply, &QNetworkReply::finished, context ? context : nam,
                     [reply, onResult = std::move(onResult)]() {
                         QByteArray data;
                         QString error;
                         if (reply->error() != QNetworkReply::NoError)
                             error = reply->errorString();
                         else
                             data = reply->readAll();
                         reply->deleteLater();
                         if (onResult)
                             onResult(data, error);
                     });
}

namespace {

QString submitErrorFromReply(QNetworkReply *reply, const QByteArray &body)
{
    if (reply->error() != QNetworkReply::NoError) {
        QString serverMsg = ComfyUIUtils::extractServerErrorFromBody(body);
        if (serverMsg.isEmpty())
            serverMsg = reply->errorString();
        return serverMsg;
    }
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    if (obj.contains(QStringLiteral("error"))) {
        QString serverMsg = ComfyUIUtils::extractServerErrorFromBody(body);
        if (serverMsg.isEmpty())
            serverMsg = ComfyTr::tr("(empty error body)");
        return serverMsg;
    }
    return QString();
}

} // namespace

void submitPrompt(QNetworkAccessManager *nam,
                  const QString &serverBaseUrl,
                  const SubmitRequest &request,
                  QObject *context,
                  std::function<void(const SubmitResult &)> onResult)
{
    SubmitResult failed;
    if (!nam) {
        failed.errorMessage = ComfyTr::tr("Not connected to ComfyUI server.");
        if (onResult)
            onResult(failed);
        return;
    }
    if (serverBaseUrl.trimmed().isEmpty()) {
        failed.errorMessage = ComfyTr::tr("Enter a server URL first.");
        if (onResult)
            onResult(failed);
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("prompt"), request.workflow);
    payload.insert(QStringLiteral("client_id"), request.clientId);
    if (!request.expectedPromptId.isEmpty())
        payload.insert(QStringLiteral("prompt_id"), request.expectedPromptId);
    if (!request.extraData.isEmpty())
        payload.insert(QStringLiteral("extra_data"), request.extraData);
    if (request.dumpPayload)
        ComfyUIUtils::dumpComfyPromptPayloadIfEnabled(payload);

    const QUrl url = promptEndpointUrl(serverBaseUrl);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = nam->post(req, body);
    if (context)
        reply->setParent(context);
    QObject::connect(reply, &QNetworkReply::finished, context ? context : nam,
                     [reply, onResult = std::move(onResult)]() {
                         SubmitResult result;
                         result.responseBody = reply->readAll();
                         result.httpStatus =
                             reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         result.errorMessage = submitErrorFromReply(reply, result.responseBody);
                         if (result.errorMessage.isEmpty()) {
                             result.promptId = QJsonDocument::fromJson(result.responseBody)
                                                   .object()
                                                   .value(QStringLiteral("prompt_id"))
                                                   .toString();
                             result.ok = !result.promptId.isEmpty();
                             if (!result.ok)
                                 result.errorMessage = ComfyTr::tr("Server did not return prompt_id.");
                         }
                         reply->deleteLater();
                         if (onResult)
                             onResult(result);
                     });
}

} // namespace ComfyPromptClient
