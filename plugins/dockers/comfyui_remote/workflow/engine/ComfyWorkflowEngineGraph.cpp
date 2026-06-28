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

bool usesSamplerCustomAdvanced(ComfyResources::Arch arch)
{
    Q_UNUSED(arch);
    return true;
}

bool isImg2imgRefineWorkflow(const QJsonObject &workflow)
{
    const QString loadClass =
        workflow.value(QStringLiteral("1")).toObject().value(QStringLiteral("class_type")).toString();
    const QString encodeClass =
        workflow.value(QStringLiteral("2")).toObject().value(QStringLiteral("class_type")).toString();
    return loadClass == QLatin1String("LoadImage") && encodeClass == QLatin1String("VAEEncode");
}

WorkflowGraphContext discoverWorkflowGraphContext(const QJsonObject &workflow)
{
    WorkflowGraphContext ctx;
    if (isImg2imgRefineWorkflow(workflow)) {
        ctx.samplerNodeId = QStringLiteral("6");
        ctx.modelNodeId = QStringLiteral("3");
        ctx.positiveNodeId = QStringLiteral("4");
        ctx.negativeNodeId = QStringLiteral("5");
        ctx.clipSourceNodeId = QStringLiteral("3");
        ctx.canvasImageNodeId = QStringLiteral("1");
        ctx.latentImageNodeId = QStringLiteral("2");
    } else {
        const QJsonArray refineLatent = workflow.value(QStringLiteral("6"))
                                        .toObject()
                                        .value(QStringLiteral("inputs"))
                                        .toObject()
                                        .value(QStringLiteral("latent_image"))
                                        .toArray();
    if (refineLatent.size() >= 1 && refineLatent.at(0).toString() == QLatin1String("2")) {
        ctx.samplerNodeId = QStringLiteral("6");
        ctx.modelNodeId = QStringLiteral("3");
        ctx.positiveNodeId = QStringLiteral("4");
        ctx.negativeNodeId = QStringLiteral("5");
        ctx.clipSourceNodeId = QStringLiteral("3");
        ctx.canvasImageNodeId = QStringLiteral("1");
        ctx.latentImageNodeId = QStringLiteral("2");
    } else {
        ctx.samplerNodeId = QStringLiteral("3");
        ctx.modelNodeId = QStringLiteral("4");
        ctx.positiveNodeId = QStringLiteral("6");
        ctx.negativeNodeId = QStringLiteral("7");
        ctx.clipSourceNodeId = QStringLiteral("4");
        ctx.latentImageNodeId = QStringLiteral("5");
    }
    }

    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        const QString cls = node.value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("EmptyLatentImage")) {
            const QJsonObject ins = node.value(QStringLiteral("inputs")).toObject();
            ctx.extentWidth = ins.value(QStringLiteral("width")).toInt(ctx.extentWidth);
            ctx.extentHeight = ins.value(QStringLiteral("height")).toInt(ctx.extentHeight);
            break;
        }
        if (cls == QLatin1String("ImageScale")) {
            const QJsonObject ins = node.value(QStringLiteral("inputs")).toObject();
            if (ins.contains(QStringLiteral("width"))) {
                ctx.extentWidth = ins.value(QStringLiteral("width")).toInt(ctx.extentWidth);
                ctx.extentHeight = ins.value(QStringLiteral("height")).toInt(ctx.extentHeight);
            }
        }
    }
    return ctx;
}

GenerationConditioningResult applyGenerationConditioning(QJsonObject *workflow,
                                                         const GenerationConditioningParams &params,
                                                         const WorkflowGraphContext &ctx,
                                                         ComfyResources::Arch arch)
{
    GenerationConditioningResult result;
    result.modelNodeId = ctx.modelNodeId;
    result.positiveNodeId = ctx.positiveNodeId;
    result.negativeNodeId = ctx.negativeNodeId;
    if (!workflow || workflow->isEmpty())
        return result;

    int nextId = 200;
    while (workflow->contains(QString::number(nextId)))
        ++nextId;

    ConditioningGraphRef graph;
    graph.samplerNodeId = QString();
    graph.modelNodeId = ctx.modelNodeId;
    graph.positiveNodeId = ctx.positiveNodeId;
    graph.negativeNodeId = ctx.negativeNodeId;
    graph.clipSourceNodeId = ctx.clipSourceNodeId;
    graph.nextNodeId = nextId;

    if (!params.ipLayers.isEmpty()) {
        applyIpAdapterLayers(workflow, params.ipLayers, arch, &graph);
        result.modelNodeId = graph.modelNodeId;
        nextId = graph.nextNodeId;
    }

    if (params.regions.size() >= 2) {
        graph.modelNodeId = result.modelNodeId;
        graph.positiveNodeId = result.positiveNodeId;
        graph.negativeNodeId = result.negativeNodeId;
        graph.nextNodeId = nextId;
        const RegionalWorkflowNodes regional = applyRegionalGeneration(workflow,
                                                                       params.regions,
                                                                       result.modelNodeId,
                                                                       result.positiveNodeId,
                                                                       result.negativeNodeId,
                                                                       ctx.clipSourceNodeId,
                                                                       &graph);
        if (regional.applied) {
            result.modelNodeId = regional.modelNodeId;
            result.positiveNodeId = regional.positiveNodeId;
            result.negativeNodeId = regional.negativeNodeId;
        }
        nextId = graph.nextNodeId;
    }

    if (!params.controlLayers.isEmpty()) {
        graph.modelNodeId = result.modelNodeId;
        graph.positiveNodeId = result.positiveNodeId;
        graph.negativeNodeId = result.negativeNodeId;
        graph.nextNodeId = nextId;
        applyControlNetLayers(workflow, params.controlLayers, arch, &graph);
        result.positiveNodeId = graph.positiveNodeId;
        nextId = graph.nextNodeId;
    }

    if (params.editReference || !params.ipLayers.isEmpty()) {
        detail::PromptOutput promptOut{result.positiveNodeId, result.negativeNodeId};
        promptOut = detail::applyReferenceConditioningForTile(workflow,
                                                      &nextId,
                                                      promptOut,
                                                      ctx.canvasImageNodeId,
                                                      ctx.latentImageNodeId,
                                                      ctx.clipSourceNodeId,
                                                      params.ipLayers,
                                                      params.editReference,
                                                      arch);
        result.positiveNodeId = promptOut.positiveId;
        result.negativeNodeId = promptOut.negativeId;
    }

    if (!result.modelNodeId.isEmpty()) {
        detail::patchSamplerNode(workflow, ctx.samplerNodeId, result.modelNodeId);
    }
    if (!result.positiveNodeId.isEmpty() && !result.negativeNodeId.isEmpty()) {
        detail::patchSamplerNode(workflow,
                         ctx.samplerNodeId,
                         QString(),
                         result.positiveNodeId,
                         result.negativeNodeId);
    }

    return result;
}


void finishWorkflowWithSamplerCustom(QJsonObject *workflow,
                                     const QString &samplerNodeId,
                                     ComfyResources::Arch arch,
                                     int extentWidth,
                                     int extentHeight,
                                     double denoiseStrength,
                                     int *nextNodeId)
{
    if (!usesSamplerCustomAdvanced(arch))
        return;
    detail::finishBuilderWithSamplerCustom(
        workflow, samplerNodeId, arch, extentWidth, extentHeight, denoiseStrength, nextNodeId);
}

void applyNsfwFilterToWorkflowOutput(QJsonObject *workflow, double sensitivity)
{
    if (!workflow || sensitivity <= 0.0)
        return;
    const QString saveId = detail::findNodeIdByClassType(*workflow, QStringLiteral("SaveImage"));
    if (saveId.isEmpty())
        return;
    const QJsonArray images =
        workflow->value(saveId).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray();
    if (images.size() < 2)
        return;
    int nextId = 600;
    while (workflow->contains(QString::number(nextId)))
        ++nextId;
    detail::appendNsfwFilterAfterDecode(workflow, images.at(0).toString(), sensitivity, &nextId);
}

void applyCheckpointStyleOptions(QJsonObject *workflow, const QString &vaeName, int clipSkip, ComfyResources::Arch arch)
{
    if (!workflow || workflow->isEmpty())
        return;
    const QString ckptId = detail::findCheckpointNodeId(*workflow);
    if (ckptId.isEmpty())
        return;

    QJsonArray clipLink = QJsonArray{ckptId, 1};
    const bool useClipSkip = clipSkip > 0 && ComfyResources::supportsClipSkip(arch);
    if (useClipSkip) {
        int nextId = 300;
        while (workflow->contains(QString::number(nextId)))
            ++nextId;
        const QString clipLayerId = QString::number(nextId++);
        workflow->insert(clipLayerId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPSetLastLayer")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("clip"), clipLink},
                                                  {QStringLiteral("stop_at_clip_layer"), -clipSkip}}}});
        clipLink = QJsonArray{clipLayerId, 0};
    }

    const QString defaultVae = QStringLiteral("Checkpoint Default");
    const bool useCustomVae = !vaeName.trimmed().isEmpty() && vaeName.trimmed() != defaultVae;
    QJsonArray vaeLink = QJsonArray{ckptId, 2};
    if (useCustomVae) {
        int nextId = 300;
        while (workflow->contains(QString::number(nextId)))
            ++nextId;
        const QString vaeLoaderId = QString::number(nextId++);
        workflow->insert(vaeLoaderId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAELoader")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("vae_name"), vaeName.trimmed()}}}});
        vaeLink = QJsonArray{vaeLoaderId, 0};
    }

    const QJsonArray ckptClip = clipLink;
    const QJsonArray ckptVae = vaeLink;
    detail::replaceInputLink(workflow, QJsonArray{ckptId, 1}, ckptClip);
    detail::replaceInputLink(workflow, QJsonArray{ckptId, 2}, ckptVae);
}

} // namespace ComfyWorkflowEngine
