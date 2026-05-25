/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_MOCK_HTTP_SERVER_H_
#define COMFY_MOCK_HTTP_SERVER_H_

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpServer>

/// Minimal ComfyUI/ETN HTTP stub for integration tests (QTcpServer, HTTP/1.1).
class ComfyMockHttpServer : public QObject
{
    Q_OBJECT

public:
    explicit ComfyMockHttpServer(QObject *parent = nullptr);

    bool listen();
    quint16 port() const;
    QString baseUrl() const;

    QString lastMethod() const { return m_lastMethod; }
    QString lastPath() const { return m_lastPath; }
    QByteArray lastBody() const { return m_lastBody; }
    int lastStatusSent() const { return m_lastStatusSent; }

private slots:
    void onNewConnection();

private:
    void handleRequest(QTcpSocket *socket, QByteArray *buffer);

    QTcpServer m_server;
    QString m_lastMethod;
    QString m_lastPath;
    QByteArray m_lastBody;
    int m_lastStatusSent = 0;
};

#endif
