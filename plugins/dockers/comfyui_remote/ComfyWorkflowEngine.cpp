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

} // namespace ComfyWorkflowEngine
