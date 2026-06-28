/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUpscaleRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyRegionLink.h"
#include "ComfyRegionProcess.h"

#include <QImage>
#include <QtGlobal>

namespace ComfyUpscaleRunnerInternal {



QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForUpscale(const ComfyUIRemoteDock::Private *d)
{
    QList<ComfyUIRemoteDock::Private::RegionEntry> regs = comfyActiveRegionEntries(d);
    if (d->generate.checkRegionOnly && d->generate.checkRegionOnly->isChecked()) {
        const int row = comfyActiveRegionRow(d);
        if (row >= 0 && row < regs.size())
            return {regs.at(row)};
    }
    return regs;
}

QList<ComfyControlLayerEntry> controlLayersForUpscale(const ComfyUIRemoteDock::Private *d)
{
    return mergedJobControlLayers(d->rootControlLayers, regionsForUpscale(d));
}

QImage maskPngForComfyUpload(const QImage &maskGray, int targetW, int targetH)
{
    QImage scaled = maskGray;
    if (targetW > 0 && targetH > 0
        && (scaled.width() != targetW || scaled.height() != targetH)) {
        scaled = scaled.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage maskPng(scaled.size(), QImage::Format_ARGB32);
    for (int y = 0; y < scaled.height(); y++) {
        for (int x = 0; x < scaled.width(); x++) {
            const int g = qGray(scaled.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    }
    return maskPng;
}

} // namespace ComfyUpscaleRunnerInternal