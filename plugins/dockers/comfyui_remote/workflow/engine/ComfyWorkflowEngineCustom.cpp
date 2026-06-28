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

namespace {

using OutputReplacement = QHash<int, QJsonValue>;

QJsonValue remapWorkflowInputValue(const QJsonValue &value, const QHash<QString, OutputReplacement> &replacements)
{
    if (!value.isArray())
        return value;
    const QJsonArray arr = value.toArray();
    if (arr.size() != 2)
        return value;
    const QString nodeId = arr.at(0).toString();
    const int outIdx = arr.at(1).toInt();
    const auto nodeRepl = replacements.constFind(nodeId);
    if (nodeRepl == replacements.constEnd())
        return value;
    const auto outRepl = nodeRepl->constFind(outIdx);
    if (outRepl == nodeRepl->constEnd())
        return value;
    return *outRepl;
}

QJsonObject remapWorkflowInputs(const QJsonObject &inputs, const QHash<QString, OutputReplacement> &replacements)
{
    QJsonObject remapped;
    for (auto it = inputs.constBegin(); it != inputs.constEnd(); ++it)
        remapped.insert(it.key(), remapWorkflowInputValue(it.value(), replacements));
    return remapped;
}

int workflowMaxNumericNodeId(const QJsonObject &workflow)
{
    int maxId = 0;
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        bool ok = false;
        const int id = it.key().toInt(&ok);
        if (ok)
            maxId = qMax(maxId, id);
    }
    return maxId;
}

bool isCustomKritaNodeExpandedByClient(const QString &classType)
{
    return classType == QLatin1String("ETN_KritaCanvas") || classType == QLatin1String("ETN_KritaSelection")
        || classType == QLatin1String("ETN_Parameter") || classType == QLatin1String("ETN_KritaImageLayer")
        || classType == QLatin1String("ETN_KritaMaskLayer") || classType == QLatin1String("ETN_KritaStyle")
        || classType == QLatin1String("ETN_KritaStyleAndPrompt");
}

void injectCheckpointOutputs(const CustomWorkflowStyleExpandInput &style,
                             const std::function<QString()> &allocId,
                             QJsonObject *injectedNodes,
                             OutputReplacement *outs)
{
    const QString ckpt =
        style.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : style.checkpoint.trimmed();
    const QString ckptId = allocId();
    injectedNodes->insert(ckptId,
                          QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CheckpointLoaderSimple")},
                                      {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("ckpt_name"), ckpt}}}});

    QString modelChain = ckptId;
    QString clipChain = ckptId;
    const QString vaeChain = ckptId;
    int clipSlot = 1;
    for (const CheckpointLoraWeight &lora : style.loras) {
        if (lora.name.isEmpty())
            continue;
        const QString loraId = allocId();
        injectedNodes->insert(loraId,
                              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoraLoader")},
                                          {QStringLiteral("inputs"),
                                           QJsonObject{{QStringLiteral("lora_name"), lora.name},
                                                       {QStringLiteral("strength_model"), lora.strengthModel},
                                                       {QStringLiteral("strength_clip"), lora.strengthClip},
                                                       {QStringLiteral("model"), QJsonArray{modelChain, 0}},
                                                       {QStringLiteral("clip"), QJsonArray{clipChain, clipSlot}}}}});
        modelChain = clipChain = loraId;
        clipSlot = 1;
    }

    outs->insert(0, QJsonArray{modelChain, 0});
    outs->insert(1, QJsonArray{clipChain, clipSlot});
    outs->insert(2, QJsonArray{vaeChain, 2});
    outs->insert(3, style.positivePrompt);
    outs->insert(4, style.negativePrompt);
    outs->insert(5, style.sampler);
    outs->insert(6, style.scheduler);
    outs->insert(7, style.steps);
    outs->insert(8, style.cfg);
}

} // namespace

QJsonObject expandCustomKritaWorkflowNodes(const ExpandCustomKritaWorkflowParams &params)
{
    const QJsonObject source = params.workflow;
    if (source.isEmpty())
        return source;

    QHash<QString, OutputReplacement> replacements;
    QJsonObject injectedNodes;
    int nextId = workflowMaxNumericNodeId(source) + 1;
    auto allocId = [&]() { return QString::number(nextId++); };

    QStringList etnIds;
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        if (isCustomKritaNodeExpandedByClient(node.value(QStringLiteral("class_type")).toString()))
            etnIds.append(it.key());
    }

    for (const QString &oldId : etnIds) {
        const QJsonObject node = source.value(oldId).toObject();
        const QString classType = node.value(QStringLiteral("class_type")).toString();
        const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        if (classType == QLatin1String("ETN_KritaCanvas")) {
            const QString loadId = allocId();
            injectedNodes.insert(loadId,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("image"), params.canvasImageName}}}});
            OutputReplacement outs;
            outs.insert(0, QJsonArray{loadId, 0});
            outs.insert(1, params.captureBounds.width());
            outs.insert(2, params.captureBounds.height());
            outs.insert(3, static_cast<qint64>(params.seed));
            outs.insert(4, QJsonArray{loadId, 1});
            replacements.insert(oldId, outs);
        } else if (classType == QLatin1String("ETN_KritaSelection")) {
            OutputReplacement outs;
            if (params.hasSelectionMask && !params.maskImageName.isEmpty()) {
                const QString loadId = allocId();
                injectedNodes.insert(loadId,
                                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                                 {QStringLiteral("inputs"),
                                                  QJsonObject{{QStringLiteral("image"), params.maskImageName}}}});
                outs.insert(0, QJsonArray{loadId, 1});
            } else {
                const QString solidId = allocId();
                injectedNodes.insert(solidId,
                                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SolidMask")},
                                                 {QStringLiteral("inputs"),
                                                  QJsonObject{{QStringLiteral("width"), params.captureBounds.width()},
                                                              {QStringLiteral("height"), params.captureBounds.height()},
                                                              {QStringLiteral("value"), 1.0}}}});
                outs.insert(0, QJsonArray{solidId, 0});
            }
            outs.insert(1, params.hasSelectionMask);
            outs.insert(2, params.captureBounds.x());
            outs.insert(3, params.captureBounds.y());
            replacements.insert(oldId, outs);
        } else if (classType == QLatin1String("ETN_Parameter")) {
            OutputReplacement outs;
            const QJsonValue def = inputs.value(QStringLiteral("default"));
            outs.insert(0, def.isUndefined() ? QJsonValue(QString()) : def);
            replacements.insert(oldId, outs);
        } else if (classType == QLatin1String("ETN_KritaImageLayer")) {
            const QString uploaded = params.layerUploadByNodeId.value(oldId);
            OutputReplacement outs;
            if (!uploaded.isEmpty()) {
                const QString loadId = allocId();
                injectedNodes.insert(loadId,
                                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                                 {QStringLiteral("inputs"),
                                                  QJsonObject{{QStringLiteral("image"), uploaded}}}});
                outs.insert(0, QJsonArray{loadId, 0});
                outs.insert(1, QJsonArray{loadId, 1});
            }
            replacements.insert(oldId, outs);
        } else if (classType == QLatin1String("ETN_KritaMaskLayer")) {
            const QString uploaded = params.layerUploadByNodeId.value(oldId);
            OutputReplacement outs;
            if (!uploaded.isEmpty()) {
                const QString loadId = allocId();
                injectedNodes.insert(loadId,
                                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                                 {QStringLiteral("inputs"),
                                                  QJsonObject{{QStringLiteral("image"), uploaded}}}});
                outs.insert(0, QJsonArray{loadId, 1});
            }
            replacements.insert(oldId, outs);
        } else if (classType == QLatin1String("ETN_KritaStyle")) {
            OutputReplacement outs;
            const CustomWorkflowStyleExpandInput styleIn = params.kritaStyleByNodeId.value(oldId);
            if (!styleIn.checkpoint.isEmpty())
                injectCheckpointOutputs(styleIn, allocId, &injectedNodes, &outs);
            replacements.insert(oldId, outs);
        } else if (classType == QLatin1String("ETN_KritaStyleAndPrompt")) {
            OutputReplacement outs;
            CustomWorkflowStyleExpandInput styleIn;
            styleIn.checkpoint = params.checkpoint;
            styleIn.loras = params.loras;
            styleIn.positivePrompt = params.positivePrompt;
            styleIn.negativePrompt = params.negativePrompt;
            styleIn.sampler = params.sampler;
            styleIn.scheduler = params.scheduler;
            styleIn.steps = params.steps;
            styleIn.cfg = params.cfg;
            injectCheckpointOutputs(styleIn, allocId, &injectedNodes, &outs);
            replacements.insert(oldId, outs);
        }
    }

    QJsonObject expanded;
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        const QString nodeId = it.key();
        const QJsonObject node = it.value().toObject();
        const QString classType = node.value(QStringLiteral("class_type")).toString();
        if (isCustomKritaNodeExpandedByClient(classType))
            continue;
        QJsonObject copy = node;
        copy.insert(QStringLiteral("inputs"),
                    remapWorkflowInputs(node.value(QStringLiteral("inputs")).toObject(), replacements));
        expanded.insert(nodeId, copy);
    }
    for (auto it = injectedNodes.constBegin(); it != injectedNodes.constEnd(); ++it)
        expanded.insert(it.key(), it.value());
    return expanded;
}

} // namespace ComfyWorkflowEngine
