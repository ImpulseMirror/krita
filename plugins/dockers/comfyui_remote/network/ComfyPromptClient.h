/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PROMPT_CLIENT_H_
#define COMFY_PROMPT_CLIENT_H_

#include <QJsonObject>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;

namespace ComfyPromptClient {

struct OutputImage {
    QString filename;
    QString subfolder;
};

enum class HistoryState {
    NetworkError,
    ExecutionError,
    Running,
    Done,
    NoImages,
};

struct HistoryFetchResult {
    HistoryState state = HistoryState::NetworkError;
    QString errorMessage;
    QJsonObject historyEntry;
    QList<OutputImage> images;
};

struct SubmitResult {
    bool ok = false;
    QString promptId;
    QString errorMessage;
    int httpStatus = 0;
    QByteArray responseBody;
};

struct SubmitRequest {
    QJsonObject workflow;
    QString clientId;
    QString expectedPromptId;
    QJsonObject extraData;
    bool dumpPayload = false;
};

QUrl promptEndpointUrl(const QString &serverBaseUrl);
QUrl historyEndpointUrl(const QString &serverBaseUrl, const QString &promptId);
QUrl viewImageUrl(const QString &serverBaseUrl, const OutputImage &image);

QList<OutputImage> extractOutputImages(const QJsonObject &historyEntry);

HistoryFetchResult parseHistoryResponse(const QByteArray &responseBody,
                                        const QString &promptId,
                                        QNetworkReply::NetworkError networkError,
                                        const QString &networkErrorString);

void fetchHistory(QNetworkAccessManager *nam,
                  const QString &serverBaseUrl,
                  const QString &promptId,
                  QObject *context,
                  std::function<void(const HistoryFetchResult &)> onResult);

void downloadOutputImage(QNetworkAccessManager *nam,
                         const QString &serverBaseUrl,
                         const OutputImage &image,
                         QObject *context,
                         std::function<void(const QByteArray &data, const QString &errorMessage)> onResult);

void submitPrompt(QNetworkAccessManager *nam,
                  const QString &serverBaseUrl,
                  const SubmitRequest &request,
                  QObject *context,
                  std::function<void(const SubmitResult &)> onResult);

} // namespace ComfyPromptClient

#endif
