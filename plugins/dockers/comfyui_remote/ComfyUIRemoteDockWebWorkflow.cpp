/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QPointer>
#include <QUuid>
#include <klocalizedstring.h>

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
#include <QAbstractSocket>
#include <QWebSocket>
#endif

namespace {

void postEtnWorkflowJson(ComfyUIRemoteDock *dock,
                         ComfyUIRemoteDock::Private *d,
                         const QString &relativePath,
                         const QJsonObject &body)
{
    if (!d->nam)
        return;
    const QString base = d->editServerUrl ? d->editServerUrl->text().trimmed() : QString();
    if (base.isEmpty())
        return;
    QUrl url = ComfyUIUtils::comfyResolveApiUrl(base, relativePath);
    if (!url.isValid())
        return;
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = d->nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, dock, [reply]() {
        reply->deleteLater();
    });
}

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)

class WebWorkflowSwitchSession : public QObject
{
public:
    WebWorkflowSwitchSession(ComfyUIRemoteDock *dock, ComfyUIRemoteDock::Private *priv)
        : QObject(dock)
        , m_dock(dock)
        , m_d(priv)
        , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        QObject::connect(m_timer, &QTimer::timeout, this, [this]() {
            shutdown();
        });

        QObject::connect(m_ws, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
            QJsonParseError err;
            QJsonObject root = QJsonDocument::fromJson(msg.toUtf8(), &err).object();
            if (err.error != QJsonParseError::NoError)
                return;
            if (root.value(QStringLiteral("type")).toString() != QLatin1String("etn_workflow_published"))
                return;
            QJsonObject data = root.value(QStringLiteral("data")).toObject();
            QJsonObject wf = data.value(QStringLiteral("workflow")).toObject();
            if (wf.isEmpty())
                return;
            if (m_d->editCustomWorkflow) {
                const QString json = QString::fromUtf8(QJsonDocument(wf).toJson(QJsonDocument::Indented));
                m_d->editCustomWorkflow->setPlainText(json);
            }
            if (m_dock)
                m_dock->setStatusMessage(i18n("Received published workflow from ComfyUI web UI."));
            if (m_dock)
                m_dock->persistOpenCustomWorkflowToDocument();
            shutdown();
        });

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
        QObject::connect(m_ws, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
#else
        QObject::connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
                         [this](QAbstractSocket::SocketError) {
#endif
                             if (m_dock)
                                 m_dock->setStatusMessage(
                                     i18n("WebSocket error while listening for workflows: %1", m_ws->errorString()),
                                     false,
                                     true);
                         });

        m_timer->start(5 * 60 * 1000);
    }

    void openSocket(const QUrl &wsUrl)
    {
        m_ws->open(wsUrl);
    }

    void shutdown()
    {
        if (m_done)
            return;
        m_done = true;
        postEtnWorkflowJson(m_dock, m_d, QStringLiteral("api/etn/workflow/unsubscribe"),
                            QJsonObject{{QStringLiteral("client_id"), m_d->clientId}});
        m_ws->close();
        m_timer->stop();
        if (m_d->webWorkflowSwitchSession == this)
            m_d->webWorkflowSwitchSession = nullptr;
        deleteLater();
    }

private:
    QPointer<ComfyUIRemoteDock> m_dock;
    ComfyUIRemoteDock::Private *m_d;
    QWebSocket *m_ws;
    QTimer *m_timer{};
    bool m_done = false;
};

#endif // COMFYUI_HAVE_QT_WEBSOCKETS

} // namespace

void ComfyUIRemoteDock::beginWebWorkflowSwitch()
{
    endWebWorkflowSwitch();

    QString base = m_d->editServerUrl ? m_d->editServerUrl->text().trimmed() : QString();
    if (base.isEmpty())
        return;
    if (m_d->clientId.isEmpty())
        m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    postEtnWorkflowJson(this, m_d.data(), QStringLiteral("api/etn/workflow/subscribe"),
                        QJsonObject{{QStringLiteral("client_id"), m_d->clientId}});

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    QUrl wsUrl = ComfyUIUtils::comfyWebSocketUrlForClient(base, m_d->clientId);
    if (!wsUrl.isValid()) {
        setStatusMessage(i18n("Could not build WebSocket URL for workflow subscription."), false, true);
        return;
    }
    auto *session = new WebWorkflowSwitchSession(this, m_d.data());
    m_d->webWorkflowSwitchSession = session;
    session->openSocket(wsUrl);
    setStatusMessage(
        i18n("Listening for workflows published from ComfyUI (5 minutes). Publish in the web UI to load the graph here."));
#else
    setStatusMessage(
        i18n("Subscribed for workflow updates. This build has no Qt WebSockets — import the exported API JSON from "
             "Settings → Workflow, or rebuild Krita’s Qt with the WebSockets module for automatic capture."),
        false,
        true);
#endif
}

void ComfyUIRemoteDock::endWebWorkflowSwitch()
{
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (m_d->webWorkflowSwitchSession) {
        auto *s = qobject_cast<WebWorkflowSwitchSession *>(m_d->webWorkflowSwitchSession);
        if (s)
            s->shutdown();
        else
            m_d->webWorkflowSwitchSession->deleteLater();
        m_d->webWorkflowSwitchSession = nullptr;
    }
#endif
}
