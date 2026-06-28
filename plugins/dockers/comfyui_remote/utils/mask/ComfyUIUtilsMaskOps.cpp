/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyResources.h"

#include <QPainter>
#include <cmath>

#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>
#include <KisViewManager.h>
#include <KoColorConversionTransformation.h>
#include <KoColorProfile.h>

namespace ComfyUIUtils {

static QImage rasterDilateGray(const QImage &maskGray, int radius)
{
    if (maskGray.isNull() || radius <= 0)
        return maskGray;
    QImage src = maskGray.format() == QImage::Format_Grayscale8
                     ? maskGray
                     : maskGray.convertToFormat(QImage::Format_Grayscale8);
    // Separable max-filter: O(w*h*r) per axis instead of O(w*h*r^2).
    QImage horiz(src.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const uchar *inLine = src.constScanLine(y);
        uchar *outLine = horiz.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            int maxv = 0;
            const int x0 = qMax(0, x - radius);
            const int x1 = qMin(src.width() - 1, x + radius);
            for (int nx = x0; nx <= x1; ++nx)
                maxv = qMax(maxv, static_cast<int>(inLine[nx]));
            outLine[x] = static_cast<uchar>(maxv);
        }
    }
    QImage out(horiz.size(), QImage::Format_Grayscale8);
    for (int x = 0; x < horiz.width(); ++x) {
        for (int y = 0; y < horiz.height(); ++y) {
            int maxv = 0;
            const int y0 = qMax(0, y - radius);
            const int y1 = qMin(horiz.height() - 1, y + radius);
            for (int ny = y0; ny <= y1; ++ny)
                maxv = qMax(maxv, static_cast<int>(horiz.constScanLine(ny)[x]));
            out.scanLine(y)[x] = static_cast<uchar>(maxv);
        }
    }
    return out;
}

static QImage rasterErodeGray(const QImage &maskGray, int radius)
{
    if (maskGray.isNull() || radius <= 0)
        return maskGray;
    QImage src = maskGray.format() == QImage::Format_Grayscale8
                     ? maskGray
                     : maskGray.convertToFormat(QImage::Format_Grayscale8);
    QImage horiz(src.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const uchar *inLine = src.constScanLine(y);
        uchar *outLine = horiz.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            int minv = 255;
            const int x0 = qMax(0, x - radius);
            const int x1 = qMin(src.width() - 1, x + radius);
            for (int nx = x0; nx <= x1; ++nx)
                minv = qMin(minv, static_cast<int>(inLine[nx]));
            outLine[x] = static_cast<uchar>(minv);
        }
    }
    QImage out(horiz.size(), QImage::Format_Grayscale8);
    for (int x = 0; x < horiz.width(); ++x) {
        for (int y = 0; y < horiz.height(); ++y) {
            int minv = 255;
            const int y0 = qMax(0, y - radius);
            const int y1 = qMin(horiz.height() - 1, y + radius);
            for (int ny = y0; ny <= y1; ++ny) {
                const int v = static_cast<int>(horiz.constScanLine(ny)[x]);
                minv = qMin(minv, v);
            }
            out.scanLine(y)[x] = static_cast<uchar>(minv);
        }
    }
    return out;
}

static QImage rasterBlurGray(const QImage &maskGray, int blurRadius)
{
    if (maskGray.isNull() || blurRadius <= 0)
        return maskGray;
    QImage src = maskGray.format() == QImage::Format_Grayscale8
                     ? maskGray
                     : maskGray.convertToFormat(QImage::Format_Grayscale8);
    const int down = qMax(1, blurRadius);
    return src.scaled(qMax(1, src.width() / down), qMax(1, src.height() / down), Qt::IgnoreAspectRatio,
                      Qt::SmoothTransformation)
        .scaled(src.width(), src.height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage rasterExpandMask(const QImage &maskGray, int grow, int feather)
{
    QImage m = maskGray;
    if (grow > 0)
        m = rasterDilateGray(m, grow);
    if (feather > 0)
        m = rasterBlurGray(m, qMax(1, feather / 4));
    return m;
}

QImage denoiseToCompositingMask(const QImage &maskGray, int grow, int feather, int blend)
{
    QImage m = rasterExpandMask(maskGray, grow, feather);
    m = m.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < m.height(); ++y) {
        uchar *line = m.scanLine(y);
        for (int x = 0; x < m.width(); ++x)
            line[x] = line[x] > 0 ? 255 : 0;
    }
    if (blend > 0) {
        const int shrink = blend / 2;
        if (shrink > 0)
            m = rasterErodeGray(m, shrink);
        m = rasterBlurGray(m, qMax(1, blend / 4));
    }
    return m;
}

int calcSelectionPreProcessGrow(int extentWidth, int extentHeight, int areaWidth, int areaHeight, double strength0to1,
                                int selectionFeatherPercent, double selectionMinTransition, int selectionGrowOffset)
{
    const SelectionPreProcess p = calcSelectionPreProcess(
        extentWidth, extentHeight, areaWidth, areaHeight, strength0to1, selectionFeatherPercent,
        selectionMinTransition, selectionGrowOffset, getSelectionBlendPixels(), false);
    return p.grow;
}

// §4.6 / §3.5: selection_feather stored as 0–25 (percent); default 10
void getSelectionModifierSettings(int *selectionFeatherPercent, double *selectionMinTransition, int *selectionGrowOffset)
{
    QJsonObject s = loadSettingsJson();
    if (selectionFeatherPercent)
        *selectionFeatherPercent = qBound(0, s.value(QStringLiteral("selection_feather")).toInt(10), 25);
    if (selectionMinTransition)
        *selectionMinTransition = qBound(0.0, s.value(QStringLiteral("selection_min_transition")).toDouble(0.0), 100.0);
    if (selectionGrowOffset)
        *selectionGrowOffset = qBound(0, s.value(QStringLiteral("selection_grow_offset")).toInt(0), inpaintGrowFeatherMax);
}

int getSelectionPaddingPercent()
{
    return qBound(0, loadSettingsJson().value(QStringLiteral("selection_padding")).toInt(6), 25);
}

static int roundUpToMultiple(int number, int multiple)
{
    if (multiple <= 1)
        return number;
    return ((number + multiple - 1) / multiple) * multiple;
}

static QRect padBoundsLikeUpstream(const QRect &bounds, const QRect &docExtent, int padding, int minSize, int multiple,
                                   bool square)
{
    if (bounds.isEmpty() || docExtent.isEmpty())
        return bounds;

    auto padScalar = [&](int x, int size, int pad) {
        const int paddedSize = size + 2 * pad;
        const int newSize = roundUpToMultiple(qMax(paddedSize, minSize), multiple);
        const int newX = x - (newSize - size) / 2;
        return std::pair<int, int>{newX, newSize};
    };

    int padX = padding;
    int padY = padding;
    if (square && bounds.width() > bounds.height())
        padX = qMax(padX / 2, padX - (bounds.width() - bounds.height()) / 2);
    else if (square && bounds.height() > bounds.width())
        padY = qMax(padY / 2, padY - (bounds.height() - bounds.width()) / 2);

    const auto [newX, newW] = padScalar(bounds.x(), bounds.width(), padX);
    const auto [newY, newH] = padScalar(bounds.y(), bounds.height(), padY);
    QRect result(newX, newY, newW, newH);

    auto clampAxis = [](int off, int size, int maxSize) {
        if (size >= maxSize)
            return std::pair<int, int>{0, maxSize};
        off = qMax(off, 0);
        const int excess = qMax((off + size) - maxSize, 0);
        return std::pair<int, int>{off - excess, size};
    };
    const auto [x, w] = clampAxis(result.x(), result.width(), docExtent.width());
    const auto [y, h] = clampAxis(result.y(), result.height(), docExtent.height());
    result = QRect(x, y, w, h);
    return result.isEmpty() ? bounds.intersected(docExtent) : result;
}

QRect computePaddedSelectionBounds(const QRect &originalSelection, const QRect &docBounds, double strength0to1,
                                   int selectionFeatherPercent, double selectionMinTransition,
                                   int selectionGrowOffset, int selectionPaddingPercent, const QString &inpaintMode,
                                   bool square)
{
    if (originalSelection.isEmpty() || docBounds.isEmpty())
        return QRect();

    const double diagonal =
        std::hypot(static_cast<double>(originalSelection.width()), static_cast<double>(originalSelection.height()));
    double featherRel = (selectionFeatherPercent / 100.0) * strength0to1;
    if (inpaintMode == QLatin1String("replace_background") && strength0to1 >= 1.0)
        featherRel = qMin(featherRel, 0.01);

    int padPx = qMax(static_cast<int>(featherRel * diagonal),
                     static_cast<int>(std::round(selectionMinTransition * strength0to1)));
    padPx += selectionGrowOffset;
    padPx += static_cast<int>((selectionPaddingPercent / 100.0) * diagonal);

    return padBoundsLikeUpstream(originalSelection, docBounds, padPx, 256, 16, square);
}

void decodeInpaintContextComboData(const QVariant &comboData, QString *contextOut, QString *contextLayerIdOut)
{
    if (contextOut)
        *contextOut = QStringLiteral("automatic");
    if (contextLayerIdOut)
        *contextLayerIdOut = QString();

    const QString data = comboData.toString().trimmed();
    if (data.isEmpty())
        return;

    const QString lower = data.toLower();
    if (lower == QLatin1String("automatic") || lower == QLatin1String("mask_bounds")
        || lower == QLatin1String("entire_image")) {
        if (contextOut)
            *contextOut = lower;
        return;
    }
    // Standalone "layer_bounds" combo preset removed (P8); only mask-node UUID selects a layer context.
    if (lower == QLatin1String("layer_bounds"))
        return;

    const QUuid uid = QUuid::fromString(data);
    if (!uid.isNull()) {
        if (contextOut)
            *contextOut = QStringLiteral("layer_bounds");
        if (contextLayerIdOut)
            *contextLayerIdOut = uid.toString(QUuid::WithoutBraces);
    }
}

KisNodeSP findDocumentNodeByUuid(KisImageSP image, const QUuid &layerId)
{
    if (!image || !image->rootLayer() || layerId.isNull())
        return KisNodeSP();
    return KisLayerUtils::findNodeByUuid(image->rootLayer(), layerId);
}

bool isInpaintContextMaskNode(KisNodeSP node)
{
    return node && (node->inherits("KisTransparencyMask") || node->inherits("KisSelectionMask"));
}

std::optional<QRect> customInpaintGetContext(KisImageSP image, const QString &contextKey,
                                             const QString &contextLayerId, const QRect &maskPaddedBounds)
{
    if (!image)
        return std::nullopt;
    const QRect doc = image->bounds();
    if (doc.isEmpty())
        return std::nullopt;

    QString ctx = contextKey.trimmed().toLower();
    if (ctx.isEmpty())
        ctx = QStringLiteral("automatic");
    ctx.replace(QLatin1Char(' '), QLatin1Char('_'));

    if (ctx == QLatin1String("automatic"))
        return std::nullopt;
    if (ctx == QLatin1String("mask_bounds"))
        return maskPaddedBounds.intersected(doc);
    if (ctx == QLatin1String("entire_image"))
        return doc;
    if (ctx == QLatin1String("layer_bounds")) {
        const QUuid uid = QUuid::fromString(contextLayerId.trimmed());
        KisNodeSP layer = findDocumentNodeByUuid(image, uid);
        if (!layer)
            return std::nullopt;
        QRect layerR = layer->exactBounds() & doc;
        if (layerR.isEmpty())
            return std::nullopt;
        return layerR.united(maskPaddedBounds).intersected(doc);
    }
    return std::nullopt;
}

InpaintParams customInpaintGetParams(const QString &customFillKind, bool useInpaintModel, bool usePromptFocus,
                                     bool isEditing)
{
    InpaintParams p;
    p.fillKind = isEditing ? QStringLiteral("none")
                           : (customFillKind.trimmed().isEmpty() ? QStringLiteral("neutral") : customFillKind.trimmed());
    p.useInpaintModel = useInpaintModel;
    p.useConditionMask = usePromptFocus;
    p.useReference = false;
    return p;
}

QString getInpaintContextFromSelectionNode(const QJsonObject &selectionNodeInputs)
{
    if (!selectionNodeInputs.contains(QStringLiteral("context")))
        return QStringLiteral("entire_image");
    return inpaintContextKeyFromJson(selectionNodeInputs.value(QStringLiteral("context")));
}
SelectionModifiers getSelectionModifiersForContext(const QString &contextKey, double strength0to1, int minSize)
{
    const QString ctx = contextKey.trimmed().toLower();
    if (ctx == QLatin1String("mask_bounds")) {
        SelectionModifiers m;
        m.multiple = 1;
        m.sizeMinPx = 0;
        return m;
    }
    return getSelectionModifiers(QString(), QStringLiteral("fill"), strength0to1, minSize);
}
bool isValidCustomWorkflowSelectionContext(const QString &contextKey)
{
    const QString ctx = contextKey.trimmed().toLower();
    return ctx == QLatin1String("automatic") || ctx == QLatin1String("mask_bounds")
           || ctx == QLatin1String("entire_image");
}
CustomWorkflowMaskPrepareResult prepareCustomWorkflowMask(const QJsonObject &selectionNodeInputs,
                                                          const MaskFromSelectionResult &maskResult,
                                                          const QRect &docBounds)
{
    CustomWorkflowMaskPrepareResult out;
    out.captureBounds = docBounds;
    if (!maskResult.valid || maskResult.paddedBounds.isEmpty() || maskResult.originalBounds.isEmpty())
        return out;

    const int pad = selectionNodeInputs.value(QStringLiteral("padding")).toInt(0);
    const QString ctx = getInpaintContextFromSelectionNode(selectionNodeInputs);
    QRect bounds;
    if (ctx == QLatin1String("entire_image")) {
        bounds = docBounds;
    } else if (ctx == QLatin1String("automatic")) {
        bounds = padBoundsLikeUpstream(maskResult.paddedBounds, docBounds, pad, 0, 8, false);
    } else if (ctx == QLatin1String("mask_bounds")) {
        bounds = padBoundsLikeUpstream(maskResult.originalBounds, docBounds, pad, 0, 1, false);
    } else {
        out.ok = false;
        out.errorMessage =
            ComfyTr::tr("Invalid inpaint context for custom workflow selection node: %1").arg(ctx);
        return out;
    }
    bounds = bounds.intersected(docBounds);
    if (bounds.isEmpty())
        return out;

    const QImage maskFull = embedGrayMaskInDocument(maskResult.maskGray, maskResult.paddedBounds, docBounds);
    out.maskInCaptureCoords = cropImageToDocumentRect(maskFull, bounds, docBounds);
    out.captureBounds = bounds;
    out.hasSelectionMask = true;
    out.ok = true;
    return out;
}
QRect makeRectSquare(const QRect &rect, int extentWidth, int extentHeight)
{
    if (rect.isEmpty() || extentWidth <= 0 || extentHeight <= 0) return rect;
    const int side = qMin(qMax(rect.width(), rect.height()), qMin(extentWidth, extentHeight));
    int x = rect.center().x() - side / 2;
    int y = rect.center().y() - side / 2;
    x = qBound(0, x, qMax(0, extentWidth - side));
    y = qBound(0, y, qMax(0, extentHeight - side));
    return QRect(x, y, side, side);
}

QImage maskPngForComfyUpload(const QImage &maskGray)
{
    if (maskGray.isNull())
        return QImage();
    QImage maskPng(maskGray.size(), QImage::Format_ARGB32);
    for (int y = 0; y < maskGray.height(); ++y) {
        for (int x = 0; x < maskGray.width(); ++x) {
            const int g = qGray(maskGray.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    }
    return maskPng;
}
QImage getMaskAsQImage(KisImageSP image, KisViewManager *viewManager, const QString &maskSource, bool invertSelection)
{
    QRect bounds = image->bounds();
    if (bounds.isEmpty()) return QImage();
    QImage maskImage(bounds.width(), bounds.height(), QImage::Format_Grayscale8);
    maskImage.fill(0);

    if (maskSource == "selection") {
        KisSelectionSP sel = viewManager ? viewManager->selection() : nullptr;
        if (!sel || !sel->pixelSelection()) return QImage();
        QRect rect = sel->pixelSelection()->selectedExactRect();
        KisPaintDeviceSP dev = sel->pixelSelection();
        rect &= bounds;
        if (rect.isEmpty()) return QImage();

        const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
        QImage slice = dev->convertToQImage(profile, rect.x(), rect.y(), rect.width(), rect.height(),
                                            KoColorConversionTransformation::internalRenderingIntent(),
                                            KoColorConversionTransformation::internalConversionFlags());
        if (!slice.isNull()) {
            for (int y = 0; y < slice.height(); y++) {
                for (int x = 0; x < slice.width(); x++) {
                    int v = qAlpha(slice.pixel(x, y));
                    if (v == 0) v = qGray(slice.pixel(x, y));
                    if (invertSelection) v = 255 - v;
                    maskImage.setPixel(rect.x() + x, rect.y() + y, qRgb(v, v, v));
                }
            }
        } else {
            int ps = dev->pixelSize();
            QVector<quint8> data(rect.width() * rect.height() * qMax(1, ps));
            dev->readBytes(data.data(), rect.x(), rect.y(), rect.width(), rect.height());
            for (int y = 0; y < rect.height(); y++) {
                for (int x = 0; x < rect.width(); x++) {
                    int srcIdx = (y * rect.width() + x) * qMax(1, ps);
                    quint8 v = ps > 0 ? data.value(srcIdx, 0) : 0;
                    if (invertSelection) v = 255 - v;
                    maskImage.setPixel(rect.x() + x, rect.y() + y, qRgb(v, v, v));
                }
            }
        }

        // Rectangular / bounded-box selections sometimes expose bounds without readable
        // interior pixels via readBytes; match upstream by treating the exact rect as filled.
        quint64 sum = 0;
        const int count = rect.width() * rect.height();
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x)
                sum += static_cast<quint64>(qGray(maskImage.pixel(x, y)));
        }
        if (count > 0 && sum < static_cast<quint64>(count) * 8 && !invertSelection) {
            QPainter p(&maskImage);
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(rect, QColor(255, 255, 255));
        }
        return maskImage;
    }

    if (maskSource.startsWith("layer:")) {
        QString layerName = maskSource.mid(6);
        KisNodeSP root = image->rootLayer();
        if (!root) return QImage();
        QList<KisNodeSP> nodes;
        nodes.append(root);
        KisNodeSP foundNode;
        while (!nodes.isEmpty()) {
            KisNodeSP n = nodes.takeFirst();
            if (n->name() == layerName) { foundNode = n; break; }
            for (int i = 0; i < static_cast<int>(n->childCount()); i++) nodes.append(n->at(i));
        }
        // §13.157: Mask-type nodes (transparency / filter masks) are KisMask, not KisLayer — use projection exactBounds, not full-image bounds
        if (foundNode) {
            if (auto *foundMask = dynamic_cast<KisMask *>(foundNode.data())) {
                KisPaintDeviceSP dev = foundMask->projection();
                if (!dev) return QImage();
                QRect rect = dev->exactBounds() & bounds;
                if (rect.isEmpty()) return QImage();
                const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
                QImage rgba = dev->convertToQImage(profile, rect.x(), rect.y(), rect.width(), rect.height(),
                                                   KoColorConversionTransformation::internalRenderingIntent(),
                                                   KoColorConversionTransformation::internalConversionFlags());
                if (rgba.isNull()) return QImage();
                for (int y = 0; y < rgba.height(); y++) {
                    for (int x = 0; x < rgba.width(); x++) {
                        const QRgb px = rgba.pixel(x, y);
                        int v = qAlpha(px);
                        if (v == 0) v = qGray(px);
                        maskImage.setPixel(rect.x() + x, rect.y() + y, qRgb(v, v, v));
                    }
                }
                return maskImage;
            }
        }
        KisLayer *foundLayer = foundNode ? dynamic_cast<KisLayer *>(foundNode.data()) : nullptr;
        if (!foundLayer || !foundLayer->projection()) return QImage();
        const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
        QImage rgba = foundLayer->projection()->convertToQImage(profile, bounds,
            KoColorConversionTransformation::internalRenderingIntent(),
            KoColorConversionTransformation::internalConversionFlags());
        if (rgba.isNull() || rgba.size() != maskImage.size()) return QImage();
        for (int y = 0; y < rgba.height(); y++) {
            for (int x = 0; x < rgba.width(); x++) {
                int a = qAlpha(rgba.pixel(x, y));
                maskImage.setPixel(x, y, qRgb(a, a, a));
            }
        }
        return maskImage;
    }
    return QImage();
}

void compositeWithMask(QImage &current, const QImage &result, const QImage &mask)
{
    if (current.size() != result.size() || current.size() != mask.size() || result.format() != QImage::Format_RGB32) return;
    if (current.format() != QImage::Format_ARGB32 && current.format() != QImage::Format_RGB32)
        current = current.convertToFormat(QImage::Format_ARGB32);
    QImage res = result.format() == QImage::Format_ARGB32 ? result : result.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < current.height(); y++) {
        for (int x = 0; x < current.width(); x++) {
            int m = qGray(mask.pixel(x, y));
            if (m <= 0) continue;
            QRgb cur = current.pixel(x, y);
            QRgb resPix = res.pixel(x, y);
            if (m >= 255) {
                current.setPixel(x, y, resPix);
            } else {
                int inv = 255 - m;
                current.setPixel(x, y, qRgba(
                    (qRed(cur) * inv + qRed(resPix) * m) / 255,
                    (qGreen(cur) * inv + qGreen(resPix) * m) / 255,
                    (qBlue(cur) * inv + qBlue(resPix) * m) / 255,
                    (qAlpha(cur) * inv + qAlpha(resPix) * m) / 255));
            }
        }
    }
}


} // namespace ComfyUIUtils
