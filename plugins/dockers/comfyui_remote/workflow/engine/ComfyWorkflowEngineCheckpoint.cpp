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

QList<CheckpointLoraWeight> checkpointLorasFromStyle(const QJsonArray &styleLoras)
{
    QList<CheckpointLoraWeight> out;
    for (const QJsonValue &v : styleLoras) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("enabled")).toBool(true))
            continue;
        QString name = o.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            name = o.value(QStringLiteral("filename")).toString().trimmed();
        if (name.isEmpty())
            continue;
        double strength = 1.0;
        if (o.contains(QStringLiteral("strength")))
            strength = o.value(QStringLiteral("strength")).toDouble(1.0);
        else if (o.contains(QStringLiteral("strength_percent")))
            strength = o.value(QStringLiteral("strength_percent")).toInt(100) / 100.0;
        if (qFuzzyIsNull(strength))
            continue;
        CheckpointLoraWeight w;
        w.name = name;
        w.strengthModel = qBound(0.01, strength, 4.0);
        w.strengthClip = w.strengthModel;
        out.append(w);
    }
    return out;
}

QList<CheckpointLoraWeight> mergeCheckpointLorasUnique(const QList<CheckpointLoraWeight> &base,
                                                       const QList<CheckpointLoraWeight> &extra)
{
    QList<CheckpointLoraWeight> out = base;
    for (const CheckpointLoraWeight &e : extra) {
        if (e.name.isEmpty())
            continue;
        bool replaced = false;
        for (CheckpointLoraWeight &existing : out) {
            if (existing.name == e.name) {
                existing = e;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            out.append(e);
    }
    return out;
}

bool loadCheckpointWithLora(QJsonObject *workflow, const CheckpointLoadParams &params, CheckpointGraphRefs *out)
{
    if (!workflow || workflow->isEmpty())
        return false;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();
    QString ckptId = detail::findCheckpointNodeId(*workflow);
    if (ckptId.isEmpty())
        return false;

    int nextId = 500;
    while (workflow->contains(QString::number(nextId)))
        ++nextId;

    QString modelId = ckptId;
    QString clipLinkNode = ckptId;
    int clipLinkSlot = 1;
    QString vaeLinkNode = ckptId;
    int vaeLinkSlot = 2;

    const bool nunchaku = ComfyResources::isNunchakuCheckpointFilename(ckpt);
    const bool needsSplitLoader =
        nunchaku && ComfyResources::isFluxLike(params.arch)
        || (ComfyResources::isFluxLike(params.arch) && ckpt.endsWith(QStringLiteral(".gguf"), Qt::CaseInsensitive));

    if (needsSplitLoader && ComfyResources::isFluxLike(params.arch) && nunchaku) {
        const QString nunchakuId = QString::number(nextId++);
        const double cache = params.dynamicCaching ? 0.12 : 0.0;
        workflow->insert(nunchakuId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("NunchakuFluxDiTLoader")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("model_path"), ckpt},
                                                  {QStringLiteral("cache_threshold"), cache}}}});
        modelId = nunchakuId;

        const ComfyResources::DualClipLoadSpec dual = ComfyResources::defaultDualClipLoadSpec(params.arch);
        if (!dual.clipName1.isEmpty()) {
            const QString dualId = QString::number(nextId++);
            QJsonObject dualInputs{{QStringLiteral("clip_name1"), dual.clipName1},
                                   {QStringLiteral("type"), dual.type}};
            if (!dual.clipName2.isEmpty())
                dualInputs.insert(QStringLiteral("clip_name2"), dual.clipName2);
            workflow->insert(dualId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("DualCLIPLoader")},
                                         {QStringLiteral("inputs"), dualInputs}});
            clipLinkNode = dualId;
            clipLinkSlot = 0;
        }

        workflow->remove(ckptId);
        detail::replaceAllLinksFromNode(workflow, ckptId, 0, modelId, 0);
        detail::replaceAllLinksFromNode(workflow, ckptId, 1, clipLinkNode, clipLinkSlot);
        detail::replaceAllLinksFromNode(workflow, ckptId, 2, vaeLinkNode, vaeLinkSlot);
        ckptId = modelId;
    } else {
        QJsonObject n = workflow->value(ckptId).toObject();
        QJsonObject i = n.value(QStringLiteral("inputs")).toObject();
        i.insert(QStringLiteral("ckpt_name"), ckpt);
        n.insert(QStringLiteral("inputs"), i);
        workflow->insert(ckptId, n);
    }

    QString modelChain = modelId;
    QString clipChainNode = clipLinkNode;
    int clipChainSlot = clipLinkSlot;

    for (const CheckpointLoraWeight &lora : params.loras) {
        if (lora.name.isEmpty())
            continue;
        if (nunchaku && ComfyResources::isFluxLike(params.arch)) {
            const QString loraId = QString::number(nextId++);
            workflow->insert(loraId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("NunchakuFluxLoraLoader")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("model"), QJsonArray{modelChain, 0}},
                                                      {QStringLiteral("lora_name"), lora.name},
                                                      {QStringLiteral("lora_strength"), lora.strengthModel}}}});
            modelChain = loraId;
        } else {
            const QString loraId = QString::number(nextId++);
            workflow->insert(loraId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoraLoader")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("model"), QJsonArray{modelChain, 0}},
                                                      {QStringLiteral("clip"), QJsonArray{clipChainNode, clipChainSlot}},
                                                      {QStringLiteral("lora_name"), lora.name},
                                                      {QStringLiteral("strength_model"), lora.strengthModel},
                                                      {QStringLiteral("strength_clip"), lora.strengthClip}}}});
            modelChain = loraId;
            clipChainNode = loraId;
            clipChainSlot = 1;
        }
    }

    if (modelChain != ckptId)
        detail::replaceAllLinksFromNode(workflow, ckptId, 0, modelChain, 0);
    if (clipChainNode != ckptId || clipChainSlot != 1)
        detail::replaceAllLinksFromNode(workflow, ckptId, 1, clipChainNode, clipChainSlot);

    if (out) {
        out->modelNodeId = modelChain;
        out->clipNodeId = clipChainNode;
        out->vaeNodeId = vaeLinkNode;
        out->vaeNodeSlot = vaeLinkSlot;
        out->nextNodeId = nextId;
    }
    return true;
}

ComfyResources::Arch resolveArch(const QString &checkpoint, const QString &styleArchitecture)
{
    const QString sa = styleArchitecture.trimmed().toLower();
    if (!sa.isEmpty() && sa != QLatin1String("auto")) {
        ComfyResources::Arch fromStyle = ComfyResources::archFromKey(sa);
        if (fromStyle != ComfyResources::Arch::Unknown)
            return fromStyle;
    }
    ComfyResources::Arch fromCkpt = ComfyResources::archFromCheckpointName(checkpoint);
    if (fromCkpt != ComfyResources::Arch::Unknown)
        return fromCkpt;
    const QString lower = checkpoint.trimmed().toLower();
    if (lower.contains(QLatin1String("xl")))
        return ComfyResources::Arch::Sdxl;
    return ComfyResources::Arch::Sd15;
}

} // namespace ComfyWorkflowEngine
