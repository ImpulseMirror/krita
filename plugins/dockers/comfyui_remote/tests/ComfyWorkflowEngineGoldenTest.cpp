/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>

#include "ComfyWorkflowEngine.h"
#include "ComfyWorkflowNormalize.h"
#include "ComfyResources.h"

namespace {

QString goldenDataDir()
{
#ifdef COMFYUI_TEST_GOLDEN_DIR
    return QStringLiteral(COMFYUI_TEST_GOLDEN_DIR);
#else
    return QCoreApplication::applicationDirPath() + QStringLiteral("/data/golden");
#endif
}

QJsonObject loadGoldenApi(const QString &name)
{
    const QString path = goldenDataDir() + QLatin1Char('/') + name + QStringLiteral(".api.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QJsonObject();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return QJsonObject();
    return doc.object();
}

void assertWorkflowMatchesGolden(const QJsonObject &built, const QString &goldenName)
{
    const QJsonObject expected = loadGoldenApi(goldenName);
    QVERIFY2(!expected.isEmpty(), qPrintable(QStringLiteral("Missing golden fixture: ") + goldenName));
    const QString normBuilt =
        ComfyWorkflowNormalize::canonicalJson(ComfyWorkflowNormalize::normalizeApiWorkflow(built));
    const QString normExpected =
        ComfyWorkflowNormalize::canonicalJson(ComfyWorkflowNormalize::normalizeApiWorkflow(expected));
    QCOMPARE(normBuilt, normExpected);
}

QJsonObject buildGoldenTextToImage(const ComfyWorkflowEngine::TextToImageParams &p)
{
    ComfyWorkflowEngine::GenerateParams gp;
    static_cast<ComfyWorkflowEngine::TextToImageParams &>(gp) = p;
    return ComfyWorkflowEngine::buildGenerate(gp);
}

QJsonObject buildGoldenRefine(const ComfyWorkflowEngine::RefineParams &p)
{
    QJsonObject workflow = ComfyWorkflowEngine::buildRefine(p);
    ComfyWorkflowEngine::finishWorkflowWithSamplerCustom(
        &workflow, QStringLiteral("6"), p.arch, 512, 512, p.denoise);
    return workflow;
}

} // namespace

class ComfyWorkflowEngineGoldenTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testNormalizeStableAcrossNodeIds();
    void testGoldenSd15TextToImage();
    void testGoldenSd15Refine();
    void testGoldenSd15Inpaint();
    void testGoldenSd15UpscaleSimple();
    void testGoldenSd15UpscaleRefine();
    void testGoldenSdxlTextToImage();
    void testGoldenFluxTextToImage();
    void testWriteGoldenFixturesFromEngine();
};

void ComfyWorkflowEngineGoldenTest::testNormalizeStableAcrossNodeIds()
{
    QJsonObject a;
    a.insert(QStringLiteral("10"),
             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                         {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("image"), QStringLiteral("x.png")}}}});
    a.insert(QStringLiteral("3"),
             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                         {QStringLiteral("inputs"),
                          QJsonObject{{QStringLiteral("model"), QJsonArray{QStringLiteral("10"), 0}}}}});

    QJsonObject b;
    b.insert(QStringLiteral("1"),
             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                         {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("image"), QStringLiteral("x.png")}}}});
    b.insert(QStringLiteral("2"),
             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                         {QStringLiteral("inputs"),
                          QJsonObject{{QStringLiteral("model"), QJsonArray{QStringLiteral("1"), 0}}}}});

    QCOMPARE(ComfyWorkflowNormalize::canonicalJson(ComfyWorkflowNormalize::normalizeApiWorkflow(a)),
             ComfyWorkflowNormalize::canonicalJson(ComfyWorkflowNormalize::normalizeApiWorkflow(b)));
}

void ComfyWorkflowEngineGoldenTest::testGoldenSd15TextToImage()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.width = 512;
    p.height = 512;
    p.batchSize = 1;
    p.seed = 1234;
    p.steps = 20;
    p.cfg = 7.0;
    p.denoise = 1.0;
    p.sampler = QStringLiteral("euler");
    p.scheduler = QStringLiteral("normal");
    p.positivePrompt = QStringLiteral("golden positive");
    p.negativePrompt = QStringLiteral("golden negative");
    p.arch = ComfyResources::Arch::Sd15;
    assertWorkflowMatchesGolden(buildGoldenTextToImage(p), QStringLiteral("sd15_text2img"));
}

void ComfyWorkflowEngineGoldenTest::testGoldenSd15Refine()
{
    ComfyWorkflowEngine::RefineParams p;
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.imageName = QStringLiteral("golden_canvas.png");
    p.seed = 1234;
    p.steps = 20;
    p.cfg = 7.0;
    p.denoise = 0.4;
    p.sampler = QStringLiteral("euler");
    p.scheduler = QStringLiteral("normal");
    p.positivePrompt = QStringLiteral("golden positive");
    p.negativePrompt = QStringLiteral("golden negative");
    assertWorkflowMatchesGolden(buildGoldenRefine(p), QStringLiteral("sd15_refine"));
}

void ComfyWorkflowEngineGoldenTest::testGoldenSd15Inpaint()
{
    ComfyWorkflowEngine::InpaintBuildParams p;
    p.imageName = QStringLiteral("golden_canvas.png");
    p.maskImageName = QStringLiteral("golden_mask.png");
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.seed = 1234;
    p.steps = 20;
    p.cfg = 7.0;
    p.denoise = 0.6;
    p.sampler = QStringLiteral("euler");
    p.scheduler = QStringLiteral("normal");
    p.growMaskBy = 12;
    p.positivePrompt = QStringLiteral("golden positive");
    p.negativePrompt = QStringLiteral("golden negative");
    assertWorkflowMatchesGolden(ComfyWorkflowEngine::buildInpaint(p), QStringLiteral("sd15_inpaint"));
}

void ComfyWorkflowEngineGoldenTest::testGoldenSd15UpscaleSimple()
{
    ComfyWorkflowEngine::UpscaleSimpleParams p;
    p.imageName = QStringLiteral("golden_canvas.png");
    p.targetWidth = 1024;
    p.targetHeight = 768;
    p.upscaleMethod = QStringLiteral("lanczos");
    assertWorkflowMatchesGolden(ComfyWorkflowEngine::buildUpscaleSimple(p), QStringLiteral("sd15_upscale_simple"));
}

void ComfyWorkflowEngineGoldenTest::testGoldenSdxlTextToImage()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.checkpoint = QStringLiteral("zavychromaxl_v80.safetensors");
    p.width = 1024;
    p.height = 768;
    p.batchSize = 1;
    p.seed = 1234;
    p.steps = 20;
    p.cfg = 7.0;
    p.denoise = 1.0;
    p.sampler = QStringLiteral("euler");
    p.scheduler = QStringLiteral("normal");
    p.positivePrompt = QStringLiteral("golden positive");
    p.negativePrompt = QStringLiteral("golden negative");
    p.arch = ComfyResources::Arch::Sdxl;
    assertWorkflowMatchesGolden(buildGoldenTextToImage(p), QStringLiteral("sdxl_text2img"));
}

void ComfyWorkflowEngineGoldenTest::testGoldenFluxTextToImage()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.checkpoint = QStringLiteral("flux1-schnell.safetensors");
    p.width = 1024;
    p.height = 768;
    p.batchSize = 1;
    p.seed = 1234;
    p.steps = 4;
    p.cfg = 3.5;
    p.denoise = 1.0;
    p.sampler = QStringLiteral("euler");
    p.scheduler = QStringLiteral("normal");
    p.positivePrompt = QStringLiteral("golden positive");
    p.negativePrompt = QString();
    p.arch = ComfyResources::Arch::Flux;
    assertWorkflowMatchesGolden(buildGoldenTextToImage(p), QStringLiteral("flux_text2img"));
}

void ComfyWorkflowEngineGoldenTest::testGoldenSd15UpscaleRefine()
{
    ComfyWorkflowEngine::UpscaleRefineParams p;
    p.imageName = QStringLiteral("golden_canvas.png");
    p.scaleWidth = 1024;
    p.scaleHeight = 768;
    p.upscaleMethod = QStringLiteral("lanczos");
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.seed = 1234;
    p.steps = 8;
    p.cfg = 8.5;
    p.denoise = 0.3;
    p.sampler = QStringLiteral("euler");
    p.scheduler = QStringLiteral("normal");
    p.positivePrompt = QStringLiteral("golden positive");
    p.negativePrompt = QStringLiteral("golden negative");
    assertWorkflowMatchesGolden(ComfyWorkflowEngine::buildUpscaleRefine(p), QStringLiteral("sd15_upscale_refine"));
}

void ComfyWorkflowEngineGoldenTest::testWriteGoldenFixturesFromEngine()
{
    if (!qEnvironmentVariableIsSet("COMFY_WRITE_GOLDEN"))
        QSKIP("Set COMFY_WRITE_GOLDEN=1 to regenerate tests/data/golden/*.api.json from the engine");

    const QString outDir = goldenDataDir();
    auto write = [&](const QString &name, const QJsonObject &wf) {
        QFile f(outDir + QLatin1Char('/') + name + QStringLiteral(".api.json"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(wf).toJson(QJsonDocument::Indented));
    };

    {
        ComfyWorkflowEngine::TextToImageParams p;
        p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
        p.width = 512;
        p.height = 512;
        p.batchSize = 1;
        p.seed = 1234;
        p.steps = 20;
        p.cfg = 7.0;
        p.denoise = 1.0;
        p.sampler = QStringLiteral("euler");
        p.scheduler = QStringLiteral("normal");
        p.positivePrompt = QStringLiteral("golden positive");
        p.negativePrompt = QStringLiteral("golden negative");
        write(QStringLiteral("sd15_text2img"), buildGoldenTextToImage(p));
    }
    {
        ComfyWorkflowEngine::TextToImageParams p;
        p.checkpoint = QStringLiteral("zavychromaxl_v80.safetensors");
        p.width = 1024;
        p.height = 768;
        p.batchSize = 1;
        p.seed = 1234;
        p.steps = 20;
        p.cfg = 7.0;
        p.denoise = 1.0;
        p.sampler = QStringLiteral("euler");
        p.scheduler = QStringLiteral("normal");
        p.positivePrompt = QStringLiteral("golden positive");
        p.negativePrompt = QStringLiteral("golden negative");
        p.arch = ComfyResources::Arch::Sdxl;
        write(QStringLiteral("sdxl_text2img"), buildGoldenTextToImage(p));
    }
    {
        ComfyWorkflowEngine::TextToImageParams p;
        p.checkpoint = QStringLiteral("flux1-schnell.safetensors");
        p.width = 1024;
        p.height = 768;
        p.batchSize = 1;
        p.seed = 1234;
        p.steps = 4;
        p.cfg = 3.5;
        p.denoise = 1.0;
        p.sampler = QStringLiteral("euler");
        p.scheduler = QStringLiteral("normal");
        p.positivePrompt = QStringLiteral("golden positive");
        p.negativePrompt = QString();
        p.arch = ComfyResources::Arch::Flux;
        write(QStringLiteral("flux_text2img"), buildGoldenTextToImage(p));
    }
    {
        ComfyWorkflowEngine::RefineParams p;
        p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
        p.imageName = QStringLiteral("golden_canvas.png");
        p.seed = 1234;
        p.steps = 20;
        p.cfg = 7.0;
        p.denoise = 0.4;
        p.sampler = QStringLiteral("euler");
        p.scheduler = QStringLiteral("normal");
        p.positivePrompt = QStringLiteral("golden positive");
        p.negativePrompt = QStringLiteral("golden negative");
        write(QStringLiteral("sd15_refine"), buildGoldenRefine(p));
    }
    {
        ComfyWorkflowEngine::InpaintBuildParams p;
        p.imageName = QStringLiteral("golden_canvas.png");
        p.maskImageName = QStringLiteral("golden_mask.png");
        p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
        p.seed = 1234;
        p.steps = 20;
        p.cfg = 7.0;
        p.denoise = 0.6;
        p.sampler = QStringLiteral("euler");
        p.scheduler = QStringLiteral("normal");
        p.growMaskBy = 12;
        p.positivePrompt = QStringLiteral("golden positive");
        p.negativePrompt = QStringLiteral("golden negative");
        write(QStringLiteral("sd15_inpaint"), ComfyWorkflowEngine::buildInpaint(p));
    }
    {
        ComfyWorkflowEngine::UpscaleSimpleParams p;
        p.imageName = QStringLiteral("golden_canvas.png");
        p.targetWidth = 1024;
        p.targetHeight = 768;
        p.upscaleMethod = QStringLiteral("lanczos");
        write(QStringLiteral("sd15_upscale_simple"), ComfyWorkflowEngine::buildUpscaleSimple(p));
    }
    {
        ComfyWorkflowEngine::UpscaleRefineParams p;
        p.imageName = QStringLiteral("golden_canvas.png");
        p.scaleWidth = 1024;
        p.scaleHeight = 768;
        p.upscaleMethod = QStringLiteral("lanczos");
        p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
        p.seed = 1234;
        p.steps = 8;
        p.cfg = 8.5;
        p.denoise = 0.3;
        p.sampler = QStringLiteral("euler");
        p.scheduler = QStringLiteral("normal");
        p.positivePrompt = QStringLiteral("golden positive");
        p.negativePrompt = QStringLiteral("golden negative");
        write(QStringLiteral("sd15_upscale_refine"), ComfyWorkflowEngine::buildUpscaleRefine(p));
    }
}

SIMPLE_TEST_MAIN(ComfyWorkflowEngineGoldenTest)
#include "ComfyWorkflowEngineGoldenTest.moc"
