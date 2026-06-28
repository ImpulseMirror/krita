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
#include "ComfyResources.h"
#include "ComfyUIUtils.h"

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

QTEST_MAIN(ComfyInpaintRegressionTest)
#include "ComfyInpaintRegressionTest.moc"
