/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyRegionProcess.h"
#include "ComfyRegionLink.h"

#include "ComfyWorkflowEngine.h"
#include "ComfyUIUtils.h"

#include <QtMath>
#include <QUuid>

#include <kis_image.h>
#include <kis_layer.h>
#include <KisViewManager.h>

#include "ComfyUIRemoteDockPrivate.h"

namespace ComfyRegionProcess {

double maskAverage(const QImage &maskGray)
{
    if (maskGray.isNull() || maskGray.width() == 0 || maskGray.height() == 0)
        return 0.0;
    QImage m = maskGray.format() == QImage::Format_Grayscale8
                   ? maskGray
                   : maskGray.convertToFormat(QImage::Format_Grayscale8);
    quint64 sum = 0;
    const int n = m.width() * m.height();
    for (int y = 0; y < m.height(); y++) {
        const uchar *line = m.constScanLine(y);
        for (int x = 0; x < m.width(); x++)
            sum += line[x];
    }
    return static_cast<double>(sum) / (n * 255.0);
}

QImage maskSubtract(const QImage &lhs, const QImage &rhs)
{
    if (lhs.isNull())
        return QImage();
    QImage l = lhs.convertToFormat(QImage::Format_Grayscale8);
    if (rhs.isNull())
        return l;
    QImage r = rhs.convertToFormat(QImage::Format_Grayscale8);
    if (r.size() != l.size())
        r = r.scaled(l.size());
    QImage out(l.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < l.height(); y++) {
        for (int x = 0; x < l.width(); x++) {
            const int v = qMax(0, qGray(l.pixel(x, y)) - qGray(r.pixel(x, y)));
            out.setPixel(x, y, qRgb(v, v, v));
        }
    }
    return out;
}

QImage maskAdd(const QImage &lhs, const QImage &rhs)
{
    if (lhs.isNull())
        return rhs;
    if (rhs.isNull())
        return lhs;
    QImage a = lhs.convertToFormat(QImage::Format_Grayscale8);
    QImage b = rhs.convertToFormat(QImage::Format_Grayscale8);
    if (a.size() != b.size())
        b = b.scaled(a.size());
    QImage out(a.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            const int v = qMax(qGray(a.pixel(x, y)), qGray(b.pixel(x, y)));
            out.setPixel(x, y, qRgb(v, v, v));
        }
    }
    return out;
}

QImage maskInvert(const QImage &maskGray)
{
    QImage m = maskGray.convertToFormat(QImage::Format_Grayscale8);
    QImage out(m.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < m.height(); y++) {
        for (int x = 0; x < m.width(); x++) {
            const int v = 255 - qGray(m.pixel(x, y));
            out.setPixel(x, y, qRgb(v, v, v));
        }
    }
    return out;
}

QRect maskNonZeroBounds(const QImage &maskGray)
{
    QRect bounds;
    const QImage m = maskGray.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < m.height(); y++) {
        for (int x = 0; x < m.width(); x++) {
            if (qGray(m.pixel(x, y)) > 0)
                bounds = bounds.united(QRect(x, y, 1, 1));
        }
    }
    return bounds;
}

ProcessRegionsResult processRegions(const QList<ComfyRegionEntry> &entries,
                                    KisImageSP image,
                                    KisViewManager *viewManager,
                                    const QString &rootPositive,
                                    double minCoverage)
{
    ProcessRegionsResult result;
    result.effectivePositive = rootPositive;
    if (!image || entries.isEmpty())
        return result;

    const QRect docBounds = image->bounds();
    const int docArea = qMax(1, docBounds.width() * docBounds.height());

    struct RawRegion {
        ComfyRegionEntry entry;
        QImage originalMask;
        QString mergedPrompt;
    };
    QList<RawRegion> raw;

    for (const ComfyRegionEntry &e : entries) {
        const QString maskSrc = ComfyRegionLink::effectiveMaskSource(e, image);
        QImage maskImg = ComfyUIUtils::getMaskAsQImage(image, viewManager, maskSrc);
        if (maskImg.isNull())
            continue;
        const QRect layerBounds = maskNonZeroBounds(maskImg);
        if (layerBounds.isEmpty())
            continue;
        const QRect inter = layerBounds.intersected(docBounds);
        const double coverageRough = inter.isEmpty() ? 0.0 : static_cast<double>(inter.width() * inter.height()) / docArea;
        if (coverageRough < 2.0 * minCoverage)
            continue;

        RawRegion r;
        r.entry = e;
        r.originalMask = maskImg;
        r.mergedPrompt = ComfyUIUtils::mergeStylePromptWithInstruction(rootPositive, e.prompt).trimmed();
        if (r.mergedPrompt.isEmpty())
            r.mergedPrompt = rootPositive;
        raw.append(r);
    }

    if (raw.isEmpty())
        return result;

    QImage accumulated;
    for (int i = raw.size() - 1; i >= 0; i--) {
        QImage mask = raw.at(i).originalMask;
        if (!accumulated.isNull())
            mask = maskSubtract(mask, accumulated);

        const double coverage = maskAverage(mask);
        if (coverage > 0.9 && minCoverage > 0.0) {
            result.mode = ProcessRegionsResult::Mode::SingleRegion;
            result.effectivePositive = raw.at(i).mergedPrompt;
            result.regions.clear();
            return result;
        }
        if (coverage < minCoverage) {
            raw.removeAt(i);
            continue;
        }
        if (accumulated.isNull())
            accumulated = raw.at(i).originalMask;
        else
            accumulated = maskAdd(accumulated, raw.at(i).originalMask);
        raw[i].originalMask = mask;
    }

    if (raw.isEmpty())
        return result;

    QList<ProcessedRegionEntry> processed;
    for (const RawRegion &r : raw) {
        ProcessedRegionEntry pe;
        pe.prompt = r.mergedPrompt;
        pe.maskGray = r.originalMask;
        processed.append(pe);
    }

    const double totalCoverage = maskAverage(accumulated);
    if (totalCoverage < 0.95) {
        ProcessedRegionEntry bg;
        bg.prompt = rootPositive;
        bg.maskGray = maskInvert(accumulated);
        bg.isBackground = true;
        processed.prepend(bg);
    }

    if (processed.size() < 2) {
        if (processed.size() == 1) {
            result.mode = ProcessRegionsResult::Mode::SingleRegion;
            result.effectivePositive = processed.first().prompt;
        }
        return result;
    }

    result.mode = ProcessRegionsResult::Mode::MultiRegion;
    result.regions = processed;
    return result;
}

QList<ComfyWorkflowEngine::RegionalPromptInput> toRegionalWorkflowInputs(const QList<ProcessedRegionEntry> &regions,
                                                                         const QString &promptTranslationLanguage)
{
    QList<ComfyWorkflowEngine::RegionalPromptInput> out;
    for (const ProcessedRegionEntry &pe : regions) {
        ComfyWorkflowEngine::RegionalPromptInput r;
        r.positivePrompt = pe.prompt;
        r.isBackground = pe.isBackground;
        r.promptTranslationLanguage = promptTranslationLanguage;
        out.append(r);
    }
    return out;
}

static bool isLayerLinkedToAnyRegionEntry(const QList<ComfyRegionEntry> &regions,
                                          KisImageSP image,
                                          KisLayerSP layer)
{
    ComfyUIRemoteDock::Private::RegionEntry bridge;
    for (const ComfyRegionEntry &entry : regions) {
        bridge.layerIds = entry.layerIds;
        if (ComfyRegionLink::isLayerLinkedToRegion(bridge, image, layer, ComfyRegionLink::LinkMode::Any))
            return true;
    }
    return false;
}

KisLayerSP resolveActiveRegionLayer(KisImageSP image,
                                    KisViewManager *viewManager,
                                    const QList<ComfyRegionEntry> &regions,
                                    bool regionOnly)
{
    if (!image || !viewManager)
        return KisLayerSP();

    KisLayerSP active = viewManager->activeLayer();
    if (!active)
        return KisLayerSP();

    KisLayerSP result = ComfyRegionLink::linkTarget(active);
    if (!isLayerLinkedToAnyRegionEntry(regions, image, result))
        return KisLayerSP();

    const bool useParent = !regionOnly;
    if (useParent) {
        KisNodeSP parent = result->parent();
        if (parent) {
            if (KisLayerSP parentLayer = dynamic_cast<KisLayer *>(parent.data()))
                result = parentLayer;
        }
    }

    // Upstream `Layer.is_root`: node has no parent.
    if (!result || !result->parent())
        return KisLayerSP();
    return result;
}

RegionInpaintMask getRegionInpaintMask(KisImageSP image,
                                       KisViewManager *viewManager,
                                       KisLayerSP regionLayer,
                                       int minSize)
{
    RegionInpaintMask result;
    if (!image || !viewManager || !regionLayer)
        return result;
    ComfyRegionEntry entry;
    entry.layerIds = regionLayer->uuid().toString(QUuid::WithoutBraces);
    entry.maskSource = ComfyRegionLink::maskSourceForLayer(regionLayer);
    return getRegionInpaintMask(image, viewManager, entry, minSize);
}

RegionInpaintMask getRegionInpaintMask(KisImageSP image,
                                       KisViewManager *viewManager,
                                       const ComfyRegionEntry &region,
                                       int minSize)
{
    RegionInpaintMask result;
    if (!image)
        return result;

    const QString maskSrc = ComfyRegionLink::effectiveMaskSource(region, image);
    QImage maskImg = ComfyUIUtils::getMaskAsQImage(image, viewManager, maskSrc);
    if (maskImg.isNull())
        return result;

    const QRect doc = image->bounds();
    QRect regionBounds = maskNonZeroBounds(maskImg);
    if (region.layerIds.isEmpty() == false) {
        KisLayerSP linkedLayer;
        const QString layerId = region.layerIds.split(QLatin1Char(','), Qt::SkipEmptyParts).value(0).trimmed();
        if (!layerId.isEmpty()) {
            const QUuid uid = QUuid::fromString(layerId);
            if (KisLayerSP layer = ComfyRegionLink::findLayerByUuid(image, uid))
                linkedLayer = layer;
        }
        if (linkedLayer) {
            const QRect layerR = linkedLayer->exactBounds() & doc;
            if (!layerR.isEmpty())
                regionBounds = layerR;
        }
    }
    if (regionBounds.isEmpty())
        return result;

    const double avgSide = (regionBounds.width() + regionBounds.height()) / 2.0;
    const int padding = qMax(1, static_cast<int>(std::round(avgSide * ComfyUIUtils::getSelectionPaddingPercent() / 100.0)));
    QRect bounds = regionBounds;
    if (minSize > 0) {
        bounds = ComfyUIUtils::padMaskBounds(bounds, doc, padding, minSize, 16, true);
    } else {
        bounds = bounds.adjusted(-padding, -padding, padding, padding).intersected(doc);
    }
    if (bounds.isEmpty())
        return result;

    const QImage cropped = ComfyUIUtils::cropImageToDocumentRect(maskImg, bounds, doc);
    QImage fullMask(doc.width(), doc.height(), QImage::Format_Grayscale8);
    fullMask.fill(0);
    if (!cropped.isNull()) {
        for (int y = 0; y < cropped.height(); ++y) {
            for (int x = 0; x < cropped.width(); ++x) {
                const int gx = bounds.x() - doc.x() + x;
                const int gy = bounds.y() - doc.y() + y;
                if (gx >= 0 && gy >= 0 && gx < fullMask.width() && gy < fullMask.height())
                    fullMask.setPixel(gx, gy, cropped.pixel(x, y));
            }
        }
    }
    result.maskGray = fullMask;
    result.bounds = bounds;
    result.valid = !result.maskGray.isNull();
    return result;
}

} // namespace ComfyRegionProcess
