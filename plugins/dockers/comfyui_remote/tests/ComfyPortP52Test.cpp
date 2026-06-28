/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QTest>

#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QUrlQuery>

#include "ComfyFileLibrary.h"
#include "ComfyMockHttpServer.h"
#include "ComfyUIUtils.h"

namespace {

QByteArray httpGetSync(QNetworkAccessManager *nam,
                       const QUrl &url,
                       QNetworkReply::NetworkError *outError = nullptr,
                       int *outHttpCode = nullptr)
{
    QEventLoop loop;
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (outError)
        *outError = reply->error();
    if (outHttpCode)
        *outHttpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}

QByteArray httpPostSync(QNetworkAccessManager *nam, const QUrl &url, const QByteArray &jsonBody)
{
    QEventLoop loop;
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = nam->post(req, jsonBody);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}

} // namespace

class ComfyPortP52Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testComfyResolveApiUrlAndWebSocket();
    void testMockServerObjectInfoParsing();
    void testMockServerSystemStats();
    void testMockServerModelsLorasAndFileLibrary();
    void testMockServerEtnModelInfoFilter();
    void testMockServerLoraUploadPut();
    void testMockServerEtnTranslate();
    void testMockServerPromptAndHistory();

private:
    ComfyMockHttpServer m_server;
};

void ComfyPortP52Test::initTestCase()
{
    QVERIFY(m_server.listen());
    QVERIFY(m_server.port() > 0);
}

void ComfyPortP52Test::cleanupTestCase()
{
}

void ComfyPortP52Test::testComfyResolveApiUrlAndWebSocket()
{
    const QUrl api =
        ComfyUIUtils::comfyResolveApiUrl(QStringLiteral("http://127.0.0.1:8188"), QStringLiteral("api/etn/model_info/checkpoints"));
    QCOMPARE(api.path(), QStringLiteral("/api/etn/model_info/checkpoints"));

    const QUrl apiSub =
        ComfyUIUtils::comfyResolveApiUrl(QStringLiteral("http://127.0.0.1:8188/foo/"), QStringLiteral("system_stats"));
    QVERIFY(apiSub.path().endsWith(QStringLiteral("system_stats")));

    const QUrl ws = ComfyUIUtils::comfyWebSocketUrlForClient(QStringLiteral("http://127.0.0.1:8188"), QStringLiteral("client-abc"));
    QCOMPARE(ws.scheme(), QStringLiteral("ws"));
    QCOMPARE(ws.path(), QStringLiteral("/ws"));
    QCOMPARE(QUrlQuery(ws.query()).queryItemValue(QStringLiteral("clientId")), QStringLiteral("client-abc"));
}

void ComfyPortP52Test::testMockServerObjectInfoParsing()
{
    QNetworkAccessManager nam;
    const QUrl url(m_server.baseUrl() + QStringLiteral("/object_info"));
    QNetworkReply::NetworkError err = QNetworkReply::UnknownNetworkError;
    int httpCode = 0;
    const QByteArray body = httpGetSync(&nam, url, &err, &httpCode);
    QCOMPARE(err, QNetworkReply::NoError);
    QCOMPARE(httpCode, 200);

    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QStringList ckpts = ComfyUIUtils::parseCheckpointNamesFromObjectInfoRoot(root);
    QVERIFY(ckpts.contains(QStringLiteral("dreamshaper_8.safetensors")));

    QStringList loras;
    ComfyUIUtils::extractLoraFilenamesFromObjectInfo(root, &loras);
    QCOMPARE(loras, QStringList{QStringLiteral("hero.safetensors")});

    const QStringList vaes = ComfyUIUtils::vaeNamesFromObjectInfo(root);
    QVERIFY(vaes.contains(QStringLiteral("vae-ft-mse.safetensors")));

    const QStringList spec58 = ComfyUIUtils::specSection58NodesPresentInObjectInfo(root);
    QVERIFY(spec58.contains(QStringLiteral("CLIPTextEncode")));
}

void ComfyPortP52Test::testMockServerSystemStats()
{
    QNetworkAccessManager nam;
    const QByteArray body = httpGetSync(&nam, QUrl(m_server.baseUrl() + QStringLiteral("/system_stats")));
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QString line = ComfyUIUtils::formatComfySystemStatsDeviceLine(root);
    QVERIFY(line.contains(QStringLiteral("Mock GPU")));
}

void ComfyPortP52Test::testMockServerModelsLorasAndFileLibrary()
{
    QNetworkAccessManager nam;
    const QByteArray body = httpGetSync(&nam, QUrl(m_server.baseUrl() + QStringLiteral("/models/loras")));
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QVERIFY(doc.isArray());
    QCOMPARE(doc.array().size(), 2);

    QStringList names;
    for (const QJsonValue &v : doc.array()) {
        if (v.isString())
            names << v.toString();
    }
    QVERIFY(ComfyUIUtils::loraFilenameKnownOnServer(QStringLiteral("hero.safetensors"), names));
    QVERIFY(!ComfyUIUtils::loraFilenameKnownOnServer(QStringLiteral("missing.safetensors"), names));

#ifdef COMFYUI_ENABLE_TEST_HOOKS
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    ComfyUITestHooks::setPluginUserDataDirOverride(tmp.path());
    QDir().mkpath(tmp.path() + QStringLiteral("/database"));
    ComfyFileLibrary::instance().resetLorasDatabaseForTests(tmp.path() + QStringLiteral("/database/loras.json"));
#endif
    ComfyFileLibrary::instance().init();
    ComfyFileLibrary::instance().updateRemoteLoras(names);
    QCOMPARE(ComfyFileLibrary::instance().loras().files().size(), 2);
#ifdef COMFYUI_ENABLE_TEST_HOOKS
    ComfyUITestHooks::clearPluginUserDataDirOverride();
#endif
}

void ComfyPortP52Test::testMockServerEtnModelInfoFilter()
{
    QNetworkAccessManager nam;
    const QUrl url = ComfyUIUtils::comfyResolveApiUrl(m_server.baseUrl(),
                                                      QStringLiteral("api/etn/model_info/checkpoints?limit=2000"));
    const QByteArray body = httpGetSync(&nam, url);
    const QStringList fromObjectInfo = {QStringLiteral("dreamshaper_8.safetensors"), QStringLiteral("other.ckpt")};
    const QStringList filtered =
        ComfyUIUtils::filterCheckpointNamesWithEtnModelInfo(fromObjectInfo, QJsonDocument::fromJson(body));
    QVERIFY(filtered.contains(QStringLiteral("dreamshaper_8.safetensors")));
}

void ComfyPortP52Test::testMockServerLoraUploadPut()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString loraPath = tmp.path() + QStringLiteral("/hero.safetensors");
    QFile f(loraPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QByteArray payload("lora-payload-bytes");
    f.write(payload);
    f.close();

    QNetworkAccessManager nam;
    QNetworkReply *reply =
        ComfyUIUtils::tryUploadLoraFileViaEtnApi(&nam, m_server.baseUrl(), loraPath, nullptr);
    QVERIFY(reply);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QCOMPARE(reply->error(), QNetworkReply::NoError);
    reply->deleteLater();

    QVERIFY(m_server.lastPath().contains(QStringLiteral("/api/etn/upload/loras/")));
    QVERIFY(m_server.lastPath().contains(QStringLiteral("hero.safetensors")));
    QCOMPARE(m_server.lastMethod(), QStringLiteral("PUT"));
    QCOMPARE(m_server.lastBody(), payload);
}

void ComfyPortP52Test::testMockServerEtnTranslate()
{
    QNetworkAccessManager nam;
    bool done = false;
    bool ok = false;
    QString translated;
    ComfyUIUtils::requestEtnPromptTranslation(&nam,
                                              m_server.baseUrl(),
                                              QStringLiteral("en"),
                                              QStringLiteral("hello world"),
                                              this,
                                              [&](bool success, const QString &text) {
                                                  ok = success;
                                                  translated = text;
                                                  done = true;
                                              });
    QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
    QVERIFY(ok);
    QCOMPARE(translated, QStringLiteral("translated-mock"));
    QVERIFY(m_server.lastPath().contains(QStringLiteral("/api/etn/translate/en/")));
}

void ComfyPortP52Test::testMockServerPromptAndHistory()
{
    QNetworkAccessManager nam;
    const QByteArray promptBody = QByteArrayLiteral("{\"prompt\":{},\"client_id\":\"test\"}");
    const QByteArray promptResp =
        httpPostSync(&nam, QUrl(m_server.baseUrl() + QStringLiteral("/prompt")), promptBody);
    QCOMPARE(QJsonDocument::fromJson(promptResp).object().value(QStringLiteral("prompt_id")).toString(),
             QStringLiteral("mock-prompt-42"));
    QCOMPARE(m_server.lastMethod(), QStringLiteral("POST"));

    const QByteArray histBody =
        httpGetSync(&nam, QUrl(m_server.baseUrl() + QStringLiteral("/history/mock-prompt-42")));
    const QJsonObject hist = QJsonDocument::fromJson(histBody).object();
    QVERIFY(hist.contains(QStringLiteral("mock-prompt-42")));
}

SIMPLE_TEST_MAIN(ComfyPortP52Test)
#include "ComfyPortP52Test.moc"
