/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_REGION_PROCESS_H_
#define COMFY_REGION_PROCESS_H_

#include <QImage>
#include <QList>
#include <QString>

#include <kis_types.h>

#include "ComfyRegionEntry.h"
#include "ComfyWorkflowEngine.h"

class KisViewManager;

namespace ComfyRegionProcess {

struct ProcessedRegionEntry {
    QString prompt;
    QImage maskGray;
    bool isBackground = false;
};

struct ProcessRegionsResult {
    enum class Mode { RootOnly, SingleRegion, MultiRegion };
    Mode mode = Mode::RootOnly;
    QString effectivePositive;
    QList<ProcessedRegionEntry> regions;
};

/// Port of ai_diffusion/region.py process_regions() for dock RegionEntry list (layer order = top to bottom).
ProcessRegionsResult processRegions(const QList<ComfyRegionEntry> &entries,
                                   KisImageSP image,
                                   KisViewManager *viewManager,
                                   const QString &rootPositive,
                                   double minCoverage = 0.02);

QList<ComfyWorkflowEngine::RegionalPromptInput> toRegionalWorkflowInputs(
    const QList<ProcessedRegionEntry> &regions,
    const QString &promptTranslationLanguage = QString());

double maskAverage(const QImage &maskGray);
QImage maskSubtract(const QImage &lhs, const QImage &rhs);
QImage maskAdd(const QImage &lhs, const QImage &rhs);
QImage maskInvert(const QImage &maskGray);
QRect maskNonZeroBounds(const QImage &maskGray);

/// §13.206: Region-only inpaint mask from active region layer transparency (get_region_inpaint_mask).
struct RegionInpaintMask {
    QImage maskGray;
    QRect bounds;
    bool valid = false;
};
RegionInpaintMask getRegionInpaintMask(KisImageSP image,
                                       KisViewManager *viewManager,
                                       const ComfyRegionEntry &region,
                                       int minSize = 0);
RegionInpaintMask getRegionInpaintMask(KisImageSP image,
                                       KisViewManager *viewManager,
                                       KisLayerSP regionLayer,
                                       int minSize = 0);

/// Port of `RootRegion.get_active_region_layer(use_parent=not region_only)`.
/// Returns null when the active canvas layer is not linked to a region (upstream `is_root`).
KisLayerSP resolveActiveRegionLayer(KisImageSP image,
                                    KisViewManager *viewManager,
                                    const QList<ComfyRegionEntry> &regions,
                                    bool regionOnly);

} // namespace ComfyRegionProcess

#endif
