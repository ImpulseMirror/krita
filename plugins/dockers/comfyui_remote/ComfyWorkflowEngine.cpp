/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyWorkflowEngine.h"

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

struct PromptOutput {
    QString positiveId;
    QString negativeId;
};

void patchSamplerNode(QJsonObject *workflow,
                      const QString &samplerNodeId,
                      const QString &modelNodeId = QString(),
                      const QString &positiveNodeId = QString(),
                      const QString &negativeNodeId = QString());

PromptOutput applyReferenceConditioningForTile(QJsonObject *workflow,
                                               int *nextId,
                                               const PromptOutput &prompt,
                                               const QString &tileImageId,
                                               const QString &latentId,
                                               const QString &vaeId,
                                               const QList<ComfyWorkflowEngine::IpAdapterLayerInput> &ipLayers,
                                               bool editReference,
                                               ComfyResources::Arch arch);

void finishBuilderWithSamplerCustom(QJsonObject *workflow,
                                    const QString &samplerNodeId,
                                    ComfyResources::Arch arch,
                                    int extentW,
                                    int extentH,
                                    double denoise);

QJsonValue clipEncodeTextInput(const QString &prompt,
                               const QString &translationLang,
                               QJsonObject *workflow,
                               int *nextInjectId)
{
    if (translationLang.trimmed().isEmpty() || prompt.trimmed().isEmpty())
        return QJsonValue(prompt);
    const QString wrapped = ComfyUIUtils::wrapPromptWithTranslationLanguage(prompt, translationLang);
    const QString id = QString::number((*nextInjectId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_Translate")},
                                 {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("text"), wrapped}}}});
    return QJsonValue(QJsonArray{id, 0});
}

void patchClipTextEncodeNode(QJsonObject &workflow,
                             const QString &nodeKey,
                             const QString &prompt,
                             const QString &translationLang,
                             int *nextInjectId)
{
    QJsonObject n = workflow.value(nodeKey).toObject();
    QJsonObject i = n.value(QStringLiteral("inputs")).toObject();
    i.insert(QStringLiteral("text"), clipEncodeTextInput(prompt, translationLang, &workflow, nextInjectId));
    n.insert(QStringLiteral("inputs"), i);
    workflow.insert(nodeKey, n);
}

QString addClipTextEncode(const std::function<QString(const QString &, const QJsonObject &)> &addNode,
                          const QJsonArray &clipLink,
                          const QString &prompt,
                          const QString &translationLang)
{
    if (!translationLang.trimmed().isEmpty() && !prompt.trimmed().isEmpty()) {
        const QString wrapped = ComfyUIUtils::wrapPromptWithTranslationLanguage(prompt, translationLang);
        const QString trans =
            addNode(QStringLiteral("ETN_Translate"), QJsonObject{{QStringLiteral("text"), wrapped}});
        return addNode(QStringLiteral("CLIPTextEncode"),
                       QJsonObject{{QStringLiteral("clip"), clipLink}, {QStringLiteral("text"), QJsonArray{trans, 0}}});
    }
    return addNode(QStringLiteral("CLIPTextEncode"),
                   QJsonObject{{QStringLiteral("clip"), clipLink}, {QStringLiteral("text"), prompt}});
}

} // namespace

static void replaceInputLink(QJsonObject *workflow, const QJsonArray &from, const QJsonArray &to)
{
    if (!workflow || from.size() < 2 || to.size() < 2)
        return;
    for (auto it = workflow->begin(); it != workflow->end(); ++it) {
        QJsonObject node = it.value().toObject();
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        bool changed = false;
        for (auto inIt = inputs.begin(); inIt != inputs.end(); ++inIt) {
            const QJsonValue v = inIt.value();
            if (v.isArray()) {
                const QJsonArray arr = v.toArray();
                if (arr.size() >= 2 && arr.at(0).toString() == from.at(0).toString()
                    && arr.at(1).toInt() == from.at(1).toInt()) {
                    inputs.insert(inIt.key(), to);
                    changed = true;
                }
            }
        }
        if (changed) {
            node.insert(QStringLiteral("inputs"), inputs);
            workflow->insert(it.key(), node);
        }
    }
}

static QString findCheckpointNodeId(const QJsonObject &workflow)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("CheckpointLoaderSimple"))
            return it.key();
    }
    return QString();
}

static QString findNodeIdByClassType(const QJsonObject &workflow, const QString &classType)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == classType)
            return it.key();
    }
    return QString();
}

static void replaceAllLinksFromNode(QJsonObject *workflow, const QString &fromNode, int fromSlot, const QString &toNode, int toSlot)
{
    replaceInputLink(workflow, QJsonArray{fromNode, fromSlot}, QJsonArray{toNode, toSlot});
}

static QString insertInpaintExpandMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int grow, int feather,
                                       int maskSlot = 1)
{
    if (grow <= 0 && feather <= 0)
        return maskNodeId;
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_ExpandMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                              {QStringLiteral("grow"), grow},
                                              {QStringLiteral("blur"), feather},
                                              {QStringLiteral("blur_type"), QStringLiteral("linear")}}}});
    return id;
}

static QString insertImageToMask(QJsonObject *workflow, int *nextId, const QString &imageNodeId, const QString &channel)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageToMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, 0}},
                                              {QStringLiteral("channel"), channel}}}});
    return id;
}

static QString insertDifferentialDiffusion(QJsonObject *workflow, int *nextId, const QString &modelNodeId)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("DifferentialDiffusion")},
                                 {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("model"), QJsonArray{modelNodeId, 0}}}}});
    return id;
}

static QString insertCropMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, const QRect &bounds)
{
    if (!workflow || bounds.isEmpty())
        return maskNodeId;
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CropMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                              {QStringLiteral("x"), bounds.x()},
                                              {QStringLiteral("y"), bounds.y()},
                                              {QStringLiteral("width"), bounds.width()},
                                              {QStringLiteral("height"), bounds.height()}}}});
    return id;
}

static QString insertCropImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot, const QRect &bounds)
{
    if (!workflow || bounds.isEmpty())
        return imageNodeId;
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageCrop")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, imageSlot}},
                                              {QStringLiteral("x"), bounds.x()},
                                              {QStringLiteral("y"), bounds.y()},
                                              {QStringLiteral("width"), bounds.width()},
                                              {QStringLiteral("height"), bounds.height()}}}});
    return id;
}

static QString insertScaleImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot, int width,
                                int height)
{
    if (!workflow || width <= 0 || height <= 0)
        return imageNodeId;
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageScale")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, imageSlot}},
                                              {QStringLiteral("width"), width},
                                              {QStringLiteral("height"), height},
                                              {QStringLiteral("upscale_method"), QStringLiteral("lanczos")},
                                              {QStringLiteral("crop"), QStringLiteral("disabled")}}}});
    return id;
}

static QString insertMaskToImage(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("MaskToImage")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}}}}});
    return id;
}

static QString insertScaleMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, int width,
                               int height)
{
    if (!workflow || width <= 0 || height <= 0)
        return maskNodeId;
    const QString asImage = insertMaskToImage(workflow, nextId, maskNodeId, maskSlot);
    const QString scaled = insertScaleImage(workflow, nextId, asImage, 0, width, height);
    return insertImageToMask(workflow, nextId, scaled, QStringLiteral("red"));
}

static QString insertShrinkMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, int shrink, int blur)
{
    if (!workflow || shrink <= 0 && blur <= 0)
        return maskNodeId;
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_ShrinkMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                              {QStringLiteral("shrink"), qMax(0, shrink)},
                                              {QStringLiteral("blur"), qMax(0, blur)},
                                              {QStringLiteral("blur_type"), QStringLiteral("gaussian")}}}});
    return id;
}

static QString insertThresholdMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot,
                                   double threshold = 0.0)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ThresholdMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                              {QStringLiteral("value"), threshold}}}});
    return id;
}

static QString insertApplyMaskToImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
                                      const QString &maskNodeId, int maskSlot)
{
    if (!workflow || imageNodeId.isEmpty() || maskNodeId.isEmpty())
        return imageNodeId;
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_ApplyMaskToImage")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, imageSlot}},
                                              {QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}}}}});
    return id;
}

static QString insertMaskedFill(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
                                const QString &maskNodeId, int maskSlot, const QString &fillKind)
{
    const QString fill = fillKind.trimmed();
    if (!workflow || imageNodeId.isEmpty() || maskNodeId.isEmpty()
        || fill.isEmpty() || fill == QLatin1String("none") || fill == QLatin1String("inpaint"))
        return imageNodeId;

    if (fill == QLatin1String("blur") || fill == QLatin1String("border")) {
        QString sourceImage = imageNodeId;
        int sourceSlot = imageSlot;
        if (fill == QLatin1String("border")) {
            const QString fillId = QString::number((*nextId)++);
            workflow->insert(fillId,
                             QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_MaskedFill")},
                                         {QStringLiteral("inputs"),
                                          QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, imageSlot}},
                                                      {QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                                      {QStringLiteral("fill"), QStringLiteral("navier-stokes")},
                                                      {QStringLiteral("falloff"), 0}}}});
            sourceImage = fillId;
            sourceSlot = 0;
        }
        const QString blurId = QString::number((*nextId)++);
        workflow->insert(blurId,
                         QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_MaskedBlur")},
                                     {QStringLiteral("inputs"),
                                      QJsonObject{{QStringLiteral("image"), QJsonArray{sourceImage, sourceSlot}},
                                                  {QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                                  {QStringLiteral("blur"), 65},
                                                  {QStringLiteral("falloff"), fill == QLatin1String("blur") ? 9 : 0}}}});
        return blurId;
    }

    QString mode = fill;
    if (fill == QLatin1String("replace"))
        mode = QStringLiteral("neutral");
    if (fill == QLatin1String("green"))
        mode = QStringLiteral("neutral"); // green edit-reference path is handled upstream for Flux edit models; neutral is safest core fallback.

    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_MaskedFill")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, imageSlot}},
                                              {QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                              {QStringLiteral("fill"), mode},
                                              {QStringLiteral("falloff"), fill == QLatin1String("neutral") ? 9 : 0}}}});
    return id;
}

static void appendColorMatchAfterDecode(QJsonObject *workflow,
                                        const QString &decodeNodeId,
                                        const QString &referenceImageNodeId,
                                        const QString &maskNodeId,
                                        int maskNodeSlot,
                                        int *nextId)
{
    const QString matchId = QString::number((*nextId)++);
    QJsonObject inputs{{QStringLiteral("target"), QJsonArray{decodeNodeId, 0}},
                       {QStringLiteral("reference"), QJsonArray{referenceImageNodeId, 0}},
                       {QStringLiteral("strength"), 1.0}};
    if (!maskNodeId.isEmpty())
        inputs.insert(QStringLiteral("exclude_mask"), QJsonArray{maskNodeId, maskNodeSlot});
    workflow->insert(matchId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_ColorMatch")},
                                 {QStringLiteral("inputs"), inputs}});
    // Rewire SaveImage only. replaceAllLinksFromNode() also rewrote ColorMatch's
    // target input (decode -> self), causing dependency_cycle on INPAINT_ColorMatch.
    const QString saveId = findNodeIdByClassType(*workflow, QStringLiteral("SaveImage"));
    if (!saveId.isEmpty()) {
        QJsonObject save = workflow->value(saveId).toObject();
        QJsonObject saveInputs = save.value(QStringLiteral("inputs")).toObject();
        const QJsonArray images = saveInputs.value(QStringLiteral("images")).toArray();
        if (images.size() >= 2 && images.at(0).toString() == decodeNodeId && images.at(1).toInt() == 0) {
            saveInputs.insert(QStringLiteral("images"), QJsonArray{matchId, 0});
            save.insert(QStringLiteral("inputs"), saveInputs);
            workflow->insert(saveId, save);
        }
    }
}

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

bool loadCheckpointWithLora(QJsonObject *workflow, const CheckpointLoadParams &params, CheckpointGraphRefs *out)
{
    if (!workflow || workflow->isEmpty())
        return false;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();
    QString ckptId = findCheckpointNodeId(*workflow);
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
        replaceAllLinksFromNode(workflow, ckptId, 0, modelId, 0);
        replaceAllLinksFromNode(workflow, ckptId, 1, clipLinkNode, clipLinkSlot);
        replaceAllLinksFromNode(workflow, ckptId, 2, vaeLinkNode, vaeLinkSlot);
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
        replaceAllLinksFromNode(workflow, ckptId, 0, modelChain, 0);
    if (clipChainNode != ckptId || clipChainSlot != 1)
        replaceAllLinksFromNode(workflow, ckptId, 1, clipChainNode, clipChainSlot);

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
    patchClipTextEncodeNode(workflow, QStringLiteral("6"), pos, params.promptTranslationLanguage, &injectId);
    patchClipTextEncodeNode(workflow, QStringLiteral("7"), params.negativePrompt, params.promptTranslationLanguage,
                            &injectId);

    return workflow;
}

bool usesSamplerCustomAdvanced(ComfyResources::Arch arch)
{
    Q_UNUSED(arch);
    return true;
}

WorkflowGraphContext discoverWorkflowGraphContext(const QJsonObject &workflow)
{
    WorkflowGraphContext ctx;
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
        PromptOutput promptOut{result.positiveNodeId, result.negativeNodeId};
        promptOut = applyReferenceConditioningForTile(workflow,
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
        patchSamplerNode(workflow, ctx.samplerNodeId, result.modelNodeId);
    }
    if (!result.positiveNodeId.isEmpty() && !result.negativeNodeId.isEmpty()) {
        patchSamplerNode(workflow,
                         ctx.samplerNodeId,
                         QString(),
                         result.positiveNodeId,
                         result.negativeNodeId);
    }

    return result;
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

void finishWorkflowWithSamplerCustom(QJsonObject *workflow,
                                     const QString &samplerNodeId,
                                     ComfyResources::Arch arch,
                                     int extentWidth,
                                     int extentHeight,
                                     double denoiseStrength)
{
    if (!usesSamplerCustomAdvanced(arch))
        return;
    finishBuilderWithSamplerCustom(
        workflow, samplerNodeId, arch, extentWidth, extentHeight, denoiseStrength);
}

QJsonObject buildControlPreview(const ControlPreviewParams &params)
{
    return ComfyUIUtils::buildControlImageWorkflow(params.uploadedImageName,
                                                 params.mode,
                                                 params.resolutionBase,
                                                 params.returnPreprocessed);
}

void applyCheckpointStyleOptions(QJsonObject *workflow, const QString &vaeName, int clipSkip, ComfyResources::Arch arch)
{
    if (!workflow || workflow->isEmpty())
        return;
    const QString ckptId = findCheckpointNodeId(*workflow);
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
    replaceInputLink(workflow, QJsonArray{ckptId, 1}, ckptClip);
    replaceInputLink(workflow, QJsonArray{ckptId, 2}, ckptVae);
}

static QJsonObject parseImg2ImgWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(img2imgWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

static QJsonObject parseInpaintingWorkflowTemplate();

QJsonObject buildRefineRegion(const RefineRegionParams &params)
{
    if (params.refine.imageName.isEmpty() || params.maskImageName.isEmpty())
        return QJsonObject();

    // Upstream workflow.refine_region(): real canvas pixels, apply_grow_feather on mask,
    // vae_encode + set_latent_noise_mask + differential_diffusion. No fill_masked,
    // no VAEEncodeForInpaint. Client composites via denoise_to_compositing_mask.
    QJsonObject workflow = parseInpaintingWorkflowTemplate();
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
    patchClipTextEncodeNode(workflow, QStringLiteral("5"), pos, params.refine.promptTranslationLanguage, &injectId);
    patchClipTextEncodeNode(workflow, QStringLiteral("6"), params.refine.negativePrompt,
                            params.refine.promptTranslationLanguage, &injectId);

    const QString vaeLinkNode = ckptRefs.vaeNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.vaeNodeId;
    const int vaeLinkSlot = ckptRefs.vaeNodeId.isEmpty() ? 2 : ckptRefs.vaeNodeSlot;

    QString processedMaskId = insertImageToMask(&workflow, &nextId, QStringLiteral("2"), QStringLiteral("red"));
    int processedMaskSlot = 0;
    if (params.growMaskBy > 0 || params.featherMaskBy > 0) {
        processedMaskId =
            insertInpaintExpandMask(&workflow, &nextId, processedMaskId, params.growMaskBy, params.featherMaskBy, 0);
        processedMaskSlot = 0;
    }

    workflow.remove(QStringLiteral("7"));
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
                                             {QStringLiteral("mask"), QJsonArray{processedMaskId, processedMaskSlot}}}}});

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
        i8.insert(QStringLiteral("latent_image"), QJsonArray{latentMaskedId, 0});
        n8.insert(QStringLiteral("inputs"), i8);
        workflow.insert(QStringLiteral("8"), n8);
    }

    const QString ckptId = findCheckpointNodeId(workflow);
    const QString modelSourceId = ckptRefs.modelNodeId.isEmpty() ? ckptId : ckptRefs.modelNodeId;
    if (!modelSourceId.isEmpty()) {
        const QString diffId = insertDifferentialDiffusion(&workflow, &nextId, modelSourceId);
        patchSamplerNode(&workflow, QStringLiteral("8"), diffId);
    }

    if (params.colorMatch) {
        appendColorMatchAfterDecode(&workflow, QStringLiteral("9"), QStringLiteral("1"), processedMaskId,
                                    processedMaskSlot, &nextId);
    }

    finishWorkflowWithSamplerCustom(&workflow, QStringLiteral("8"), arch,
                                    qMax(64, params.extentWidth), qMax(64, params.extentHeight),
                                    params.refine.denoise);
    return workflow;
}

QJsonObject buildRefine(const RefineParams &params)
{
    QJsonObject workflow = parseImg2ImgWorkflowTemplate();
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
    patchClipTextEncodeNode(workflow, QStringLiteral("4"), pos, params.promptTranslationLanguage, &injectId);
    patchClipTextEncodeNode(workflow, QStringLiteral("5"), params.negativePrompt, params.promptTranslationLanguage,
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

    finishWorkflowWithSamplerCustom(
        &workflow, QStringLiteral("6"), arch, 1024, 1024, params.denoise);
    return workflow;
}

static QJsonObject parseInpaintingWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(inpaintingWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
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
                                                 clipEncodeTextInput(bgText, QString(), workflow, &injectId)}}}});

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

    replaceAllLinksFromNode(workflow, modelNodeId, 0, attnId, 0);
    *nextNodeId = nid;
    return true;
}

QJsonObject buildInpaint(const InpaintBuildParams &params)
{
    QJsonObject workflow = parseInpaintingWorkflowTemplate();
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
        patchClipTextEncodeNode(workflow, QStringLiteral("5"), pos, params.promptTranslationLanguage, &injectId);
        patchClipTextEncodeNode(workflow, QStringLiteral("6"), params.negativePrompt, params.promptTranslationLanguage,
                                &injectId);
        while (workflow.contains(QString::number(growNext)))
            ++growNext;
        const QString baseMaskId = insertImageToMask(&workflow, &growNext, QStringLiteral("2"), QStringLiteral("red"));
        const int baseMaskSlot = 0;
        maskForEncodeId = baseMaskId;
        maskForEncodeSlot = baseMaskSlot;
        if (params.growMaskBy > 0 || params.featherMaskBy > 0) {
            maskForEncodeId =
                insertInpaintExpandMask(&workflow, &growNext, baseMaskId, params.growMaskBy, params.featherMaskBy,
                                        baseMaskSlot);
            maskForEncodeSlot = 0;
        }
        const int fillGrow = qMax(0, params.growMaskBy - params.featherMaskBy / 2);
        QString fillMaskId = baseMaskId;
        int fillMaskSlot = baseMaskSlot;
        if (fillGrow > 0) {
            fillMaskId = insertInpaintExpandMask(&workflow, &growNext, baseMaskId, fillGrow, 0, baseMaskSlot);
            fillMaskSlot = 0;
        }
        imageForEncodeId = insertMaskedFill(&workflow, &growNext, QStringLiteral("1"), 0, fillMaskId, fillMaskSlot, params.fillKind);
        imageForEncodeSlot = 0;
        if (params.useConditionMask) {
            const QString modelId = ckptRefs.modelNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.modelNodeId;
            const QString clipId = ckptRefs.clipNodeId.isEmpty() ? QStringLiteral("4") : ckptRefs.clipNodeId;
            applyInpaintPromptFocus(&workflow, &growNext, modelId, QStringLiteral("5"), clipId, maskForEncodeId,
                                    maskForEncodeSlot, params.backgroundPrompt);
        }
        QJsonObject n7 = workflow.value(QStringLiteral("7")).toObject();
        QJsonObject i7 = n7.value(QStringLiteral("inputs")).toObject();
        i7.insert(QStringLiteral("mask"), QJsonArray{maskForEncodeId, maskForEncodeSlot});
        i7.insert(QStringLiteral("pixels"), QJsonArray{imageForEncodeId, imageForEncodeSlot});
        i7.insert(QStringLiteral("grow_mask_by"), 0);
        n7.insert(QStringLiteral("inputs"), i7);
        workflow.insert(QStringLiteral("7"), n7);
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
        &workflow, QStringLiteral("8"), arch, 1024, 1024, params.denoise);

    if (!params.targetBoundsRelative.isEmpty()) {
        while (workflow.contains(QString::number(growNext)))
            ++growNext;
        QString outputImageId = QStringLiteral("9");
        int outputImageSlot = 0;
        QString outputMaskId = maskForEncodeId;
        int outputMaskSlot = maskForEncodeSlot;
        outputImageId = insertCropImage(&workflow, &growNext, outputImageId, outputImageSlot, params.targetBoundsRelative);
        outputImageSlot = 0;
        outputMaskId = insertCropMask(&workflow, &growNext, outputMaskId, outputMaskSlot, params.targetBoundsRelative);
        outputMaskSlot = 0;
        if (params.blendMaskBy > 0) {
            outputMaskId = insertShrinkMask(&workflow, &growNext, outputMaskId, outputMaskSlot,
                                            params.blendMaskBy / 2, params.blendMaskBy);
            outputMaskSlot = 0;
        }
        outputImageId = insertApplyMaskToImage(&workflow, &growNext, outputImageId, outputImageSlot, outputMaskId, outputMaskSlot);
        outputImageSlot = 0;
        QJsonObject save = workflow.value(QStringLiteral("10")).toObject();
        QJsonObject saveInputs = save.value(QStringLiteral("inputs")).toObject();
        saveInputs.insert(QStringLiteral("images"), QJsonArray{outputImageId, outputImageSlot});
        save.insert(QStringLiteral("inputs"), saveInputs);
        workflow.insert(QStringLiteral("10"), save);
    }
    return workflow;
}

QJsonObject buildLive(const LiveParams &params)
{
    return buildRefine(params);
}

qint64 animationFrameSeed(const qint64 batchBaseSeed, const int frameIndex, const int batchSeedStep)
{
    const int step = qMax(1, batchSeedStep);
    return batchBaseSeed + static_cast<qint64>(frameIndex) * static_cast<qint64>(step);
}

QJsonObject buildAnimationFrame(const AnimationFrameParams &params)
{
    TextToImageParams gen = params.base;
    gen.seed = animationFrameSeed(params.batchBaseSeed, params.frameIndex, params.batchSeedStep);
    return buildTextToImage(gen);
}

static QJsonObject parseUpscaleWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(upscaleWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

static QJsonObject parseUpscaleRefineWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(upscaleRefineWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QJsonObject buildUpscaleSimple(const UpscaleSimpleParams &params)
{
    QJsonObject workflow = parseUpscaleWorkflowTemplate();
    if (workflow.isEmpty())
        return workflow;

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
        i2.insert(QStringLiteral("width"), qMax(64, params.targetWidth));
        i2.insert(QStringLiteral("height"), qMax(64, params.targetHeight));
        const QString method =
            params.upscaleMethod.trimmed().isEmpty() ? QStringLiteral("lanczos") : params.upscaleMethod.trimmed();
        i2.insert(QStringLiteral("upscale_method"), method);
        n2.insert(QStringLiteral("inputs"), i2);
        workflow.insert(QStringLiteral("2"), n2);
    }
    return workflow;
}

QJsonObject buildUpscaleRefine(const UpscaleRefineParams &params)
{
    QJsonObject workflow = parseUpscaleRefineWorkflowTemplate();
    if (workflow.isEmpty())
        return workflow;

    double cfg = params.cfg;
    if (!ComfyResources::supportsCfg(params.arch) && cfg > 4.0)
        cfg = 3.5;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();
    const QString method =
        params.upscaleMethod.trimmed().isEmpty() ? QStringLiteral("lanczos") : params.upscaleMethod.trimmed();
    const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("high quality, detailed")
                                                                    : params.positivePrompt;

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
        i2.insert(QStringLiteral("width"), qMax(64, params.scaleWidth));
        i2.insert(QStringLiteral("height"), qMax(64, params.scaleHeight));
        i2.insert(QStringLiteral("upscale_method"), method);
        n2.insert(QStringLiteral("inputs"), i2);
        workflow.insert(QStringLiteral("2"), n2);
    }
    {
        QJsonObject n4 = workflow.value(QStringLiteral("4")).toObject();
        QJsonObject i4 = n4.value(QStringLiteral("inputs")).toObject();
        i4.insert(QStringLiteral("ckpt_name"), ckpt);
        n4.insert(QStringLiteral("inputs"), i4);
        workflow.insert(QStringLiteral("4"), n4);
    }
    int injectId = 90;
    patchClipTextEncodeNode(workflow, QStringLiteral("5"), pos, params.promptTranslationLanguage, &injectId);
    patchClipTextEncodeNode(workflow, QStringLiteral("6"), params.negativePrompt, params.promptTranslationLanguage,
                            &injectId);
    {
        QJsonObject n7 = workflow.value(QStringLiteral("7")).toObject();
        QJsonObject i7 = n7.value(QStringLiteral("inputs")).toObject();
        i7.insert(QStringLiteral("seed"), static_cast<double>(params.seed));
        i7.insert(QStringLiteral("steps"), params.steps);
        i7.insert(QStringLiteral("cfg"), cfg);
        i7.insert(QStringLiteral("denoise"), qBound(0.01, params.denoise, 1.0));
        i7.insert(QStringLiteral("sampler_name"), params.sampler);
        i7.insert(QStringLiteral("scheduler"), params.scheduler.isEmpty() ? QStringLiteral("normal") : params.scheduler);
        n7.insert(QStringLiteral("inputs"), i7);
        workflow.insert(QStringLiteral("7"), n7);
    }
    finishWorkflowWithSamplerCustom(&workflow,
                                    QStringLiteral("7"),
                                    params.arch,
                                    qMax(64, params.scaleWidth),
                                    qMax(64, params.scaleHeight),
                                    params.denoise);
    return workflow;
}

namespace {

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
                                 int extentH)
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
                                                  {QStringLiteral("negative"), QJsonArray{negativeId, 0}},
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
                                              {QStringLiteral("latent_image"), QJsonArray{latentId, 0}}}}});
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
                                                        extentH);

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
                                    double /*denoise*/)
{
    replaceWorkflowSamplerWithCustomAdvanced(workflow, samplerNodeId, arch, extentW, extentH, nullptr);
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

} // namespace

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

QJsonObject buildUpscaleTiled(const UpscaleTiledParams &params)
{
    QJsonObject workflow;
    if (params.imageName.isEmpty() || params.scaledWidth <= 0 || params.scaledHeight <= 0)
        return workflow;

    double cfg = params.cfg;
    if (!ComfyResources::supportsCfg(params.arch) && cfg > 4.0)
        cfg = 3.5;

    const QString ckpt =
        params.checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : params.checkpoint.trimmed();
    const QString pos = params.positivePrompt.trimmed().isEmpty() ? QStringLiteral("4k uhd") : params.positivePrompt;
    const int multiple = qMax(1, ComfyResources::latentCompressionFactor(params.arch));

    const ComfyUIUtils::UpscaleTiledLayoutSpec layout = ComfyUIUtils::computeUpscaleTiledLayoutSpec(
        params.scaledWidth,
        params.scaledHeight,
        params.arch,
        params.stylePreferredResolution,
        params.denoise,
        params.tileOverlapPx);
    const int padding = layout.padding;
    const int blending = layout.blending;
    const int tileSize = layout.minTileSize;

    int nextId = 1;
    auto addNode = [&](const QString &classType, const QJsonObject &inputs) -> QString {
        const QString id = QString::number(nextId++);
        workflow.insert(id,
                        QJsonObject{{QStringLiteral("class_type"), classType}, {QStringLiteral("inputs"), inputs}});
        return id;
    };

    QString workingImage = addNode(QStringLiteral("LoadImage"), {{QStringLiteral("image"), params.imageName}});
    if (!params.upscaleModelName.trimmed().isEmpty()) {
        const QString loader =
            addNode(QStringLiteral("UpscaleModelLoader"), {{QStringLiteral("model_name"), params.upscaleModelName.trimmed()}});
        workingImage = addNode(QStringLiteral("ImageUpscaleWithModel"),
                               {{QStringLiteral("upscale_model"), QJsonArray{loader, 0}},
                                {QStringLiteral("image"), QJsonArray{workingImage, 0}}});
    }
    if (params.scaledWidth != params.targetWidth || params.scaledHeight != params.targetHeight) {
        workingImage = addNode(QStringLiteral("ImageScale"),
                               {{QStringLiteral("image"), QJsonArray{workingImage, 0}},
                                {QStringLiteral("width"), qMax(64, params.scaledWidth)},
                                {QStringLiteral("height"), qMax(64, params.scaledHeight)},
                                {QStringLiteral("upscale_method"), QStringLiteral("lanczos")}});
    }

    const QString layoutId = addNode(QStringLiteral("ETN_TileLayout"),
                                     {{QStringLiteral("image"), QJsonArray{workingImage, 0}},
                                      {QStringLiteral("min_tile_size"), tileSize},
                                      {QStringLiteral("padding"), padding},
                                      {QStringLiteral("blending"), blending},
                                      {QStringLiteral("multiple"), multiple}});

    const QString checkpointId =
        addNode(QStringLiteral("CheckpointLoaderSimple"), {{QStringLiteral("ckpt_name"), ckpt}});

    ComfyUIUtils::DiffusionTileLayout tileGrid;
    if (params.tileOverlapPx < 0) {
        tileGrid = ComfyUIUtils::DiffusionTileLayout::fromDenoiseStrength(
            QSize(params.scaledWidth, params.scaledHeight), layout.minTileSize, params.denoise, multiple, layout.padding);
    } else {
        tileGrid = ComfyUIUtils::DiffusionTileLayout::fromUniformGrid(
            params.scaledWidth, params.scaledHeight, layout.minTileSize, layout.padding, layout.minTileSize,
            layout.blending);
    }
    const int tiles = qMax(1, qMin(layout.totalTiles, tileGrid.tileCount > 0 ? tileGrid.tileCount : layout.totalTiles));

    QString mergedImage = workingImage;
    for (int i = 0; i < tiles; ++i) {
        const QString tileImg = addNode(QStringLiteral("ETN_ExtractImageTile"),
                                        {{QStringLiteral("image"), QJsonArray{workingImage, 0}},
                                         {QStringLiteral("layout"), QJsonArray{layoutId, 0}},
                                         {QStringLiteral("index"), i}});
        const QString tileMask = addNode(QStringLiteral("ETN_GenerateTileMask"),
                                         {{QStringLiteral("layout"), QJsonArray{layoutId, 0}}, {QStringLiteral("index"), i}});
        const QString latent = addNode(QStringLiteral("VAEEncode"),
                                       {{QStringLiteral("pixels"), QJsonArray{tileImg, 0}},
                                        {QStringLiteral("vae"), QJsonArray{checkpointId, 2}}});
        const QString latentMasked = addNode(QStringLiteral("SetLatentNoiseMask"),
                                             {{QStringLiteral("samples"), QJsonArray{latent, 0}},
                                              {QStringLiteral("mask"), QJsonArray{tileMask, 0}}});
        const QJsonArray clipLink = QJsonArray{checkpointId, 1};
        const QString positive =
            addClipTextEncode(addNode, clipLink, pos, params.promptTranslationLanguage);
        const QString negative =
            addClipTextEncode(addNode, clipLink, params.negativePrompt, params.promptTranslationLanguage);

        QString modelId = checkpointId;
        QString posId = positive;
        QString negId = negative;

        ConditioningGraphRef cond;
        cond.modelNodeId = modelId;
        cond.positiveNodeId = posId;
        cond.negativeNodeId = negId;
        cond.clipSourceNodeId = checkpointId;
        cond.tileLayoutNodeId = layoutId;
        cond.tileIndex = i;
        cond.nextNodeId = nextId;
        cond.scaledWidth = params.scaledWidth;
        cond.scaledHeight = params.scaledHeight;

        if (!params.ipAdapterLayers.isEmpty())
            applyIpAdapterLayers(&workflow, params.ipAdapterLayers, params.arch, &cond);
        modelId = cond.modelNodeId;
        nextId = cond.nextNodeId;

        QList<RegionalPromptInput> tileRegions = params.regionalPrompts;
        if (params.regionalPrompts.size() >= 2 && tileGrid.tileCount > 0) {
            const QRect tileBounds = tileGrid.bounds(i);
            tileRegions = filterRegionalPromptsForTile(params.regionalPrompts, tileBounds);
        }
        if (tileRegions.size() >= 2) {
            cond.modelNodeId = modelId;
            cond.positiveNodeId = posId;
            cond.negativeNodeId = negId;
            cond.nextNodeId = nextId;
            const RegionalWorkflowNodes regional = applyRegionalGeneration(
                &workflow, tileRegions, modelId, posId, negId, checkpointId, &cond);
            if (regional.applied) {
                modelId = regional.modelNodeId;
                posId = regional.positiveNodeId;
                negId = regional.negativeNodeId;
            }
            nextId = cond.nextNodeId;
        }

        if (!params.controlLayers.isEmpty()) {
            cond.modelNodeId = modelId;
            cond.positiveNodeId = posId;
            cond.negativeNodeId = negId;
            cond.nextNodeId = nextId;
            applyControlNetLayers(&workflow, params.controlLayers, params.arch, &cond);
            posId = cond.positiveNodeId;
            nextId = cond.nextNodeId;
        }

        PromptOutput promptOut{posId, negId};
        const QRect tileBounds = tileGrid.tileCount > 0 ? tileGrid.bounds(i) : QRect();
        const int tileW = tileBounds.isValid() ? tileBounds.width() : params.scaledWidth;
        const int tileH = tileBounds.isValid() ? tileBounds.height() : params.scaledHeight;
        promptOut = applyReferenceConditioningForTile(&workflow,
                                                      &nextId,
                                                      promptOut,
                                                      tileImg,
                                                      latentMasked,
                                                      checkpointId,
                                                      params.ipAdapterLayers,
                                                      params.editReference,
                                                      params.arch);
        posId = promptOut.positiveId;
        negId = promptOut.negativeId;

        const int startAtStep =
            static_cast<int>(qRound(params.steps * (1.0 - qBound(0.01, params.denoise, 1.0))));
        const QString samplerNode = addSamplerCustomAdvanced(&workflow,
                                                             &nextId,
                                                             modelId,
                                                             posId,
                                                             negId,
                                                             latentMasked,
                                                             params.arch,
                                                             cfg,
                                                             params.steps,
                                                             startAtStep,
                                                             params.sampler,
                                                             params.scheduler.isEmpty() ? QStringLiteral("normal")
                                                                                        : params.scheduler,
                                                             params.seed + i,
                                                             tileW,
                                                             tileH);
        const QString decoded = addNode(QStringLiteral("VAEDecode"),
                                        {{QStringLiteral("samples"), QJsonArray{samplerNode, 1}},
                                         {QStringLiteral("vae"), QJsonArray{checkpointId, 2}}});
        mergedImage = addNode(QStringLiteral("ETN_MergeImageTile"),
                              {{QStringLiteral("image"), QJsonArray{mergedImage, 0}},
                               {QStringLiteral("layout"), QJsonArray{layoutId, 0}},
                               {QStringLiteral("index"), i},
                               {QStringLiteral("tile"), QJsonArray{decoded, 0}}});
    }

    QString outImage = mergedImage;
    if (params.targetWidth != params.scaledWidth || params.targetHeight != params.scaledHeight) {
        outImage = addNode(QStringLiteral("ImageScale"),
                           {{QStringLiteral("image"), QJsonArray{outImage, 0}},
                            {QStringLiteral("width"), qMax(64, params.targetWidth)},
                            {QStringLiteral("height"), qMax(64, params.targetHeight)},
                            {QStringLiteral("upscale_method"), QStringLiteral("lanczos")}});
    }
    addNode(QStringLiteral("SaveImage"),
            {{QStringLiteral("images"), QJsonArray{outImage, 0}},
             {QStringLiteral("filename_prefix"), QStringLiteral("ComfyUI_upscale_tiled")}});
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
                graph ? insertConditioningImageNode(workflow, &nextId, layer.imageName, tileGraph)
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
                graph ? insertConditioningImageNode(workflow, &nextId, layer->imageName, tileGraph)
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
                                                  {QStringLiteral("weight_type"), ipAdapterWeightType(embedMode)},
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
            patchSamplerNode(workflow, graph->samplerNodeId, modelSource);
    } else {
        patchSamplerNode(workflow, QStringLiteral("3"), modelSource);
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

    const QString ckptId = findCheckpointNodeId(*workflow);
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
                graph ? insertConditioningImageNode(workflow, &nextId, layer.imageName, tileGraph)
                      : QString::number(nextId++);
            if (!graph) {
                workflow->insert(imageId,
                                 QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                             {QStringLiteral("inputs"),
                                              QJsonObject{{QStringLiteral("image"), layer.imageName}}}});
            }
            QJsonArray vaeLink = QJsonArray{vaeSource, 2};
            const QString decodeId = findNodeIdByClassType(*workflow, QStringLiteral("VAEDecode"));
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
            graph ? insertConditioningImageNode(workflow, &nextId, layer.imageName, tileGraph)
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
                patchSamplerNode(workflow, graph->samplerNodeId, modelSource);
            patchSamplerNode(workflow, graph->samplerNodeId, QString(), positiveNode);
        }
    } else {
        if (modelSource != initialModelSource)
            patchSamplerNode(workflow, QStringLiteral("3"), modelSource);
        patchSamplerNode(workflow, QStringLiteral("3"), QString(), positiveNode);
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
                                           clipEncodeTextInput(region.positivePrompt,
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
                                           clipEncodeTextInput(region.positivePrompt,
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
            patchSamplerNode(workflow, graph->samplerNodeId, attnId, combinedPos, combinedPos);
    } else {
        patchSamplerNode(workflow, QStringLiteral("3"), attnId, combinedPos, combinedPos);
    }

    return result;
}

} // namespace ComfyWorkflowEngine
