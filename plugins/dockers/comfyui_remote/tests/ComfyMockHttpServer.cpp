/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyMockHttpServer.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>

namespace {

QByteArray minimalObjectInfoJson()
{
    return QByteArrayLiteral(R"json({
        "CheckpointLoaderSimple": {
            "input": {
                "required": {
                    "ckpt_name": [["dreamshaper_8.safetensors", "other.ckpt"]]
                }
            }
        },
        "KSampler": {
            "input": {
                "required": {
                    "sampler_name": [["euler", "dpmpp_2m"]]
                }
            }
        },
        "LoraLoader": {
            "input": {
                "required": {
                    "lora_name": [["hero.safetensors"]]
                }
            }
        },
        "VAELoader": {
            "input": {
                "required": {
                    "vae_name": [["vae-ft-mse.safetensors"]]
                }
            }
        },
        "CLIPTextEncode": {}
    })json");
}

QByteArray systemStatsJson()
{
    QJsonObject root;
    QJsonArray devs;
    devs.append(QJsonObject{{QStringLiteral("name"), QStringLiteral("Mock GPU")},
                            {QStringLiteral("type"), QStringLiteral("cuda")},
                            {QStringLiteral("vram_total"), 8589934592.0}});
    root.insert(QStringLiteral("devices"), devs);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray modelsLorasJson()
{
    return QJsonDocument(QJsonArray{QStringLiteral("hero.safetensors"), QStringLiteral("remote.safetensors")})
        .toJson(QJsonDocument::Compact);
}

QByteArray etnModelInfoCheckpointsJson()
{
    QJsonObject item;
    item.insert(QStringLiteral("filename"), QStringLiteral("dreamshaper_8.safetensors"));
    item.insert(QStringLiteral("arch"), QStringLiteral("sd15"));
    QJsonObject root;
    root.insert(QStringLiteral("items"), QJsonArray{item});
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void writeHttpResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body)
{
    const QByteArray statusLine = (statusCode == 200) ? QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                                                      : QByteArrayLiteral("HTTP/1.1 404 Not Found\r\n");
    QByteArray resp = statusLine;
    resp += QByteArrayLiteral("Content-Type: ");
    resp += contentType;
    resp += QByteArrayLiteral("\r\nContent-Length: ");
    resp += QByteArray::number(body.size());
    resp += QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
    resp += body;
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

QString pathFromRequestLine(const QByteArray &line)
{
    const QList<QByteArray> parts = line.trimmed().split(' ');
    if (parts.size() < 2)
        return QString();
    return QString::fromUtf8(parts.at(1));
}

} // namespace

ComfyMockHttpServer::ComfyMockHttpServer(QObject *parent)
    : QObject(parent)
{
    QObject::connect(&m_server, &QTcpServer::newConnection, this, &ComfyMockHttpServer::onNewConnection);
}

bool ComfyMockHttpServer::listen()
{
    return m_server.listen(QHostAddress::LocalHost, 0);
}

quint16 ComfyMockHttpServer::port() const
{
    return m_server.serverPort();
}

QString ComfyMockHttpServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

void ComfyMockHttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        auto *buffer = new QByteArray();
        QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n\r\n"))
                return;

            const int headerEnd = buffer->indexOf("\r\n\r\n");
            int contentLength = 0;
            const QList<QByteArray> headerLines = buffer->left(headerEnd).split('\n');
            for (int i = 1; i < headerLines.size(); ++i) {
                const QByteArray hl = headerLines.at(i).trimmed();
                if (hl.toLower().startsWith("content-length:")) {
                    contentLength = hl.mid(15).trimmed().toInt();
                    break;
                }
            }
            const int bodyStart = headerEnd + 4;
            if (buffer->size() - bodyStart < contentLength)
                return;

            handleRequest(socket, buffer);
            delete buffer;
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void ComfyMockHttpServer::handleRequest(QTcpSocket *socket, QByteArray *buffer)
{
    const int headerEnd = buffer->indexOf("\r\n\r\n");
    const QByteArray headerBlock = buffer->left(headerEnd);
    QByteArray body = buffer->mid(headerEnd + 4);

    const QList<QByteArray> headerLines = headerBlock.split('\n');
    if (headerLines.isEmpty()) {
        writeHttpResponse(socket, 404, QByteArrayLiteral("text/plain"), QByteArrayLiteral("bad request"));
        m_lastStatusSent = 404;
        return;
    }

    const QByteArray requestLine = headerLines.first().trimmed();
    const QString path = pathFromRequestLine(requestLine);
    const QList<QByteArray> reqParts = requestLine.split(' ');
    m_lastMethod = reqParts.isEmpty() ? QString() : QString::fromUtf8(reqParts.first());
    m_lastPath = path;

    int contentLength = 0;
    for (int i = 1; i < headerLines.size(); ++i) {
        const QByteArray hl = headerLines.at(i).trimmed();
        if (hl.toLower().startsWith("content-length:")) {
            contentLength = hl.mid(15).trimmed().toInt();
            break;
        }
    }
    if (contentLength > 0 && body.size() < contentLength) {
        while (body.size() < contentLength && socket->bytesAvailable() > 0)
            body.append(socket->readAll());
    }
    m_lastBody = body;

    const QString pathOnly = QUrl(path).path();
    QByteArray responseBody;
    QByteArray contentType = QByteArrayLiteral("application/json");
    int status = 200;

    if (pathOnly.endsWith(QLatin1String("/object_info")) || pathOnly == QLatin1String("/object_info")) {
        responseBody = minimalObjectInfoJson();
    } else if (pathOnly.endsWith(QLatin1String("/system_stats")) || pathOnly == QLatin1String("/system_stats")) {
        responseBody = systemStatsJson();
    } else if (pathOnly.contains(QLatin1String("/models/loras"))) {
        responseBody = modelsLorasJson();
    } else if (pathOnly.contains(QLatin1String("/api/etn/model_info/checkpoints"))) {
        responseBody = etnModelInfoCheckpointsJson();
    } else if (pathOnly.contains(QLatin1String("/api/etn/translate/"))) {
        contentType = QByteArrayLiteral("text/plain; charset=utf-8");
        responseBody = QByteArrayLiteral("translated-mock");
    } else if (pathOnly.contains(QLatin1String("/api/etn/upload/loras/"))) {
        responseBody = QByteArrayLiteral("{\"ok\":true}");
    } else if (pathOnly.endsWith(QLatin1String("/upload/image")) || pathOnly.contains(QLatin1String("/upload/image"))) {
        m_uploadImageHitCount++;
        responseBody = QByteArrayLiteral("{\"name\":\"mock-upload.png\"}");
    } else if (pathOnly.endsWith(QLatin1String("/prompt")) || pathOnly == QLatin1String("/prompt")) {
        responseBody = QByteArrayLiteral("{\"prompt_id\":\"mock-prompt-42\"}");
    } else if (pathOnly.contains(QLatin1String("/history/"))) {
        QJsonObject hist;
        hist.insert(QStringLiteral("mock-prompt-42"),
                    QJsonObject{{QStringLiteral("outputs"),
                                 QJsonObject{{QStringLiteral("9"),
                                              QJsonObject{{QStringLiteral("images"),
                                                           QJsonArray{QJsonObject{{QStringLiteral("filename"),
                                                                                    QStringLiteral("out.png")}}}}}}}}});
        responseBody = QJsonDocument(hist).toJson(QJsonDocument::Compact);
    } else {
        status = 404;
        contentType = QByteArrayLiteral("text/plain");
        responseBody = QByteArrayLiteral("not found");
    }

    m_lastStatusSent = status;
    writeHttpResponse(socket, status, contentType, responseBody);
}
