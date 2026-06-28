/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyWorkflowEngine.h"
#include "ComfyWorkflowEngineInternal.h"

#include "ComfyUIUtils.h"
#include "ComfyUIWorkflows.h"
#include "ComfyResources.h"
#include "ComfyFileLibrary.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <functional>

namespace ComfyWorkflowEngine {

static QJsonObject imageScaleInputs(const QJsonArray &imageLink, int width, int height, const QString &method)
{
    return QJsonObject{{QStringLiteral("image"), imageLink},
                       {QStringLiteral("width"), width},
                       {QStringLiteral("height"), height},
                       {QStringLiteral("upscale_method"), method},
                       {QStringLiteral("crop"), QStringLiteral("disabled")}};
}

static QJsonObject parseUpscaleWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(upscaleWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

static QJsonObject parseUpscaleRefineWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(upscaleRefineWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QJsonObject buildUpscaleSimple(const UpscaleSimpleParams &params)
{
    if (params.imageName.isEmpty())
        return QJsonObject();

    const QString method =
        params.upscaleMethod.trimmed().isEmpty() ? QStringLiteral("lanczos") : params.upscaleMethod.trimmed();
    const int targetW = qMax(64, params.targetWidth);
    const int targetH = qMax(64, params.targetHeight);

    if (params.upscaleModelName.trimmed().isEmpty()) {
        QJsonObject workflow = parseUpscaleWorkflowTemplate();
        if (workflow.isEmpty())
            return workflow;

        {
            QJsonObject n1 = workflow.value(QStringLiteral("1")).toObject();
            QJsonObject i1 = n1.value(QStringLiteral("inputs")).toObject();
            i1.insert(QStringLiteral("image"), params.imageName);
            n1.insert(QStringLiteral("inputs"), i1);
            workflow.insert(QStringLiteral("1"), n1);
        }
        {
            QJsonObject n2 = workflow.value(QStringLiteral("2")).toObject();
            QJsonObject i2 = n2.value(QStringLiteral("inputs")).toObject();
            i2.insert(QStringLiteral("width"), targetW);
            i2.insert(QStringLiteral("height"), targetH);
            i2.insert(QStringLiteral("upscale_method"), method);
            i2.insert(QStringLiteral("crop"), QStringLiteral("disabled"));
            n2.insert(QStringLiteral("inputs"), i2);
            workflow.insert(QStringLiteral("2"), n2);
        }
        return workflow;
    }

    QJsonObject workflow;
    int nextId = 1;
    auto addNode = [&](const QString &classType, const QJsonObject &inputs) -> QString {
        const QString id = QString::number(nextId++);
        workflow.insert(id,
                        QJsonObject{{QStringLiteral("class_type"), classType}, {QStringLiteral("inputs"), inputs}});
        return id;
    };

    QString workingImage = addNode(QStringLiteral("LoadImage"), {{QStringLiteral("image"), params.imageName}});
    const QString loader =
        addNode(QStringLiteral("UpscaleModelLoader"), {{QStringLiteral("model_name"), params.upscaleModelName.trimmed()}});
    workingImage = addNode(QStringLiteral("ImageUpscaleWithModel"),
                           {{QStringLiteral("upscale_model"), QJsonArray{loader, 0}},
                            {QStringLiteral("image"), QJsonArray{workingImage, 0}}});
    workingImage = addNode(QStringLiteral("ImageScale"),
                           imageScaleInputs(QJsonArray{workingImage, 0}, targetW, targetH, method));
    addNode(QStringLiteral("SaveImage"),
            {{QStringLiteral("filename_prefix"), QStringLiteral("ComfyUI_upscale")},
             {QStringLiteral("images"), QJsonArray{workingImage, 0}}});
    return workflow;
}

QJsonObject buildUpscaleRefine(const UpscaleRefineParams &params)
{
    QJsonObject workflow = parseUpscaleRefineWorkflowTemplate();
    if (workflow.isEmpty())
        return workflow;

    double cfg = params.cfg;
    if (!ComfyResources::supportsCfg(params.arch) && cfg > 4.0)
        cfg = 3.5;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();
    const QString method =
        params.upscaleMethod.trimmed().isEmpty() ? QStringLiteral("lanczos") : params.upscaleMethod.trimmed();
    const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("high quality, detailed")
                                                                    : params.positivePrompt;

    {
        QJsonObject n1 = workflow.value(QStringLiteral("1")).toObject();
        QJsonObject i1 = n1.value(QStringLiteral("inputs")).toObject();
        i1.insert(QStringLiteral("image"), params.imageName);
        n1.insert(QStringLiteral("inputs"), i1);
        workflow.insert(QStringLiteral("1"), n1);
    }
    {
        QJsonObject n2 = workflow.value(QStringLiteral("2")).toObject();
        QJsonObject i2 = n2.value(QStringLiteral("inputs")).toObject();
        i2.insert(QStringLiteral("width"), qMax(64, params.scaleWidth));
        i2.insert(QStringLiteral("height"), qMax(64, params.scaleHeight));
        i2.insert(QStringLiteral("upscale_method"), method);
        i2.insert(QStringLiteral("crop"), QStringLiteral("disabled"));
        n2.insert(QStringLiteral("inputs"), i2);
        workflow.insert(QStringLiteral("2"), n2);
    }
    {
        QJsonObject n4 = workflow.value(QStringLiteral("4")).toObject();
        QJsonObject i4 = n4.value(QStringLiteral("inputs")).toObject();
        i4.insert(QStringLiteral("ckpt_name"), ckpt);
        n4.insert(QStringLiteral("inputs"), i4);
        workflow.insert(QStringLiteral("4"), n4);
    }
    int injectId = 90;
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("5"), pos, params.promptTranslationLanguage, &injectId);
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("6"), params.negativePrompt, params.promptTranslationLanguage,
                            &injectId);
    {
        QJsonObject n7 = workflow.value(QStringLiteral("7")).toObject();
        QJsonObject i7 = n7.value(QStringLiteral("inputs")).toObject();
        i7.insert(QStringLiteral("seed"), static_cast<double>(params.seed));
        i7.insert(QStringLiteral("steps"), params.steps);
        i7.insert(QStringLiteral("cfg"), cfg);
        i7.insert(QStringLiteral("denoise"), qBound(0.01, params.denoise, 1.0));
        i7.insert(QStringLiteral("sampler_name"), params.sampler);
        i7.insert(QStringLiteral("scheduler"), params.scheduler.isEmpty() ? QStringLiteral("normal") : params.scheduler);
        n7.insert(QStringLiteral("inputs"), i7);
        workflow.insert(QStringLiteral("7"), n7);
    }
    finishWorkflowWithSamplerCustom(&workflow,
                                    QStringLiteral("7"),
                                    params.arch,
                                    qMax(64, params.scaleWidth),
                                    qMax(64, params.scaleHeight),
                                    params.denoise);
    return workflow;
}
QJsonObject buildUpscaleTiled(const UpscaleTiledParams &params)
{
    QJsonObject workflow;
    if (params.imageName.isEmpty() || params.scaledWidth <= 0 || params.scaledHeight <= 0)
        return workflow;

    double cfg = params.cfg;
    if (!ComfyResources::supportsCfg(params.arch) && cfg > 4.0)
        cfg = 3.5;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();
    const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("4k uhd") : params.positivePrompt;
    const int multiple = qMax(1, ComfyResources::latentCompressionFactor(params.arch));

    const ComfyUIUtils::UpscaleTiledLayoutSpec layout = ComfyUIUtils::computeUpscaleTiledLayoutSpec(
        params.scaledWidth,
        params.scaledHeight,
        params.arch,
        params.stylePreferredResolution,
        params.denoise,
        params.tileOverlapPx);
    const int padding = layout.padding;
    const int blending = layout.blending;
    const int tileSize = layout.minTileSize;

    int nextId = 1;
    auto addNode = [&](const QString &classType, const QJsonObject &inputs) -> QString {
        const QString id = QString::number(nextId++);
        workflow.insert(id,
                        QJsonObject{{QStringLiteral("class_type"), classType}, {QStringLiteral("inputs"), inputs}});
        return id;
    };

    QString workingImage = addNode(QStringLiteral("LoadImage"), {{QStringLiteral("image"), params.imageName}});
    if (!params.upscaleModelName.trimmed().isEmpty()) {
        const QString loader =
            addNode(QStringLiteral("UpscaleModelLoader"), {{QStringLiteral("model_name"), params.upscaleModelName.trimmed()}});
        workingImage = addNode(QStringLiteral("ImageUpscaleWithModel"),
                               {{QStringLiteral("upscale_model"), QJsonArray{loader, 0}},
                                {QStringLiteral("image"), QJsonArray{workingImage, 0}}});
    }
    if (params.scaledWidth != params.targetWidth || params.scaledHeight != params.targetHeight) {
        workingImage = addNode(QStringLiteral("ImageScale"),
                               imageScaleInputs(QJsonArray{workingImage, 0},
                                                qMax(64, params.scaledWidth),
                                                qMax(64, params.scaledHeight),
                                                QStringLiteral("lanczos")));
    }

    const QString layoutId = addNode(QStringLiteral("ETN_TileLayout"),
                                     {{QStringLiteral("image"), QJsonArray{workingImage, 0}},
                                      {QStringLiteral("min_tile_size"), tileSize},
                                      {QStringLiteral("padding"), padding},
                                      {QStringLiteral("blending"), blending},
                                      {QStringLiteral("multiple"), multiple}});

    const QString checkpointId =
        addNode(QStringLiteral("CheckpointLoaderSimple"), {{QStringLiteral("ckpt_name"), ckpt}});

    ComfyUIUtils::DiffusionTileLayout tileGrid;
    if (params.tileOverlapPx < 0) {
        tileGrid = ComfyUIUtils::DiffusionTileLayout::fromDenoiseStrength(
            QSize(params.scaledWidth, params.scaledHeight), layout.minTileSize, params.denoise, multiple, layout.padding);
    } else {
        tileGrid = ComfyUIUtils::DiffusionTileLayout::fromUniformGrid(
            params.scaledWidth, params.scaledHeight, layout.minTileSize, layout.padding, layout.minTileSize,
            layout.blending);
    }
    const int tiles = qMax(1, qMin(layout.totalTiles, tileGrid.tileCount > 0 ? tileGrid.tileCount : layout.totalTiles));

    QString mergedImage = workingImage;
    for (int i = 0; i < tiles; ++i) {
        const QString tileImg = addNode(QStringLiteral("ETN_ExtractImageTile"),
                                        {{QStringLiteral("image"), QJsonArray{workingImage, 0}},
                                         {QStringLiteral("layout"), QJsonArray{layoutId, 0}},
                                         {QStringLiteral("index"), i}});
        const QString tileMask = addNode(QStringLiteral("ETN_GenerateTileMask"),
                                         {{QStringLiteral("layout"), QJsonArray{layoutId, 0}}, {QStringLiteral("index"), i}});
        const QString latent = addNode(QStringLiteral("VAEEncode"),
                                       {{QStringLiteral("pixels"), QJsonArray{tileImg, 0}},
                                        {QStringLiteral("vae"), QJsonArray{checkpointId, 2}}});
        const QString latentMasked = addNode(QStringLiteral("SetLatentNoiseMask"),
                                             {{QStringLiteral("samples"), QJsonArray{latent, 0}},
                                              {QStringLiteral("mask"), QJsonArray{tileMask, 0}}});
        const QJsonArray clipLink = QJsonArray{checkpointId, 1};
        const QString positive =
            detail::addClipTextEncode(addNode, clipLink, pos, params.promptTranslationLanguage);
        const QString negative =
            detail::addClipTextEncode(addNode, clipLink, params.negativePrompt, params.promptTranslationLanguage);

        QString modelId = checkpointId;
        QString posId = positive;
        QString negId = negative;

        ConditioningGraphRef cond;
        cond.modelNodeId = modelId;
        cond.positiveNodeId = posId;
        cond.negativeNodeId = negId;
        cond.clipSourceNodeId = checkpointId;
        cond.tileLayoutNodeId = layoutId;
        cond.tileIndex = i;
        cond.nextNodeId = nextId;
        cond.scaledWidth = params.scaledWidth;
        cond.scaledHeight = params.scaledHeight;

        if (!params.ipAdapterLayers.isEmpty())
            applyIpAdapterLayers(&workflow, params.ipAdapterLayers, params.arch, &cond);
        modelId = cond.modelNodeId;
        nextId = cond.nextNodeId;

        QList<RegionalPromptInput> tileRegions = params.regionalPrompts;
        if (params.regionalPrompts.size() >= 2 && tileGrid.tileCount > 0) {
            const QRect tileBounds = tileGrid.bounds(i);
            tileRegions = detail::filterRegionalPromptsForTile(params.regionalPrompts, tileBounds);
        }
        if (tileRegions.size() >= 2) {
            cond.modelNodeId = modelId;
            cond.positiveNodeId = posId;
            cond.negativeNodeId = negId;
            cond.nextNodeId = nextId;
            const RegionalWorkflowNodes regional = applyRegionalGeneration(
                &workflow, tileRegions, modelId, posId, negId, checkpointId, &cond);
            if (regional.applied) {
                modelId = regional.modelNodeId;
                posId = regional.positiveNodeId;
                negId = regional.negativeNodeId;
            }
            nextId = cond.nextNodeId;
        }

        if (!params.controlLayers.isEmpty()) {
            cond.modelNodeId = modelId;
            cond.positiveNodeId = posId;
            cond.negativeNodeId = negId;
            cond.nextNodeId = nextId;
            applyControlNetLayers(&workflow, params.controlLayers, params.arch, &cond);
            posId = cond.positiveNodeId;
            nextId = cond.nextNodeId;
        }

        detail::PromptOutput promptOut{posId, negId};
        const QRect tileBounds = tileGrid.tileCount > 0 ? tileGrid.bounds(i) : QRect();
        const int tileW = tileBounds.isValid() ? tileBounds.width() : params.scaledWidth;
        const int tileH = tileBounds.isValid() ? tileBounds.height() : params.scaledHeight;
        promptOut = detail::applyReferenceConditioningForTile(&workflow,
                                                      &nextId,
                                                      promptOut,
                                                      tileImg,
                                                      latentMasked,
                                                      checkpointId,
                                                      params.ipAdapterLayers,
                                                      params.editReference,
                                                      params.arch);
        posId = promptOut.positiveId;
        negId = promptOut.negativeId;

        const int startAtStep =
            static_cast<int>(qRound(params.steps * (1.0 - qBound(0.01, params.denoise, 1.0))));
        const QString samplerNode = detail::addSamplerCustomAdvanced(&workflow,
                                                             &nextId,
                                                             modelId,
                                                             posId,
                                                             negId,
                                                             latentMasked,
                                                             params.arch,
                                                             cfg,
                                                             params.steps,
                                                             startAtStep,
                                                             params.sampler,
                                                             params.scheduler.isEmpty() ? QStringLiteral("normal")
                                                                                        : params.scheduler,
                                                             params.seed + i,
                                                             tileW,
                                                             tileH);
        const QString decoded = addNode(QStringLiteral("VAEDecode"),
                                        {{QStringLiteral("samples"), QJsonArray{samplerNode, 1}},
                                         {QStringLiteral("vae"), QJsonArray{checkpointId, 2}}});
        mergedImage = addNode(QStringLiteral("ETN_MergeImageTile"),
                              {{QStringLiteral("image"), QJsonArray{mergedImage, 0}},
                               {QStringLiteral("layout"), QJsonArray{layoutId, 0}},
                               {QStringLiteral("index"), i},
                               {QStringLiteral("tile"), QJsonArray{decoded, 0}}});
    }

    QString outImage = mergedImage;
    if (params.targetWidth != params.scaledWidth || params.targetHeight != params.scaledHeight) {
        outImage = addNode(QStringLiteral("ImageScale"),
                           imageScaleInputs(QJsonArray{outImage, 0},
                                            qMax(64, params.targetWidth),
                                            qMax(64, params.targetHeight),
                                            QStringLiteral("lanczos")));
    }
    addNode(QStringLiteral("SaveImage"),
            {{QStringLiteral("images"), QJsonArray{outImage, 0}},
             {QStringLiteral("filename_prefix"), QStringLiteral("ComfyUI_upscale_tiled")}});
    return workflow;
}

} // namespace ComfyWorkflowEngine
