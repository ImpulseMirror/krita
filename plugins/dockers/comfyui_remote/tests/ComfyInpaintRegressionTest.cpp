/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Offline regression tests for Android refine inpaint (partial mask assembly),
 * composite merge, and INPAINT_DIAG verdict helpers. No ComfyUI server required.
 */

#include <simpletest.h>
#include <QTest>

#include <QPainter>
#include <QRandomGenerator>

#include <cmath>

#include "ComfyInpaintRunnerInternal.h"
#include "ComfyLiveRunnerInternal.h"
#include "ComfyPrepareWorkflow.h"
#include "ComfyResources.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

using namespace ComfyUIUtils;
using namespace ComfyInpaintRunnerInternal;

namespace {

QImage makeSolidRectMask(const QSize &size, const QRect &whiteRectInImage)
{
    QImage mask(size.width(), size.height(), QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter painter(&mask);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(whiteRectInImage, QColor(255, 255, 255));
    return mask;
}

QImage makeFilledRgbImage(const QSize &size, QRgb color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

QImage makeEllipseMask(const QSize &size)
{
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRect(QPoint(0, 0), size));
    return mask;
}

/// Simulates Android convertToQImage on selectedExactRect — entire bbox opaque white.
QImage simulateAndroidConvertToQImageSolidRect(const QSize &size)
{
    QImage wrong(size, QImage::Format_Grayscale8);
    wrong.fill(255);
    return wrong;
}

/// Square selection with area = canvas.area() / 8 (same formula as integration test).
QRect eighthAreaSelectionAtSeed(const QSize &canvas, quint32 placementSeed)
{
    const int w = canvas.width();
    const int h = canvas.height();
    const double targetArea = double(w) * double(h) / 8.0;
    int side = qMax(32, int(std::sqrt(targetArea) + 0.5));
    side = qMin(side, qMin(w, h));

    QRandomGenerator rng(placementSeed);
    const int maxX = qMax(0, w - side);
    const int maxY = qMax(0, h - side);
    const int x = maxX > 0 ? int(rng.bounded(maxX + 1)) : 0;
    const int y = maxY > 0 ? int(rng.bounded(maxY + 1)) : 0;
    return QRect(x, y, side, side);
}

QString findNodeIdByClassType(const QJsonObject &workflow, const QString &classType)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == classType)
            return it.key();
    }
    return QString();
}

bool workflowContainsClassType(const QJsonObject &workflow, const QString &classType)
{
    return !findNodeIdByClassType(workflow, classType).isEmpty();
}

/// Walk SaveImage ← image inputs; fail if INPAINT_ColorMatch appears in chain.
bool refineRegionSaveChainAvoidsColorMatch(const QJsonObject &workflow)
{
    const QString saveId = findNodeIdByClassType(workflow, QStringLiteral("SaveImage"));
    if (saveId.isEmpty())
        return false;

    QString nodeId =
        workflow.value(saveId).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray().at(0).toString();
    for (int hop = 0; hop < 8 && !nodeId.isEmpty(); ++hop) {
        const QJsonObject node = workflow.value(nodeId).toObject();
        const QString cls = node.value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("INPAINT_ColorMatch"))
            return false;
        if (cls == QLatin1String("VAEDecode"))
            return true;

        const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        if (inputs.contains(QStringLiteral("image"))) {
            const QJsonArray link = inputs.value(QStringLiteral("image")).toArray();
            if (link.isEmpty())
                break;
            nodeId = link.at(0).toString();
            continue;
        }
        if (inputs.contains(QStringLiteral("target"))) {
            nodeId = inputs.value(QStringLiteral("target")).toArray().at(0).toString();
            continue;
        }
        break;
    }
    return false;
}

ComfyWorkflowEngine::RefineRegionParams makeAndroidRefineRegionFixture()
{
    ComfyWorkflowEngine::RefineRegionParams rrp;
    rrp.refine.imageName = QStringLiteral("krita_live.png");
    rrp.maskImageName = QStringLiteral("krita_live_mask.png");
    rrp.refine.checkpoint = QStringLiteral("novaAnimeXL_ilV140.safetensors");
    rrp.refine.arch = ComfyResources::Arch::Sdxl;
    rrp.refine.positivePrompt = QStringLiteral("best quality, highres, eye patch");
    rrp.refine.negativePrompt = QStringLiteral("bad quality, low resolution, blurry");
    rrp.refine.seed = 1434105552;
    rrp.refine.steps = 20;
    rrp.refine.cfg = 8.0;
    rrp.refine.denoise = 0.75;
    rrp.refine.sampler = QStringLiteral("euler");
    rrp.refine.scheduler = QStringLiteral("normal");
    rrp.growMaskBy = 10;
    rrp.featherMaskBy = 20;
    rrp.blendMaskBy = 20;
    rrp.extentWidth = 800;
    rrp.extentHeight = 800;
    rrp.contextExtentWidth = 800;
    rrp.contextExtentHeight = 800;
    rrp.colorMatch = true;
    rrp.nsfwFilterSensitivity = 0.0;
    ComfyUIUtils::applyStrengthResolvedSamplingToRefine(
        &rrp.refine, nullptr, QJsonObject(), rrp.refine.sampler, rrp.refine.steps, rrp.refine.cfg, 0.75);
    return rrp;
}

/// Simulates Android convertToQImage reading padded bounds — entire crop opaque white.
QImage simulateAndroidPaddedBoundsReadBug(const QRect &paddedBounds)
{
    QImage wrong(paddedBounds.width(), paddedBounds.height(), QImage::Format_Grayscale8);
    wrong.fill(255);
    return wrong;
}

} // namespace

class ComfyInpaintRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testAssembleSelectionMaskEighthArea();
    void testAssembleSelectionMaskAndroidLogcatScenario();
    void testFullWhitePaddedMaskIsRegression();
    void testScaledMaskRemainsPartialAfterDiffusionExtent();
    void testCompositeRefinePartialMaskMergesServerPatch();
    void testInpaintFailureVerdictHelpers();
    void testShapedMaskFillFractionBelowSolidRectangle();
    void testChooseSelectionMaskPrefersBytesOverSolidConvert();
    void testChooseSelectionMaskFallsBackToConvertWhenBytesEmpty();
    void testOvalMaskAndroidLogcatAssemblyScenario();
    void testBuildRefineRegionSkipsServerColorMatch();
    void testBuildRefineRegionSaveImageReachesVaeDecode();
    void testBuildInpaintFillStillUsesServerColorMatch();
    void testCompositeRefineBlackServerPaintsMaskRegionBlack();
    void testCompositeLivePreviewMaskClipKeepsCrosshatchOutside();
    void testCompositeLivePreviewDiagCrossTintsOutside();
    void testCompositeLiveServerUsesFeatherBlendMask();
    void testCompositeLiveServerPreservesContextOutsideMask();
};

void ComfyInpaintRegressionTest::testAssembleSelectionMaskEighthArea()
{
    const QSize doc(1024, 1024);
    const QRect selection = eighthAreaSelectionAtSeed(doc, 424242u);
    QVERIFY(selection.isValid());

    const SelectionModifiers mods = getSelectionModifiers(QStringLiteral("sdxl"), QStringLiteral("fill"), 0.67);
    const SelectionPreProcess preprocess =
        calcSelectionPreProcessFromModifiers(selection, doc.width(), doc.height(), mods);
    int padPx = preprocess.grow + preprocess.feather / 2 + 8;
    padPx = qMax(padPx, 24);
    QRect padded = selection.adjusted(-padPx, -padPx, padPx, padPx);
    padded = padded.intersected(QRect(QPoint(0, 0), doc));
    QVERIFY(padded.width() > selection.width());
    QVERIFY(padded.height() > selection.height());

    const QImage coreMask = makeSolidRectMask(selection.size(), QRect(0, 0, selection.width(), selection.height()));
    const QImage assembled = assembleSelectionMaskInPaddedBounds(coreMask, selection, padded);
    QVERIFY(!assembled.isNull());
    QCOMPARE(assembled.size(), padded.size());

    const double nonWhite = maskNonWhiteFraction(assembled);
    const double selectionFraction =
        double(selection.width() * selection.height()) / double(padded.width() * padded.height());
    QVERIFY2(nonWhite < 0.95, qPrintable(QStringLiteral("mask should not be full white: nonWhite=%1").arg(nonWhite)));
    QVERIFY2(nonWhite > 0.05, qPrintable(QStringLiteral("mask should cover selection: nonWhite=%1").arg(nonWhite)));
    QVERIFY2(qAbs(nonWhite - selectionFraction) < 0.15,
             qPrintable(QStringLiteral("nonWhite=%1 expected ~%2").arg(nonWhite).arg(selectionFraction)));
}

void ComfyInpaintRegressionTest::testAssembleSelectionMaskAndroidLogcatScenario()
{
    // Captured from Android INPAINT_DIAG: maskPx nonBlack=1.000 before fix.
    const QRect selectionOriginal(457, 190, 320, 348);
    const QRect maskPaddedBounds(393, 124, 448, 480);

    const QImage coreMask =
        makeSolidRectMask(selectionOriginal.size(), QRect(0, 0, selectionOriginal.width(), selectionOriginal.height()));
    const QImage assembled = assembleSelectionMaskInPaddedBounds(coreMask, selectionOriginal, maskPaddedBounds);
    QVERIFY(!assembled.isNull());

    const double nonWhite = maskNonWhiteFraction(assembled);
    const double expected =
        double(selectionOriginal.width() * selectionOriginal.height())
        / double(maskPaddedBounds.width() * maskPaddedBounds.height());
    QVERIFY2(nonWhite < 0.75,
             qPrintable(QStringLiteral("Android bug was nonWhite=1.0; got %1").arg(nonWhite)));
    QVERIFY2(qAbs(nonWhite - expected) < 0.05,
             qPrintable(QStringLiteral("nonWhite=%1 expected ~%2").arg(nonWhite).arg(expected)));
}

void ComfyInpaintRegressionTest::testFullWhitePaddedMaskIsRegression()
{
    const QRect selectionOriginal(457, 190, 320, 348);
    const QRect maskPaddedBounds(393, 124, 448, 480);

    const QImage wrongMask = simulateAndroidPaddedBoundsReadBug(maskPaddedBounds);
    QCOMPARE(maskNonWhiteFraction(wrongMask), 1.0);

    const QImage coreMask =
        makeSolidRectMask(selectionOriginal.size(), QRect(0, 0, selectionOriginal.width(), selectionOriginal.height()));
    const QImage correct = assembleSelectionMaskInPaddedBounds(coreMask, selectionOriginal, maskPaddedBounds);
    QVERIFY(maskNonWhiteFraction(correct) < maskNonWhiteFraction(wrongMask) - 0.2);
}

void ComfyInpaintRegressionTest::testScaledMaskRemainsPartialAfterDiffusionExtent()
{
    const QSize nativeContext(448, 480);
    const QRect selectionInContext(64, 66, 320, 348);
    QImage nativeMask = makeSolidRectMask(nativeContext, selectionInContext);

    const DiffusionPreparedExtent prepared =
        prepareDiffusionInputExtent(nativeContext, ComfyResources::Arch::Sdxl);
    QVERIFY(prepared.initial.isValid());

    QImage scaled = nativeMask.scaled(prepared.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const double nonWhite = maskNonWhiteFraction(scaled);
    QVERIFY2(nonWhite > 0.05 && nonWhite < 0.95,
             qPrintable(QStringLiteral("scaled mask nonWhite=%1").arg(nonWhite)));
}

void ComfyInpaintRegressionTest::testCompositeRefinePartialMaskMergesServerPatch()
{
    const QSize contextSize(512, 512);
    const QRect targetBounds(192, 192, 128, 128);
    const QRect contextBounds(0, 0, contextSize.width(), contextSize.height());

    QImage context = makeFilledRgbImage(contextSize, qRgb(40, 40, 40));
    QImage server = makeFilledRgbImage(contextSize, qRgb(200, 50, 50));
    QImage mask = makeSolidRectMask(contextSize, targetBounds);

    InpaintCompositeParams params;
    params.serverResult = server;
    params.contextImage = context;
    params.compositingMask = mask;
    params.contextBounds = contextBounds;
    params.targetBounds = targetBounds;
    params.refineRegionWorkflow = true;
    params.diffusionExtent = contextSize;

    const InpaintCompositeResult result = compositeInpaintServerOntoContext(params);
    QVERIFY(!result.output.isNull());
    QCOMPARE(result.pathTaken, QStringLiteral("full_context"));

    const double nonBlack = imageNonBlackFraction(result.output);
    QVERIFY2(nonBlack > 0.05,
             qPrintable(QStringLiteral("composite output should retain server pixels: nonBlack=%1").arg(nonBlack)));

    const QRgb center = result.output.pixel(targetBounds.center());
    QVERIFY(qRed(center) > 100);
}

void ComfyInpaintRegressionTest::testInpaintFailureVerdictHelpers()
{
    QCOMPARE(inpaintFailureVerdict(0.0, 0.0, QStringLiteral("inpaint_conditioning"), QStringLiteral("sdxl"), 0.67,
                                   true),
             QStringLiteral("server_black"));
    QCOMPARE(inpaintFailureVerdict(0.0, 0.0, QStringLiteral("vae_encode_noise_mask"), QStringLiteral("sdxl"), 0.67,
                                   true),
             QStringLiteral("server_black_sdxl_old_latent_path"));
    QCOMPARE(inpaintFailureVerdict(0.5, 0.0, QStringLiteral("inpaint_conditioning"), QStringLiteral("sdxl"), 0.67,
                                   true),
             QStringLiteral("composite_black_raw_ok"));
    QCOMPARE(inpaintFailureVerdict(0.5, 0.5, QStringLiteral("inpaint_conditioning"), QStringLiteral("sdxl"), 0.67,
                                   true),
             QStringLiteral("ok"));
}

void ComfyInpaintRegressionTest::testShapedMaskFillFractionBelowSolidRectangle()
{
    const QSize size(467, 193);
    const QImage solid = makeSolidRectMask(size, QRect(QPoint(0, 0), size));
    QCOMPARE(maskNonWhiteFraction(solid), 1.0);

    QImage ellipse(size, QImage::Format_Grayscale8);
    ellipse.fill(0);
    QPainter painter(&ellipse);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRect(QPoint(0, 0), size));
    painter.end();

    const double shapedFill = maskNonWhiteFraction(ellipse);
    QVERIFY2(shapedFill < 0.90,
             qPrintable(QStringLiteral("oval fill=%1 should be below solid-rect threshold").arg(shapedFill)));
    QVERIFY2(shapedFill > 0.50,
             qPrintable(QStringLiteral("oval fill=%1 should still cover interior").arg(shapedFill)));
}

void ComfyInpaintRegressionTest::testChooseSelectionMaskPrefersBytesOverSolidConvert()
{
    const QSize size(467, 193);
    const QImage solidConvert = simulateAndroidConvertToQImageSolidRect(size);
    const QImage bytesEllipse = makeEllipseMask(size);
    QCOMPARE(maskNonWhiteFraction(solidConvert), 1.0);

    const SelectionMaskReadResult chosen = chooseSelectionMaskRead(solidConvert, bytesEllipse);
    QCOMPARE(chosen.source, SelectionMaskReadSource::ReadBytesOverSolidConvert);
    QVERIFY2(maskNonWhiteFraction(chosen.mask) < 0.90,
             qPrintable(QStringLiteral("chosen mask fill=%1").arg(maskNonWhiteFraction(chosen.mask))));
    QVERIFY(maskNonWhiteFraction(chosen.mask) > 0.50);
}

void ComfyInpaintRegressionTest::testChooseSelectionMaskFallsBackToConvertWhenBytesEmpty()
{
    const QSize size(467, 193);
    const QImage solidConvert = simulateAndroidConvertToQImageSolidRect(size);
    QImage emptyBytes(size, QImage::Format_Grayscale8);
    emptyBytes.fill(0);

    const SelectionMaskReadResult chosen = chooseSelectionMaskRead(solidConvert, emptyBytes);
    QCOMPARE(chosen.source, SelectionMaskReadSource::ConvertToQImageFallback);
    QCOMPARE(maskNonWhiteFraction(chosen.mask), 1.0);
}

void ComfyInpaintRegressionTest::testOvalMaskAndroidLogcatAssemblyScenario()
{
    // Android logcat 116043bf: convertToQImage returned solid 467×193; readBytes had oval shape.
    const QRect selectionOriginal(281, 415, 467, 193);
    const QRect maskPaddedBounds(195, 328, 640, 368);

    const QImage solidConvert = simulateAndroidConvertToQImageSolidRect(selectionOriginal.size());
    const QImage shapedBytes = makeEllipseMask(selectionOriginal.size());

    const SelectionMaskReadResult chosen = chooseSelectionMaskRead(solidConvert, shapedBytes);
    QCOMPARE(chosen.source, SelectionMaskReadSource::ReadBytesOverSolidConvert);

    const QImage buggyAssembled =
        assembleSelectionMaskInPaddedBounds(solidConvert, selectionOriginal, maskPaddedBounds);
    const QImage fixedAssembled =
        assembleSelectionMaskInPaddedBounds(chosen.mask, selectionOriginal, maskPaddedBounds);

    const double buggyFill = maskNonWhiteFraction(buggyAssembled);
    const double fixedFill = maskNonWhiteFraction(fixedAssembled);
    const double rectFraction = double(selectionOriginal.width() * selectionOriginal.height())
                                / double(maskPaddedBounds.width() * maskPaddedBounds.height());

    QVERIFY2(qAbs(buggyFill - rectFraction) < 0.05,
             qPrintable(QStringLiteral("solid convert assembly fill=%1 expected ~%2")
                            .arg(buggyFill)
                            .arg(rectFraction)));
    QVERIFY2(fixedFill < buggyFill - 0.03,
             qPrintable(QStringLiteral("oval bytes fill=%1 should be below solid %2")
                            .arg(fixedFill)
                            .arg(buggyFill)));
    QVERIFY2(fixedFill > 0.05 && fixedFill < 0.95,
             qPrintable(QStringLiteral("oval assembly fill=%1").arg(fixedFill)));
}

void ComfyInpaintRegressionTest::testBuildRefineRegionSkipsServerColorMatch()
{
    const ComfyWorkflowEngine::RefineRegionParams rrp = makeAndroidRefineRegionFixture();
    QVERIFY(rrp.colorMatch);

    const QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rrp);
    QVERIFY(!wf.isEmpty());
    QVERIFY2(!workflowContainsClassType(wf, QStringLiteral("INPAINT_ColorMatch")),
             "refine_region must not insert INPAINT_ColorMatch (server SaveImage went all-black)");

    QString latentPath;
    const QString summary = summarizeWorkflowGraph(wf, &latentPath);
    QCOMPARE(latentPath, QStringLiteral("inpaint_conditioning"));
    QVERIFY(summary.contains(QStringLiteral("hasInpaintCond=1")));
}

void ComfyInpaintRegressionTest::testBuildRefineRegionSaveImageReachesVaeDecode()
{
    ComfyWorkflowEngine::RefineRegionParams rrp = makeAndroidRefineRegionFixture();
    QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rrp);
    ComfyWorkflowEngine::applyCheckpointStyleOptions(&wf, QString(), 0, ComfyResources::Arch::Sdxl);
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(wf);

    QVERIFY2(refineRegionSaveChainAvoidsColorMatch(wf),
             "SaveImage output chain must reach VAEDecode without INPAINT_ColorMatch");

    const QJsonObject save = wf.value(QStringLiteral("10")).toObject();
    const QJsonArray saveImages =
        save.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray();
    QVERIFY2(saveImages.size() >= 2, "SaveImage images link missing");
    const QString saveSourceId = saveImages.at(0).toString();
    QVERIFY2(wf.contains(saveSourceId), "SaveImage source node must exist");
    const QString saveSourceClass = wf.value(saveSourceId).toObject().value(QStringLiteral("class_type")).toString();
    QVERIFY2(saveSourceClass == QLatin1String("VAEDecode")
                   || saveSourceClass == QLatin1String("ETN_NSFWFilter"),
               qPrintable(QStringLiteral("SaveImage must read VAEDecode or NSFW filter, got ") + saveSourceClass));
}

void ComfyInpaintRegressionTest::testBuildInpaintFillStillUsesServerColorMatch()
{
    ComfyWorkflowEngine::InpaintBuildParams bp;
    bp.imageName = QStringLiteral("canvas.png");
    bp.maskImageName = QStringLiteral("mask.png");
    bp.arch = ComfyResources::Arch::Sdxl;
    bp.useInpaintModel = true;
    bp.controlNetInpaintFile.clear();
    bp.colorMatch = true;
    bp.initialExtentWidth = 512;
    bp.initialExtentHeight = 512;
    bp.contextExtentWidth = 512;
    bp.contextExtentHeight = 512;
    const QJsonObject wf = ComfyWorkflowEngine::buildInpaint(bp);
    QVERIFY(workflowContainsClassType(wf, QStringLiteral("INPAINT_ColorMatch")));
}

void ComfyInpaintRegressionTest::testCompositeRefineBlackServerPaintsMaskRegionBlack()
{
    const QSize contextSize(800, 800);
    const QRect maskLocal(290, 284, 220, 231);
    const QRect contextBounds(187, 18, 800, 800);

    QImage context = makeFilledRgbImage(contextSize, qRgb(214, 191, 179));
    QImage server = makeFilledRgbImage(contextSize, qRgb(0, 0, 0));
    QImage mask = makeSolidRectMask(contextSize, maskLocal);

    InpaintCompositeParams params;
    params.serverResult = server;
    params.contextImage = context;
    params.compositingMask = mask;
    params.contextBounds = contextBounds;
    params.targetBounds = contextBounds;
    params.preprocessGrow = 10;
    params.preprocessFeather = 20;
    params.preprocessBlend = 20;
    params.refineRegionWorkflow = true;
    params.diffusionExtent = contextSize;

    const InpaintCompositeResult result = compositeInpaintServerOntoContext(params);
    QCOMPARE(result.pathTaken, QStringLiteral("full_context"));

    const double rawNonBlack = imageNonBlackFraction(server);
    const double compositeNonBlack = imageNonBlackFraction(result.output);
    QCOMPARE(inpaintFailureVerdict(rawNonBlack, compositeNonBlack, QStringLiteral("inpaint_conditioning"),
                                   QStringLiteral("sdxl"), 0.75, true),
             QStringLiteral("server_black"));

    const QRgb maskedCenter = result.output.pixel(maskLocal.center());
    QVERIFY(qRed(maskedCenter) < 30 && qGreen(maskedCenter) < 30 && qBlue(maskedCenter) < 30);
    const QRgb outsideCorner = result.output.pixel(10, 10);
    QVERIFY(qRed(outsideCorner) > 100);
}

void ComfyInpaintRegressionTest::testCompositeLivePreviewMaskClipKeepsCrosshatchOutside()
{
    QImage context(64, 64, QImage::Format_ARGB32);
    context.fill(qRgb(200, 180, 160));
    QImage mask(64, 64, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter maskPainter(&mask);
    maskPainter.fillRect(QRect(20, 20, 24, 24), QColor(255, 255, 255));

    QImage patch(64, 64, QImage::Format_ARGB32);
    patch.fill(qRgb(40, 90, 140));

    const QRect bounds(0, 0, 64, 64);
    const QImage preview =
        ComfyUIUtils::compositeLiveResultPreviewFromContext(context, bounds, bounds, patch, true, mask);

    const QRgb outside = preview.pixel(5, 5);
    const QRgb inside = preview.pixel(32, 32);
    QVERIFY2(outside != qRgb(40, 90, 140), "outside selection should not show patch color");
    QVERIFY2(inside == qRgb(40, 90, 140), "inside selection should show patch color");
    QVERIFY2(outside != qRgb(200, 180, 160), "outside selection should show DiagCross overlay");
}

void ComfyInpaintRegressionTest::testCompositeLivePreviewDiagCrossTintsOutside()
{
    QImage context(32, 32, QImage::Format_ARGB32);
    context.fill(qRgb(220, 200, 180));
    QImage mask(32, 32, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter maskPainter(&mask);
    maskPainter.fillRect(QRect(10, 10, 12, 12), QColor(255, 255, 255));
    QImage patch(32, 32, QImage::Format_ARGB32);
    patch.fill(qRgb(40, 90, 140));
    const QRect bounds(0, 0, 32, 32);
    const QImage preview =
        ComfyUIUtils::compositeLiveResultPreviewFromContext(context, bounds, bounds, patch, true, mask);
    const QRgb outside = preview.pixel(5, 5);
    const QRgb inside = preview.pixel(16, 16);
    QVERIFY(inside == qRgb(40, 90, 140));
    QVERIFY2(outside != qRgb(220, 200, 180), "outside selection should show DiagCross overlay");
    QVERIFY2(outside != qRgb(40, 90, 140), "outside selection should not show patch color");
}

void ComfyInpaintRegressionTest::testCompositeLiveServerUsesFeatherBlendMask()
{
    const QSize size(64, 64);
    QImage context(size, QImage::Format_ARGB32);
    context.fill(qRgb(100, 120, 140));
    QImage server(size, QImage::Format_RGB32);
    server.fill(qRgb(240, 10, 10));

    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter mp(&mask);
    mp.fillRect(QRect(16, 16, 32, 32), QColor(255, 255, 255));

    ComfyPrepareLiveWorkflow::Result prep;
    prep.hasMask = true;
    prep.contextBounds = QRect(0, 0, 64, 64);
    prep.maskPaddedBounds = prep.contextBounds;
    prep.nativeContextImage = context;
    prep.contextImage = context;
    prep.nativeCompositingMask = mask;
    prep.compositingMaskCropped = mask;
    prep.preprocess.grow = 0;
    prep.preprocess.feather = 0;
    prep.preprocess.blend = 8;
    prep.workflowKind = ComfyPrepareWorkflow::WorkflowKind::RefineRegion;

    const QImage softEdgeComposite = ComfyLiveRunnerInternal::compositeLiveServerResult(server, prep);
    prep.preprocess.blend = 0;
    const QImage hardEdgeComposite = ComfyLiveRunnerInternal::compositeLiveServerResult(server, prep);

    QRgb softEdge = 0;
    QRgb hardEdge = 0;
    bool foundEdgeDiff = false;
    for (int y = 16; y < 48 && !foundEdgeDiff; ++y) {
        for (int x = 12; x < 20; ++x) {
            const QRgb softPx = softEdgeComposite.pixel(x, y);
            const QRgb hardPx = hardEdgeComposite.pixel(x, y);
            if (softPx != hardPx) {
                softEdge = softPx;
                hardEdge = hardPx;
                foundEdgeDiff = true;
                break;
            }
        }
    }
    QVERIFY2(foundEdgeDiff, "blend preprocess should soften compositing boundary");
    QVERIFY(qAbs(qRed(softEdge) - 100) < qAbs(qRed(hardEdge) - 100));
}

void ComfyInpaintRegressionTest::testCompositeLiveServerPreservesContextOutsideMask()
{
    const QSize size(64, 64);
    QImage context(size, QImage::Format_ARGB32);
    context.fill(qRgb(80, 90, 100));
    QImage server(size, QImage::Format_RGB32);
    server.fill(qRgb(240, 10, 10));

    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter mp(&mask);
    mp.fillRect(QRect(20, 20, 24, 24), QColor(255, 255, 255));

    ComfyPrepareLiveWorkflow::Result prep;
    prep.hasMask = true;
    prep.contextBounds = QRect(0, 0, 64, 64);
    prep.maskPaddedBounds = prep.contextBounds;
    prep.nativeContextImage = context;
    prep.contextImage = context;
    prep.nativeCompositingMask = mask;
    prep.compositingMaskCropped = mask;
    prep.preprocess.grow = 0;
    prep.preprocess.feather = 0;
    prep.preprocess.blend = 0;
    prep.workflowKind = ComfyPrepareWorkflow::WorkflowKind::RefineRegion;

    const QImage composite = ComfyLiveRunnerInternal::compositeLiveServerResult(server, prep);
    QCOMPARE(composite.pixel(5, 5), qRgb(80, 90, 100));
    const QRgb inside = composite.pixel(32, 32);
    QVERIFY(qRed(inside) > 200);
    QVERIFY(qGreen(inside) < 50);
}

QTEST_MAIN(ComfyInpaintRegressionTest)
#include "ComfyInpaintRegressionTest.moc"
