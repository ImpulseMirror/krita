/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QTest>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include "ComfyMockHttpServer.h"
#include "ComfyPromptClient.h"

namespace {

QJsonObject historyEntryWithOutputs(const QJsonObject &outputs)
{
    return QJsonObject{{QStringLiteral("outputs"), outputs}};
}

QJsonObject historyRoot(const QString &promptId, const QJsonObject &entry)
{
    QJsonObject root;
    root.insert(promptId, entry);
    return root;
}

void waitForCallback(bool *done)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(5000);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start();
    while (!*done && timer.isActive())
        loop.processEvents();
    QVERIFY(*done);
}

} // namespace

class ComfyPromptClientTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testParseHistoryRunning();
    void testParseHistoryDone();
    void testParseHistoryExecutionError();
    void testParseHistoryNoImages();
    void testExtractOutputImages();
    void testExtractOutputImagesPrefersSaveImageNode();
    void testFetchHistoryMockServer();
    void testSubmitPromptMockServer();

private:
    ComfyMockHttpServer m_server;
};

void ComfyPromptClientTest::initTestCase()
{
    QVERIFY(m_server.listen());
}

void ComfyPromptClientTest::cleanupTestCase()
{
}

void ComfyPromptClientTest::testParseHistoryRunning()
{
    const QJsonObject entry = historyEntryWithOutputs(QJsonObject{});
    const QByteArray body = QJsonDocument(historyRoot(QStringLiteral("pid-1"), entry)).toJson();
    const ComfyPromptClient::HistoryFetchResult result =
        ComfyPromptClient::parseHistoryResponse(body, QStringLiteral("pid-1"), QNetworkReply::NoError, QString());
    QCOMPARE(result.state, ComfyPromptClient::HistoryState::Running);
    QVERIFY(result.images.isEmpty());
}

void ComfyPromptClientTest::testParseHistoryDone()
{
    const QJsonObject outputs =
        QJsonObject{{QStringLiteral("9"),
                     QJsonObject{{QStringLiteral("images"),
                                  QJsonArray{QJsonObject{{QStringLiteral("filename"), QStringLiteral("out.png")},
                                                          {QStringLiteral("subfolder"), QStringLiteral("sub")}}}}}}};
    const QByteArray body = QJsonDocument(historyRoot(QStringLiteral("pid-2"), historyEntryWithOutputs(outputs))).toJson();
    const ComfyPromptClient::HistoryFetchResult result =
        ComfyPromptClient::parseHistoryResponse(body, QStringLiteral("pid-2"), QNetworkReply::NoError, QString());
    QCOMPARE(result.state, ComfyPromptClient::HistoryState::Done);
    QCOMPARE(result.images.size(), 1);
    QCOMPARE(result.images.first().filename, QStringLiteral("out.png"));
    QCOMPARE(result.images.first().subfolder, QStringLiteral("sub"));
}

void ComfyPromptClientTest::testParseHistoryExecutionError()
{
    QJsonArray messages;
    messages.append(QJsonArray{QStringLiteral("execution_error"),
                               QJsonObject{{QStringLiteral("exception_message"), QStringLiteral("boom")}}});
    const QJsonObject entry =
        QJsonObject{{QStringLiteral("status"), QJsonObject{{QStringLiteral("messages"), messages}}}};
    const QByteArray body = QJsonDocument(historyRoot(QStringLiteral("pid-3"), entry)).toJson();
    const ComfyPromptClient::HistoryFetchResult result =
        ComfyPromptClient::parseHistoryResponse(body, QStringLiteral("pid-3"), QNetworkReply::NoError, QString());
    QCOMPARE(result.state, ComfyPromptClient::HistoryState::ExecutionError);
    QVERIFY(result.errorMessage.contains(QStringLiteral("boom")));
}

void ComfyPromptClientTest::testParseHistoryNoImages()
{
    const QJsonObject outputs = QJsonObject{{QStringLiteral("9"), QJsonObject{}}};
    const QByteArray body = QJsonDocument(historyRoot(QStringLiteral("pid-4"), historyEntryWithOutputs(outputs))).toJson();
    const ComfyPromptClient::HistoryFetchResult result =
        ComfyPromptClient::parseHistoryResponse(body, QStringLiteral("pid-4"), QNetworkReply::NoError, QString());
    QCOMPARE(result.state, ComfyPromptClient::HistoryState::NoImages);
}

void ComfyPromptClientTest::testExtractOutputImages()
{
    const QJsonObject entry = historyEntryWithOutputs(
        QJsonObject{{QStringLiteral("9"),
                     QJsonObject{{QStringLiteral("images"),
                                  QJsonArray{QJsonObject{{QStringLiteral("filename"), QStringLiteral("a.png")}}}}}}});
    const QList<ComfyPromptClient::OutputImage> images = ComfyPromptClient::extractOutputImages(entry);
    QCOMPARE(images.size(), 1);
    QCOMPARE(images.first().filename, QStringLiteral("a.png"));
}

void ComfyPromptClientTest::testExtractOutputImagesPrefersSaveImageNode()
{
    QJsonObject outputs;
    outputs.insert(QStringLiteral("501"),
                   QJsonObject{{QStringLiteral("images"),
                                QJsonArray{QJsonObject{{QStringLiteral("filename"), QStringLiteral("mask_preview.png")}}}}});
    outputs.insert(QStringLiteral("10"),
                   QJsonObject{{QStringLiteral("images"),
                                QJsonArray{QJsonObject{{QStringLiteral("filename"), QStringLiteral("result.png")},
                                                        {QStringLiteral("subfolder"), QStringLiteral("out")}}}}});
    const QJsonObject entry = historyEntryWithOutputs(outputs);
    const QList<ComfyPromptClient::OutputImage> images = ComfyPromptClient::extractOutputImages(entry);
    QCOMPARE(images.size(), 1);
    QCOMPARE(images.first().filename, QStringLiteral("result.png"));
    QCOMPARE(images.first().subfolder, QStringLiteral("out"));
}

void ComfyPromptClientTest::testFetchHistoryMockServer()
{
    QNetworkAccessManager nam;
    bool done = false;
    ComfyPromptClient::HistoryFetchResult fetched;
    ComfyPromptClient::fetchHistory(&nam, m_server.baseUrl(), QStringLiteral("mock-prompt-42"), this,
                                    [&done, &fetched](const ComfyPromptClient::HistoryFetchResult &result) {
                                        fetched = result;
                                        done = true;
                                    });
    waitForCallback(&done);
    QCOMPARE(fetched.state, ComfyPromptClient::HistoryState::Done);
    QCOMPARE(fetched.images.size(), 1);
    QCOMPARE(fetched.images.first().filename, QStringLiteral("out.png"));
}

void ComfyPromptClientTest::testSubmitPromptMockServer()
{
    QNetworkAccessManager nam;
    bool done = false;
    ComfyPromptClient::SubmitResult submitted;
    ComfyPromptClient::SubmitRequest req;
    req.workflow = QJsonObject{{QStringLiteral("1"), QJsonObject{}}};
    req.clientId = QStringLiteral("client-test");
    ComfyPromptClient::submitPrompt(&nam, m_server.baseUrl(), req, this,
                                    [&done, &submitted](const ComfyPromptClient::SubmitResult &result) {
                                        submitted = result;
                                        done = true;
                                    });
    waitForCallback(&done);
    QVERIFY(submitted.ok);
    QCOMPARE(submitted.promptId, QStringLiteral("mock-prompt-42"));
}

QTEST_MAIN(ComfyPromptClientTest)
#include "ComfyPromptClientTest.moc"
