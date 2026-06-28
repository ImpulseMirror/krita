/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QTest>

#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>

#include "ComfyMockHttpServer.h"
#include "ComfyUploadPipeline.h"

class ComfyUploadPipelineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    void testLoraThenImageUploadOrder();
    void testEmptyBatchCompletesImmediately();

private:
    ComfyMockHttpServer m_server;
};

void ComfyUploadPipelineTest::initTestCase()
{
    QVERIFY(m_server.listen());
}

void ComfyUploadPipelineTest::cleanupTestCase()
{
}

void ComfyUploadPipelineTest::testEmptyBatchCompletesImmediately()
{
    QNetworkAccessManager nam;
    QObject owner;
    bool completed = false;
    auto *run = new ComfyUploadPipeline::Run(&nam, &owner);
    ComfyUploadPipeline::Handlers handlers;
    handlers.onComplete = [&](const ComfyUploadPipeline::Result &result) {
        completed = true;
        QVERIFY(result.uploadedImageNames.isEmpty());
    };
    handlers.onAbort = []() { QFAIL("unexpected abort"); };
    run->start(m_server.baseUrl(), {}, {}, std::move(handlers));
    QTRY_VERIFY_WITH_TIMEOUT(completed, 3000);
}

void ComfyUploadPipelineTest::testLoraThenImageUploadOrder()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString loraPath = tmp.path() + QStringLiteral("/pipeline.safetensors");
    {
        QFile f(loraPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("lora-bytes");
    }

    QNetworkAccessManager nam;
    QObject owner;
    bool completed = false;
    ComfyUploadPipeline::ImageItem img;
    img.filenameHint = QStringLiteral("step.png");
    img.prepareImage = []() {
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::white);
        return image;
    };

    auto *run = new ComfyUploadPipeline::Run(&nam, &owner);
    ComfyUploadPipeline::Handlers handlers;
    handlers.onComplete = [&](const ComfyUploadPipeline::Result &result) {
        completed = true;
        QCOMPARE(result.uploadedImageNames.size(), 1);
        QCOMPARE(result.uploadedImageNames.first(), QStringLiteral("mock-upload.png"));
    };
    handlers.onAbort = []() { QFAIL("unexpected abort"); };

    run->start(m_server.baseUrl(), {loraPath}, {img}, std::move(handlers));

    QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
    QCOMPARE(m_server.uploadImageHitCount(), 1);
}

SIMPLE_TEST_MAIN(ComfyUploadPipelineTest)
#include "ComfyUploadPipelineTest.moc"
