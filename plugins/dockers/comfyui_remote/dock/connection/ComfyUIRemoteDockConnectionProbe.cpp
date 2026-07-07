/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyConnectionInternal.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUiStyle.h"
#include "ComfyUIUtils.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QTimer>
#include <KSharedConfig>
#include <KConfigGroup>

using ComfyConnectionInternal::requiredObjectInfoNodes;

void ComfyUIRemoteDock::cancelConnectionAutostartRetry()
{
    if (m_d->connectionRetryTimer)
        m_d->connectionRetryTimer->stop();
}

void ComfyUIRemoteDock::scheduleConnectionAutostartRetry(const QString &reason)
{
    if (!m_d->connectionAutostartActive)
        return;

    if (m_d->connectionAutostartRetryAttempt >= m_d->connectionAutostartMaxRetries) {
        qWarning("ComfyUI Remote: autostart connect gave up after %d retries (%s).",
                 m_d->connectionAutostartMaxRetries,
                 qPrintable(reason));
        m_d->connectionAutostartActive = false;
        m_d->connectionAutostartRetryAttempt = 0;
        m_d->isConnecting = false;
        m_d->isConnected = false;
        m_d->connectionErrorOccurred = true;
        setStatusMessage(ComfyTr::tr("Could not connect to ComfyUI after %1 attempts. Use Connect to retry.",
                                    m_d->connectionAutostartMaxRetries),
                         true);
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
            ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
        }
        refreshConnectionActionButton();
        updateWelcomeVisibility();
        return;
    }

    const int attempt = m_d->connectionAutostartRetryAttempt + 1;
    const int delayMs = qMin(8000, 400 * (1 << m_d->connectionAutostartRetryAttempt));
    m_d->connectionAutostartRetryAttempt = attempt;

    qWarning("ComfyUI Remote: autostart connect retry %d/%d in %d ms (%s).",
             attempt,
             m_d->connectionAutostartMaxRetries,
             delayMs,
             qPrintable(reason));

    if (!m_d->connectionRetryTimer) {
        m_d->connectionRetryTimer = new QTimer(this);
        m_d->connectionRetryTimer->setSingleShot(true);
        connect(m_d->connectionRetryTimer, &QTimer::timeout, this, [this]() {
            if (m_d->connectionAutostartActive)
                slotTestConnection();
        });
    }

    m_d->isConnecting = true;
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(
            ComfyTr::tr("Retrying (%1/%2)", attempt, m_d->connectionAutostartMaxRetries));
        ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
    }
    setStatusMessage(ComfyTr::tr("Server not ready, retrying connection (%1/%2)…",
                                 attempt,
                                 m_d->connectionAutostartMaxRetries));
    refreshConnectionActionButton();
    updateWelcomeVisibility();
    m_d->connectionRetryTimer->start(delayMs);
}

void ComfyUIRemoteDock::handleConnectionProbeFailure(uint session,
                                                     const QString &retryReason,
                                                     const QString &userMessage,
                                                     bool isError,
                                                     bool allowAutostartRetry)
{
    if (session != m_d->connectionSessionId)
        return;

    m_d->comfyDeviceSummary.clear();
    m_d->lastComfySystemStats = QJsonObject();
    clearObjectInfoDerivedServerCaches();
    refreshStylesTabLoraWarning();
    applyStylesTabLoraListFilter();
    if (m_d->labelPerfDevice)
        m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
    m_d->isConnected = false;
    m_d->connectionErrorOccurred = true;

    if (m_d->connectionAutostartActive && allowAutostartRetry) {
        scheduleConnectionAutostartRetry(retryReason);
        if (m_d->connectionAutostartActive)
            return;
    } else if (m_d->connectionAutostartActive) {
        m_d->connectionAutostartActive = false;
        m_d->connectionAutostartRetryAttempt = 0;
    }

    m_d->isConnecting = false;
    m_d->connectionErrorKind = QStringLiteral("network");
    setStatusMessage(userMessage, isError);
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Error: %1", userMessage));
        ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Error);
    }
    if (m_d->labelDetectedModels) {
        m_d->labelDetectedModels->setText(
            ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        ComfyUiStyle::styleCaption(m_d->labelDetectedModels);
        m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
    }
    updateWelcomeVisibility();
    refreshConnectionActionButton();
}

void ComfyUIRemoteDock::handleConnectionEstablished(uint session,
                                                      const QJsonObject &objectInfoRoot,
                                                      const QString &base)
{
    if (session != m_d->connectionSessionId)
        return;

    cancelConnectionAutostartRetry();
    m_d->connectionAutostartActive = false;
    m_d->connectionAutostartRetryAttempt = 0;
    m_d->isConnecting = false;
    m_d->isConnected = true;
    m_d->connectionErrorOccurred = false;
    m_d->connectionErrorKind.clear();

    syncFromObjectInfoRoot(objectInfoRoot);
    fetchComfyModelsLorasMergeAndRefreshStylesTab();
    {
        const QStringList ckptNames = ComfyUIUtils::parseCheckpointNamesFromObjectInfoRoot(objectInfoRoot);
        const QString bu = m_d->editServerUrl->text().trimmed();
        if (!ckptNames.isEmpty() && !bu.isEmpty())
            fetchFilteredCheckpointListAndApply(ckptNames, bu, true);
    }

    {
        QStringList missing;
        for (const QString &key : requiredObjectInfoNodes()) {
            if (!objectInfoRoot.contains(key))
                missing << key;
        }
        if (!missing.isEmpty())
            m_d->connectionErrorKind = QStringLiteral("missing_resources");
    }

    setStatusMessage(ComfyTr::tr("Connected to ComfyUI."));
    updateWelcomeVisibility();
    refreshConnectionTabUi();
    refreshInterfacePromptTranslationCombo();
    Q_UNUSED(base);
}

void ComfyUIRemoteDock::slotTestConnection()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        cancelConnectionAutostartRetry();
        m_d->connectionAutostartActive = false;
        m_d->connectionAutostartRetryAttempt = 0;
        setStatusMessage(ComfyTr::tr("Enter a server URL."), true);
        m_d->isConnected = false;
        m_d->isConnecting = false;
        m_d->connectionErrorOccurred = false;
        m_d->connectionErrorKind.clear();
        m_d->comfyDeviceSummary.clear();
        m_d->lastComfySystemStats = QJsonObject();
        if (m_d->labelPerfDevice)
            m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
            ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
        }
        if (m_d->labelDetectedModels) {
            m_d->labelDetectedModels->setText(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
            ComfyUiStyle::styleCaption(m_d->labelDetectedModels);
            m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
        }
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        updateWelcomeVisibility();
        refreshConnectionActionButton();
        return;
    }
    const QString base = ComfyUIUtils::normalizeComfyServerBaseUrl(urlStr);
    QUrl url = ComfyUIUtils::comfyResolveApiUrl(base, QStringLiteral("system_stats"));
    if (!url.isValid() || url.host().isEmpty()) {
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        m_d->isConnected = false;
        m_d->isConnecting = false;
        m_d->connectionErrorOccurred = true;
        m_d->comfyDeviceSummary.clear();
        m_d->lastComfySystemStats = QJsonObject();
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        if (m_d->labelPerfDevice)
            m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Error: Invalid URL"));
            ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Error);
        }
        if (m_d->labelDetectedModels) {
            m_d->labelDetectedModels->setText(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        }
        updateWelcomeVisibility();
        refreshConnectionActionButton();
        return;
    }
    cancelConnectionAutostartRetry();
    const uint session = ++m_d->connectionSessionId;
    m_d->isConnected = false;
    setStatusMessage(ComfyTr::tr("Connecting…"));
    m_d->isConnecting = true;
    m_d->connectionErrorOccurred = false;
    updateWelcomeVisibility();
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Connecting"));
        ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
    }
    refreshConnectionActionButton();
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, base, session]() {
        if (session != m_d->connectionSessionId) {
            reply->deleteLater();
            return;
        }
        const QByteArray responseBody = reply->readAll();
        reply->deleteLater();
        const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && httpCode == 200) {
            const QJsonObject sysRoot = QJsonDocument::fromJson(responseBody).object();
            m_d->lastComfySystemStats = sysRoot;
            m_d->comfyDeviceSummary = ComfyUIUtils::formatComfySystemStatsDeviceLine(sysRoot);
            if (m_d->labelPerfDevice)
                m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary);
            syncPerformanceFromAutoPreset();
            setStatusMessage(ComfyTr::tr("Loading server capabilities…"));
            if (m_d->labelConnectionStatus) {
                m_d->labelConnectionStatus->setText(ComfyTr::tr("Loading server capabilities…"));
                ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
            }

            QUrl objUrl = ComfyUIUtils::comfyResolveApiUrl(base, QStringLiteral("object_info"));
            QNetworkRequest reqObj(objUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqObj);
            QNetworkReply *replyObj = m_d->nam->get(reqObj);
            connect(replyObj, &QNetworkReply::finished, this, [this, replyObj, base, session]() {
                if (session != m_d->connectionSessionId) {
                    replyObj->deleteLater();
                    return;
                }
                replyObj->deleteLater();
                if (replyObj->error() != QNetworkReply::NoError) {
                    qWarning("ComfyUI Remote: object_info failed during connect: %s",
                             qPrintable(replyObj->errorString()));
                    handleConnectionProbeFailure(
                        session,
                        replyObj->errorString(),
                        ComfyTr::tr("Connected to server but could not load capabilities: %1",
                                    replyObj->errorString()));
                    return;
                }
                const QJsonObject root = QJsonDocument::fromJson(replyObj->readAll()).object();
                if (root.isEmpty()) {
                    qWarning("ComfyUI Remote: object_info returned empty JSON during connect.");
                    handleConnectionProbeFailure(
                        session,
                        QStringLiteral("empty object_info"),
                        ComfyTr::tr("Connected to server but received an empty capability list."));
                    return;
                }
                handleConnectionEstablished(session, root, base);
            });
            return;
        }

        QString userMessage;
        QString retryReason = reply->errorString();
        if (httpCode == 401) {
            m_d->connectionErrorKind = QStringLiteral("network");
            userMessage = ComfyTr::tr("Unauthorized (401). Check server authentication.");
            retryReason = QStringLiteral("HTTP 401");
            handleConnectionProbeFailure(session, retryReason, userMessage, true, false);
            return;
        }

        const bool networkError = (reply->error() != QNetworkReply::NoError
                                 && reply->error() != QNetworkReply::ContentNotFoundError);
        m_d->connectionErrorKind = networkError ? QStringLiteral("network") : QStringLiteral("unknown");
        userMessage = ComfyTr::tr("Connection failed: %1", reply->errorString());
        qWarning("ComfyUI Remote: system_stats failed during connect (http=%d): %s",
                 httpCode,
                 qPrintable(retryReason));
        handleConnectionProbeFailure(session, retryReason, userMessage);
    });
}

void ComfyUIRemoteDock::tryAutostartServerFallback()
{
    if (!m_d->editServerUrl || !m_d->nam || m_d->autostartServerProbeDone)
        return;

    m_d->autostartServerProbeDone = true;

    if (m_d->isConnected || m_d->isConnecting)
        return;

    const QString savedUrl = ComfyUIUtils::savedServerUrl();
    if (savedUrl.isEmpty())
        return;

    if (m_d->editServerUrl->text().trimmed().isEmpty()) {
        QSignalBlocker b(m_d->editServerUrl);
        m_d->editServerUrl->setText(savedUrl);
    }

    KConfigGroup modeCfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const QString mode = modeCfg.readEntry(QStringLiteral("ServerMode"), QStringLiteral("undefined"));
    if (mode != QLatin1String("external")) {
        modeCfg.writeEntry(QStringLiteral("ServerMode"), QStringLiteral("external"));
        KSharedConfig::openConfig()->sync();
    }

    m_d->connectionAutostartActive = true;
    m_d->connectionAutostartRetryAttempt = 0;
    slotTestConnection();
}
