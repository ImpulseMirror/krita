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

QJsonObject buildRefineRegion(const RefineRegionParams &params)
{
    if (params.refine.imageName.isEmpty() || params.maskImageName.isEmpty())
        return QJsonObject();

    // Upstream workflow.refine_region(): real canvas pixels, apply_grow_feather on mask,
    // vae_encode + set_latent_noise_mask + differential_diffusion. No fill_masked,
    // no VAEEncodeForInpaint. Client composites via denoise_to_compositing_mask.
    QJsonObject workflow = detail::parseInpaintingWorkflowTemplate();
    if (workflow.isEmpty())
        return QJsonObject();

    const ComfyResources::Arch arch = params.refine.arch;
    double cfg = params.refine.cfg;
    if (!ComfyResources::supportsCfg(arch) && cfg > 4.0)
        cfg = 3.5;

    const QString ckpt = params.refine.checkpoint.trimmed().isEmpty()
                             ? QStringLiteral("v1-5-pruned-emaonly.safetensors")
                             : params.refine.checkpoint.trimmed();

    {
        QJsonObject n1 = workflow.value(QStringLiteral("1")).toObject();
        QJsonObject i1 = n1.value(QStringLiteral("inputs")).toObject();
        i1.insert(QStringLiteral("image"), params.refine.imageName);
        n1.insert(QStringLiteral("inputs"), i1);
        workflow.insert(QStringLiteral("1"), n1);
    }
    {
        QJsonObject n2 = workflow.value(QStringLiteral("2")).toObject();
        QJsonObject i2 = n2.value(QStringLiteral("inputs")).toObject();
        i2.insert(QStringLiteral("image"), params.maskImageName);
        n2.insert(QStringLiteral("inputs"), i2);
        workflow.insert(QStringLiteral("2"), n2);
    }

    CheckpointLoadParams cl;
    cl.checkpoint = ckpt;
    cl.arch = arch;
    cl.loras = checkpointLorasFromStyle(params.refine.styleLoras);
    CheckpointGraphRefs ckptRefs;
    loadCheckpointWithLora(&workflow, cl, &ckptRefs);

    int nextId = ckptRefs.nextNodeId;
    int injectId = 90;
    const QString pos = params.refine.positivePrompt.trimmed().isEmpty() ? QStringLiteral("a beautiful painting")
                                                                         : params.refine.positivePrompt;
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("5"), pos, params.refine.promptTranslationLanguage, &injectId);
    detail::patchClipTextEncodeNode(workflow, QStringLiteral("6"), params.refine.negativePrompt,
                            params.refine.promptTranslationLanguage, &injectId);

    const QString vaeLinkNode = ckptRefs.vaeNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.vaeNodeId;
    const int vaeLinkSlot = ckptRefs.vaeNodeId.isEmpty() ? 2 : ckptRefs.vaeNodeSlot;

    QString processedMaskId = detail::insertImageToMask(&workflow, &nextId, QStringLiteral("2"), QStringLiteral("red"));
    int processedMaskSlot = 0;
    if (params.growMaskBy > 0 || params.featherMaskBy > 0) {
        processedMaskId =
            detail::insertInpaintExpandMask(&workflow, &nextId, processedMaskId, params.growMaskBy, params.featherMaskBy, 0);
        processedMaskSlot = 0;
    }

    workflow.remove(QStringLiteral("7"));
    const QString ckptId = detail::findCheckpointNodeId(workflow);
    const QString modelSourceId = ckptRefs.modelNodeId.isEmpty() ? ckptId : ckptRefs.modelNodeId;

    QString positiveNodeId = QStringLiteral("5");
    QString negativeNodeId = QStringLiteral("6");
    int negativeNodeSlot = 0;
    if (params.useInpaintModel && !params.controlNetInpaintFile.isEmpty()) {
        const auto patched = detail::applyInpaintControlNetConditioning(&workflow,
                                                                &nextId,
                                                                positiveNodeId,
                                                                negativeNodeId,
                                                                vaeLinkNode,
                                                                vaeLinkSlot,
                                                                QStringLiteral("1"),
                                                                0,
                                                                processedMaskId,
                                                                processedMaskSlot,
                                                                params.controlNetInpaintFile,
                                                                arch);
        if (patched.first != QStringLiteral("5")) {
            positiveNodeId = patched.first;
            negativeNodeId = patched.second;
            negativeNodeSlot = 1;
            QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
            QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
            i8.insert(QStringLiteral("positive"), QJsonArray{positiveNodeId, 0});
            i8.insert(QStringLiteral("negative"), QJsonArray{negativeNodeId, negativeNodeSlot});
            n8.insert(QStringLiteral("inputs"), i8);
            workflow.insert(QStringLiteral("8"), n8);
        }
    }

    QString latentForSampler;
    QString modelForSampler = modelSourceId;
    // SDXL refine uses the same INPAINT_VAEEncodeInpaintConditioning latent path as fill @ 100%
    // (VAEEncode + SetLatentNoiseMask returns all-black PNGs at partial denoise on SDXL).
    const bool useFooocusInpaint = params.useInpaintModel && arch == ComfyResources::Arch::Sdxl
                                   && !params.fooocusInpaintHead.isEmpty() && !params.fooocusInpaintPatch.isEmpty();
    const bool useSdxlInpaintConditioning = arch == ComfyResources::Arch::Sdxl
                                            && params.controlNetInpaintFile.isEmpty() && !useFooocusInpaint;
    const bool useInpaintConditioningLatent = useFooocusInpaint || useSdxlInpaintConditioning;
    if (useFooocusInpaint) {
        const QString condId = QString::number(nextId++);
        workflow.insert(condId,
                        QJsonObject{{QStringLiteral("class_type"),
                                     QStringLiteral("INPAINT_VAEEncodeInpaintConditioning")},
                                    {QStringLiteral("inputs"),
                                     QJsonObject{{QStringLiteral("vae"), QJsonArray{vaeLinkNode, vaeLinkSlot}},
                                                 {QStringLiteral("pixels"), QJsonArray{QStringLiteral("1"), 0}},
                                                 {QStringLiteral("mask"),
                                                  QJsonArray{processedMaskId, processedMaskSlot}},
                                                 {QStringLiteral("positive"), QJsonArray{positiveNodeId, 0}},
                                                 {QStringLiteral("negative"), QJsonArray{negativeNodeId, negativeNodeSlot}}}}});
        const QString loadFooocusId = QString::number(nextId++);
        workflow.insert(loadFooocusId,
                        QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_LoadFooocusInpaint")},
                                    {QStringLiteral("inputs"),
                                     QJsonObject{{QStringLiteral("head"), params.fooocusInpaintHead},
                                                 {QStringLiteral("patch"), params.fooocusInpaintPatch}}}});
        const QString applyFooocusId = QString::number(nextId++);
        workflow.insert(applyFooocusId,
                        QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_ApplyFooocusInpaint")},
                                    {QStringLiteral("inputs"),
                                     QJsonObject{{QStringLiteral("model"), QJsonArray{modelSourceId, 0}},
                                                 {QStringLiteral("patch"), QJsonArray{loadFooocusId, 0}},
                                                 {QStringLiteral("latent"), QJsonArray{condId, 2}}}}});
        modelForSampler = applyFooocusId;
        latentForSampler = condId;
        {
            QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
            QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
            i8.insert(QStringLiteral("positive"), QJsonArray{condId, 0});
            i8.insert(QStringLiteral("negative"), QJsonArray{condId, 1});
            i8.insert(QStringLiteral("latent_image"), QJsonArray{condId, 3});
            n8.insert(QStringLiteral("inputs"), i8);
            workflow.insert(QStringLiteral("8"), n8);
        }
    } else if (useSdxlInpaintConditioning) {
        const QString condId = QString::number(nextId++);
        workflow.insert(condId,
                        QJsonObject{{QStringLiteral("class_type"),
                                     QStringLiteral("INPAINT_VAEEncodeInpaintConditioning")},
                                    {QStringLiteral("inputs"),
                                     QJsonObject{{QStringLiteral("vae"), QJsonArray{vaeLinkNode, vaeLinkSlot}},
                                                 {QStringLiteral("pixels"), QJsonArray{QStringLiteral("1"), 0}},
                                                 {QStringLiteral("mask"),
                                                  QJsonArray{processedMaskId, processedMaskSlot}},
                                                 {QStringLiteral("positive"), QJsonArray{positiveNodeId, 0}},
                                                 {QStringLiteral("negative"), QJsonArray{negativeNodeId, negativeNodeSlot}}}}});
        latentForSampler = condId;
        {
            QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
            QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
            i8.insert(QStringLiteral("positive"), QJsonArray{condId, 0});
            i8.insert(QStringLiteral("negative"), QJsonArray{condId, 1});
            i8.insert(QStringLiteral("latent_image"), QJsonArray{condId, 3});
            n8.insert(QStringLiteral("inputs"), i8);
            workflow.insert(QStringLiteral("8"), n8);
        }
    } else {
        const QString vaeEncodeId = QString::number(nextId++);
        workflow.insert(vaeEncodeId,
                        QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEEncode")},
                                    {QStringLiteral("inputs"),
                                     QJsonObject{{QStringLiteral("pixels"), QJsonArray{QStringLiteral("1"), 0}},
                                                 {QStringLiteral("vae"), QJsonArray{vaeLinkNode, vaeLinkSlot}}}}});
        const QString latentMaskedId = QString::number(nextId++);
        workflow.insert(latentMaskedId,
                        QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SetLatentNoiseMask")},
                                    {QStringLiteral("inputs"),
                                     QJsonObject{{QStringLiteral("samples"), QJsonArray{vaeEncodeId, 0}},
                                                 {QStringLiteral("mask"),
                                                  QJsonArray{processedMaskId, processedMaskSlot}}}}});
        latentForSampler = latentMaskedId;
    }

    {
        QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
        QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
        i8.insert(QStringLiteral("seed"), static_cast<double>(params.refine.seed));
        i8.insert(QStringLiteral("steps"), params.refine.steps);
        i8.insert(QStringLiteral("cfg"), cfg);
        i8.insert(QStringLiteral("denoise"), qBound(0.01, params.refine.denoise, 1.0));
        i8.insert(QStringLiteral("sampler_name"), params.refine.sampler);
        i8.insert(QStringLiteral("scheduler"),
                  params.refine.scheduler.isEmpty() ? QStringLiteral("normal") : params.refine.scheduler);
        if (!useInpaintConditioningLatent)
            i8.insert(QStringLiteral("latent_image"), QJsonArray{latentForSampler, 0});
        n8.insert(QStringLiteral("inputs"), i8);
        workflow.insert(QStringLiteral("8"), n8);
    }

    if (!modelSourceId.isEmpty()) {
        const QString diffId = detail::insertDifferentialDiffusion(&workflow, &nextId, modelSourceId);
        if (useFooocusInpaint) {
            for (auto it = workflow.begin(); it != workflow.end(); ++it) {
                QJsonObject node = it.value().toObject();
                if (node.value(QStringLiteral("class_type")).toString()
                    != QLatin1String("INPAINT_ApplyFooocusInpaint"))
                    continue;
                QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
                inputs.insert(QStringLiteral("model"), QJsonArray{diffId, 0});
                node.insert(QStringLiteral("inputs"), inputs);
                workflow.insert(it.key(), node);
                break;
            }
            detail::patchSamplerNode(&workflow, QStringLiteral("8"), modelForSampler);
        } else {
            detail::patchSamplerNode(&workflow, QStringLiteral("8"), diffId);
        }
    }

    // color_match setting is intentionally not applied server-side on refine_region:
    // INPAINT_ColorMatch + exclude_mask zeros SaveImage output while VAEDecode stays valid.
    // Client composite preserves context outside the compositing mask.

    QString outputImageId = QStringLiteral("9");
    outputImageId = detail::appendNsfwFilterAfterDecode(&workflow, outputImageId, params.nsfwFilterSensitivity, &nextId);

    while (workflow.contains(QString::number(nextId)))
        ++nextId;
    finishWorkflowWithSamplerCustom(&workflow, QStringLiteral("8"), arch,
                                    qMax(64, params.extentWidth), qMax(64, params.extentHeight),
                                    params.refine.denoise, &nextId);

    const int nativeW = qMax(64, params.contextExtentWidth > 0 ? params.contextExtentWidth : params.extentWidth);
    const int nativeH = qMax(64, params.contextExtentHeight > 0 ? params.contextExtentHeight : params.extentHeight);
    // refine_region: client composites via denoise_to_compositing_mask (upstream draw_image).
    // Server-side ETN_ApplyMaskToImage can zero the decode when mask/decode extents diverge at partial denoise.
    detail::appendInpaintNativeScaledSaveOutput(&workflow,
                                                &nextId,
                                                outputImageId,
                                                0,
                                                params.extentWidth,
                                                params.extentHeight,
                                                nativeW,
                                                nativeH);
    return workflow;
}

bool applyInpaintPromptFocus(QJsonObject *workflow, int *nextNodeId, const QString &modelNodeId,
                             const QString &positiveClipNodeId, const QString &clipSourceNodeId,
                             const QString &maskNodeId, int maskNodeSlot, const QString &backgroundPrompt)
{
    if (!workflow || modelNodeId.isEmpty() || positiveClipNodeId.isEmpty() || maskNodeId.isEmpty())
        return false;

    int nid = *nextNodeId;
    const auto alloc = [&]() {
        while (workflow->contains(QString::number(nid)))
            ++nid;
        return QString::number(nid++);
    };

    const QJsonArray clipLink =
        clipSourceNodeId.isEmpty() ? QJsonArray{QStringLiteral("4"), 1} : QJsonArray{clipSourceNodeId, 1};

    const QString bgPosId = alloc();
    int injectId = nid + 50;
    const QString bgText = backgroundPrompt.trimmed().isEmpty() ? QStringLiteral(" ") : backgroundPrompt.trimmed();
    workflow->insert(bgPosId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPTextEncode")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("clip"), clipLink},
                                                {QStringLiteral("text"),
                                                 detail::clipEncodeTextInput(bgText, QString(), workflow, &injectId)}}}});

    const QString bgRegionId = alloc();
    workflow->insert(bgRegionId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_BackgroundRegion")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("conditioning"), QJsonArray{bgPosId, 0}}}}});

    QString maskTensorId = maskNodeId;
    if (maskNodeSlot == 1) {
        const QString itmId = alloc();
        workflow->insert(itmId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageToMask")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{maskNodeId, 0}},
                                                  {QStringLiteral("channel"), QStringLiteral("red")}}}});
        maskTensorId = itmId;
        maskNodeSlot = 0;
    }

    const QString defineId = alloc();
    workflow->insert(defineId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_DefineRegion")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("regions"), QJsonArray{bgRegionId, 0}},
                                              {QStringLiteral("mask"), QJsonArray{maskTensorId, maskNodeSlot}},
                                              {QStringLiteral("conditioning"),
                                               QJsonArray{positiveClipNodeId, 0}}}}});

    const QString attnId = alloc();
    workflow->insert(attnId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_AttentionMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("model"), QJsonArray{modelNodeId, 0}},
                                              {QStringLiteral("regions"), QJsonArray{defineId, 0}}}}});

    detail::replaceAllLinksFromNode(workflow, modelNodeId, 0, attnId, 0);
    *nextNodeId = nid;
    return true;
}

QJsonObject buildInpaint(const InpaintBuildParams &params)
{
    QJsonObject workflow = detail::parseInpaintingWorkflowTemplate();
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
        QJsonObject n2 = workflow.value(QStringLiteral("2")).toObject();
        QJsonObject i2 = n2.value(QStringLiteral("inputs")).toObject();
        i2.insert(QStringLiteral("image"), params.maskImageName);
        n2.insert(QStringLiteral("inputs"), i2);
        workflow.insert(QStringLiteral("2"), n2);
    }
    QString maskForEncodeId = QStringLiteral("2");
    int maskForEncodeSlot = 1;
    int growNext = 500;
    QString imageForEncodeId = QStringLiteral("1");
    int imageForEncodeSlot = 0;
    QString vaeLinkNode = QStringLiteral("4");
    int vaeLinkSlot = 2;
    QString modelSourceId = QStringLiteral("4");
    QString positiveNodeId = QStringLiteral("5");
    QString negativeNodeId = QStringLiteral("6");
    int negativeNodeSlot = 0;
    {
        CheckpointLoadParams cl;
        cl.checkpoint = ckpt;
        cl.arch = arch;
        cl.loras = checkpointLorasFromStyle(params.styleLoras);
        CheckpointGraphRefs ckptRefs;
        loadCheckpointWithLora(&workflow, cl, &ckptRefs);
        growNext = ckptRefs.nextNodeId;
        int injectId = 90;
        const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("a beautiful painting")
                                                                        : params.positivePrompt;
        detail::patchClipTextEncodeNode(workflow, QStringLiteral("5"), pos, params.promptTranslationLanguage, &injectId);
        detail::patchClipTextEncodeNode(workflow, QStringLiteral("6"), params.negativePrompt, params.promptTranslationLanguage,
                                &injectId);

        vaeLinkNode = ckptRefs.vaeNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.vaeNodeId;
        vaeLinkSlot = ckptRefs.vaeNodeId.isEmpty() ? 2 : ckptRefs.vaeNodeSlot;
        modelSourceId = ckptRefs.modelNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.modelNodeId;

        if (params.useReference && ComfyResources::supportsIpAdapterWorkflow(arch)) {
            ComfyWorkflowEngine::IpAdapterLayerInput ref;
            ref.mode = QString::fromUtf8(ComfyResources::ControlMode::reference);
            ref.imageName = params.imageName;
            ref.strength = 0.5;
            ref.startPercent = 0.2;
            ref.endPercent = 0.8;
            ComfyWorkflowEngine::ConditioningGraphRef graph;
            graph.modelNodeId = modelSourceId;
            graph.positiveNodeId = QStringLiteral("5");
            graph.negativeNodeId = QStringLiteral("6");
            graph.clipSourceNodeId = ckptRefs.clipNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.clipNodeId;
            graph.nextNodeId = growNext;
            applyIpAdapterLayers(&workflow, {ref}, arch, &graph);
            growNext = graph.nextNodeId;
        }

        while (workflow.contains(QString::number(growNext)))
            ++growNext;
        const QString baseMaskId = detail::insertImageToMask(&workflow, &growNext, QStringLiteral("2"), QStringLiteral("red"));
        const int baseMaskSlot = 0;
        maskForEncodeId = baseMaskId;
        maskForEncodeSlot = baseMaskSlot;
        if (params.growMaskBy > 0 || params.featherMaskBy > 0) {
            maskForEncodeId =
                detail::insertInpaintExpandMask(&workflow, &growNext, baseMaskId, params.growMaskBy, params.featherMaskBy,
                                        baseMaskSlot);
            maskForEncodeSlot = 0;
        }
        const int fillGrow = qMax(0, params.growMaskBy - params.featherMaskBy / 2);
        QString fillMaskId = baseMaskId;
        int fillMaskSlot = baseMaskSlot;
        if (fillGrow > 0) {
            fillMaskId = detail::insertInpaintExpandMask(&workflow, &growNext, baseMaskId, fillGrow, 0, baseMaskSlot);
            fillMaskSlot = 0;
        }
        imageForEncodeId = detail::insertMaskedFill(&workflow, &growNext, QStringLiteral("1"), 0, fillMaskId, fillMaskSlot, params.fillKind);
        imageForEncodeSlot = 0;
        if (params.useConditionMask) {
            const QString modelId = ckptRefs.modelNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.modelNodeId;
            const QString clipId = ckptRefs.clipNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.clipNodeId;
            applyInpaintPromptFocus(&workflow, &growNext, modelId, QStringLiteral("5"), clipId, maskForEncodeId,
                                    maskForEncodeSlot, params.backgroundPrompt);
        }

        positiveNodeId = QStringLiteral("5");
        negativeNodeId = QStringLiteral("6");
        negativeNodeSlot = 0;
        if (params.useInpaintModel && !params.controlNetInpaintFile.isEmpty()) {
            const auto patched = detail::applyInpaintControlNetConditioning(&workflow,
                                                                    &growNext,
                                                                    positiveNodeId,
                                                                    negativeNodeId,
                                                                    vaeLinkNode,
                                                                    vaeLinkSlot,
                                                                    imageForEncodeId,
                                                                    imageForEncodeSlot,
                                                                    maskForEncodeId,
                                                                    maskForEncodeSlot,
                                                                    params.controlNetInpaintFile,
                                                                    arch);
            if (patched.first != QStringLiteral("5")) {
                positiveNodeId = patched.first;
                negativeNodeId = patched.second;
                negativeNodeSlot = 1;
            }
        }

        const bool useFooocusInpaint = params.useInpaintModel && arch == ComfyResources::Arch::Sdxl
                                       && !params.fooocusInpaintHead.isEmpty()
                                       && !params.fooocusInpaintPatch.isEmpty();
        const bool isInpaintModelNoControl = params.useInpaintModel && params.controlNetInpaintFile.isEmpty();
        const QString diffId = detail::insertDifferentialDiffusion(&workflow, &growNext, modelSourceId);
        if (useFooocusInpaint) {
            workflow.remove(QStringLiteral("7"));
            const QString condId = QString::number(growNext++);
            workflow.insert(condId,
                            QJsonObject{{QStringLiteral("class_type"),
                                         QStringLiteral("INPAINT_VAEEncodeInpaintConditioning")},
                                        {QStringLiteral("inputs"),
                                         QJsonObject{{QStringLiteral("vae"), QJsonArray{vaeLinkNode, vaeLinkSlot}},
                                                     {QStringLiteral("pixels"), QJsonArray{imageForEncodeId, imageForEncodeSlot}},
                                                     {QStringLiteral("mask"), QJsonArray{maskForEncodeId, maskForEncodeSlot}},
                                                     {QStringLiteral("positive"), QJsonArray{positiveNodeId, 0}},
                                                     {QStringLiteral("negative"), QJsonArray{negativeNodeId, negativeNodeSlot}}}}});
            const QString loadFooocusId = QString::number(growNext++);
            workflow.insert(loadFooocusId,
                            QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_LoadFooocusInpaint")},
                                        {QStringLiteral("inputs"),
                                         QJsonObject{{QStringLiteral("head"), params.fooocusInpaintHead},
                                                     {QStringLiteral("patch"), params.fooocusInpaintPatch}}}});
            const QString applyFooocusId = QString::number(growNext++);
            workflow.insert(applyFooocusId,
                            QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_ApplyFooocusInpaint")},
                                        {QStringLiteral("inputs"),
                                         QJsonObject{{QStringLiteral("model"), QJsonArray{diffId, 0}},
                                                     {QStringLiteral("patch"), QJsonArray{loadFooocusId, 0}},
                                                     {QStringLiteral("latent"), QJsonArray{condId, 2}}}}});
            QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
            QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
            i8.insert(QStringLiteral("positive"), QJsonArray{condId, 0});
            i8.insert(QStringLiteral("negative"), QJsonArray{condId, 1});
            i8.insert(QStringLiteral("latent_image"), QJsonArray{condId, 3});
            n8.insert(QStringLiteral("inputs"), i8);
            workflow.insert(QStringLiteral("8"), n8);
            detail::patchSamplerNode(&workflow, QStringLiteral("8"), applyFooocusId);
        } else if (isInpaintModelNoControl) {
            workflow.remove(QStringLiteral("7"));
            const QString condId = QString::number(growNext++);
            workflow.insert(condId,
                            QJsonObject{{QStringLiteral("class_type"),
                                         QStringLiteral("INPAINT_VAEEncodeInpaintConditioning")},
                                        {QStringLiteral("inputs"),
                                         QJsonObject{{QStringLiteral("vae"), QJsonArray{vaeLinkNode, vaeLinkSlot}},
                                                     {QStringLiteral("pixels"), QJsonArray{imageForEncodeId, imageForEncodeSlot}},
                                                     {QStringLiteral("mask"), QJsonArray{maskForEncodeId, maskForEncodeSlot}},
                                                     {QStringLiteral("positive"), QJsonArray{positiveNodeId, 0}},
                                                     {QStringLiteral("negative"), QJsonArray{negativeNodeId, negativeNodeSlot}}}}});
            QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
            QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
            i8.insert(QStringLiteral("positive"), QJsonArray{condId, 0});
            i8.insert(QStringLiteral("negative"), QJsonArray{condId, 1});
            i8.insert(QStringLiteral("latent_image"), QJsonArray{condId, 3});
            n8.insert(QStringLiteral("inputs"), i8);
            workflow.insert(QStringLiteral("8"), n8);
            detail::patchSamplerNode(&workflow, QStringLiteral("8"), diffId);
        } else {
            workflow.remove(QStringLiteral("7"));
            const QString vaeEncodeId = QString::number(growNext++);
            workflow.insert(vaeEncodeId,
                            QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEEncode")},
                                        {QStringLiteral("inputs"),
                                         QJsonObject{{QStringLiteral("pixels"), QJsonArray{imageForEncodeId, imageForEncodeSlot}},
                                                     {QStringLiteral("vae"), QJsonArray{vaeLinkNode, vaeLinkSlot}}}}});
            const QString latentMaskedId = QString::number(growNext++);
            workflow.insert(latentMaskedId,
                            QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SetLatentNoiseMask")},
                                        {QStringLiteral("inputs"),
                                         QJsonObject{{QStringLiteral("samples"), QJsonArray{vaeEncodeId, 0}},
                                                     {QStringLiteral("mask"), QJsonArray{maskForEncodeId, maskForEncodeSlot}}}}});
            QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
            QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
            i8.insert(QStringLiteral("latent_image"), QJsonArray{latentMaskedId, 0});
            n8.insert(QStringLiteral("inputs"), i8);
            workflow.insert(QStringLiteral("8"), n8);
            detail::patchSamplerNode(&workflow, QStringLiteral("8"), diffId);
        }
    }
    {
        QJsonObject n8 = workflow.value(QStringLiteral("8")).toObject();
        QJsonObject i8 = n8.value(QStringLiteral("inputs")).toObject();
        i8.insert(QStringLiteral("seed"), static_cast<double>(params.seed));
        i8.insert(QStringLiteral("steps"), params.steps);
        i8.insert(QStringLiteral("cfg"), cfg);
        i8.insert(QStringLiteral("denoise"), qBound(0.01, params.denoise, 1.0));
        i8.insert(QStringLiteral("sampler_name"), params.sampler);
        i8.insert(QStringLiteral("scheduler"), params.scheduler.isEmpty() ? QStringLiteral("normal") : params.scheduler);
        n8.insert(QStringLiteral("inputs"), i8);
        workflow.insert(QStringLiteral("8"), n8);
    }

    finishWorkflowWithSamplerCustom(
        &workflow, QStringLiteral("8"), arch, qMax(64, params.initialExtentWidth > 0 ? params.initialExtentWidth : 1024),
        qMax(64, params.initialExtentHeight > 0 ? params.initialExtentHeight : 1024), params.denoise, &growNext);

    while (workflow.contains(QString::number(growNext)))
        ++growNext;

    const int initialW = qMax(64, params.initialExtentWidth > 0 ? params.initialExtentWidth : 1024);
    const int initialH = qMax(64, params.initialExtentHeight > 0 ? params.initialExtentHeight : 1024);
    const int desiredW = qMax(64, params.desiredExtentWidth > 0 ? params.desiredExtentWidth : initialW);
    const int desiredH = qMax(64, params.desiredExtentHeight > 0 ? params.desiredExtentHeight : initialH);

    QString outputImageId = QStringLiteral("9");
    int outputImageSlot = 0;

    if (params.refinementUpscale) {
        outputImageId = detail::appendInpaintRefinementUpscalePass(&workflow,
                                                           &growNext,
                                                           QStringLiteral("9"),
                                                           vaeLinkNode,
                                                           vaeLinkSlot,
                                                           modelSourceId,
                                                           positiveNodeId,
                                                           negativeNodeId,
                                                           negativeNodeSlot,
                                                           maskForEncodeId,
                                                           maskForEncodeSlot,
                                                           params,
                                                           arch);
        outputImageSlot = 0;
    } else {
        QString imageId = QStringLiteral("9");
        int imageSlot = 0;
        if (params.colorMatch) {
            imageId = detail::insertColorMatchAfterImage(&workflow, &growNext, imageId, QStringLiteral("1"), maskForEncodeId,
                                                 maskForEncodeSlot);
            imageSlot = 0;
        }
        if (desiredW != initialW || desiredH != initialH) {
            if (!params.initialBoundsRelative.isEmpty()) {
                imageId = detail::insertCropImage(&workflow, &growNext, imageId, imageSlot, params.initialBoundsRelative);
                imageSlot = 0;
            }
            imageId = detail::insertScaleImage(&workflow, &growNext, imageId, imageSlot, desiredW, desiredH);
            imageSlot = 0;
        }
        outputImageId = imageId;
        outputImageSlot = imageSlot;
    }

    detail::appendNsfwFilterAfterDecode(&workflow, outputImageId, params.nsfwFilterSensitivity, &growNext);

    const int nativeW = qMax(64, params.contextExtentWidth);
    const int nativeH = qMax(64, params.contextExtentHeight);
    const QRect nativeBounds = params.nativeTargetBoundsRelative.isEmpty() ? params.targetBoundsRelative
                                                                           : params.nativeTargetBoundsRelative;
    detail::appendInpaintNativeScaledMaskedSaveOutput(&workflow,
                                              &growNext,
                                              outputImageId,
                                              outputImageSlot,
                                              maskForEncodeId,
                                              maskForEncodeSlot,
                                              nativeBounds,
                                              initialW,
                                              initialH,
                                              nativeW,
                                              nativeH,
                                              params.blendMaskBy);
    return workflow;
}

QJsonObject buildLive(const LiveParams &params)
{
    return buildRefine(params);
}

} // namespace ComfyWorkflowEngine
