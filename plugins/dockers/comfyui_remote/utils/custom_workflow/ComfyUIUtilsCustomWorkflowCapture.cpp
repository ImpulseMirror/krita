/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyResources.h"
#include "ComfyControlLayer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>

#include <kis_image.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>

#include <KisViewManager.h>

namespace ComfyUIUtils {

namespace {

ResolvedSamplerInputs resolveSamplerFromStyleRegular(const ComfyStyleEntry *styleEntry,
                                                     const QJsonObject &settings,
                                                     const QString &dockSamplerText,
                                                     int dockSteps,
                                                     double dockCfg)
{
    ResolvedSamplerInputs r;
    QString key = styleEntry && !styleEntry->samplerPresetName.isEmpty() ? styleEntry->samplerPresetName
                                                                         : settings.value(QStringLiteral("quality_sampler_preset")).toString().trimmed();
    if (!key.isEmpty()) {
        const QJsonObject root = builtinSamplerPresetsRoot();
        QString sam, sch;
        int st = 0, minSt = 0;
        double cfg = 7.0;
        if (samplerPresetLookup(root, key, &sam, &sch, &st, &minSt, &cfg)) {
            r.sampler = sam;
            r.scheduler = sch;
            r.steps = qMax(st, minSt);
            r.minSteps = minSt;
            r.cfg = cfg;
            return r;
        }
    }
    r.sampler = dockSamplerText.trimmed().isEmpty() ? QStringLiteral("euler") : dockSamplerText.trimmed();
    r.scheduler = QStringLiteral("normal");
    if (styleEntry && styleEntry->samplerSteps > 0)
        r.steps = styleEntry->samplerSteps;
    else
        r.steps = dockSteps;
    if (styleEntry && styleEntry->cfgScale > 0)
        r.cfg = styleEntry->cfgScale;
    else
        r.cfg = dockCfg;
    return r;
}

} // namespace

CustomWorkflowStyleBundle resolveCustomWorkflowStyleBundle(const ComfyStyleEntry *styleEntry,
                                                           const QJsonObject &settings,
                                                           const QString &dockSamplerText,
                                                           int dockSteps,
                                                           double dockCfg,
                                                           const QString &nodeSamplerPreset,
                                                           bool customGenerationModeLive)
{
    CustomWorkflowStyleBundle out;
    if (!styleEntry) {
        out.errorMessage = ComfyTr::tr("Style not found for custom workflow.");
        return out;
    }
    if (styleEntry->checkpoints.isEmpty()) {
        out.errorMessage = ComfyTr::tr("Style has no checkpoint for custom workflow.");
        return out;
    }
    out.checkpoint = styleEntry->checkpoints.first();
    out.styleLoras = styleEntry->loras;
    out.positivePrompt = styleEntry->stylePrompt;
    out.negativePrompt = styleEntry->negativePrompt;
    const ResolvedSamplerInputs si =
        customWorkflowNodeUsesLiveSampling(nodeSamplerPreset, customGenerationModeLive)
            ? resolveSamplerForLive(styleEntry, settings, dockSamplerText, dockSteps, dockCfg)
            : resolveSamplerFromStyleRegular(styleEntry, settings, dockSamplerText, dockSteps, dockCfg);
    out.sampler = si.sampler;
    out.scheduler = si.scheduler;
    out.steps = si.steps;
    out.cfg = si.cfg;
    out.ok = true;
    return out;
}

ExtractLorasFromPromptResult extractLorasFromPrompt(QString prompt)
{
    ExtractLorasFromPromptResult out;
    out.cleanedPrompt = prompt;
    ComfyFileLibrary::instance().init();
    static const QRegularExpression pattern(
        QStringLiteral("<lora:([^:<>]+)(?::(-?[^:<>]*))?>"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = pattern.globalMatch(prompt);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString input = match.captured(1).trimmed().toLower();
        if (input.isEmpty())
            continue;
        const ComfyFileRecord *loraFile = nullptr;
        for (const ComfyFileRecord &file : ComfyFileLibrary::instance().loras().files()) {
            if (file.source == ComfyFileSourceUnavailable)
                continue;
            const QString stem = QFileInfo(file.id).completeBaseName().toLower();
            const QString normalized = file.name.trimmed().toLower();
            if (input == stem || input == normalized || input == file.id.trimmed().toLower()) {
                loraFile = &file;
                break;
            }
        }
        if (!loraFile) {
            out.errorMessage = ComfyTr::tr("LoRA not found: %1").arg(match.captured(1).trimmed());
            return out;
        }
        double strength = loraFile->meta(QStringLiteral("lora_strength")).toDouble(1.0);
        if (strength <= 0.0)
            strength = loraFile->meta(QStringLiteral("strength_percent")).toInt(100) / 100.0;
        if (match.lastCapturedIndex() >= 2 && !match.captured(2).trimmed().isEmpty()) {
            bool ok = false;
            const double parsed = match.captured(2).trimmed().toDouble(&ok);
            if (ok)
                strength = parsed;
        }
        ExtractedPromptLora l;
        l.name = loraFile->id;
        l.strength = qBound(0.01, strength, 4.0);
        out.loras.append(l);
    }
    out.cleanedPrompt = prompt;
    out.cleanedPrompt.remove(pattern);
    out.cleanedPrompt = out.cleanedPrompt.trimmed();
    return out;
}

CustomWorkflowEvaluatedPrompts prepareCustomWorkflowStyleAndPrompts(const QString &userPositive,
                                                                      const QString &userNegative,
                                                                      const ComfyStyleEntry *styleEntry,
                                                                      const qint64 seed,
                                                                      const double effectiveCfg,
                                                                      const QString &translationLanguage,
                                                                      const ComfyResources::Arch arch)
{
    CustomWorkflowEvaluatedPrompts out;
    const QString promptRaw = stripPromptComments(userPositive).trimmed();
    const QString negativeRaw = stripPromptComments(userNegative).trimmed();
    QString pos = promptRaw;
    pos = evalWildcards(pos, static_cast<quint32>(seed & 0xFFFFFFFFu));
    if (pos != promptRaw)
        out.metadata.insert(QStringLiteral("prompt_eval"), pos);
    const ExtractLorasFromPromptResult loras = extractLorasFromPrompt(pos);
    if (!loras.errorMessage.isEmpty()) {
        out.ok = false;
        out.errorMessage = loras.errorMessage;
        return out;
    }
    pos = loras.cleanedPrompt;
    out.promptLoras = loras.loras;
    const QString layerReplace = layerPlaceholderReplacementForArch(arch);
    extractLayerPlaceholders(pos, layerReplace);
    if (styleEntry)
        pos = mergeStyleLoraTriggersIntoPositivePrompt(pos, styleEntry->loras);
    if (styleEntry)
        pos = mergeStylePromptWithInstruction(styleEntry->stylePrompt, pos);
    if (!translationLanguage.trimmed().isEmpty())
        pos = wrapPromptWithTranslationLanguage(pos, translationLanguage.trimmed());
    out.positiveFinal = pos;
    out.metadata.insert(QStringLiteral("prompt"), promptRaw);
    out.metadata.insert(QStringLiteral("prompt_final"), pos);

    if (effectiveCfg <= 1.0 + 1e-6) {
        out.negativeFinal = QString();
        out.metadata.insert(QStringLiteral("negative_prompt"), negativeRaw);
        out.metadata.insert(QStringLiteral("negative_prompt_final"), QString());
        return out;
    }
    QString neg = negativeRaw;
    neg = evalWildcards(neg, static_cast<quint32>(seed & 0xFFFFFFFFu));
    if (neg != negativeRaw)
        out.metadata.insert(QStringLiteral("negative_prompt_eval"), neg);
    if (styleEntry)
        neg = mergeStylePromptWithInstruction(styleEntry->negativePrompt, neg);
    out.negativeFinal = neg;
    out.metadata.insert(QStringLiteral("negative_prompt"), negativeRaw);
    out.metadata.insert(QStringLiteral("negative_prompt_final"), neg);
    return out;
}


namespace {

QString firstPaintLayerUuid(KisImageSP image)
{
    if (!image || !image->rootLayer())
        return QString();
    QList<KisNodeSP> nodes;
    nodes.append(image->rootLayer());
    while (!nodes.isEmpty()) {
        KisNodeSP n = nodes.takeFirst();
        if (dynamic_cast<KisPaintLayer *>(n.data()))
            return n->uuid().toString(QUuid::WithoutBraces);
        for (quint32 i = 0; i < n->childCount(); ++i)
            nodes.append(n->at(i));
    }
    return QString();
}

QString firstInpaintContextMaskLayerUuid(KisImageSP image)
{
    if (!image || !image->rootLayer())
        return QString();
    QList<KisNodeSP> nodes;
    nodes.append(image->rootLayer());
    while (!nodes.isEmpty()) {
        KisNodeSP n = nodes.takeFirst();
        if (isInpaintContextMaskNode(n))
            return n->uuid().toString(QUuid::WithoutBraces);
        for (quint32 i = 0; i < n->childCount(); ++i)
            nodes.append(n->at(i));
    }
    return QString();
}

QString layerNodeNameByUuid(KisImageSP image, const QString &uuidWithoutBraces, bool maskLayerOnly)
{
    if (!image || uuidWithoutBraces.isEmpty() || !image->rootLayer())
        return QString();
    QList<KisNodeSP> nodes;
    nodes.append(image->rootLayer());
    while (!nodes.isEmpty()) {
        KisNodeSP n = nodes.takeFirst();
        if (n->uuid().toString(QUuid::WithoutBraces) == uuidWithoutBraces) {
            if (maskLayerOnly && !isInpaintContextMaskNode(n))
                return QString();
            if (!maskLayerOnly && !dynamic_cast<KisPaintLayer *>(n.data()))
                return QString();
            return n->name();
        }
        for (quint32 i = 0; i < n->childCount(); ++i)
            nodes.append(n->at(i));
    }
    return QString();
}

QByteArray imagePngDigest(const QImage &image)
{
    if (image.isNull())
        return QByteArray();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return QByteArray();
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

} // namespace

QString resolveCustomWorkflowLayerName(KisImageSP image,
                                       const QString &nodeId,
                                       const QString &paramName,
                                       const QString &nameFromWorkflow,
                                       const QMap<QString, QVariant> &paramOverrides,
                                       const bool maskLayer)
{
    if (!nameFromWorkflow.trimmed().isEmpty())
        return nameFromWorkflow.trimmed();
    auto resolveUuid = [&](const QVariant &value) -> QString {
        return layerNodeNameByUuid(image, value.toString().trimmed(), maskLayer);
    };
    if (paramOverrides.contains(nodeId)) {
        const QString resolved = resolveUuid(paramOverrides.value(nodeId));
        if (!resolved.isEmpty())
            return resolved;
    }
    if (!paramName.isEmpty() && paramOverrides.contains(paramName)) {
        const QString resolved = resolveUuid(paramOverrides.value(paramName));
        if (!resolved.isEmpty())
            return resolved;
    }
    if (maskLayer)
        return layerNodeNameByUuid(image, firstInpaintContextMaskLayerUuid(image), true);
    return paintLayerNameByUuid(image, firstPaintLayerUuid(image));
}

QImage exportCustomWorkflowLayerImage(KisImageSP image,
                                      KisViewManager *viewManager,
                                      const QString &layerName,
                                      const QRect &exportBoundsInDoc,
                                      const bool maskLayer)
{
    if (!image || layerName.isEmpty() || exportBoundsInDoc.isEmpty())
        return QImage();
    const QRect docBounds = image->bounds();
    QImage layerImg;
    if (maskLayer) {
        layerImg = getMaskAsQImage(image, viewManager, QStringLiteral("layer:") + layerName, false);
    } else {
        layerImg = getLayerProjectionAsQImage(image, layerName);
    }
    if (layerImg.isNull())
        return QImage();
    layerImg = cropImageToDocumentRect(layerImg, exportBoundsInDoc, docBounds);
    if (layerImg.isNull())
        return QImage();
    if (maskLayer)
        return maskPngForComfyUpload(layerImg.convertToFormat(QImage::Format_Grayscale8));
    return layerImg.convertToFormat(QImage::Format_ARGB32);
}

QByteArray computeCustomWorkflowInputFingerprint(const QJsonObject &workflow,
                                                 const CustomWorkflowKritaCapture &capture,
                                                 qint64 seed,
                                                 const QString &positivePrompt,
                                                 const QString &negativePrompt,
                                                 const QJsonArray &loraMetadata,
                                                 const QMap<QString, QVariant> &paramOverrides)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QJsonDocument(workflow).toJson(QJsonDocument::Compact));
    const QRect b = capture.captureBounds;
    hash.addData(QByteArray::number(b.x()));
    hash.addData(QByteArray::number(b.y()));
    hash.addData(QByteArray::number(b.width()));
    hash.addData(QByteArray::number(b.height()));
    hash.addData(QByteArray::number(seed));
    hash.addData(positivePrompt.toUtf8());
    hash.addData(negativePrompt.toUtf8());
    hash.addData(QJsonDocument(loraMetadata).toJson(QJsonDocument::Compact));
    hash.addData(imagePngDigest(capture.canvasImage));
    if (capture.hasSelectionMask)
        hash.addData(imagePngDigest(capture.maskImage));
    QList<QString> overrideKeys = paramOverrides.keys();
    std::sort(overrideKeys.begin(), overrideKeys.end());
    for (const QString &key : overrideKeys)
        hash.addData((key + QLatin1Char('=') + paramOverrides.value(key).toString()).toUtf8());
    return hash.result();
}


CustomWorkflowKritaCapture captureCustomWorkflowKritaInput(KisImageSP image,
                                                           KisViewManager *viewManager,
                                                           const QJsonObject &workflow,
                                                           double strength0to1,
                                                           bool excludeInternal,
                                                           const QList<ComfyControlLayerEntry> &rootControlLayers,
                                                           const QString &previewLayerId)
{
    CustomWorkflowKritaCapture out;
    if (!image || !viewManager) {
        out.errorMessage = ComfyTr::tr("Open a document first.");
        return out;
    }
    const QRect docBounds = image->bounds();
    if (docBounds.isEmpty()) {
        out.errorMessage = ComfyTr::tr("Document has no image bounds.");
        return out;
    }

    CustomWorkflowMaskPrepareResult maskPrep;
    const QString selectionNodeId = findFirstWorkflowNodeIdByClassType(workflow, QStringLiteral("ETN_KritaSelection"));
    if (!selectionNodeId.isEmpty()) {
        const QJsonObject selectionInputs =
            workflow.value(selectionNodeId).toObject().value(QStringLiteral("inputs")).toObject();
        const QString ctx = getInpaintContextFromSelectionNode(selectionInputs);
        if (!isValidCustomWorkflowSelectionContext(ctx)) {
            out.errorMessage =
                ComfyTr::tr("Invalid inpaint context for custom workflow selection node: %1").arg(ctx);
            return out;
        }
        const SelectionModifiers mods = getSelectionModifiersForContext(ctx, strength0to1);
        const MaskFromSelectionResult maskResult = createMaskFromSelection(image, viewManager, mods);
        maskPrep = prepareCustomWorkflowMask(selectionInputs, maskResult, docBounds);
        if (!maskPrep.ok && !maskPrep.errorMessage.isEmpty()) {
            out.errorMessage = maskPrep.errorMessage;
            return out;
        }
    } else {
        maskPrep.captureBounds = docBounds;
    }

    const QList<KisNodeSP> excludeNodes =
        collectInpaintExcludeNodes(image, excludeInternal, rootControlLayers, previewLayerId);
    const DocumentImageResult docCapture = getDocumentImage(image, maskPrep.captureBounds, excludeNodes);
    if (!docCapture) {
        out.errorMessage = docCapture.errorMessage.isEmpty() ? ComfyTr::tr("Could not export canvas for custom workflow.")
                                                             : docCapture.errorMessage;
        return out;
    }

    out.captureBounds = maskPrep.captureBounds;
    out.canvasImage = docCapture.image.convertToFormat(QImage::Format_ARGB32);
    out.maskImage = maskPrep.maskInCaptureCoords;
    out.hasSelectionMask = maskPrep.hasSelectionMask;
    out.ok = true;
    return out;
}

} // namespace ComfyUIUtils
