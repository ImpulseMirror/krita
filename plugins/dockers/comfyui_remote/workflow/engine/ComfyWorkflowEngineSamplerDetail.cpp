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

namespace detail {

QString insertConditioningImageNode(QJsonObject *workflow,
                                    int *nextId,
                                    const QString &imageName,
                                    const ComfyWorkflowEngine::ConditioningGraphRef &graph)
{
    QString imageNode = QString::number((*nextId)++);
    workflow->insert(imageNode,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), imageName}}}});
    if (graph.scaledWidth > 0 && graph.scaledHeight > 0) {
        const QString scaleId = QString::number((*nextId)++);
        workflow->insert(scaleId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageScale")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{imageNode, 0}},
                                                  {QStringLiteral("width"), graph.scaledWidth},
                                                  {QStringLiteral("height"), graph.scaledHeight},
                                                  {QStringLiteral("upscale_method"), QStringLiteral("lanczos")}}}});
        imageNode = scaleId;
    }
    if (!graph.tileLayoutNodeId.isEmpty() && graph.tileIndex >= 0) {
        const QString extractId = QString::number((*nextId)++);
        workflow->insert(extractId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_ExtractImageTile")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{imageNode, 0}},
                                                  {QStringLiteral("layout"), QJsonArray{graph.tileLayoutNodeId, 0}},
                                                  {QStringLiteral("index"), graph.tileIndex}}}});
        imageNode = extractId;
    }
    return imageNode;
}

void patchSamplerNode(QJsonObject *workflow,
                      const QString &samplerNodeId,
                      const QString &modelNodeId,
                      const QString &positiveNodeId,
                      const QString &negativeNodeId)
{
    if (!workflow || samplerNodeId.isEmpty() || !workflow->contains(samplerNodeId))
        return;
    QJsonObject sampler = workflow->value(samplerNodeId).toObject();
    QJsonObject samplerInputs = sampler.value(QStringLiteral("inputs")).toObject();
    if (!modelNodeId.isEmpty())
        samplerInputs.insert(QStringLiteral("model"), QJsonArray{modelNodeId, 0});
    if (!positiveNodeId.isEmpty())
        samplerInputs.insert(QStringLiteral("positive"), QJsonArray{positiveNodeId, 0});
    if (!negativeNodeId.isEmpty())
        samplerInputs.insert(QStringLiteral("negative"), QJsonArray{negativeNodeId, 0});
    sampler.insert(QStringLiteral("inputs"), samplerInputs);
    workflow->insert(samplerNodeId, sampler);
}

PromptOutput addReferenceLatentPair(QJsonObject *workflow,
                                    int *nextId,
                                    const QString &positiveId,
                                    const QString &negativeId,
                                    const QString &latentId)
{
    const QString posRef = QString::number((*nextId)++);
    workflow->insert(posRef,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ReferenceLatent")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("conditioning"), QJsonArray{positiveId, 0}},
                                              {QStringLiteral("latent"), QJsonArray{latentId, 0}}}}});
    const QString negRef = QString::number((*nextId)++);
    workflow->insert(negRef,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ReferenceLatent")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("conditioning"), QJsonArray{negativeId, 0}},
                                              {QStringLiteral("latent"), QJsonArray{latentId, 0}}}}});
    return {posRef, negRef};
}

PromptOutput applyReferenceConditioningForTile(QJsonObject *workflow,
                                               int *nextId,
                                               const PromptOutput &prompt,
                                               const QString &tileImageId,
                                               const QString &latentId,
                                               const QString &vaeId,
                                               const QList<ComfyWorkflowEngine::IpAdapterLayerInput> &ipLayers,
                                               bool editReference,
                                               ComfyResources::Arch arch)
{
    if (!ComfyResources::supportsEditInstructions(arch))
        return prompt;

    PromptOutput out = prompt;
    const auto addRef = [&](const QString &latentSource) {
        out = addReferenceLatentPair(workflow, nextId, out.positiveId, out.negativeId, latentSource);
    };

    const bool flux2Family =
        arch == ComfyResources::Arch::Flux2_4b || arch == ComfyResources::Arch::Flux2_9b
        || arch == ComfyResources::Arch::QwenEP;
    const bool fluxKOrQwenE = arch == ComfyResources::Arch::FluxK || arch == ComfyResources::Arch::QwenE;

    if (flux2Family || fluxKOrQwenE) {
        QStringList extraImageNodes;
        for (const ComfyWorkflowEngine::IpAdapterLayerInput &layer : ipLayers) {
            if (layer.imageName.isEmpty() || !ComfyResources::ControlMode::isIpAdapter(layer.mode))
                continue;
            const QString loadId = QString::number((*nextId)++);
            workflow->insert(loadId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
            extraImageNodes.append(loadId);
        }

        if (fluxKOrQwenE && !extraImageNodes.isEmpty()) {
            QStringList stitchImages;
            if (editReference && !tileImageId.isEmpty())
                stitchImages.append(tileImageId);
            stitchImages.append(extraImageNodes);
            QString stitched = stitchImages.first();
            for (int i = 1; i < stitchImages.size(); ++i) {
                const QString stitchId = QString::number((*nextId)++);
                workflow->insert(stitchId,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageStitch")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("image1"), QJsonArray{stitched, 0}},
                                                          {QStringLiteral("image2"), QJsonArray{stitchImages.at(i), 0}},
                                                          {QStringLiteral("direction"), QStringLiteral("right")},
                                                          {QStringLiteral("match_image_size"), false},
                                                          {QStringLiteral("spacing_width"), 0},
                                                          {QStringLiteral("spacing_color"), QStringLiteral("white")}}}});
                stitched = stitchId;
            }
            const QString encoded = QString::number((*nextId)++);
            workflow->insert(encoded,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEEncode")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("pixels"), QJsonArray{stitched, 0}},
                                                      {QStringLiteral("vae"), QJsonArray{vaeId, 2}}}}});
            addRef(encoded);
        } else {
            if (editReference && !latentId.isEmpty())
                addRef(latentId);
            for (const QString &imgId : extraImageNodes) {
                const QString encoded = QString::number((*nextId)++);
                workflow->insert(encoded,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEEncode")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("pixels"), QJsonArray{imgId, 0}},
                                                          {QStringLiteral("vae"), QJsonArray{vaeId, 2}}}}});
                addRef(encoded);
            }
        }
    }

    return out;
}

QString addSamplerCustomAdvanced(QJsonObject *workflow,
                                 int *nextId,
                                 const QString &modelId,
                                 const QString &positiveId,
                                 const QString &negativeId,
                                 const QString &latentId,
                                 ComfyResources::Arch arch,
                                 double cfg,
                                 int steps,
                                 int startAtStep,
                                 const QString &sampler,
                                 const QString &scheduler,
                                 qint64 seed,
                                 int extentW,
                                 int extentH,
                                 int negativeSlot,
                                 int latentSlot)
{
    QString guidedPositive = positiveId;
    if (ComfyResources::isFluxLike(arch)) {
        const double guidance = cfg > 1.0 ? cfg : 3.5;
        guidedPositive = QString::number((*nextId)++);
        workflow->insert(guidedPositive,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("FluxGuidance")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("conditioning"), QJsonArray{positiveId, 0}},
                                                  {QStringLiteral("guidance"), guidance}}}});
    }

    const QString guiderId = QString::number((*nextId)++);
    if (ComfyResources::isFluxLike(arch) || qAbs(cfg - 1.0) < 0.001) {
        workflow->insert(guiderId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("BasicGuider")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("model"), QJsonArray{modelId, 0}},
                                                  {QStringLiteral("conditioning"), QJsonArray{guidedPositive, 0}}}}});
    } else {
        workflow->insert(guiderId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CFGGuider")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("model"), QJsonArray{modelId, 0}},
                                                  {QStringLiteral("positive"), QJsonArray{positiveId, 0}},
                                                  {QStringLiteral("negative"), QJsonArray{negativeId, negativeSlot}},
                                                  {QStringLiteral("cfg"), cfg}}}});
    }

    QString sigmasId;
    const QString sched = scheduler.trimmed().toLower();
    if (sched == QLatin1String("flux2")) {
        sigmasId = QString::number((*nextId)++);
        workflow->insert(sigmasId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("Flux2Scheduler")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("steps"), steps},
                                                  {QStringLiteral("width"), qMax(64, extentW)},
                                                  {QStringLiteral("height"), qMax(64, extentH)}}}});
    } else {
        sigmasId = QString::number((*nextId)++);
        workflow->insert(sigmasId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("BasicScheduler")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("model"), QJsonArray{modelId, 0}},
                                                  {QStringLiteral("scheduler"), scheduler},
                                                  {QStringLiteral("steps"), steps},
                                                  {QStringLiteral("denoise"), 1.0}}}});
    }

    QString sigmasLink = sigmasId;
    int sigmasOutput = 0;
    if (startAtStep > 0) {
        const QString splitId = QString::number((*nextId)++);
        workflow->insert(splitId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SplitSigmas")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("sigmas"), QJsonArray{sigmasId, 0}},
                                                  {QStringLiteral("step"), startAtStep}}}});
        sigmasLink = splitId;
        sigmasOutput = 1;
    }

    const QString noiseId = QString::number((*nextId)++);
    workflow->insert(noiseId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("RandomNoise")},
                                 {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("noise_seed"), seed}}}});

    QString samplerSelectId;
    if (sampler == QLatin1String("euler_cfgpp")) {
        samplerSelectId = QString::number((*nextId)++);
        workflow->insert(samplerSelectId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SamplerEulerCFGpp")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("version"), QStringLiteral("regular")}}}});
    } else {
        samplerSelectId = QString::number((*nextId)++);
        workflow->insert(samplerSelectId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSamplerSelect")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("sampler_name"), sampler}}}});
    }

    const QString samplerAdvancedId = QString::number((*nextId)++);
    workflow->insert(samplerAdvancedId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SamplerCustomAdvanced")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("noise"), QJsonArray{noiseId, 0}},
                                              {QStringLiteral("guider"), QJsonArray{guiderId, 0}},
                                              {QStringLiteral("sampler"), QJsonArray{samplerSelectId, 0}},
                                              {QStringLiteral("sigmas"), QJsonArray{sigmasLink, sigmasOutput}},
                                              {QStringLiteral("latent_image"), QJsonArray{latentId, latentSlot}}}}});
    return samplerAdvancedId;
}

bool replaceWorkflowSamplerWithCustomAdvanced(QJsonObject *workflow,
                                              const QString &ksamplerNodeId,
                                              ComfyResources::Arch arch,
                                              int extentW,
                                              int extentH,
                                              int *nextNodeIdInOut)
{
    if (!workflow || !workflow->contains(ksamplerNodeId))
        return false;
    const QJsonObject ks = workflow->value(ksamplerNodeId).toObject();
    if (ks.value(QStringLiteral("class_type")).toString() != QLatin1String("KSampler"))
        return false;

    const QJsonObject inputs = ks.value(QStringLiteral("inputs")).toObject();
    const QJsonArray modelArr = inputs.value(QStringLiteral("model")).toArray();
    const QJsonArray posArr = inputs.value(QStringLiteral("positive")).toArray();
    const QJsonArray negArr = inputs.value(QStringLiteral("negative")).toArray();
    const QJsonArray latentArr = inputs.value(QStringLiteral("latent_image")).toArray();
    if (modelArr.isEmpty() || posArr.isEmpty() || negArr.isEmpty() || latentArr.isEmpty())
        return false;

    const QString modelId = modelArr.at(0).toString();
    const QString posId = posArr.at(0).toString();
    const QString negId = negArr.at(0).toString();
    const QString latentId = latentArr.at(0).toString();
    const int latentSlot = latentArr.size() > 1 ? latentArr.at(1).toInt(0) : 0;
    const int negativeSlot = negArr.size() > 1 ? negArr.at(1).toInt(0) : 0;
    const double cfg = inputs.value(QStringLiteral("cfg")).toDouble(7.0);
    const int steps = inputs.value(QStringLiteral("steps")).toInt(20);
    const double denoise = inputs.value(QStringLiteral("denoise")).toDouble(1.0);
    const QString sampler = inputs.value(QStringLiteral("sampler_name")).toString(QStringLiteral("euler"));
    const QString scheduler =
        inputs.value(QStringLiteral("scheduler")).toString(QStringLiteral("normal"));
    const qint64 seed = static_cast<qint64>(inputs.value(QStringLiteral("seed")).toDouble(0));

    int nextId = nextNodeIdInOut ? *nextNodeIdInOut : 500;
    while (workflow->contains(QString::number(nextId)))
        ++nextId;

    const int startAtStep =
        static_cast<int>(qRound(steps * (1.0 - qBound(0.01, denoise, 1.0))));
    const QString newSampler = addSamplerCustomAdvanced(workflow,
                                                        &nextId,
                                                        modelId,
                                                        posId,
                                                        negId,
                                                        latentId,
                                                        arch,
                                                        cfg,
                                                        steps,
                                                        startAtStep,
                                                        sampler,
                                                        scheduler,
                                                        seed,
                                                        extentW,
                                                        extentH,
                                                        negativeSlot,
                                                        latentSlot);

    workflow->remove(ksamplerNodeId);
    replaceInputLink(workflow, QJsonArray{ksamplerNodeId, 0}, QJsonArray{newSampler, 1});

    if (nextNodeIdInOut)
        *nextNodeIdInOut = nextId;
    return true;
}

void finishBuilderWithSamplerCustom(QJsonObject *workflow,
                                    const QString &samplerNodeId,
                                    ComfyResources::Arch arch,
                                    int extentW,
                                    int extentH,
                                    double /*denoise*/,
                                    int *nextNodeIdInOut)
{
    replaceWorkflowSamplerWithCustomAdvanced(workflow, samplerNodeId, arch, extentW, extentH, nextNodeIdInOut);
}

QList<ComfyWorkflowEngine::RegionalPromptInput> filterRegionalPromptsForTile(

    const QList<ComfyWorkflowEngine::RegionalPromptInput> &regions,
    const QRect &tileBounds)
{
    QList<ComfyWorkflowEngine::RegionalPromptInput> out;
    for (const ComfyWorkflowEngine::RegionalPromptInput &region : regions) {
        if (region.isBackground) {
            out.append(region);
            continue;
        }
        if (!region.maskBoundsUpscaled.isValid()) {
            out.append(region);
            continue;
        }
        const QRect inter = tileBounds.intersected(region.maskBoundsUpscaled);
        const double coverage =
            static_cast<double>(qMax(0, inter.width()) * qMax(0, inter.height()))
            / static_cast<double>(qMax(1, tileBounds.width() * tileBounds.height()));
        if (coverage > 0.1)
            out.append(region);
    }
    return out;
}

} // namespace detail

} // namespace ComfyWorkflowEngine
