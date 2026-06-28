/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Live ComfyUI integration: generate "1girl" @ 1024² → refine_region on random 1/8 area.
 * Skips unless COMFY_INTEGRATION_TEST=1 (see scripts/run_comfy_integration_test.sh).
 */

#include <simpletest.h>
#include <QTest>

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <cmath>

#include "ComfyInpaintRunnerInternal.h"
#include "ComfyPromptClient.h"
#include "ComfyResources.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

namespace {

constexpr int kPollIntervalMs = 750;
constexpr int kGenerateTimeoutMs = 300000;
constexpr int kRefineTimeoutMs = 300000;
constexpr double kMinNonBlackFraction = 0.05;
constexpr int kGenerateSize = 1024;
const char kIntegrationPrompt[] = "1girl";

double imageNonBlackFraction(const QImage &image)
{
    if (image.isNull())
        return 0.0;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    int nonBlack = 0;
    const int total = rgb.width() * rgb.height();
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = rgb.pixel(x, y);
            if (qMax(qRed(px), qMax(qGreen(px), qBlue(px))) > 20)
                ++nonBlack;
        }
    }
    return total > 0 ? nonBlack / double(total) : 0.0;
}

QString integrationComfyUrl()
{
    return qEnvironmentVariable("COMFY_URL", QStringLiteral("http://127.0.0.1:8188")).trimmed();
}

bool integrationEnabled()
{
    return qEnvironmentVariableIntValue("COMFY_INTEGRATION_TEST") == 1;
}

QString integrationSaveDir()
{
    const QString fromEnv = qEnvironmentVariable("COMFY_INTEGRATION_SAVE_DIR").trimmed();
    if (!fromEnv.isEmpty())
        return fromEnv;
    const QFileInfo testFile(QString::fromUtf8(__FILE__));
    return testFile.absolutePath() + QStringLiteral("/output");
}

void cleanSaveDir(const QString &dir)
{
    QDir d(dir);
    if (!d.exists())
        return;
    const QFileInfoList entries = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        if (!QFile::remove(fi.absoluteFilePath()))
            qWarning("ComfyInpaintIntegrationTest: failed to remove %s", qPrintable(fi.absoluteFilePath()));
    }
    qWarning("ComfyInpaintIntegrationTest: cleaned %d file(s) from %s", entries.size(), qPrintable(dir));
}

void saveProofImage(const QString &dir, const QString &filename, const QImage &image)
{
    if (dir.isEmpty() || image.isNull())
        return;
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + filename;
    if (!image.save(path))
        qWarning("ComfyInpaintIntegrationTest: failed to save %s", qPrintable(path));
    else
        qWarning("ComfyInpaintIntegrationTest: saved %s", qPrintable(path));
}

QByteArray syncHttpGet(QNetworkAccessManager *nam, const QUrl &url, int timeoutMs, QString *errorOut)
{
    if (errorOut)
        errorOut->clear();
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = nam->get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(timeoutMs);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start();
    loop.exec();
    QByteArray body;
    if (timer.isActive()) {
        timer.stop();
        if (reply->error() != QNetworkReply::NoError) {
            if (errorOut)
                *errorOut = reply->errorString();
        } else {
            body = reply->readAll();
        }
    } else if (errorOut) {
        *errorOut = QStringLiteral("HTTP GET timeout");
    }
    reply->deleteLater();
    return body;
}

bool comfyServerReachable(QNetworkAccessManager *nam, const QString &baseUrl, QString *errorOut)
{
    const QUrl url = ComfyUIUtils::comfyResolveApiUrl(baseUrl, QStringLiteral("system_stats"));
    return !syncHttpGet(nam, url, 8000, errorOut).isEmpty();
}

QString pickCheckpoint(QNetworkAccessManager *nam, const QString &baseUrl)
{
    const QString fromEnv = qEnvironmentVariable("COMFY_CHECKPOINT").trimmed();
    if (!fromEnv.isEmpty())
        return fromEnv;

    const QUrl url = ComfyUIUtils::comfyResolveApiUrl(baseUrl, QStringLiteral("models/checkpoints"));
    QString err;
    const QByteArray body = syncHttpGet(nam, url, 10000, &err);
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array()) {
            const QString name = v.toString().trimmed();
            if (name.contains(QLatin1String("sdxl"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("XL"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("novaAnime"), Qt::CaseInsensitive)
                || name.contains(QLatin1String("noobai"), Qt::CaseInsensitive)) {
                return name;
            }
        }
        if (!doc.array().isEmpty())
            return doc.array().first().toString();
    }
    return QStringLiteral("v1-5-pruned-emaonly.safetensors");
}

ComfyResources::Arch archForCheckpoint(const QString &checkpoint)
{
    return ComfyResources::archFromKey(ComfyUIUtils::classifyCheckpointArch(checkpoint));
}

QString submitWorkflowSync(QNetworkAccessManager *nam,
                           const QString &baseUrl,
                           const QJsonObject &workflow,
                           QString *errorOut)
{
    if (errorOut)
        errorOut->clear();
    bool done = false;
    ComfyPromptClient::SubmitResult submit;
    ComfyPromptClient::SubmitRequest req;
    req.workflow = workflow;
    req.clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ComfyPromptClient::submitPrompt(nam, baseUrl, req, nam, [&](const ComfyPromptClient::SubmitResult &r) {
        submit = r;
        done = true;
    });
    QElapsedTimer wait;
    wait.start();
    while (!done && wait.elapsed() < 30000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!done) {
        if (errorOut)
            *errorOut = QStringLiteral("submitPrompt timed out");
        return QString();
    }
    if (!submit.ok) {
        if (errorOut)
            *errorOut = submit.errorMessage;
        return QString();
    }
    return submit.promptId;
}

ComfyPromptClient::HistoryFetchResult pollUntilDone(QNetworkAccessManager *nam,
                                                    const QString &baseUrl,
                                                    const QString &promptId,
                                                    int timeoutMs,
                                                    QString *errorOut)
{
    ComfyPromptClient::HistoryFetchResult last;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        bool done = false;
        ComfyPromptClient::fetchHistory(nam, baseUrl, promptId, nam, [&](const ComfyPromptClient::HistoryFetchResult &r) {
            last = r;
            done = true;
        });
        while (!done)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        if (last.state == ComfyPromptClient::HistoryState::Done)
            return last;
        if (last.state == ComfyPromptClient::HistoryState::ExecutionError
            || last.state == ComfyPromptClient::HistoryState::NetworkError
            || last.state == ComfyPromptClient::HistoryState::NoImages) {
            if (errorOut)
                *errorOut = last.errorMessage;
            return last;
        }
        QThread::msleep(kPollIntervalMs);
    }
    if (errorOut)
        *errorOut = QStringLiteral("history poll timed out");
    last.state = ComfyPromptClient::HistoryState::NetworkError;
    return last;
}

QImage downloadFirstOutputSync(QNetworkAccessManager *nam,
                               const QString &baseUrl,
                               const ComfyPromptClient::HistoryFetchResult &history,
                               QString *errorOut)
{
    if (history.images.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("no output images in history");
        return QImage();
    }
    bool done = false;
    QByteArray bytes;
    QString dlErr;
    ComfyPromptClient::downloadOutputImage(nam, baseUrl, history.images.first(), nam,
                                         [&](const QByteArray &data, const QString &err) {
                                             bytes = data;
                                             dlErr = err;
                                             done = true;
                                         });
    while (!done)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!dlErr.isEmpty()) {
        if (errorOut)
            *errorOut = dlErr;
        return QImage();
    }
    QImage img;
    if (!img.loadFromData(bytes)) {
        if (errorOut)
            *errorOut = QStringLiteral("failed to decode PNG from ComfyUI");
    }
    return img;
}

void dumpWorkflowOnFailure(const QJsonObject &workflow, const QString &label)
{
    const QString path = integrationSaveDir() + QLatin1Char('/') + label + QStringLiteral(".json");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(workflow).toJson(QJsonDocument::Indented));
    qWarning("ComfyInpaintIntegrationTest: wrote workflow to %s", qPrintable(path));
}

QImage scaleImageLikePrepare(const QImage &src, const QSize &target)
{
    if (src.size() == target)
        return src;
    return src.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/// Square selection with area = canvas.area() / 8, placed at random.
QRect randomEighthAreaSelection(const QSize &canvas, quint32 *placementSeedOut)
{
    const int w = canvas.width();
    const int h = canvas.height();
    const double targetArea = double(w) * double(h) / 8.0;
    int side = qMax(32, int(std::sqrt(targetArea) + 0.5));
    side = qMin(side, qMin(w, h));

    const quint32 placementSeed = QRandomGenerator::global()->generate();
    if (placementSeedOut)
        *placementSeedOut = placementSeed;
    QRandomGenerator rng(placementSeed);

    const int maxX = qMax(0, w - side);
    const int maxY = qMax(0, h - side);
    const int x = maxX > 0 ? int(rng.bounded(maxX + 1)) : 0;
    const int y = maxY > 0 ? int(rng.bounded(maxY + 1)) : 0;
    return QRect(x, y, side, side);
}

QImage maskFromSelection(const QSize &size, const QRect &selection)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    const QRect clip = selection.intersected(QRect(QPoint(0, 0), size));
    for (int y = clip.top(); y < clip.bottom(); ++y) {
        uchar *line = mask.scanLine(y);
        for (int x = clip.left(); x < clip.right(); ++x)
            line[x] = 255;
    }
    return mask;
}

struct GeneratedFixture {
    QImage fullCanvas;
    QString checkpoint;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    double cfg = 7.0;
    QString sampler = QStringLiteral("euler");
    int steps = 20;
    QString positivePrompt;
};

GeneratedFixture generateFixture(QNetworkAccessManager *nam,
                                 const QString &comfyUrl,
                                 const QString &checkpoint,
                                 ComfyResources::Arch arch,
                                 int size,
                                 const QString &prompt)
{
    GeneratedFixture fx;
    fx.checkpoint = checkpoint;
    fx.arch = arch;
    fx.positivePrompt = prompt;
    ComfyWorkflowEngine::TextToImageParams gen;
    gen.checkpoint = checkpoint;
    gen.arch = arch;
    gen.width = size;
    gen.height = size;
    gen.positivePrompt = prompt;
    gen.negativePrompt = QStringLiteral("blurry, low quality");
    gen.seed = 424242;
    gen.steps = 20;
    gen.cfg = ComfyResources::isSdxlLike(arch) ? 7.0 : 8.0;
    fx.cfg = gen.cfg;
    fx.steps = gen.steps;
    fx.sampler = gen.sampler;

    const QJsonObject genWorkflow = ComfyWorkflowEngine::buildTextToImage(gen);
    QString submitErr;
    const QString genPromptId = submitWorkflowSync(nam, comfyUrl, genWorkflow, &submitErr);
    Q_ASSERT(!genPromptId.isEmpty());
    QString pollErr;
    const ComfyPromptClient::HistoryFetchResult genHistory =
        pollUntilDone(nam, comfyUrl, genPromptId, kGenerateTimeoutMs, &pollErr);
    Q_ASSERT(genHistory.state == ComfyPromptClient::HistoryState::Done);
    fx.fullCanvas = downloadFirstOutputSync(nam, comfyUrl, genHistory, &pollErr);
    Q_ASSERT(!fx.fullCanvas.isNull());
    return fx;
}

} // namespace

class ComfyInpaintIntegrationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void testGenerateThenRefineRandomEighthNotBlack();

private:
    QString m_comfyUrl;
    QString m_checkpoint;
    ComfyResources::Arch m_arch = ComfyResources::Arch::Sd15;
    QNetworkAccessManager m_nam;
    QString m_saveDir;
};

void ComfyInpaintIntegrationTest::initTestCase()
{
    if (!integrationEnabled())
        QSKIP("Set COMFY_INTEGRATION_TEST=1 to run live ComfyUI integration tests "
              "(see plugins/dockers/comfyui_remote/scripts/run_comfy_integration_test.sh).");

    m_comfyUrl = integrationComfyUrl();
    QString err;
    if (!comfyServerReachable(&m_nam, m_comfyUrl, &err))
        QSKIP(qPrintable(QStringLiteral("ComfyUI not reachable at %1: %2").arg(m_comfyUrl, err)));

    m_checkpoint = pickCheckpoint(&m_nam, m_comfyUrl);
    m_arch = archForCheckpoint(m_checkpoint);
    m_saveDir = integrationSaveDir();
    cleanSaveDir(m_saveDir);

    qWarning("ComfyInpaintIntegrationTest: COMFY_URL=%s checkpoint=%s arch=%d saveDir=%s",
             qPrintable(m_comfyUrl),
             qPrintable(m_checkpoint),
             int(m_arch),
             qPrintable(m_saveDir));
}

void ComfyInpaintIntegrationTest::testGenerateThenRefineRandomEighthNotBlack()
{
    const GeneratedFixture fx = generateFixture(&m_nam,
                                                m_comfyUrl,
                                                m_checkpoint,
                                                m_arch,
                                                kGenerateSize,
                                                QString::fromLatin1(kIntegrationPrompt));
    saveProofImage(m_saveDir, QStringLiteral("01_generate.png"), fx.fullCanvas);

    const double genNonBlack = imageNonBlackFraction(fx.fullCanvas);
    qWarning("generate %s nonBlack=%.3f",
             qPrintable(ComfyInpaintRunnerInternal::describeImagePixels(fx.fullCanvas, QStringLiteral("generate"))),
             genNonBlack);
    QVERIFY2(genNonBlack >= kMinNonBlackFraction,
             qPrintable(QStringLiteral("generate output nearly all black (nonBlack=%1)").arg(genNonBlack)));

    const double strength0to1 = 0.67;
    quint32 placementSeed = 0;
    const QRect selectionOriginal = randomEighthAreaSelection(fx.fullCanvas.size(), &placementSeed);
    QVERIFY(selectionOriginal.isValid());

    const ComfyUIUtils::SelectionModifiers mods =
        ComfyUIUtils::getSelectionModifiers(ComfyResources::archToKey(fx.arch), QStringLiteral("fill"), strength0to1);
    const ComfyUIUtils::SelectionPreProcess preprocess =
        ComfyUIUtils::calcSelectionPreProcessFromModifiers(selectionOriginal, fx.fullCanvas.width(),
                                                          fx.fullCanvas.height(), mods);

    int padPx = preprocess.grow + preprocess.feather / 2 + 8;
    padPx = qMax(padPx, 24);
    QRect maskPaddedBounds = selectionOriginal.adjusted(-padPx, -padPx, padPx, padPx);
    maskPaddedBounds = maskPaddedBounds.intersected(QRect(QPoint(0, 0), fx.fullCanvas.size()));
    QVERIFY(!maskPaddedBounds.isEmpty());

    const QRect contextBounds = maskPaddedBounds;
    const QImage nativeContext = fx.fullCanvas.copy(contextBounds).convertToFormat(QImage::Format_ARGB32);
    const QRect selectionInContext = selectionOriginal.translated(-contextBounds.topLeft());
    const QImage nativeMask = maskFromSelection(nativeContext.size(), selectionInContext);

    saveProofImage(m_saveDir, QStringLiteral("02_mask.png"), nativeMask);

    qWarning("selection=%dx%d@%d,%d (%.1f%% of canvas) placementSeed=%u grow=%d feather=%d context=%dx%d",
             selectionOriginal.width(),
             selectionOriginal.height(),
             selectionOriginal.x(),
             selectionOriginal.y(),
             100.0 * double(selectionOriginal.width() * selectionOriginal.height())
                 / double(fx.fullCanvas.width() * fx.fullCanvas.height()),
             placementSeed,
             preprocess.grow,
             preprocess.feather,
             nativeContext.width(),
             nativeContext.height());

    const ComfyUIUtils::DiffusionPreparedExtent diffPrep =
        ComfyUIUtils::prepareDiffusionInputExtent(nativeContext.size(), fx.arch);
    QVERIFY(diffPrep.initial.isValid());

    QImage contextImage = scaleImageLikePrepare(nativeContext, diffPrep.initial);
    QImage compositingMask = scaleImageLikePrepare(nativeMask, diffPrep.initial);
    const QRect targetBoundsRelative = maskPaddedBounds.translated(-contextBounds.topLeft());

    QString uploadErr;
    const QString imageName =
        ComfyUIUtils::uploadImageToComfySync(&m_nam, m_comfyUrl, contextImage, QStringLiteral("integration_ctx.png"),
                                             &uploadErr);
    QVERIFY2(!imageName.isEmpty(), qPrintable(QStringLiteral("context upload failed: ") + uploadErr));

    const QString maskName =
        ComfyUIUtils::uploadImageToComfySync(&m_nam, m_comfyUrl, compositingMask, QStringLiteral("integration_mask.png"),
                                             &uploadErr);
    QVERIFY2(!maskName.isEmpty(), qPrintable(QStringLiteral("mask upload failed: ") + uploadErr));

    ComfyWorkflowEngine::RefineRegionParams rrp;
    rrp.refine.imageName = imageName;
    rrp.maskImageName = maskName;
    rrp.refine.checkpoint = fx.checkpoint;
    rrp.refine.positivePrompt = fx.positivePrompt;
    rrp.refine.negativePrompt = QStringLiteral("blurry, low quality");
    rrp.refine.seed = 424243;
    rrp.refine.steps = fx.steps;
    rrp.refine.cfg = fx.cfg;
    rrp.refine.denoise = strength0to1;
    rrp.refine.arch = fx.arch;
    ComfyUIUtils::applyStrengthResolvedSamplingToRefine(&rrp.refine, nullptr, QJsonObject(), fx.sampler, fx.steps, fx.cfg,
                                                        strength0to1);
    rrp.growMaskBy = preprocess.grow;
    rrp.featherMaskBy = preprocess.feather;
    rrp.blendMaskBy = preprocess.blend;
    rrp.targetBoundsRelative = targetBoundsRelative;
    rrp.nativeTargetBoundsRelative = targetBoundsRelative;
    rrp.contextExtentWidth = nativeContext.width();
    rrp.contextExtentHeight = nativeContext.height();
    rrp.extentWidth = diffPrep.initial.width();
    rrp.extentHeight = diffPrep.initial.height();
    rrp.useInpaintModel = ComfyUIUtils::detectInpaintParams(QStringLiteral("fill"), ComfyResources::archToKey(fx.arch),
                                                            strength0to1, false, false, false)
                              .useInpaintModel;

    const QJsonObject refineWorkflow = ComfyWorkflowEngine::buildRefineRegion(rrp);
    QVERIFY2(!refineWorkflow.isEmpty(), "buildRefineRegion returned empty workflow");

    QString submitErr;
    const QString refinePromptId = submitWorkflowSync(&m_nam, m_comfyUrl, refineWorkflow, &submitErr);
    QVERIFY2(!refinePromptId.isEmpty(), qPrintable(QStringLiteral("refine submit failed: ") + submitErr));

    QString pollErr;
    const ComfyPromptClient::HistoryFetchResult refineHistory =
        pollUntilDone(&m_nam, m_comfyUrl, refinePromptId, kRefineTimeoutMs, &pollErr);
    if (refineHistory.state != ComfyPromptClient::HistoryState::Done) {
        dumpWorkflowOnFailure(refineWorkflow, QStringLiteral("refine_fail"));
        QFAIL(qPrintable(QStringLiteral("refine history failed state=%1 err=%2")
                             .arg(int(refineHistory.state))
                             .arg(pollErr)));
    }

    QImage rawRefine = downloadFirstOutputSync(&m_nam, m_comfyUrl, refineHistory, &pollErr);
    QVERIFY2(!rawRefine.isNull(), qPrintable(QStringLiteral("refine download failed: ") + pollErr));
    saveProofImage(m_saveDir, QStringLiteral("03_refine_raw.png"), rawRefine);

    const double rawNonBlack = imageNonBlackFraction(rawRefine);
    qWarning("refine rawDownload %s nonBlack=%.3f",
             qPrintable(ComfyInpaintRunnerInternal::describeImagePixels(rawRefine, QStringLiteral("rawDownload"))),
             rawNonBlack);

    if (rawNonBlack < kMinNonBlackFraction) {
        dumpWorkflowOnFailure(refineWorkflow, QStringLiteral("refine_black"));
        QFAIL(qPrintable(QStringLiteral("refine_region server PNG all black (nonBlack=%1)").arg(rawNonBlack)));
    }

    ComfyInpaintRunnerInternal::InpaintCompositeParams compositeParams;
    compositeParams.serverResult = rawRefine;
    compositeParams.contextImage = nativeContext;
    compositeParams.compositingMask = nativeMask;
    compositeParams.contextBounds = contextBounds;
    compositeParams.targetBounds = maskPaddedBounds;
    compositeParams.preprocessGrow = preprocess.grow;
    compositeParams.preprocessFeather = preprocess.feather;
    compositeParams.preprocessBlend = preprocess.blend;
    compositeParams.diffusionExtent = diffPrep.initial;
    compositeParams.refineRegionWorkflow = true;
    compositeParams.serverPreMasked = false;

    const ComfyInpaintRunnerInternal::InpaintCompositeResult composite =
        ComfyInpaintRunnerInternal::compositeInpaintServerOntoContext(compositeParams);
    saveProofImage(m_saveDir, QStringLiteral("04_refine_composite.png"), composite.output);

    const double outNonBlack = imageNonBlackFraction(composite.output);
    qWarning("composite path=%s nonBlack=%.3f saveDir=%s",
             qPrintable(composite.pathTaken),
             outNonBlack,
             qPrintable(m_saveDir));
    QVERIFY2(outNonBlack >= kMinNonBlackFraction,
             qPrintable(QStringLiteral("composite output nearly all black (nonBlack=%1 path=%2)")
                            .arg(outNonBlack)
                            .arg(composite.pathTaken)));

    QFile readme(m_saveDir + QStringLiteral("/README.txt"));
    if (readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
        readme.write(QStringLiteral("prompt=1girl\nsize=1024x1024\nselection=%1,%2 %3x%4 (1/8 area)\n"
                                    "placementSeed=%5\nstrength=0.67\nrawNonBlack=%6\ncompositeNonBlack=%7\n")
                         .arg(selectionOriginal.x())
                         .arg(selectionOriginal.y())
                         .arg(selectionOriginal.width())
                         .arg(selectionOriginal.height())
                         .arg(placementSeed)
                         .arg(rawNonBlack, 0, 'f', 3)
                         .arg(outNonBlack, 0, 'f', 3)
                         .toUtf8());
    }
}

QTEST_MAIN(ComfyInpaintIntegrationTest)
#include "ComfyInpaintIntegrationTest.moc"
