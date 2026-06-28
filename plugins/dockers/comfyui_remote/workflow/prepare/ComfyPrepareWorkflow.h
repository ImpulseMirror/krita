/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PREPARE_WORKFLOW_H_
#define COMFY_PREPARE_WORKFLOW_H_

#include <QImage>
#include <QList>
#include <QRect>
#include <QString>

#include <kis_types.h>

#include "ComfyControlLayer.h"
#include "ComfyRegionEntry.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyUIUtils.h"

class KisViewManager;

namespace ComfyPrepareWorkflow {

/// Upstream `WorkflowKind` at `workflow.prepare` time (after mask promotion).
enum class WorkflowKind {
    Generate,
    Refine,
    Inpaint,
    RefineRegion,
};

struct PrepareFlags {
    bool requireMask = true;
    bool captureImage = true;
    bool isLive = false;
};

struct Input {
    KisImageSP image;
    KisViewManager *viewManager = nullptr;
    QString modifierMode = QStringLiteral("automatic");
    QString contextKey = QStringLiteral("automatic");
    QString contextLayerId;
    QString checkpoint;
    QString styleArch;
    QString rootPositivePrompt;
    double strength0to1 = 1.0;
    bool editMode = false;
    bool regionOnly = false;
    int activeRegionRow = -1;
    QList<ComfyRegionEntry> activeRegions;
    QList<ComfyControlLayerEntry> rootControlLayers;
    QList<ComfyControlLayerEntry> jobControlLayers;
    QString previewLayerId;
    bool persistUseInpaintModel = true;
    bool persistUsePromptFocus = false;
    QString customFillKind;
};

struct Result {
    bool ok = false;
    QString errorMessage;
    WorkflowKind workflowKind = WorkflowKind::Generate;
    bool hasMask = false;
    bool fromRegionLayer = false;
    QString modifierMode;
    QString effectiveInpaintMode;
    QRect selectionOriginalBounds;
    QRect maskPaddedBounds;
    QRect contextBounds;
    QRect targetBoundsRelative;
    /// Mask-padded bounds in native context coords (before diffusion upscale).
    QRect nativeTargetBoundsRelative;
    QImage maskFullDoc;
    QImage contextImage;
    QImage nativeContextImage;
    QImage nativeCompositingMask;
    QImage compositingMaskCropped;
    ComfyUIUtils::SelectionModifiers selectionMods;
    ComfyUIUtils::SelectionPreProcess preprocess;
    ComfyUIUtils::InpaintParams inpaintParams;
    QString fillKind;
    ComfyRegionProcess::ProcessRegionsResult processedRegions;
    QString effectivePositivePrompt;
    bool useProcessedPositive = false;
    QSize nativeContextSize;
    QSize diffusionExtent;
    ComfyResources::Arch arch = ComfyResources::Arch::Sdxl;
    double strength0to1 = 1.0;
    int minMaskSize = 800;
};

/// Unified `_prepare_workflow` / `_prepare_live_workflow` entry (flags.isLive selects live path).
Result prepare(const Input &input, const PrepareFlags &flags);

} // namespace ComfyPrepareWorkflow

#endif
