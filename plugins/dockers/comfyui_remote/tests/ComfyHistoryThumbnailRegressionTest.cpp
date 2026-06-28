/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression tests for masked history thumbnails (oval/feathered selection shape).
 */

#include <simpletest.h>
#include <QTest>

#include <QPainter>
#include <QTemporaryDir>
#include <QFile>

#include "ComfyHistoryInternal.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfyUIRemoteDockPrivate.h"

using ComfyHistoryEntry = ComfyUIRemoteDock::Private::HistoryEntry;

namespace {

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

int cornerAlphaAverage(const QImage &img)
{
    if (img.isNull())
        return -1;
    QImage argb = img.format() == QImage::Format_ARGB32 ? img : img.convertToFormat(QImage::Format_ARGB32);
    const int w = argb.width();
    const int h = argb.height();
    if (w <= 0 || h <= 0)
        return -1;
    auto alphaAt = [&](int x, int y) {
        return qAlpha(argb.pixel(qBound(0, x, w - 1), qBound(0, y, h - 1)));
    };
    return (alphaAt(0, 0) + alphaAt(w - 1, 0) + alphaAt(0, h - 1) + alphaAt(w - 1, h - 1)) / 4;
}

int firstOpaqueRow(const QImage &img, int alphaMin = 16)
{
    const QImage argb = img.format() == QImage::Format_ARGB32
                            ? img
                            : img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) > alphaMin)
                return y;
        }
    }
    return -1;
}

} // namespace

class ComfyHistoryThumbnailRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHistoryThumbnailSidecarHasTransparentCornersForOvalMask();
    void testHistoryThumbnailPixmapPreservesMaskAlpha();
    void testHistoryThumbnailPixmapCropsVerticalPadding();
    void testMaskShapeDescriptionDetectsOval();
    void testMaskShapeDescriptionDetectsSolidRectangle();
};

void ComfyHistoryThumbnailRegressionTest::testHistoryThumbnailSidecarHasTransparentCornersForOvalMask()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("result.png"));

    ComfyHistoryEntry entry;
    entry.hasMask = true;
    entry.contextBounds = QRect(0, 0, 640, 368);
    entry.targetBounds = QRect(128, 88, 467, 193);
    entry.width = entry.targetBounds.width();
    entry.height = entry.targetBounds.height();

    QImage result(entry.contextBounds.size(), QImage::Format_ARGB32);
    result.fill(qRgb(90, 110, 130));

    QImage ovalMask(entry.contextBounds.size(), QImage::Format_Grayscale8);
    ovalMask.fill(0);
    QPainter maskPainter(&ovalMask);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    maskPainter.setBrush(QColor(255, 255, 255));
    maskPainter.setPen(Qt::NoPen);
    maskPainter.drawEllipse(entry.targetBounds);

    QVERIFY(ComfyHistoryInternal::saveHistoryDisplayThumbnail(path, entry, result, ovalMask));
    QVERIFY(QFile::exists(ComfyHistoryInternal::historyThumbnailSidecarPath(path)));

    QImage thumb;
    QVERIFY(thumb.load(ComfyHistoryInternal::historyThumbnailSidecarPath(path)));
    const int cornerAlpha = cornerAlphaAverage(thumb);
    const int centerAlpha = qAlpha(thumb.pixel(thumb.width() / 2, thumb.height() / 2));
    QVERIFY2(cornerAlpha < 32,
             qPrintable(QStringLiteral("thumb corners should be transparent: cornerAlpha=%1").arg(cornerAlpha)));
    QVERIFY2(centerAlpha > 200,
             qPrintable(QStringLiteral("thumb center should be opaque: centerAlpha=%1").arg(centerAlpha)));
}

void ComfyHistoryThumbnailRegressionTest::testHistoryThumbnailPixmapPreservesMaskAlpha()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("pixmap.png"));

    ComfyHistoryEntry entry;
    entry.hasMask = true;
    entry.contextBounds = QRect(0, 0, 336, 304);
    entry.targetBounds = QRect(40, 30, 256, 244);
    entry.width = entry.targetBounds.width();
    entry.height = entry.targetBounds.height();

    QImage result(entry.contextBounds.size(), QImage::Format_ARGB32);
    result.fill(qRgb(200, 180, 160));

    QImage fullMask(entry.contextBounds.size(), QImage::Format_Grayscale8);
    fullMask.fill(0);
    QPainter p(&fullMask);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(255, 255, 255));
    p.setPen(Qt::NoPen);
    p.drawEllipse(entry.targetBounds);

    QVERIFY(ComfyHistoryInternal::saveHistoryDisplayThumbnail(path, entry, result, fullMask));

    const QPixmap pix = ComfyHistoryInternal::historyThumbnailPixmap(entry, path, QSize(96, 96), nullptr);
    QVERIFY(!pix.isNull());
    const QImage scaled = pix.toImage();
    const int cornerAlpha = cornerAlphaAverage(scaled);
    QVERIFY2(cornerAlpha < 48,
             qPrintable(QStringLiteral("list pixmap corners transparent: cornerAlpha=%1 detail=%2")
                            .arg(cornerAlpha)
                            .arg(ComfyUiLayoutDiagnostics::imageAlphaCornerStats(scaled))));
}

void ComfyHistoryThumbnailRegressionTest::testHistoryThumbnailPixmapCropsVerticalPadding()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.filePath(QStringLiteral("tight.png"));

    ComfyHistoryEntry entry;
    entry.hasMask = true;
    entry.contextBounds = QRect(0, 0, 640, 368);
    entry.targetBounds = QRect(128, 200, 467, 150);
    entry.width = entry.targetBounds.width();
    entry.height = entry.targetBounds.height();

    QImage result(entry.contextBounds.size(), QImage::Format_ARGB32);
    result.fill(qRgb(90, 110, 130));

    QImage ovalMask(entry.contextBounds.size(), QImage::Format_Grayscale8);
    ovalMask.fill(0);
    QPainter maskPainter(&ovalMask);
    maskPainter.setRenderHint(QPainter::Antialiasing, true);
    maskPainter.setBrush(QColor(255, 255, 255));
    maskPainter.setPen(Qt::NoPen);
    maskPainter.drawEllipse(entry.targetBounds);

    QVERIFY(ComfyHistoryInternal::saveHistoryDisplayThumbnail(path, entry, result, ovalMask));

    const QPixmap pix = ComfyHistoryInternal::historyThumbnailPixmap(entry, path, QSize(96, 96), nullptr);
    QVERIFY(!pix.isNull());
    QVERIFY2(pix.height() < 96,
             qPrintable(QStringLiteral("tight crop should be shorter than icon box: h=%1").arg(pix.height())));
    const int firstRow = firstOpaqueRow(pix.toImage());
    QVERIFY2(firstRow >= 0 && firstRow <= 2,
             qPrintable(QStringLiteral("opaque content should start at top: firstRow=%1").arg(firstRow)));
}

void ComfyHistoryThumbnailRegressionTest::testMaskShapeDescriptionDetectsOval()
{
    const QImage oval = makeEllipseMask(QSize(467, 193));
    const QString desc = ComfyUiLayoutDiagnostics::maskShapeDescription(oval);
    QVERIFY2(desc.contains(QStringLiteral("round=yes")),
             qPrintable(QStringLiteral("expected round=yes: %1").arg(desc)));
    QVERIFY2(desc.contains(QStringLiteral("corner=0")),
             qPrintable(QStringLiteral("oval mask corners should be black: %1").arg(desc)));
}

void ComfyHistoryThumbnailRegressionTest::testMaskShapeDescriptionDetectsSolidRectangle()
{
    QImage solid(100, 80, QImage::Format_Grayscale8);
    solid.fill(255);
    const QString desc = ComfyUiLayoutDiagnostics::maskShapeDescription(solid);
    QVERIFY2(desc.contains(QStringLiteral("round=no")),
             qPrintable(QStringLiteral("solid rect should not be round: %1").arg(desc)));
    QVERIFY2(desc.contains(QStringLiteral("bboxFill=100.0%")),
             qPrintable(QStringLiteral("solid fill expected: %1").arg(desc)));
}

QTEST_MAIN(ComfyHistoryThumbnailRegressionTest)
#include "ComfyHistoryThumbnailRegressionTest.moc"
