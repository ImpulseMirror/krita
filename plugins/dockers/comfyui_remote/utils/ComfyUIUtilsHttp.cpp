/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"

#include <QDir>
#include <QObject>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace ComfyUIUtils {

QStringList promptIdsFromComfyQueueResponse(const QJsonObject &queueRoot)
{
    QStringList ids;
    const auto collect = [&](const char *key) {
        const QJsonArray arr = queueRoot.value(QString::fromUtf8(key)).toArray();
        for (const QJsonValue &v : arr) {
            if (!v.isArray())
                continue;
            const QJsonArray item = v.toArray();
            if (item.size() >= 2) {
                const QString id = item.at(1).toString();
                if (!id.isEmpty())
                    ids.append(id);
            }
        }
    };
    collect("queue_running");
    collect("queue_pending");
    return ids;
}

void requestComfyClearAllQueueJobs(QNetworkAccessManager *nam,
                                   const QString &baseUrlTrimmed,
                                   const QStringList &localPromptIds,
                                   QObject *context)
{
    if (!nam || !context)
        return;
    const QString base = baseUrlTrimmed.trimmed();
    if (base.isEmpty())
        return;

    QSet<QString> ids;
    for (const QString &id : localPromptIds) {
        if (!id.isEmpty())
            ids.insert(id);
    }

    const auto postInterrupt = [nam, base]() {
        QUrl url = comfyResolveApiUrl(base, QStringLiteral("/interrupt"));
        QNetworkRequest req(url);
        setComfyUIRequestHeaders(req);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        nam->post(req, QByteArray("{}"));
    };

    const auto postDelete = [nam, base, postInterrupt, context](const QStringList &deleteIds) {
        if (deleteIds.isEmpty()) {
            postInterrupt();
            return;
        }
        QJsonObject delPayload;
        QJsonArray arr;
        for (const QString &id : deleteIds)
            arr.append(id);
        delPayload.insert(QStringLiteral("delete"), arr);
        QUrl queueUrl = comfyResolveApiUrl(base, QStringLiteral("/queue"));
        QNetworkRequest reqQueue(queueUrl);
        setComfyUIRequestHeaders(reqQueue);
        reqQueue.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *delReply =
            nam->post(reqQueue, QJsonDocument(delPayload).toJson(QJsonDocument::Compact));
        QObject::connect(delReply, &QNetworkReply::finished, context, [delReply, postInterrupt]() {
            delReply->deleteLater();
            postInterrupt();
        });
    };

    QUrl queueGetUrl = comfyResolveApiUrl(base, QStringLiteral("/queue"));
    QNetworkRequest reqGet(queueGetUrl);
    setComfyUIRequestHeaders(reqGet);
    QNetworkReply *getReply = nam->get(reqGet);
    QObject::connect(getReply, &QNetworkReply::finished, context, [getReply, ids, postDelete]() mutable {
        getReply->deleteLater();
        if (getReply->error() == QNetworkReply::NoError) {
            const QJsonObject obj = QJsonDocument::fromJson(getReply->readAll()).object();
            for (const QString &id : promptIdsFromComfyQueueResponse(obj))
                ids.insert(id);
        }
        QStringList deleteIds;
        deleteIds.reserve(ids.size());
        for (const QString &id : ids)
            deleteIds.append(id);
        postDelete(deleteIds);
    });
}
void setComfyUIRequestHeaders(QNetworkRequest &req)
{
    req.setRawHeader(QByteArrayLiteral("ngrok-skip-browser-warning"), QByteArrayLiteral("69420"));
}

QString normalizeComfyServerBaseUrl(const QString &hostOrUrl)
{
    QString base = hostOrUrl.trimmed();
    if (base.isEmpty())
        return QString();
    if (!base.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        base = QStringLiteral("http://") + base;
    }
    return base;
}

QUrl comfyResolveApiUrl(const QString &baseUrlTrimmed, const QString &relativeApiPath)
{
    QString base = baseUrlTrimmed.trimmed();
    QString rel = relativeApiPath;
    if (rel.startsWith(QLatin1Char('/')))
        rel = rel.mid(1);
    QUrl url(base);
    if (!url.isValid())
        return url;
    QString path = url.path();
    if (path.isEmpty() || path == QLatin1String("/"))
        url.setPath(QLatin1Char('/') + rel);
    else if (!path.endsWith(QLatin1Char('/')))
        url.setPath(path + QLatin1Char('/') + rel);
    else
        url.setPath(path + rel);
    return url;
}

QUrl comfyWebSocketUrlForClient(const QString &httpUrlTrimmed, const QString &clientId)
{
    QString base = httpUrlTrimmed.trimmed();
    if (base.isEmpty())
        return QUrl();
    if (!base.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        base = QStringLiteral("http://") + base;
    }
    QUrl http(base);
    if (!http.isValid() || http.host().isEmpty())
        return QUrl();
    const bool tls = http.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0;
    QUrl ws;
    ws.setScheme(tls ? QStringLiteral("wss") : QStringLiteral("ws"));
    ws.setHost(http.host());
    if (http.port() > 0)
        ws.setPort(http.port());
    ws.setPath(QStringLiteral("/ws"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("clientId"), clientId);
    ws.setQuery(q);
    return ws;
}

QNetworkReply *tryUploadLoraFileViaEtnApi(QNetworkAccessManager *nam,
                                          const QString &baseUrlTrimmed,
                                          const QString &localFilePath,
                                          QObject *parentForReply)
{
    if (!nam || baseUrlTrimmed.trimmed().isEmpty())
        return nullptr;
    auto *device = new QFile(localFilePath);
    if (!device->open(QIODevice::ReadOnly)) {
        delete device;
        return nullptr;
    }
    const QString baseName = QFileInfo(localFilePath).fileName();
    if (baseName.isEmpty()) {
        delete device;
        return nullptr;
    }
    QUrl root(baseUrlTrimmed.trimmed());
    QString rootPath = root.path();
    if (!rootPath.endsWith(QLatin1Char('/')))
        root.setPath(rootPath + QLatin1Char('/'));
    const QString rel = QStringLiteral("api/etn/upload/loras/")
        + QString::fromUtf8(QUrl::toPercentEncoding(baseName, QByteArray(), QByteArray("/")));
    const QUrl target = root.resolved(QUrl(rel, QUrl::StrictMode));

    QNetworkRequest req(target);
    setComfyUIRequestHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    QNetworkReply *reply = nam->put(req, device);
    if (parentForReply)
        reply->setParent(parentForReply);
    return reply;
}

// §13.142: Show LCM deprecation message when backend rejects LCM (cloud/managed server)
QString formatServerErrorMessage(const QString &serverError)
{
    if (serverError.toLower().contains(QLatin1String("lcm")))
        return ComfyTr::tr("LCM is no longer supported by the server. Please change the Style's sampling method to 'Realtime - Hyper'.");
    return serverError;
}

QString extractServerErrorFromBody(const QByteArray &responseBody)
{
    if (responseBody.isEmpty())
        return QString();

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        // Not JSON - return the body itself (trimmed and capped). Surfaces
        // HTML error pages, plaintext errors from reverse proxies, etc.
        QString raw = QString::fromUtf8(responseBody).trimmed();
        if (raw.size() > 400)
            raw = raw.left(400) + QLatin1String("…");
        return raw;
    }

    const QJsonObject root = doc.object();
    QStringList parts;

    // Handle the structured form first: {"error": {"type":..., "message":..., "details":..., "extra_info":...}}
    const QJsonValue errVal = root.value(QStringLiteral("error"));
    if (errVal.isObject()) {
        const QJsonObject errObj = errVal.toObject();
        const QString type = errObj.value(QStringLiteral("type")).toString();
        const QString message = errObj.value(QStringLiteral("message")).toString();
        const QString details = errObj.value(QStringLiteral("details")).toString();
        QString line;
        if (!type.isEmpty())
            line = type;
        if (!message.isEmpty())
            line = line.isEmpty() ? message : (line + QStringLiteral(": ") + message);
        if (!details.isEmpty())
            line = line.isEmpty() ? details : (line + QStringLiteral(" — ") + details);
        if (line.isEmpty())
            line = QString::fromUtf8(QJsonDocument(errObj).toJson(QJsonDocument::Compact));
        parts << formatServerErrorMessage(line);
    } else if (errVal.isString()) {
        const QString s = errVal.toString().trimmed();
        if (!s.isEmpty())
            parts << formatServerErrorMessage(s);
    }

    // ComfyUI 0.3+ surfaces per-node errors here. Each entry is
    // {"<node_id>": {"errors":[{"type":..., "message":..., "details":...}], "class_type": "..."}}.
    // These are the most diagnostic part of a 400 response (e.g. missing checkpoint file,
    // invalid input type) so we surface them prominently when present.
    const QJsonObject nodeErrors = root.value(QStringLiteral("node_errors")).toObject();
    for (auto it = nodeErrors.constBegin(); it != nodeErrors.constEnd(); ++it) {
        const QJsonObject info = it.value().toObject();
        const QString classType = info.value(QStringLiteral("class_type")).toString();
        const QJsonArray errs = info.value(QStringLiteral("errors")).toArray();
        for (const QJsonValue &ev : errs) {
            const QJsonObject e = ev.toObject();
            const QString message = e.value(QStringLiteral("message")).toString();
            const QString details = e.value(QStringLiteral("details")).toString();
            QString line = QStringLiteral("node %1 (%2): %3")
                                .arg(it.key(), classType.isEmpty() ? QStringLiteral("?") : classType,
                                     message.isEmpty() ? QStringLiteral("(no message)") : message);
            if (!details.isEmpty())
                line += QStringLiteral(" — ") + details;
            parts << formatServerErrorMessage(line);
        }
    }

    if (parts.isEmpty()) {
        // Last-resort: stringify the JSON so something always reaches the user/log
        // instead of an empty status message.
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)).left(400);
    }
    QString joined = parts.join(QStringLiteral("; "));
    if (joined.size() > 600)
        joined = joined.left(600) + QLatin1String("…");
    return joined;
}
QString uploadImageToComfySync(::QNetworkAccessManager *nam,
                               const QString &serverBaseUrl,
                               const QImage &image,
                               const QString &filenameHint,
                               QString *errorOut)
{
    if (errorOut)
        errorOut->clear();
    if (!nam || serverBaseUrl.trimmed().isEmpty() || image.isNull()) {
        if (errorOut)
            *errorOut = ComfyTr::tr("Invalid upload parameters.");
        return QString();
    }

    QUrl uploadUrl = ComfyUIUtils::comfyResolveApiUrl(serverBaseUrl.trimmed(), QStringLiteral("upload/image"));
    if (!uploadUrl.isValid()) {
        if (errorOut)
            *errorOut = ComfyTr::tr("Invalid server URL.");
        return QString();
    }

    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    tmp.setFileTemplate(QDir::tempPath() + QLatin1String("/krita_comfy_upload_XXXXXX.png"));
    if (!tmp.open()) {
        if (errorOut)
            *errorOut = ComfyTr::tr("Could not create temp image file.");
        return QString();
    }
    if (!image.save(&tmp, "PNG")) {
        if (errorOut)
            *errorOut = ComfyTr::tr("Could not save temp image.");
        return QString();
    }
    tmp.seek(0);

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    const QString fname = filenameHint.isEmpty() ? QStringLiteral("krita_upload.png") : filenameHint;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(fname)));
    part.setBodyDevice(&tmp);
    multiPart->append(part);

    QNetworkRequest req(uploadUrl);
    setComfyUIRequestHeaders(req);
    QNetworkReply *reply = nam->post(req, multiPart);
    multiPart->setParent(reply);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString serverName;
    if (reply->error() != QNetworkReply::NoError) {
        if (errorOut)
            *errorOut = reply->errorString();
    } else {
        serverName = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (serverName.isEmpty() && errorOut)
            *errorOut = ComfyTr::tr("Server did not return image name.");
    }
    reply->deleteLater();
    return serverName;
}
QString comfyHistoryExecutionError(const QJsonObject &historyEntry)
{
    if (historyEntry.isEmpty())
        return QString();

    const QJsonObject status = historyEntry.value(QStringLiteral("status")).toObject();
    const QJsonArray messages = status.value(QStringLiteral("messages")).toArray();
    for (const QJsonValue &mv : messages) {
        const QJsonArray msg = mv.toArray();
        if (msg.size() < 2)
            continue;
        const QString kind = msg.at(0).toString();
        if (kind != QLatin1String("execution_error") && kind != QLatin1String("execution_failed"))
            continue;
        const QJsonObject detail = msg.at(1).toObject();
        QString text = detail.value(QStringLiteral("exception_message")).toString().trimmed();
        if (text.isEmpty())
            text = detail.value(QStringLiteral("message")).toString().trimmed();
        const QString nodeType = detail.value(QStringLiteral("node_type")).toString();
        const QString nodeId = detail.value(QStringLiteral("node_id")).toString();
        QString where;
        if (!nodeType.isEmpty() && !nodeId.isEmpty())
            where = QStringLiteral("%1 (%2)").arg(nodeType, nodeId);
        else if (!nodeType.isEmpty())
            where = nodeType;
        else if (!nodeId.isEmpty())
            where = nodeId;
        if (!where.isEmpty() && !text.isEmpty())
            return ComfyTr::tr("%1: %2", where, text);
        if (!text.isEmpty())
            return text;
        if (!where.isEmpty())
            return ComfyTr::tr("Node %1 failed.", where);
        break;
    }

    const QString statusStr = status.value(QStringLiteral("status_str")).toString();
    if (statusStr == QLatin1String("error") || statusStr == QLatin1String("failed")) {
        const QJsonObject outputs = historyEntry.value(QStringLiteral("outputs")).toObject();
        if (outputs.isEmpty())
            return ComfyTr::tr("ComfyUI reported an execution error.");
    }
    return QString();
}

void requestEtnPromptTranslation(QNetworkAccessManager *nam,
                                 const QString &baseUrlTrimmed,
                                 const QString &langCode,
                                 const QString &text,
                                 QObject *context,
                                 std::function<void(bool ok, const QString &translated)> onDone)
{
    if (!onDone) {
        return;
    }
    if (!nam || text.trimmed().isEmpty() || langCode.isEmpty() || langCode == QLatin1String("disabled")) {
        onDone(false, text);
        return;
    }
    QString base = baseUrlTrimmed.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(text));
    QUrl url(base + QStringLiteral("/api/etn/translate/") + langCode + QLatin1Char('/') + encoded);
    QNetworkRequest req(url);
    setComfyUIRequestHeaders(req);
    QNetworkReply *reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, onDone, text]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            onDone(false, text);
            return;
        }
        QString translated = QString::fromUtf8(reply->readAll()).trimmed();
        if (translated.isEmpty())
            translated = text;
        onDone(true, translated);
    });
}

}
