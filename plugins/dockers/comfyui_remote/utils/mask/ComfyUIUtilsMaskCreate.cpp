/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyResources.h"

#include <QPainter>
#include <QLoggingCategory>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>
#include <KisViewManager.h>
#include <KoColorConversionTransformation.h>
#include <KoColorProfile.h>

namespace ComfyUIUtils {

// §13.154: Entire-document selection → no mask, generation uses full image
bool isSelectionEntireDocument(KisImageSP image, KisViewManager *viewManager)
{
    if (!image || !viewManager) return false;
    KisSelectionSP sel = viewManager->selection();
    if (!sel || !sel->pixelSelection()) return false;
    QRect docBounds = image->bounds();
    if (docBounds.isEmpty()) return false;
    QRect rect = sel->pixelSelection()->selectedExactRect();
    if (rect.x() != 0 || rect.y() != 0
        || rect.width() != docBounds.width() || rect.height() != docBounds.height()) {
        return false;
    }
    KisPaintDeviceSP dev = sel->pixelSelection();
    const int ps = dev->pixelSize();
    if (ps <= 0) return false;
    QVector<quint8> data(rect.width() * rect.height() * ps);
    dev->readBytes(data.data(), rect.x(), rect.y(), rect.width(), rect.height());
    for (int i = 0; i < data.size(); i += ps) {
        if (ps == 1) {
            if (data[i] != 0xff) return false;
        } else {
            for (int c = 0; c < ps; ++c)
                if (data[i + c] != 0xff) return false;
        }
    }
    return true;
}
// §13.206: detect_inpaint() — InpaintParams from mode, arch, strength, conditioning
InpaintParams detectInpaintParams(const QString &mode, const QString &arch, double strength0to1,
                                 bool positiveEmpty, bool hasStructuralControl, bool editReference)
{
    InpaintParams p;
    const bool fillOrExpand = (mode == QLatin1String("fill") || mode == QLatin1String("expand"));
    if (editReference && fillOrExpand && arch == QLatin1String("flux2_4b")) {
        p.fillKind = QStringLiteral("green");
        p.useInpaintModel = true;
        p.useReference = false;
        p.useConditionMask = false;
        return p;
    }
    if (editReference) {
        p.fillKind = QStringLiteral("none");
        p.useInpaintModel = false;
        p.useReference = false;
        p.useConditionMask = false;
        return p;
    }
    // Fill by mode (when edit_reference is False)
    if (mode == QLatin1String("fill"))
        p.fillKind = QStringLiteral("blur");
    else if (mode == QLatin1String("expand"))
        p.fillKind = QStringLiteral("border");
    else if (mode == QLatin1String("add_object"))
        p.fillKind = QStringLiteral("neutral");
    else if (mode == QLatin1String("remove_object"))
        p.fillKind = QStringLiteral("inpaint");
    else if (mode == QLatin1String("replace_background"))
        p.fillKind = QStringLiteral("replace");
    else if (mode == QLatin1String("custom"))
        p.fillKind = QStringLiteral("none");
    else
        p.fillKind = QStringLiteral("blur");
    p.useReference = fillOrExpand && positiveEmpty;
    // use_inpaint_model by arch
    p.useInpaintModel = (arch == QLatin1String("sd15") && strength0to1 > 0.5)
        || (arch == QLatin1String("sdxl") && strength0to1 > 0.8)
        || ((arch == QLatin1String("flux") || arch == QLatin1String("flux2_4b")) && strength0to1 >= 1.0);
    // use_condition_mask (SD1.5 only): add_object, positive non-empty, no structural control
    p.useConditionMask = (arch == QLatin1String("sd15")) && (mode == QLatin1String("add_object"))
        && !positiveEmpty && !hasStructuralControl;
    // Python workflow.detect_inpaint — native edit arch forces custom fill semantics
    if (ComfyResources::isEditArch(ComfyResources::archFromKey(arch))) {
        p.fillKind = QStringLiteral("none");
        p.isEditMode = true;
    }
    return p;
}

QString defaultFillKindForInpaintMode(const QString &mode)
{
    if (mode == QLatin1String("fill"))
        return QStringLiteral("blur");
    if (mode == QLatin1String("expand"))
        return QStringLiteral("border");
    if (mode == QLatin1String("add_object"))
        return QStringLiteral("neutral");
    if (mode == QLatin1String("remove_object"))
        return QStringLiteral("inpaint");
    if (mode == QLatin1String("replace_background"))
        return QStringLiteral("replace");
    if (mode == QLatin1String("custom"))
        return QStringLiteral("none");
    return QStringLiteral("blur");
}
SelectionModifiers getSelectionModifiers(const QString &archKey, const QString &modifierMode, double strength0to1,
                                         int minSize)
{
    Q_UNUSED(archKey);
    SelectionModifiers m;
    int featherPct = 10;
    double minTransition = 0.0;
    int growOffset = 0;
    getSelectionModifierSettings(&featherPct, &minTransition, &growOffset);

    double feather = featherPct / 100.0;
    bool invert = false;
    const QString mode = modifierMode.trimmed().toLower();
    if (mode == QLatin1String("replace_background") && strength0to1 >= 1.0) {
        feather = qMin(feather, 0.01);
        invert = true;
    }

    m.featherRel = feather * strength0to1;
    m.featherMinPx = static_cast<int>(qRound(minTransition * strength0to1));
    m.padRel = getSelectionPaddingPercent() / 100.0;
    m.padOffsetPx = growOffset;
    m.sizeMinPx = minSize;
    m.multiple = 16;
    m.square = false;
    m.invert = invert;
    return m;
}

QString resolveInpaintMode(const QString &modifierMode, int extentWidth, int extentHeight,
                           const QRect &originalSelectionBounds)
{
    const QString mode = modifierMode.trimmed().toLower();
    if (mode != QLatin1String("automatic"))
        return mode;
    return detectInpaintMode(extentWidth, extentHeight, originalSelectionBounds.x(), originalSelectionBounds.y(),
                             originalSelectionBounds.width(), originalSelectionBounds.height());
}

QRect padBoundsLikeUpstream(const QRect &bounds, const QRect &docExtent, int padding, int minSize, int multiple,
                            bool square)
{
    if (bounds.isEmpty() || docExtent.isEmpty())
        return bounds;

    auto padScalar = [&](int x, int size, int pad) {
        const int paddedSize = size + 2 * pad;
        const int newSize = ((qMax(paddedSize, minSize) + multiple - 1) / multiple) * multiple;
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

static QString maskStatsForLog(const QImage &mask, const QString &label)
{
    if (mask.isNull())
        return label + QStringLiteral(" null");
    const QImage g = mask.format() == QImage::Format_Grayscale8
                         ? mask
                         : mask.convertToFormat(QImage::Format_Grayscale8);
    quint64 sum = 0;
    int nonWhite = 0;
    const int total = g.width() * g.height();
    int minV = 255;
    int maxV = 0;
    for (int y = 0; y < g.height(); ++y) {
        for (int x = 0; x < g.width(); ++x) {
            const int v = qGray(g.pixel(x, y));
            sum += static_cast<quint64>(v);
            minV = qMin(minV, v);
            maxV = qMax(maxV, v);
            if (v > 20)
                ++nonWhite;
        }
    }
    return QStringLiteral("%1 %2x%3 min=%4 max=%5 mean=%6 nonWhite=%7")
        .arg(label)
        .arg(g.width())
        .arg(g.height())
        .arg(minV)
        .arg(maxV)
        .arg(total > 0 ? int(sum / quint64(total)) : 0)
        .arg(total > 0 ? QString::number(nonWhite / double(total), 'f', 3) : QStringLiteral("n/a"));
}

static void blitGrayMaskInto(QImage *dest, const QImage &src, QPoint topLeft)
{
    if (!dest || dest->isNull() || src.isNull())
        return;
    const QImage s = src.format() == QImage::Format_Grayscale8
                         ? src
                         : src.convertToFormat(QImage::Format_Grayscale8);
    if (dest->format() != QImage::Format_Grayscale8)
        *dest = dest->convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < s.height(); ++y) {
        const int dy = topLeft.y() + y;
        if (dy < 0 || dy >= dest->height())
            continue;
        uchar *destLine = dest->scanLine(dy);
        const uchar *srcLine = s.constScanLine(y);
        for (int x = 0; x < s.width(); ++x) {
            const int dx = topLeft.x() + x;
            if (dx < 0 || dx >= dest->width())
                continue;
            destLine[dx] = srcLine[x];
        }
    }
}

static QImage grayMaskFromPaintDevice(KisPaintDeviceSP dev, KisImageSP image, const QRect &rect, bool invertSelection,
                                      const QRect &fallbackFillInRect = QRect())
{
    if (!dev || rect.isEmpty())
        return QImage();
    QImage maskImage(rect.width(), rect.height(), QImage::Format_Grayscale8);
    maskImage.fill(0);
    const KoColorProfile *profile = image && image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    QImage slice = dev->convertToQImage(profile, rect.x(), rect.y(), rect.width(), rect.height(),
                                        KoColorConversionTransformation::internalRenderingIntent(),
                                        KoColorConversionTransformation::internalConversionFlags());
    const bool usedConvertToQImage = !slice.isNull();
    if (usedConvertToQImage) {
        for (int y = 0; y < slice.height(); ++y) {
            for (int x = 0; x < slice.width(); ++x) {
                int v = qAlpha(slice.pixel(x, y));
                if (v == 0)
                    v = qGray(slice.pixel(x, y));
                if (invertSelection)
                    v = 255 - v;
                maskImage.setPixel(x, y, qRgb(v, v, v));
            }
        }
    } else {
        const int ps = dev->pixelSize();
        QVector<quint8> data(rect.width() * rect.height() * qMax(1, ps));
        dev->readBytes(data.data(), rect.x(), rect.y(), rect.width(), rect.height());
        for (int y = 0; y < rect.height(); ++y) {
            for (int x = 0; x < rect.width(); ++x) {
                const int srcIdx = (y * rect.width() + x) * qMax(1, ps);
                quint8 v = ps > 0 ? data.value(srcIdx, 0) : 0;
                if (invertSelection)
                    v = 255 - v;
                maskImage.setPixel(x, y, qRgb(v, v, v));
            }
        }
    }

    quint64 sum = 0;
    const int count = rect.width() * rect.height();
    for (int y = 0; y < rect.height(); ++y) {
        for (int x = 0; x < rect.width(); ++x)
            sum += static_cast<quint64>(qGray(maskImage.pixel(x, y)));
    }
    const QString readStats = maskStatsForLog(maskImage, QStringLiteral("read"));
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "INPAINT_DIAG grayMaskFromPaintDevice rect=" << rect << " path="
        << (usedConvertToQImage ? "convertToQImage" : "readBytes") << " sum=" << sum
        << " count=" << count << " " << readStats;

    if (count > 0 && sum < static_cast<quint64>(count) * 8 && !invertSelection) {
        // Match getMaskAsQImage: bounded-box selections may expose selectedExactRect without
        // readable interior bytes (common on Android). Fill only the original selection sub-rect,
        // not the entire padded bounds — filling the whole crop makes ComfyUI return black PNGs.
        QRect fillLocal = fallbackFillInRect.isEmpty()
                              ? QRect(0, 0, rect.width(), rect.height())
                              : fallbackFillInRect.intersected(QRect(0, 0, rect.width(), rect.height()));
        if (fillLocal.isEmpty())
            fillLocal = QRect(0, 0, rect.width(), rect.height());
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "INPAINT_DIAG grayMaskFromPaintDevice: selection pixels unreadable sum=" << sum
            << " count=" << count << " fillLocal=" << fillLocal << " bounds=" << rect;
        QPainter p(&maskImage);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(fillLocal, QColor(255, 255, 255));
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "INPAINT_DIAG grayMaskFromPaintDevice afterFallback " << maskStatsForLog(maskImage, QStringLiteral("fallback"));
    }
    return maskImage;
}

QImage embedGrayMaskInDocument(const QImage &maskGray, const QRect &maskDocBounds, const QRect &docBounds)
{
    QImage full(docBounds.width(), docBounds.height(), QImage::Format_Grayscale8);
    full.fill(0);
    if (maskGray.isNull() || maskDocBounds.isEmpty())
        return full;
    const QPoint offset = maskDocBounds.topLeft() - docBounds.topLeft();
    const QImage src = maskGray.format() == QImage::Format_Grayscale8
                           ? maskGray
                           : maskGray.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const int dy = offset.y() + y;
        if (dy < 0 || dy >= full.height())
            continue;
        uchar *destLine = full.scanLine(dy);
        const uchar *srcLine = src.constScanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const int dx = offset.x() + x;
            if (dx < 0 || dx >= full.width())
                continue;
            destLine[dx] = srcLine[x];
        }
    }
    return full;
}

MaskFromSelectionResult createMaskFromSelection(KisImageSP image, KisViewManager *viewManager,
                                                const SelectionModifiers &mod)
{
    MaskFromSelectionResult out;
    if (!image || !viewManager)
        return out;
    KisSelectionSP sel = viewManager->selection();
    if (!sel || !sel->pixelSelection())
        return out;
    if (isSelectionEntireDocument(image, viewManager))
        return out;

    const QRect docBounds = image->bounds();
    const QRect originalBounds = sel->pixelSelection()->selectedExactRect().intersected(docBounds);
    if (originalBounds.isEmpty())
        return out;

    KisPixelSelectionSP dupPs = new KisPixelSelection(sel->pixelSelection(), KritaUtils::CopySnapshot);

    const double sizeFactor =
        std::hypot(static_cast<double>(originalBounds.width()), static_cast<double>(originalBounds.height()));
    int padPx = qMax(static_cast<int>(mod.featherRel * sizeFactor), mod.featherMinPx);
    padPx += mod.padOffsetPx;
    padPx += static_cast<int>(mod.padRel * sizeFactor);

    if (mod.invert)
        dupPs->invert();

    QRect bounds = dupPs->selectedExactRect().intersected(docBounds);
    if (bounds.isEmpty())
        bounds = originalBounds;
    bounds = padBoundsLikeUpstream(bounds, docBounds, padPx, mod.sizeMinPx, mod.multiple, mod.square);

    // Read only the original selection rect — reading padded bounds via convertToQImage on
    // Android returns opaque white for the entire bounding box, not the selection shape.
    const QRect readRect = originalBounds;
    const QImage coreMask =
        grayMaskFromPaintDevice(dupPs, image, readRect, false, QRect(0, 0, readRect.width(), readRect.height()));
    if (coreMask.isNull())
        return out;

    QImage paddedMask(bounds.width(), bounds.height(), QImage::Format_Grayscale8);
    paddedMask.fill(0);
    const QPoint coreOffset = originalBounds.topLeft() - bounds.topLeft();
    blitGrayMaskInto(&paddedMask, coreMask, coreOffset);

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "INPAINT_DIAG createMaskFromSelection original=" << originalBounds << " padded=" << bounds
        << " coreOffset=" << coreOffset << " " << maskStatsForLog(coreMask, QStringLiteral("core"))
        << " " << maskStatsForLog(paddedMask, QStringLiteral("padded"));

    out.maskGray = paddedMask;
    out.originalBounds = originalBounds;
    out.paddedBounds = bounds;
    out.valid = true;
    return out;
}

QRect computeInpaintDiffusionBounds(int extentWidth, int extentHeight, const QRect &maskPaddedBounds,
                                    bool refineWorkflowKind)
{
    const QRect doc(0, 0, extentWidth, extentHeight);
    if (maskPaddedBounds.isEmpty())
        return doc;
    const QRect mask = maskPaddedBounds.intersected(doc);
    if (mask.isEmpty())
        return doc;
    if (refineWorkflowKind)
        return mask;
    const int longestSide = qMax(extentWidth, extentHeight);
    const int avgSide = (mask.width() + mask.height()) / 2;
    const int contextPadding = qMax(longestSide / 16, avgSide / 2);
    return padBoundsLikeUpstream(mask, doc, contextPadding, 512, 8, true);
}

QRect padMaskBounds(const QRect &bounds, const QRect &docExtent, int padding, int minSize, int multiple, bool square)
{
    return padBoundsLikeUpstream(bounds, docExtent, padding, minSize, multiple, square).intersected(docExtent);
}

SelectionPreProcess calcSelectionPreProcessFromModifiers(const QRect &selectionBounds, int extentWidth,
                                                         int extentHeight, const SelectionModifiers &mods)
{
    SelectionPreProcess out;
    int featherPct = 10;
    getSelectionModifierSettings(&featherPct, nullptr, nullptr);
    if (featherPct <= 0 || selectionBounds.isEmpty())
        return out;

    double diagonal = std::hypot(static_cast<double>(selectionBounds.width()),
                                 static_cast<double>(selectionBounds.height()));
    if (diagonal <= 0.0 && extentWidth > 0 && extentHeight > 0)
        diagonal = std::hypot(static_cast<double>(extentWidth), static_cast<double>(extentHeight));
    if (diagonal <= 0.0)
        return out;

    int feather = static_cast<int>(std::round(mods.featherRel * diagonal));
    if (!mods.invert)
        feather = qMax(feather, mods.featherMinPx);
    feather = clampInpaintGrowFeather(feather);

    int grow = mods.padOffsetPx + feather / 2;
    grow = clampInpaintGrowFeather(grow);

    int blend = qMin(getSelectionBlendPixels(), grow + feather / 2);
    blend = clampInpaintGrowFeather(blend);

    out.grow = grow;
    out.feather = feather;
    out.blend = blend;
    return out;
}

// §13.43: grow from get_selection_modifiers + calc_selection_pre_process (feather_rel × size + feather_min_px; grow = selection_grow_offset + feather/2)
SelectionPreProcess calcSelectionPreProcess(int extentWidth, int extentHeight, int areaWidth, int areaHeight,
                                             double strength0to1, int selectionFeatherPercent,
                                             double selectionMinTransition, int selectionGrowOffset,
                                             int selectionBlendPixels, bool invertSelection)
{
    SelectionPreProcess out;
    if (selectionFeatherPercent <= 0)
        return out;

    double diagonal = 0.0;
    if (areaWidth > 0 && areaHeight > 0)
        diagonal = std::sqrt(static_cast<double>(areaWidth) * areaWidth + static_cast<double>(areaHeight) * areaHeight);
    if (diagonal <= 0.0 && extentWidth > 0 && extentHeight > 0)
        diagonal = std::sqrt(static_cast<double>(extentWidth) * extentWidth + static_cast<double>(extentHeight) * extentHeight);
    if (diagonal <= 0.0)
        return out;

    const double featherRel = (selectionFeatherPercent / 100.0) * strength0to1;
    int feather = static_cast<int>(std::round(featherRel * diagonal));
    const int featherMinPx = static_cast<int>(std::round(selectionMinTransition * strength0to1));
    if (!invertSelection)
        feather = qMax(feather, featherMinPx);
    feather = clampInpaintGrowFeather(feather);

    int grow = selectionGrowOffset + feather / 2;
    grow = clampInpaintGrowFeather(grow);

    int blend = qMin(selectionBlendPixels, grow + feather / 2);
    blend = clampInpaintGrowFeather(blend);

    out.grow = grow;
    out.feather = feather;
    out.blend = blend;
    return out;
}

int getSelectionBlendPixels()
{
    return qBound(0, loadSettingsJson().value(QStringLiteral("selection_blend")).toInt(25), 100);
}


} // namespace ComfyUIUtils
