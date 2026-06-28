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

void packLatentLayersAfterSampler(QJsonObject *workflow, int layerCount, int batchSlice)
{
    if (!workflow || layerCount <= 1)
        return;

    QString decodeId;
    for (auto it = workflow->constBegin(); it != workflow->constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("VAEDecode")) {
            decodeId = it.key();
            break;
        }
    }
    if (decodeId.isEmpty())
        return;

    QJsonObject decode = workflow->value(decodeId).toObject();
    QJsonObject inputs = decode.value(QStringLiteral("inputs")).toObject();
    const QJsonArray samplesLink = inputs.value(QStringLiteral("samples")).toArray();
    if (samplesLink.size() < 2)
        return;

    int nextId = 500;
    while (workflow->contains(QString::number(nextId)))
        ++nextId;
    const QString cutId = QString::number(nextId++);
    workflow->insert(cutId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LatentCutToBatch")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("samples"), samplesLink},
                                              {QStringLiteral("dim"), QStringLiteral("t")},
                                              {QStringLiteral("slice_size"), qMax(1, batchSlice)}}}});
    inputs.insert(QStringLiteral("samples"), QJsonArray{cutId, 0});
    decode.insert(QStringLiteral("inputs"), inputs);
    workflow->insert(decodeId, decode);
}

bool applyIpAdapterLayers(QJsonObject *workflow,
                          const QList<IpAdapterLayerInput> &layers,
                          ComfyResources::Arch arch,
                          ConditioningGraphRef *graph)
{
    if (!workflow || workflow->isEmpty() || !ComfyResources::supportsIpAdapterWorkflow(arch))
        return false;

    const QString clipVisionFile = ComfyResources::defaultClipVisionFileName(arch);
    const QString ipAdapterFile = ComfyResources::defaultIpAdapterFileName(arch, QStringLiteral("reference"));
    const QString ipFaceFile = ComfyResources::defaultIpAdapterFaceFileName(arch);
    if (clipVisionFile.isEmpty() || ipAdapterFile.isEmpty())
        return false;

    QString modelSource = graph && !graph->modelNodeId.isEmpty() ? graph->modelNodeId : QStringLiteral("4");
    int nextId = graph ? graph->nextNodeId : 100;
    ConditioningGraphRef tileGraph;
    if (graph) {
        tileGraph = *graph;
    }
    bool applied = false;

    const QString clipVisionId = QString::number(nextId++);
    workflow->insert(clipVisionId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPVisionLoader")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("clip_name"), clipVisionFile}}}});

    const QString ipAdapterId = QString::number(nextId++);
    workflow->insert(ipAdapterId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("IPAdapterModelLoader")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("ipadapter_file"), ipAdapterFile}}}});

    QString insightFaceId;
    if (!ipFaceFile.isEmpty()) {
        for (const IpAdapterLayerInput &layer : layers) {
            if (layer.imageName.isEmpty() || layer.mode != QLatin1String("face"))
                continue;
            if (insightFaceId.isEmpty()) {
                insightFaceId = QString::number(nextId++);
                workflow->insert(insightFaceId,
                                 QJsonObject{
                                     {QStringLiteral("class_type"), QStringLiteral("IPAdapterInsightFaceLoader")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("provider"), QStringLiteral("CPU")},
                                                  {QStringLiteral("model_name"), QStringLiteral("buffalo_l")}}}});
            }
            const QString ipFaceAdapterId = QString::number(nextId++);
            workflow->insert(ipFaceAdapterId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("IPAdapterModelLoader")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("ipadapter_file"), ipFaceFile}}}});
            const QString imageId =
                graph ? detail::insertConditioningImageNode(workflow, &nextId, layer.imageName, tileGraph)
                      : QString::number(nextId++);
            if (!graph) {
                workflow->insert(imageId,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
            }
            const QString faceApplyId = QString::number(nextId++);
            workflow->insert(faceApplyId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("IPAdapterFaceID")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("model"), QJsonArray{modelSource, 0}},
                                                      {QStringLiteral("ipadapter"), QJsonArray{ipFaceAdapterId, 0}},
                                                      {QStringLiteral("image"), QJsonArray{imageId, 0}},
                                                      {QStringLiteral("clip_vision"), QJsonArray{clipVisionId, 0}},
                                                      {QStringLiteral("insightface"), QJsonArray{insightFaceId, 0}},
                                                      {QStringLiteral("weight"), layer.strength},
                                                      {QStringLiteral("weight_faceidv2"), layer.strength * 2.0},
                                                      {QStringLiteral("weight_type"), QStringLiteral("linear")},
                                                      {QStringLiteral("combine_embeds"), QStringLiteral("concat")},
                                                      {QStringLiteral("start_at"), layer.startPercent},
                                                      {QStringLiteral("end_at"), layer.endPercent},
                                                      {QStringLiteral("embeds_scaling"), QStringLiteral("V only")}}}});
            modelSource = faceApplyId;
            applied = true;
        }
    }

    const QStringList embedModes = {QStringLiteral("reference"), QStringLiteral("style"),
                                    QStringLiteral("composition")};
    for (const QString &embedMode : embedModes) {
        QList<const IpAdapterLayerInput *> group;
        for (const IpAdapterLayerInput &layer : layers) {
            if (layer.imageName.isEmpty())
                continue;
            if (layer.mode == embedMode)
                group.append(&layer);
        }
        if (group.isEmpty())
            continue;

        QStringList encoderIds;
        double rangeLo = 1.0;
        double rangeHi = 0.0;
        for (const IpAdapterLayerInput *layer : group) {
            const QString imageId =
                graph ? detail::insertConditioningImageNode(workflow, &nextId, layer->imageName, tileGraph)
                      : QString::number(nextId++);
            if (!graph) {
                workflow->insert(imageId,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("image"), layer->imageName}}}});
            }
            const QString encoderId = QString::number(nextId++);
            workflow->insert(encoderId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("IPAdapterEncoder")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), QJsonArray{imageId, 0}},
                                                      {QStringLiteral("weight"), layer->strength},
                                                      {QStringLiteral("ipadapter"), QJsonArray{ipAdapterId, 0}},
                                                      {QStringLiteral("clip_vision"), QJsonArray{clipVisionId, 0}}}}});
            encoderIds.append(encoderId);
            rangeLo = qMin(rangeLo, layer->startPercent);
            rangeHi = qMax(rangeHi, layer->endPercent);
        }

        QString embedsNode = encoderIds.first();
        if (encoderIds.size() > 1) {
            QJsonObject combineInputs;
            combineInputs.insert(QStringLiteral("method"), QStringLiteral("concat"));
            for (int i = 0; i < encoderIds.size(); i++)
                combineInputs.insert(QStringLiteral("embed%1").arg(i + 1), QJsonArray{encoderIds.at(i), 0});
            embedsNode = QString::number(nextId++);
            workflow->insert(embedsNode,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("IPAdapterCombineEmbeds")},
                                         {QStringLiteral("inputs"), combineInputs}});
        }

        const QString applyId = QString::number(nextId++);
        workflow->insert(applyId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("IPAdapterEmbeds")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("model"), QJsonArray{modelSource, 0}},
                                                  {QStringLiteral("ipadapter"), QJsonArray{ipAdapterId, 0}},
                                                  {QStringLiteral("pos_embed"), QJsonArray{embedsNode, 0}},
                                                  {QStringLiteral("clip_vision"), QJsonArray{clipVisionId, 0}},
                                                  {QStringLiteral("weight"), 1.0},
                                                  {QStringLiteral("weight_type"), detail::ipAdapterWeightType(embedMode)},
                                                  {QStringLiteral("embeds_scaling"), QStringLiteral("V only")},
                                                  {QStringLiteral("start_at"), rangeLo},
                                                  {QStringLiteral("end_at"), rangeHi}}}});
        modelSource = applyId;
        applied = true;
    }

    if (!applied)
        return false;

    if (graph) {
        graph->modelNodeId = modelSource;
        graph->nextNodeId = nextId;
        if (!graph->samplerNodeId.isEmpty())
            detail::patchSamplerNode(workflow, graph->samplerNodeId, modelSource);
    } else {
        detail::patchSamplerNode(workflow, QStringLiteral("3"), modelSource);
    }
    return true;
}

bool applyControlNetLayers(QJsonObject *workflow,
                             const QList<ControlNetLayerInput> &layers,
                             ComfyResources::Arch arch,
                             ConditioningGraphRef *graph)
{
    if (!workflow || workflow->isEmpty())
        return false;

    QString positiveNode = graph && !graph->positiveNodeId.isEmpty() ? graph->positiveNodeId : QStringLiteral("6");
    const QString negativeNode = graph && !graph->negativeNodeId.isEmpty() ? graph->negativeNodeId : QStringLiteral("7");
    int nextId = graph ? graph->nextNodeId : 50;
    bool applied = false;
    ConditioningGraphRef tileGraph;
    if (graph) {
        tileGraph = *graph;
    }

    const QString ckptId = detail::findCheckpointNodeId(*workflow);
    QString modelSource = graph && !graph->modelNodeId.isEmpty() ? graph->modelNodeId : ckptId;
    if (modelSource.isEmpty())
        modelSource = QStringLiteral("4");
    const QString initialModelSource = modelSource;
    const QString vaeSource = ckptId.isEmpty() ? QStringLiteral("4") : ckptId;

    for (const ControlNetLayerInput &layer : layers) {
        if (layer.imageName.isEmpty())
            continue;
        if (ComfyResources::ControlMode::isIpAdapter(layer.mode))
            continue;
        if (!ComfyResources::ControlMode::isStructural(layer.mode))
            continue;
        QString cnFile = ComfyResources::defaultControlNetFileName(arch, layer.mode);

        if (cnFile.isEmpty() && arch == ComfyResources::Arch::ZImage) {
            const QString patchFile = ComfyResources::defaultZImageFunControlPatchFileName();
            if (patchFile.isEmpty())
                continue;
            const QString patchId = QString::number(nextId++);
            workflow->insert(patchId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ModelPatchLoader")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("name"), patchFile}}}});
            const QString imageId =
                graph ? detail::insertConditioningImageNode(workflow, &nextId, layer.imageName, tileGraph)
                      : QString::number(nextId++);
            if (!graph) {
                workflow->insert(imageId,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
            }
            QJsonArray vaeLink = QJsonArray{vaeSource, 2};
            const QString decodeId = detail::findNodeIdByClassType(*workflow, QStringLiteral("VAEDecode"));
            if (!decodeId.isEmpty()) {
                const QJsonArray vaeArr = workflow->value(decodeId)
                                              .toObject()
                                              .value(QStringLiteral("inputs"))
                                              .toObject()
                                              .value(QStringLiteral("vae"))
                                              .toArray();
                if (vaeArr.size() >= 2)
                    vaeLink = vaeArr;
            }
            const QString zimgId = QString::number(nextId++);
            workflow->insert(zimgId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ZImageFunControlnet")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("model"), QJsonArray{modelSource, 0}},
                                                      {QStringLiteral("model_patch"), QJsonArray{patchId, 0}},
                                                      {QStringLiteral("vae"), vaeLink},
                                                      {QStringLiteral("image"), QJsonArray{imageId, 0}},
                                                      {QStringLiteral("strength"), layer.strength}}}});
            modelSource = zimgId;
            applied = true;
            continue;
        }

        if (cnFile.isEmpty())
            continue;

        const QString loaderId = QString::number(nextId++);
        QString imageId =
            graph ? detail::insertConditioningImageNode(workflow, &nextId, layer.imageName, tileGraph)
                  : QString::number(nextId++);
        const QString applyId = QString::number(nextId++);

        if (!graph) {
            workflow->insert(imageId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
        }
        if (ComfyResources::ControlMode::isLines(layer.mode)) {
            const QString invertId = QString::number(nextId++);
            workflow->insert(invertId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageInvert")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), QJsonArray{imageId, 0}}}}});
            imageId = invertId;
        }
        workflow->insert(loaderId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ControlNetLoader")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("control_net_name"), cnFile}}}});
        QString controlNetLink = loaderId;
        if (ComfyResources::controlNetUsesUnionTypeNode(cnFile, layer.mode)) {
            const QString typeId = QString::number(nextId++);
            workflow->insert(typeId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SetUnionControlNetType")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("control_net"), QJsonArray{loaderId, 0}},
                                                      {QStringLiteral("type"),
                                                       ComfyResources::unionControlNetTypeForMode(layer.mode)}}}});
            controlNetLink = typeId;
        }
        workflow->insert(applyId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ControlNetApplyAdvanced")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("positive"), QJsonArray{positiveNode, 0}},
                                                  {QStringLiteral("negative"), QJsonArray{negativeNode, 0}},
                                                  {QStringLiteral("control_net"), QJsonArray{controlNetLink, 0}},
                                                  {QStringLiteral("image"), QJsonArray{imageId, 0}},
                                                  {QStringLiteral("strength"), layer.strength},
                                                  {QStringLiteral("start_percent"), layer.startPercent},
                                                  {QStringLiteral("end_percent"), layer.endPercent}}}});

        positiveNode = applyId;
        applied = true;
    }

    if (!applied)
        return false;

    if (graph) {
        graph->positiveNodeId = positiveNode;
        graph->nextNodeId = nextId;
        if (!graph->samplerNodeId.isEmpty()) {
            if (modelSource != initialModelSource)
                detail::patchSamplerNode(workflow, graph->samplerNodeId, modelSource);
            detail::patchSamplerNode(workflow, graph->samplerNodeId, QString(), positiveNode);
        }
    } else {
        if (modelSource != initialModelSource)
            detail::patchSamplerNode(workflow, QStringLiteral("3"), modelSource);
        detail::patchSamplerNode(workflow, QStringLiteral("3"), QString(), positiveNode);
    }
    return true;
}

RegionalWorkflowNodes applyRegionalGeneration(QJsonObject *workflow,
                                              const QList<RegionalPromptInput> &regions,
                                              const QString &modelSourceNode,
                                              const QString &rootPositiveNode,
                                              const QString &rootNegativeNode,
                                              const QString &clipSourceNode,
                                              ConditioningGraphRef *graph)
{
    RegionalWorkflowNodes result;
    if (!workflow || regions.size() < 2)
        return result;

    int nextId = graph ? graph->nextNodeId : 200;
    const bool tiledMask = graph && !graph->tileLayoutNodeId.isEmpty() && graph->tileIndex >= 0;
    const QString bgRegionId = QString::number(nextId++);
    workflow->insert(bgRegionId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_BackgroundRegion")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("conditioning"), QJsonArray{rootPositiveNode, 0}}}}});
    QString regionsChain = bgRegionId;

    for (const RegionalPromptInput &region : regions) {
        if (region.isBackground || region.maskImageName.isEmpty())
            continue;

        QString maskImageNode = QString::number(nextId++);
        workflow->insert(maskImageNode,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), region.maskImageName}}}});
        if (tiledMask) {
            const QString extractId = QString::number(nextId++);
            workflow->insert(extractId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_ExtractImageTile")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), QJsonArray{maskImageNode, 0}},
                                                      {QStringLiteral("layout"), QJsonArray{graph->tileLayoutNodeId, 0}},
                                                      {QStringLiteral("index"), graph->tileIndex}}}});
            maskImageNode = extractId;
        }
        const QString maskId = QString::number(nextId++);
        workflow->insert(maskId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageToMask")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{maskImageNode, 0}},
                                                  {QStringLiteral("channel"), QStringLiteral("alpha")}}}});
        const QString regionPosId = QString::number(nextId++);
        workflow->insert(regionPosId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPTextEncode")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{
                                          {QStringLiteral("clip"), QJsonArray{clipSourceNode, 1}},
                                          {QStringLiteral("text"),
                                           detail::clipEncodeTextInput(region.positivePrompt,
                                                               region.promptTranslationLanguage,
                                                               workflow,
                                                               &nextId)}}}});
        const QString defineId = QString::number(nextId++);
        workflow->insert(defineId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_DefineRegion")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("regions"), QJsonArray{regionsChain, 0}},
                                                  {QStringLiteral("mask"), QJsonArray{maskId, 0}},
                                                  {QStringLiteral("conditioning"), QJsonArray{regionPosId, 0}}}}});
        regionsChain = defineId;
    }

    const QString attnId = QString::number(nextId++);
    workflow->insert(attnId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_AttentionMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("model"), QJsonArray{modelSourceNode, 0}},
                                              {QStringLiteral("regions"), QJsonArray{regionsChain, 0}}}}});

    const QString listMasksId = QString::number(nextId++);
    workflow->insert(listMasksId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_ListRegionMasks")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("regions"), QJsonArray{regionsChain, 0}}}}});

    const QString maskBatchImgId = QString::number(nextId++);
    workflow->insert(maskBatchImgId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("MaskToImage")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{listMasksId, 0}}}}});

    QString combinedPos;
    for (int i = 0; i < regions.size(); i++) {
        const RegionalPromptInput &region = regions.at(i);
        const QString regionPosId = QString::number(nextId++);
        workflow->insert(regionPosId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPTextEncode")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{
                                          {QStringLiteral("clip"), QJsonArray{clipSourceNode, 1}},
                                          {QStringLiteral("text"),
                                           detail::clipEncodeTextInput(region.positivePrompt,
                                                               region.promptTranslationLanguage,
                                                               workflow,
                                                               &nextId)}}}});

        const QString batchImgId = QString::number(nextId++);
        workflow->insert(batchImgId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageFromBatch")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{maskBatchImgId, 0}},
                                                  {QStringLiteral("batch_index"), i},
                                                  {QStringLiteral("length"), 1}}}});
        const QString regionMaskId = QString::number(nextId++);
        workflow->insert(regionMaskId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageToMask")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{batchImgId, 0}},
                                                  {QStringLiteral("channel"), QStringLiteral("red")}}}});

        const QString combineId = QString::number(nextId++);
        if (combinedPos.isEmpty()) {
            workflow->insert(combineId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("PairConditioningSetProperties")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("positive_NEW"), QJsonArray{regionPosId, 0}},
                                                      {QStringLiteral("negative_NEW"), QJsonArray{rootNegativeNode, 0}},
                                                      {QStringLiteral("mask"), QJsonArray{regionMaskId, 0}},
                                                      {QStringLiteral("strength"), 1.0},
                                                      {QStringLiteral("set_cond_area"), QStringLiteral("default")}}}});
        } else {
            workflow->insert(combineId,
                             QJsonObject{{QStringLiteral("class_type"),
                                          QStringLiteral("PairConditioningSetPropertiesAndCombine")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("positive"), QJsonArray{combinedPos, 0}},
                                                      {QStringLiteral("negative"), QJsonArray{combinedPos, 1}},
                                                      {QStringLiteral("positive_NEW"), QJsonArray{regionPosId, 0}},
                                                      {QStringLiteral("negative_NEW"), QJsonArray{rootNegativeNode, 0}},
                                                      {QStringLiteral("mask"), QJsonArray{regionMaskId, 0}},
                                                      {QStringLiteral("strength"), 1.0},
                                                      {QStringLiteral("set_cond_area"), QStringLiteral("default")}}}});
        }
        combinedPos = combineId;
    }

    if (combinedPos.isEmpty())
        combinedPos = rootPositiveNode;

    result.modelNodeId = attnId;
    result.positiveNodeId = combinedPos;
    result.negativeNodeId = combinedPos;
    result.applied = true;

    if (graph) {
        graph->modelNodeId = attnId;
        graph->positiveNodeId = combinedPos;
        graph->negativeNodeId = combinedPos;
        graph->nextNodeId = nextId;
        if (!graph->samplerNodeId.isEmpty())
            detail::patchSamplerNode(workflow, graph->samplerNodeId, attnId, combinedPos, combinedPos);
    } else {
        detail::patchSamplerNode(workflow, QStringLiteral("3"), attnId, combinedPos, combinedPos);
    }

    return result;
}

} // namespace ComfyWorkflowEngine
