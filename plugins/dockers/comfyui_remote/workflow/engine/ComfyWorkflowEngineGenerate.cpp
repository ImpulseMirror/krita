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

QJsonObject buildTextToImage(const TextToImageParams &params)
{
    QJsonObject workflow = detail::parseDefaultWorkflowTemplate();
    if (workflow.isEmpty())
        return workflow;

    const ComfyResources::Arch arch = params.arch;
    double cfg = params.cfg;
    if (!ComfyResources::supportsCfg(arch)) {
        // Flux / Flux Kontext: match Python default guidance (~3.5)
        if (cfg > 4.0)
            cfg = 3.5;
    }

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();

    {
        QJsonObject n3 = workflow.value(QStringLiteral("3")).toObject();
        QJsonObject i3 = n3.value(QStringLiteral("inputs")).toObject();
        i3.insert(QStringLiteral("seed"), static_cast<double>(params.seed));
        i3.insert(QStringLiteral("steps"), params.steps);
        i3.insert(QStringLiteral("cfg"), cfg);
        i3.insert(QStringLiteral("denoise"), params.denoise);
        i3.insert(QStringLiteral("sampler_name"), params.sampler);
        i3.insert(QStringLiteral("scheduler"), params.scheduler.isEmpty() ? QStringLiteral("normal") : params.scheduler);
        n3.insert(QStringLiteral("inputs"), i3);
        workflow.insert(QStringLiteral("3"), n3);
    }
    {
        CheckpointLoadParams cl;
        cl.checkpoint = ckpt;
        cl.arch = arch;
        cl.loras = checkpointLorasFromStyle(params.styleLoras);
        loadCheckpointWithLora(&workflow, cl);
    }
    {
        QJsonObject n5 = workflow.value(QStringLiteral("5")).toObject();
        QJsonObject i5 = n5.value(QStringLiteral("inputs")).toObject();
        i5.insert(QStringLiteral("width"), qMax(64, params.width));
        i5.insert(QStringLiteral("height"), qMax(64, params.height));
        i5.insert(QStringLiteral("batch_size"), qMax(1, params.batchSize));
        if (arch == ComfyResources::Arch::QwenL && params.layerCount > 1) {
            n5.insert(QStringLiteral("class_type"), QStringLiteral("EmptyHunyuanLatentVideo"));
            i5.insert(QStringLiteral("length"), 1 + params.layerCount * 4);
            i5.remove(QStringLiteral("batch_size"));
            i5.insert(QStringLiteral("batch_size"), qMax(1, params.batchSize));
        }
        n5.insert(QStringLiteral("inputs"), i5);
        workflow.insert(QStringLiteral("5"), n5);
    }
    int injectId = 90;
    const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("a beautiful painting")
                                                                    : params.positivePrompt;
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("6"), pos, params.promptTranslationLanguage, &injectId);
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("7"), params.negativePrompt, params.promptTranslationLanguage,
                            &injectId);

    return workflow;
}

QJsonObject buildGenerate(const GenerateParams &params)
{
    QJsonObject workflow = buildTextToImage(params);
    if (workflow.isEmpty())
        return workflow;

    applyCheckpointStyleOptions(&workflow, params.styleVae, params.styleClipSkip, params.arch);

    WorkflowGraphContext ctx = discoverWorkflowGraphContext(workflow);
    ctx.extentWidth = qMax(64, params.width);
    ctx.extentHeight = qMax(64, params.height);
    applyGenerationConditioning(&workflow, params.conditioning, ctx, params.arch);
    finishWorkflowWithSamplerCustom(
        &workflow, ctx.samplerNodeId, params.arch, ctx.extentWidth, ctx.extentHeight, params.denoise);
    return workflow;
}
QJsonObject buildControlPreview(const ControlPreviewParams &params)
{
    return ComfyUIUtils::buildControlImageWorkflow(params.uploadedImageName,
                                                 params.mode,
                                                 params.resolutionBase,
                                                 params.returnPreprocessed);
}

QJsonObject buildRefine(const RefineParams &params)
{
    QJsonObject workflow = detail::parseImg2ImgWorkflowTemplate();
    if (workflow.isEmpty())
        return workflow;

    const ComfyResources::Arch arch = params.arch;
    double cfg = params.cfg;
    if (!ComfyResources::supportsCfg(arch) && cfg > 4.0)
        cfg = 3.5;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();

    {
        QJsonObject n1 = workflow.value(QStringLiteral("1")).toObject();
        QJsonObject i1 = n1.value(QStringLiteral("inputs")).toObject();
        i1.insert(QStringLiteral("image"), params.imageName);
        n1.insert(QStringLiteral("inputs"), i1);
        workflow.insert(QStringLiteral("1"), n1);
    }
    {
        CheckpointLoadParams cl;
        cl.checkpoint = ckpt;
        cl.arch = arch;
        cl.loras = checkpointLorasFromStyle(params.styleLoras);
        loadCheckpointWithLora(&workflow, cl);
    }
    int injectId = 90;
    const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("a beautiful painting")
                                                                    : params.positivePrompt;
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("4"), pos, params.promptTranslationLanguage, &injectId);
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("5"), params.negativePrompt, params.promptTranslationLanguage,
                            &injectId);
    {
        QJsonObject n6 = workflow.value(QStringLiteral("6")).toObject();
        QJsonObject i6 = n6.value(QStringLiteral("inputs")).toObject();
        i6.insert(QStringLiteral("seed"), static_cast<double>(params.seed));
        i6.insert(QStringLiteral("steps"), params.steps);
        i6.insert(QStringLiteral("cfg"), cfg);
        i6.insert(QStringLiteral("denoise"), qBound(0.01, params.denoise, 1.0));
        i6.insert(QStringLiteral("sampler_name"), params.sampler);
        i6.insert(QStringLiteral("scheduler"), params.scheduler.isEmpty() ? QStringLiteral("normal") : params.scheduler);
        n6.insert(QStringLiteral("inputs"), i6);
        workflow.insert(QStringLiteral("6"), n6);
    }

    // SamplerCustom + NSFW are applied in finalizeGenerateWorkflowAndSubmit (same as txt2img).
    // Calling finish here breaks discoverWorkflowGraphContext on the second pass and corrupts
    // the checkpoint node via applyGenerationConditioning.
    return workflow;
}

} // namespace ComfyWorkflowEngine
