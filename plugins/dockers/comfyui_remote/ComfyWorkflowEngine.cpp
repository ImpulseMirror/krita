/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyWorkflowEngine.h"

#include "ComfyUIWorkflows.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace ComfyWorkflowEngine {

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

static QJsonObject parseDefaultWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(defaultWorkflow), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QJsonObject buildTextToImage(const TextToImageParams &params)
{
    QJsonObject workflow = parseDefaultWorkflowTemplate();
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
        QJsonObject n4 = workflow.value(QStringLiteral("4")).toObject();
        QJsonObject i4 = n4.value(QStringLiteral("inputs")).toObject();
        i4.insert(QStringLiteral("ckpt_name"), ckpt);
        n4.insert(QStringLiteral("inputs"), i4);
        workflow.insert(QStringLiteral("4"), n4);
    }
    {
        QJsonObject n5 = workflow.value(QStringLiteral("5")).toObject();
        QJsonObject i5 = n5.value(QStringLiteral("inputs")).toObject();
        i5.insert(QStringLiteral("width"), qMax(64, params.width));
        i5.insert(QStringLiteral("height"), qMax(64, params.height));
        i5.insert(QStringLiteral("batch_size"), qMax(1, params.batchSize));
        n5.insert(QStringLiteral("inputs"), i5);
        workflow.insert(QStringLiteral("5"), n5);
    }
    {
        QJsonObject n6 = workflow.value(QStringLiteral("6")).toObject();
        QJsonObject i6 = n6.value(QStringLiteral("inputs")).toObject();
        const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("a beautiful painting")
                                                                        : params.positivePrompt;
        i6.insert(QStringLiteral("text"), pos);
        n6.insert(QStringLiteral("inputs"), i6);
        workflow.insert(QStringLiteral("6"), n6);
    }
    {
        QJsonObject n7 = workflow.value(QStringLiteral("7")).toObject();
        QJsonObject i7 = n7.value(QStringLiteral("inputs")).toObject();
        i7.insert(QStringLiteral("text"), params.negativePrompt);
        n7.insert(QStringLiteral("inputs"), i7);
        workflow.insert(QStringLiteral("7"), n7);
    }

    return workflow;
}

static QString ipAdapterWeightType(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("style"))
        return QStringLiteral("style transfer");
    if (m == QLatin1String("composition"))
        return QStringLiteral("composition");
    return QStringLiteral("linear");
}

bool applyIpAdapterLayers(QJsonObject *workflow,
                          const QList<IpAdapterLayerInput> &layers,
                          ComfyResources::Arch arch)
{
    if (!workflow || workflow->isEmpty() || !ComfyResources::supportsIpAdapterWorkflow(arch))
        return false;

    const QString clipVisionFile = ComfyResources::defaultClipVisionFileName(arch);
    const QString ipAdapterFile = ComfyResources::defaultIpAdapterFileName(arch, QStringLiteral("reference"));
    const QString ipFaceFile = ComfyResources::defaultIpAdapterFaceFileName(arch);
    if (clipVisionFile.isEmpty() || ipAdapterFile.isEmpty())
        return false;

    QString modelSource = QStringLiteral("4");
    int nextId = 100;
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
            const QString imageId = QString::number(nextId++);
            workflow->insert(imageId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
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
            const QString imageId = QString::number(nextId++);
            workflow->insert(imageId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), layer->imageName}}}});
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
                                                  {QStringLiteral("weight_type"), ipAdapterWeightType(embedMode)},
                                                  {QStringLiteral("embeds_scaling"), QStringLiteral("V only")},
                                                  {QStringLiteral("start_at"), rangeLo},
                                                  {QStringLiteral("end_at"), rangeHi}}}});
        modelSource = applyId;
        applied = true;
    }

    if (!applied)
        return false;

    QJsonObject sampler = workflow->value(QStringLiteral("3")).toObject();
    QJsonObject samplerInputs = sampler.value(QStringLiteral("inputs")).toObject();
    samplerInputs.insert(QStringLiteral("model"), QJsonArray{modelSource, 0});
    sampler.insert(QStringLiteral("inputs"), samplerInputs);
    workflow->insert(QStringLiteral("3"), sampler);
    return true;
}

bool applyControlNetLayers(QJsonObject *workflow,
                             const QList<ControlNetLayerInput> &layers,
                             ComfyResources::Arch arch)
{
    if (!workflow || workflow->isEmpty())
        return false;

    QString positiveNode = QStringLiteral("6");
    const QString negativeNode = QStringLiteral("7");
    int nextId = 50;
    bool applied = false;

    for (const ControlNetLayerInput &layer : layers) {
        if (layer.imageName.isEmpty())
            continue;
        if (ComfyResources::ControlMode::isIpAdapter(layer.mode))
            continue;
        if (!ComfyResources::ControlMode::isStructural(layer.mode))
            continue;
        const QString cnFile = ComfyResources::defaultControlNetFileName(arch, layer.mode);
        if (cnFile.isEmpty())
            continue;

        const QString loaderId = QString::number(nextId++);
        const QString imageId = QString::number(nextId++);
        const QString applyId = QString::number(nextId++);

        workflow->insert(imageId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
        workflow->insert(loaderId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ControlNetLoader")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("control_net_name"), cnFile}}}});
        workflow->insert(applyId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ControlNetApplyAdvanced")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("positive"), QJsonArray{positiveNode, 0}},
                                                  {QStringLiteral("negative"), QJsonArray{negativeNode, 0}},
                                                  {QStringLiteral("control_net"), QJsonArray{loaderId, 0}},
                                                  {QStringLiteral("image"), QJsonArray{imageId, 0}},
                                                  {QStringLiteral("strength"), layer.strength},
                                                  {QStringLiteral("start_percent"), layer.startPercent},
                                                  {QStringLiteral("end_percent"), layer.endPercent}}}});

        positiveNode = applyId;
        applied = true;
    }

    if (!applied)
        return false;

    QJsonObject sampler = workflow->value(QStringLiteral("3")).toObject();
    QJsonObject samplerInputs = sampler.value(QStringLiteral("inputs")).toObject();
    samplerInputs.insert(QStringLiteral("positive"), QJsonArray{positiveNode, 0});
    sampler.insert(QStringLiteral("inputs"), samplerInputs);
    workflow->insert(QStringLiteral("3"), sampler);
    return true;
}

} // namespace ComfyWorkflowEngine
