/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPrepareWorkflow.h"

#include "ComfyControlLayer.h"
#include "ComfyLocalization.h"
#include "ComfyWorkflowEngine.h"

#include <kis_selection.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <KisViewManager.h>

#include <QtMath>

namespace ComfyPrepareWorkflow {

namespace {

int liveMinMaskSizeForArch(ComfyResources::Arch arch)
{
    return arch == ComfyResources::Arch::Sd15 ? 512 : 800;
}

ComfyUIUtils::SelectionPreProcess regionLayerInpaintPreprocess(const QRect &maskBounds, const QRect &layerBounds)
{
    ComfyUIUtils::SelectionPreProcess out;
    const int freeW = qMax(0, maskBounds.width() - layerBounds.width());
    const int freeH = qMax(0, maskBounds.height() - layerBounds.height());
    const int freeShort = qMin(freeW, freeH);
    out.grow = qBound(8, freeShort / 2, 128);
    out.feather = out.grow / 2;
    out.blend = qMax(out.feather / 2, 15);
    return out;
}

Result prepareGenerate(const Input &input, const PrepareFlags &flags)
{
    Result out;
    KisImageSP image = input.image;
    if (!image || !input.viewManager) {
        out.errorMessage = ComfyTr::tr("Open a document first.");
        return out;
    }

    const int extentW = image->width();
    const int extentH = image->height();
    const QRect docBounds = image->bounds();
    if (docBounds.isEmpty()) {
        out.errorMessage = ComfyTr::tr("Could not export canvas.");
        return out;
    }

    out.modifierMode = input.modifierMode.trimmed().isEmpty() ? QStringLiteral("automatic") : input.modifierMode.trimmed();
    out.strength0to1 = input.strength0to1;
    out.arch = ComfyWorkflowEngine::resolveArch(input.checkpoint, input.styleArch);
    if (out.arch == ComfyResources::Arch::QwenL)
        out.strength0to1 = 1.0;

    const bool refineInitial = out.strength0to1 < 1.0 || input.editMode;
    out.workflowKind = refineInitial ? WorkflowKind::Refine : WorkflowKind::Generate;

    QRect maskPaddedBounds;
    QRect selectionOriginalBounds;
    QImage maskFullDoc;
    KisSelectionSP sel = input.viewManager->selection();
    const bool hasPartialSelection =
        sel && sel->pixelSelection() && !sel->pixelSelection()->selectedExactRect().isEmpty()
        && !ComfyUIUtils::isSelectionEntireDocument(image, input.viewManager);

    if (hasPartialSelection) {
        out.selectionMods = ComfyUIUtils::getSelectionModifiers(ComfyResources::archToKey(out.arch), out.modifierMode,
                                                                out.strength0to1);
        const ComfyUIUtils::MaskFromSelectionResult maskResult =
            ComfyUIUtils::createMaskFromSelection(image, input.viewManager, out.selectionMods);
        if (!maskResult.valid) {
            out.errorMessage =
                ComfyTr::tr("Make a selection or paint on a layer linked to a region.");
            return out;
        }
        maskPaddedBounds = maskResult.paddedBounds;
        selectionOriginalBounds = maskResult.originalBounds;
        maskFullDoc = ComfyUIUtils::embedGrayMaskInDocument(maskResult.maskGray, maskPaddedBounds, docBounds);
        out.hasMask = true;
    } else {
        const KisLayerSP regionLayer = ComfyRegionProcess::resolveActiveRegionLayer(
            image, input.viewManager, input.activeRegions, input.regionOnly);
        if (regionLayer) {
            const ComfyRegionProcess::RegionInpaintMask rim =
                ComfyRegionProcess::getRegionInpaintMask(image, input.viewManager, regionLayer);
            if (rim.valid) {
                maskFullDoc = rim.maskGray;
                maskPaddedBounds = rim.bounds;
                selectionOriginalBounds = rim.bounds;
                out.hasMask = true;
                out.fromRegionLayer = true;
            }
        }
    }

    if (!out.hasMask || maskFullDoc.isNull() || maskPaddedBounds.isEmpty()) {
        if (flags.requireMask) {
            out.errorMessage =
                ComfyTr::tr("Make a selection or paint on a layer linked to a region.");
            return out;
        }
        if (!refineInitial) {
            out.errorMessage = ComfyTr::tr("Could not prepare refine workflow.");
            return out;
        }
        out.contextBounds = docBounds;
        out.processedRegions =
            ComfyRegionProcess::processRegions(input.activeRegions, image, input.viewManager, input.rootPositivePrompt);
        out.effectivePositivePrompt = out.processedRegions.effectivePositive;
        out.useProcessedPositive =
            out.processedRegions.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion;
        if (flags.captureImage) {
            const QList<KisNodeSP> excludeNodes = ComfyUIUtils::collectInpaintExcludeNodes(
                image, true, input.rootControlLayers, input.previewLayerId);
            const auto docCapture = ComfyUIUtils::getDocumentImage(image, docBounds, excludeNodes);
            if (!docCapture) {
                out.errorMessage = docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas.")
                                                                     : docCapture.errorMessage;
                return out;
            }
            out.contextImage = docCapture.image.convertToFormat(QImage::Format_ARGB32);
        }
        out.nativeContextSize = out.contextImage.size();
        out.diffusionExtent = out.nativeContextSize;
        out.ok = true;
        return out;
    }

    out.maskPaddedBounds = maskPaddedBounds;
    out.selectionOriginalBounds = selectionOriginalBounds;
    out.maskFullDoc = maskFullDoc;

    if (out.fromRegionLayer)
        out.effectiveInpaintMode = QStringLiteral("add_object");
    else
        out.effectiveInpaintMode =
            ComfyUIUtils::resolveInpaintMode(out.modifierMode, extentW, extentH, selectionOriginalBounds);

    QRect contextBounds;
    if (out.fromRegionLayer) {
        contextBounds = maskPaddedBounds;
    } else {
        contextBounds =
            ComfyUIUtils::computeInpaintDiffusionBounds(extentW, extentH, maskPaddedBounds, refineInitial);
        if (out.modifierMode == QLatin1String("custom")) {
            if (const std::optional<QRect> customCtx = ComfyUIUtils::customInpaintGetContext(
                    image, input.contextKey, input.contextLayerId, maskPaddedBounds))
                contextBounds = *customCtx;
        }
    }
    out.contextBounds = contextBounds;

    out.processedRegions =
        ComfyRegionProcess::processRegions(input.activeRegions, image, input.viewManager, input.rootPositivePrompt);
    out.effectivePositivePrompt = out.processedRegions.effectivePositive;
    out.useProcessedPositive =
        out.processedRegions.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion;

    const bool needsCapture = out.hasMask || refineInitial;
    if (needsCapture && flags.captureImage) {
        const QRect captureRect = contextBounds.isEmpty() ? docBounds : contextBounds;
        const QList<KisNodeSP> excludeNodes = ComfyUIUtils::collectInpaintExcludeNodes(
            image, true, input.rootControlLayers, input.previewLayerId);
        const auto docCapture = ComfyUIUtils::getDocumentImage(image, captureRect, excludeNodes);
        if (!docCapture) {
            out.errorMessage = docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas.")
                                                                 : docCapture.errorMessage;
            return out;
        }
        out.contextImage = docCapture.image.convertToFormat(QImage::Format_ARGB32);
    }

    if (out.hasMask) {
        if (out.workflowKind == WorkflowKind::Generate)
            out.workflowKind = WorkflowKind::Inpaint;
        else if (out.workflowKind == WorkflowKind::Refine)
            out.workflowKind = WorkflowKind::RefineRegion;

        out.targetBoundsRelative = maskPaddedBounds.translated(-contextBounds.topLeft());
        out.nativeTargetBoundsRelative = out.targetBoundsRelative;
    }

    out.compositingMaskCropped =
        ComfyUIUtils::cropImageToDocumentRect(maskFullDoc, contextBounds, docBounds);
    if (flags.captureImage
        && (out.contextImage.isNull() || out.compositingMaskCropped.isNull())) {
        out.errorMessage = ComfyTr::tr("Could not crop inpaint context.");
        return out;
    }
    if (flags.captureImage
        && ComfyRegionProcess::maskAverage(out.compositingMaskCropped) < 0.001) {
        out.errorMessage = ComfyTr::tr("Selection mask is empty. Re-select the area and try again.");
        return out;
    }

    const QString archKey = ComfyUIUtils::classifyCheckpointArch(input.checkpoint);
    const bool positiveEmpty = input.rootPositivePrompt.trimmed().isEmpty();
    const bool hasStructuralControl = ComfyControlLayer::hasStructuralControlAmong(
        input.jobControlLayers.isEmpty() ? input.rootControlLayers : input.jobControlLayers);
    if (out.modifierMode == QLatin1String("custom")) {
        out.inpaintParams = ComfyUIUtils::customInpaintGetParams(
            input.customFillKind, input.persistUseInpaintModel, input.persistUsePromptFocus, input.editMode);
        out.fillKind = out.inpaintParams.fillKind;
    } else {
        out.inpaintParams = ComfyUIUtils::detectInpaintParams(out.effectiveInpaintMode, archKey, out.strength0to1,
                                                              positiveEmpty, hasStructuralControl, input.editMode);
        if (!input.persistUseInpaintModel)
            out.inpaintParams.useInpaintModel = false;
        out.inpaintParams.useConditionMask =
            out.inpaintParams.useConditionMask || input.persistUsePromptFocus;
        out.fillKind = out.inpaintParams.fillKind;
    }

    if (out.modifierMode != QLatin1String("custom")
        && ComfyResources::isEditArch(ComfyResources::archFromKey(archKey))) {
        out.effectiveInpaintMode = QStringLiteral("custom");
    }

    if (out.fromRegionLayer) {
        const ComfyUIUtils::SelectionModifiers regionMods =
            ComfyUIUtils::getSelectionModifiers(archKey, QStringLiteral("add_object"), out.strength0to1);
        out.preprocess = ComfyUIUtils::calcSelectionPreProcessFromModifiers(selectionOriginalBounds, extentW, extentH,
                                                                          regionMods);
    } else {
        out.preprocess = ComfyUIUtils::calcSelectionPreProcessFromModifiers(selectionOriginalBounds, extentW, extentH,
                                                                          out.selectionMods);
    }

    out.nativeContextSize = out.contextImage.size();
    out.nativeContextImage = out.contextImage;
    out.nativeCompositingMask = out.compositingMaskCropped;
    out.diffusionExtent = out.nativeContextSize;
    const bool scaleDiffusionInput =
        (out.workflowKind == WorkflowKind::RefineRegion || out.workflowKind == WorkflowKind::Inpaint)
        && out.nativeContextSize.isValid();
    if (scaleDiffusionInput) {
        const ComfyUIUtils::DiffusionPreparedExtent prep =
            ComfyUIUtils::prepareDiffusionInputExtent(out.nativeContextSize, out.arch);
        out.diffusionExtent = prep.initial;
        if (flags.captureImage && prep.initial.isValid() && prep.initial != out.nativeContextSize) {
            const double sx = static_cast<double>(prep.initial.width()) / out.nativeContextSize.width();
            const double sy = static_cast<double>(prep.initial.height()) / out.nativeContextSize.height();
            out.contextImage = out.contextImage.scaled(prep.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            out.compositingMaskCropped = out.compositingMaskCropped.scaled(
                prep.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (!out.targetBoundsRelative.isEmpty()) {
                out.targetBoundsRelative = QRect(qRound(out.targetBoundsRelative.x() * sx),
                                                 qRound(out.targetBoundsRelative.y() * sy),
                                                 qRound(out.targetBoundsRelative.width() * sx),
                                                 qRound(out.targetBoundsRelative.height() * sy));
            }
        }
    }

    out.ok = true;
    return out;
}

Result prepareLive(const Input &input, const PrepareFlags &flags)
{
    Q_UNUSED(flags);
    Result out;
    KisImageSP image = input.image;
    if (!image || !input.viewManager) {
        out.errorMessage = ComfyTr::tr("Open a document first.");
        return out;
    }

    const QRect docBounds = image->bounds();
    if (docBounds.isEmpty()) {
        out.errorMessage = ComfyTr::tr("Could not export canvas.");
        return out;
    }

    out.strength0to1 = input.strength0to1;
    out.arch = ComfyWorkflowEngine::resolveArch(input.checkpoint, input.styleArch);
    if (out.arch == ComfyResources::Arch::QwenL)
        out.strength0to1 = 1.0;

    const bool refineInitial = out.strength0to1 < 1.0 || input.editMode;
    out.workflowKind = refineInitial ? WorkflowKind::Refine : WorkflowKind::Generate;

    out.minMaskSize = liveMinMaskSizeForArch(out.arch);
    const QString archKey = ComfyResources::archToKey(out.arch);

    const bool positiveEmpty = input.rootPositivePrompt.trimmed().isEmpty();
    out.inpaintParams = ComfyUIUtils::detectInpaintParams(QStringLiteral("fill"), archKey, out.strength0to1,
                                                          positiveEmpty, false, input.editMode);

    out.selectionMods =
        ComfyUIUtils::getSelectionModifiers(archKey, QStringLiteral("fill"), out.strength0to1, out.minMaskSize);

    QRect maskPaddedBounds;
    QRect selectionOriginalBounds;
    QImage maskFullDoc;
    KisSelectionSP sel = input.viewManager->selection();
    const bool hasPartialSelection =
        sel && sel->pixelSelection() && !sel->pixelSelection()->selectedExactRect().isEmpty()
        && !ComfyUIUtils::isSelectionEntireDocument(image, input.viewManager);

    if (hasPartialSelection) {
        const ComfyUIUtils::MaskFromSelectionResult maskResult =
            ComfyUIUtils::createMaskFromSelection(image, input.viewManager, out.selectionMods);
        if (!maskResult.valid) {
            out.errorMessage =
                ComfyTr::tr("Make a selection or paint on a layer linked to a region.");
            return out;
        }
        maskPaddedBounds = maskResult.paddedBounds;
        selectionOriginalBounds = maskResult.originalBounds;
        maskFullDoc = ComfyUIUtils::embedGrayMaskInDocument(maskResult.maskGray, maskPaddedBounds, docBounds);
        out.hasMask = true;
        out.preprocess =
            ComfyUIUtils::calcSelectionPreProcessFromModifiers(selectionOriginalBounds, image->width(), image->height(),
                                                               out.selectionMods);
    } else {
        const KisLayerSP regionLayer = ComfyRegionProcess::resolveActiveRegionLayer(
            image, input.viewManager, input.activeRegions, true);
        const QRect layerBounds = regionLayer ? (regionLayer->exactBounds() & docBounds) : QRect();
        if (regionLayer && layerBounds.isValid() && layerBounds != docBounds) {
            const ComfyRegionProcess::RegionInpaintMask rim =
                ComfyRegionProcess::getRegionInpaintMask(image, input.viewManager, regionLayer, out.minMaskSize);
            if (rim.valid) {
                maskFullDoc = rim.maskGray;
                maskPaddedBounds = rim.bounds;
                selectionOriginalBounds = rim.bounds;
                out.hasMask = true;
                out.fromRegionLayer = true;
                out.preprocess = regionLayerInpaintPreprocess(maskPaddedBounds, layerBounds);
            }
        }
    }

    out.maskPaddedBounds = maskPaddedBounds;
    out.selectionOriginalBounds = selectionOriginalBounds;
    out.maskFullDoc = maskFullDoc;

    if (out.hasMask)
        out.workflowKind = WorkflowKind::RefineRegion;

    QRect contextBounds;
    if (out.hasMask) {
        if (out.fromRegionLayer)
            contextBounds = maskPaddedBounds;
        else
            contextBounds = ComfyUIUtils::computeInpaintDiffusionBounds(docBounds.width(), docBounds.height(),
                                                                          maskPaddedBounds, refineInitial);
    } else {
        contextBounds = docBounds;
    }
    out.contextBounds = contextBounds;

    out.processedRegions =
        ComfyRegionProcess::processRegions(input.activeRegions, image, input.viewManager, input.rootPositivePrompt);
    out.effectivePositivePrompt = out.processedRegions.effectivePositive;
    out.useProcessedPositive =
        out.processedRegions.mode == ComfyRegionProcess::ProcessRegionsResult::Mode::SingleRegion;

    const bool needsCapture = out.hasMask || refineInitial;
    if (needsCapture) {
        const QRect captureRect = contextBounds.isEmpty() ? docBounds : contextBounds;
        const auto docCapture = ComfyUIUtils::getDocumentImage(image, captureRect, {});
        if (!docCapture) {
            out.errorMessage = docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas.")
                                                                 : docCapture.errorMessage;
            return out;
        }
        out.contextImage = docCapture.image.convertToFormat(QImage::Format_ARGB32);
    }

    if (out.hasMask) {
        out.targetBoundsRelative = maskPaddedBounds.translated(-contextBounds.topLeft());
        out.nativeTargetBoundsRelative = out.targetBoundsRelative;
        out.compositingMaskCropped =
            ComfyUIUtils::cropImageToDocumentRect(maskFullDoc, contextBounds, docBounds);
        if (out.contextImage.isNull() || out.compositingMaskCropped.isNull()) {
            out.errorMessage = ComfyTr::tr("Could not crop inpaint context.");
            return out;
        }
        if (ComfyRegionProcess::maskAverage(out.compositingMaskCropped) < 0.001) {
            out.errorMessage = ComfyTr::tr("Selection mask is empty. Re-select the area and try again.");
            return out;
        }
    }

    out.nativeContextSize = out.contextImage.size();
    if (!out.nativeContextSize.isValid() && out.workflowKind == WorkflowKind::Generate)
        out.nativeContextSize = docBounds.size();

    out.nativeContextImage = out.contextImage;
    out.nativeCompositingMask = out.compositingMaskCropped;
    out.diffusionExtent = out.nativeContextSize;

    const bool scaleDiffusionInput = out.hasMask && out.nativeContextSize.isValid();
    if (scaleDiffusionInput) {
        const ComfyUIUtils::DiffusionPreparedExtent prep =
            ComfyUIUtils::prepareDiffusionInputExtent(out.nativeContextSize, out.arch);
        out.diffusionExtent = prep.initial;
        if (prep.initial.isValid() && prep.initial != out.nativeContextSize) {
            const double sx = static_cast<double>(prep.initial.width()) / out.nativeContextSize.width();
            const double sy = static_cast<double>(prep.initial.height()) / out.nativeContextSize.height();
            out.contextImage = out.contextImage.scaled(prep.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            out.compositingMaskCropped = out.compositingMaskCropped.scaled(
                prep.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            if (!out.targetBoundsRelative.isEmpty()) {
                out.targetBoundsRelative = QRect(qRound(out.targetBoundsRelative.x() * sx),
                                                 qRound(out.targetBoundsRelative.y() * sy),
                                                 qRound(out.targetBoundsRelative.width() * sx),
                                                 qRound(out.targetBoundsRelative.height() * sy));
            }
        }
    }

    if (out.contextImage.isNull() && !docBounds.isEmpty()) {
        const QRect captureRect = contextBounds.isEmpty() ? docBounds : contextBounds;
        const auto docCapture = ComfyUIUtils::getDocumentImage(image, captureRect, {});
        if (!docCapture) {
            out.errorMessage = docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas.")
                                                                 : docCapture.errorMessage;
            return out;
        }
        out.contextImage = docCapture.image.convertToFormat(QImage::Format_ARGB32);
        if (!out.contextBounds.isValid())
            out.contextBounds = captureRect;
    }

    out.ok = true;
    return out;
}

} // namespace

Result prepare(const Input &input, const PrepareFlags &flags)
{
    if (flags.isLive)
        return prepareLive(input, flags);
    return prepareGenerate(input, flags);
}

} // namespace ComfyPrepareWorkflow
