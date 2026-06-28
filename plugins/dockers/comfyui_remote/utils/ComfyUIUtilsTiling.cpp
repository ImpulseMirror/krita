/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyStyleCollection.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyTheme.h"
#include "ComfyControlLayer.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QRandomGenerator>
#include <QHash>
#include <QBuffer>
#include <QCryptographicHash>
#include <QImageWriter>
#include <QRegularExpression>
#include <QUuid>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QPainter>
#include <thread>
#include <KoColorConversionTransformation.h>

#ifdef COMFYUI_HAVE_KARCHIVE
#include <KZip>
#endif

#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_annotation.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>
#include <krita_utils.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorProfile.h>
#include <KoColorModelStandardIds.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>


namespace ComfyUIUtils {

void DiffusionTileLayout::initGridFromExtent()
{
    const int width = imageExtent.width();
    const int height = imageExtent.height();
    if (width <= 0 || height <= 0 || tileExtent <= 1) {
        gridW = gridH = tileCount = 0;
        step = 1;
        return;
    }
    int overlap = padding;
    if (overlap < 0)
        overlap = qBound(8, tileExtent / 8, 128);
    overlap = qBound(0, overlap, tileExtent - 1);
    step = qMax(1, tileExtent - overlap);
    auto count1d = [this](int dim) {
        if (dim <= tileExtent)
            return 1;
        return 1 + (dim - tileExtent + step - 1) / step;
    };
    gridW = count1d(width);
    gridH = count1d(height);
    tileCount = gridW * gridH;
}

DiffusionTileLayout DiffusionTileLayout::fromUniformGrid(int width, int height, int tileExtentPx, int overlapPx,
                                                         int minTileSize, int blendPx)
{
    DiffusionTileLayout L;
    L.imageExtent = QSize(width, height);
    L.tileExtent = tileExtentPx;
    L.minSize = minTileSize;
    L.padding = overlapPx;
    L.blending = blendPx;
    L.initGridFromExtent();
    return L;
}

DiffusionTileLayout DiffusionTileLayout::fromDenoiseStrength(QSize extent, int minTileSize, double strength0to1,
                                                           int multiple, int overlapPx)
{
    const int w = extent.width();
    const int h = extent.height();
    double t = qBound(0.0, strength0to1, 1.0);
    // Higher denoise → smaller tiles (more VRAM-friendly splits); lower → larger tiles.
    int te = static_cast<int>(std::lround(static_cast<double>(minTileSize) * (1.0 + 3.0 * (1.0 - t))));
    const int maxDim = qMax(w, h);
    if (maxDim > 0)
        te = qMin(te, maxDim);
    te = qMax(minTileSize, te);
    if (multiple > 1)
        te = ((te + multiple - 1) / multiple) * multiple;
    if (maxDim > 0)
        te = qMin(te, maxDim);
    te = qMax(minTileSize, te);
    return fromUniformGrid(w, h, te, overlapPx, minTileSize, 0);
}

QPoint DiffusionTileLayout::coord(int index) const
{
    if (gridW <= 0 || index < 0 || index >= tileCount)
        return QPoint(-1, -1);
    return QPoint(index % gridW, index / gridW);
}

int DiffusionTileLayout::tileIndex(QPoint tileCoord) const
{
    if (tileCoord.x() < 0 || tileCoord.y() < 0 || tileCoord.x() >= gridW || tileCoord.y() >= gridH)
        return -1;
    return tileCoord.y() * gridW + tileCoord.x();
}

QRect DiffusionTileLayout::boundsAtTileCoord(QPoint c) const
{
    const int w = imageExtent.width();
    const int h = imageExtent.height();
    if (c.x() < 0 || c.y() < 0)
        return QRect();
    const int x0 = c.x() * step;
    const int y0 = c.y() * step;
    const int x1 = qMin(x0 + tileExtent, w);
    const int y1 = qMin(y0 + tileExtent, h);
    return QRect(x0, y0, qMax(0, x1 - x0), qMax(0, y1 - y0));
}

QRect DiffusionTileLayout::bounds(int index) const
{
    const QPoint c = coord(index);
    if (c.x() < 0)
        return QRect();
    return boundsAtTileCoord(c);
}

QPoint DiffusionTileLayout::start(QPoint tileCoord) const
{
    if (tileCoord.x() < 0 || tileCoord.y() < 0)
        return QPoint(-1, -1);
    return QPoint(tileCoord.x() * step, tileCoord.y() * step);
}

QPoint DiffusionTileLayout::end(QPoint tileCoord) const
{
    const QRect r = boundsAtTileCoord(tileCoord);
    if (r.isEmpty())
        return QPoint(-1, -1);
    return QPoint(r.right(), r.bottom());
}

int estimateUniformTileGridCount2D(int width, int height, int tileExtent, int overlapPx)
{
    return DiffusionTileLayout::fromUniformGrid(width, height, tileExtent, overlapPx).totalTiles();
}

int diffusionUpscaleTileEstimateExtentPx(const QJsonObject &settingsRoot)
{
    int v = settingsRoot.value(QStringLiteral("upscale_tile_estimate_extent")).toInt(512);
    v = qBound(256, v, 2048);
    return (v / 256) * 256;
}

namespace {

int multipleOfInt(int value, int multiple)
{
    if (multiple <= 1)
        return value;
    return ((value + multiple - 1) / multiple) * multiple;
}

} // namespace

int computeUpscaleTiledMinTileSizePx(int targetWidth, int targetHeight, ComfyResources::Arch arch,
                                     int stylePreferredResolution)
{
    int base = stylePreferredResolution > 0 ? stylePreferredResolution
                                            : (arch == ComfyResources::Arch::Sd15 ? 800 : 1024);
    const int longest = qMax(targetWidth, targetHeight);
    base = qMax(base, longest / 12);
    const int multiple = qMax(1, ComfyResources::latentCompressionFactor(arch));
    base = multipleOfInt(base - 128, multiple);
    return qMax(multiple, base);
}

UpscaleTiledLayoutSpec computeUpscaleTiledLayoutSpec(int imageWidth,
                                                     int imageHeight,
                                                     ComfyResources::Arch arch,
                                                     int stylePreferredResolution,
                                                     double denoiseStrength0to1,
                                                     int customOverlapPx)
{
    UpscaleTiledLayoutSpec spec;
    const int multiple = qMax(1, ComfyResources::latentCompressionFactor(arch));
    spec.minTileSize = computeUpscaleTiledMinTileSizePx(imageWidth, imageHeight, arch, stylePreferredResolution);
    if (customOverlapPx < 0) {
        const double t = qBound(0.0, denoiseStrength0to1, 1.0);
        spec.padding = multipleOfInt(qRound(16.0 + 64.0 * t), multiple);
    } else {
        spec.padding = multipleOfInt(customOverlapPx, multiple);
    }
    spec.blending = spec.padding > 0 ? qMax(1, spec.padding / 16) * 8 : 0;
    const int divisor = qMax(multiple, spec.minTileSize - 2 * spec.padding);
    const int tw = qMax(1, imageWidth / divisor);
    const int th = qMax(1, imageHeight / divisor);
    spec.totalTiles = tw * th;
    return spec;
}

void clampExtentToMaxMegapixels(int *width, int *height)
{
    if (!width || !height) return;
    int w = *width;
    int h = *height;
    if (w <= 0 || h <= 0) return;

    QJsonObject s = loadSettingsJson();
    if (s.value(QStringLiteral("max_pixel_auto")).toBool(true))
        return;
    const int maxMp = s.value(QStringLiteral("max_pixel_count_mp")).toInt(8);
    if (maxMp <= 0) return;

    const qint64 maxArea = static_cast<qint64>(maxMp) * 1000000LL;
    qint64 area = static_cast<qint64>(w) * static_cast<qint64>(h);
    if (area <= maxArea) return;

    double scale = std::sqrt(static_cast<double>(maxArea) / static_cast<double>(area));
    w = qMax(64, static_cast<int>(w * scale));
    h = qMax(64, static_cast<int>(h * scale));
    area = static_cast<qint64>(w) * h;
    int guard = 0;
    while (area > maxArea && (w > 64 || h > 64) && guard++ < 16384) {
        if (w >= h)
            w = qMax(64, w - 1);
        else
            h = qMax(64, h - 1);
        area = static_cast<qint64>(w) * h;
    }
    *width = w;
    *height = h;
}

// §13.45 / §13.193: Frame paths for Live (.live-frames/frame-N.webp) and Animation (.animation/frame-N.png)

bool filesContentsEqual(const QString &pathA, const QString &pathB)
{
    if (pathA.isEmpty() || pathB.isEmpty())
        return false;
    if (pathA == pathB)
        return true;
    QFileInfo ia(pathA), ib(pathB);
    if (!ia.exists() || !ib.exists() || ia.size() != ib.size())
        return false;
    QFile fa(pathA), fb(pathB);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    constexpr qint64 kChunk = 1 << 20;
    while (!fa.atEnd()) {
        const QByteArray ba = fa.read(kChunk);
        const QByteArray bb = fb.read(kChunk);
        if (ba != bb)
            return false;
    }
    return true;
}

DiffusionPreparedExtent prepareDiffusionInputExtent(const QSize &inputExtent, ComfyResources::Arch arch)
{
    DiffusionPreparedExtent out;
    const int w = inputExtent.width();
    const int h = inputExtent.height();
    if (w <= 0 || h <= 0)
        return out;

    out.initial = inputExtent;
    out.scaleFromInput = 1.0;

    const qint64 pixelCount = static_cast<qint64>(w) * static_cast<qint64>(h);
    int minSize = 512;
    qint64 minPixelCount = 512LL * 512;
    if (ComfyResources::isSdxlLike(arch) || arch == ComfyResources::Arch::Sd3) {
        minSize = 640;
        minPixelCount = 800LL * 800;
    }

    const double minScale = std::sqrt(static_cast<double>(minPixelCount) / static_cast<double>(pixelCount));
    int initialW = w;
    int initialH = h;
    if (minScale > 1.0 && w < minSize && h < minSize) {
        initialW = static_cast<int>(std::round(w * minScale));
        initialH = static_cast<int>(std::round(h * minScale));
        out.scaleFromInput = minScale;
    }
    initialW = qMax(minSize, initialW);
    initialH = qMax(minSize, initialH);

    const int multiple = 16;
    initialW = qMax(multiple, ((initialW + multiple - 1) / multiple) * multiple);
    initialH = qMax(multiple, ((initialH + multiple - 1) / multiple) * multiple);
    out.initial = QSize(initialW, initialH);
    return out;
}

} // namespace ComfyUIUtils
