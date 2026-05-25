/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyRegionProcess.h"
#include "ComfyRegionLink.h"

#include "ComfyWorkflowEngine.h"
#include "ComfyUIUtils.h"

#include <QPainter>
#include <QtMath>

#include <kis_image.h>
#include <KisViewManager.h>

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
    QImage out = lhs.convertToFormat(QImage::Format_ARGB32);
    if (!rhs.isNull()) {
        QImage r = rhs.convertToFormat(QImage::Format_ARGB32);
        if (r.size() != out.size())
            r = r.scaled(out.size());
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_SourceOut);
        p.drawImage(0, 0, r);
    }
    QImage gray(out.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            const int a = qAlpha(out.pixel(x, y));
            gray.setPixel(x, y, qRgb(a, a, a));
        }
    }
    return gray;
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

ProcessRegionsResult processRegions(const QList<ComfyUIRemoteDock::Private::RegionEntry> &entries,
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
        ComfyUIRemoteDock::Private::RegionEntry entry;
        QImage originalMask;
        QString mergedPrompt;
    };
    QList<RawRegion> raw;

    for (const ComfyUIRemoteDock::Private::RegionEntry &e : entries) {
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

} // namespace ComfyRegionProcess
