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

void replaceInputLink(QJsonObject *workflow, const QJsonArray &from, const QJsonArray &to)
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

QString findCheckpointNodeId(const QJsonObject &workflow)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("CheckpointLoaderSimple"))
            return it.key();
    }
    return QString();
}

QString findNodeIdByClassType(const QJsonObject &workflow, const QString &classType)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == classType)
            return it.key();
    }
    return QString();
}

QString ipAdapterWeightType(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("style"))
        return QStringLiteral("style transfer");
    if (m == QLatin1String("composition"))
        return QStringLiteral("composition");
    return QStringLiteral("linear");
}

void replaceAllLinksFromNode(QJsonObject *workflow, const QString &fromNode, int fromSlot, const QString &toNode, int toSlot)
{
    detail::replaceInputLink(workflow, QJsonArray{fromNode, fromSlot}, QJsonArray{toNode, toSlot});
}

QString insertInpaintExpandMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int grow, int feather,
                                       int maskSlot)
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

QString insertImageToMask(QJsonObject *workflow, int *nextId, const QString &imageNodeId, const QString &channel)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageToMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{imageNodeId, 0}},
                                              {QStringLiteral("channel"), channel}}}});
    return id;
}

QString insertDifferentialDiffusion(QJsonObject *workflow, int *nextId, const QString &modelNodeId)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("DifferentialDiffusion")},
                                 {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("model"), QJsonArray{modelNodeId, 0}}}}});
    return id;
}

QString insertCropMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, const QRect &bounds)
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

QString insertCropImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot, const QRect &bounds)
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

QString insertScaleImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot, int width,
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

QString insertMaskToImage(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot)
{
    const QString id = QString::number((*nextId)++);
    workflow->insert(id,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("MaskToImage")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}}}}});
    return id;
}

QString insertScaleMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, int width,
                               int height)
{
    if (!workflow || width <= 0 || height <= 0)
        return maskNodeId;
    const QString asImage = insertMaskToImage(workflow, nextId, maskNodeId, maskSlot);
    const QString scaled = insertScaleImage(workflow, nextId, asImage, 0, width, height);
    return insertImageToMask(workflow, nextId, scaled, QStringLiteral("red"));
}

QString insertShrinkMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, int shrink, int blur)
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

QString insertThresholdMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot,
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

QString insertApplyMaskToImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
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

QString insertMaskedFill(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
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

void appendColorMatchAfterDecode(QJsonObject *workflow,
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

QString appendNsfwFilterAfterDecode(QJsonObject *workflow, const QString &decodeNodeId, double sensitivity,
                                           int *nextId)
{
    if (!workflow || sensitivity <= 0.0 || decodeNodeId.isEmpty())
        return decodeNodeId;
    const QString filterId = QString::number((*nextId)++);
    workflow->insert(filterId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_NSFWFilter")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("image"), QJsonArray{decodeNodeId, 0}},
                                              {QStringLiteral("sensitivity"), sensitivity}}}});
    const QString saveId = findNodeIdByClassType(*workflow, QStringLiteral("SaveImage"));
    if (!saveId.isEmpty()) {
        QJsonObject save = workflow->value(saveId).toObject();
        QJsonObject saveInputs = save.value(QStringLiteral("inputs")).toObject();
        const QJsonArray images = saveInputs.value(QStringLiteral("images")).toArray();
        if (images.size() >= 2 && images.at(0).toString() == decodeNodeId && images.at(1).toInt() == 0) {
            saveInputs.insert(QStringLiteral("images"), QJsonArray{filterId, 0});
            save.insert(QStringLiteral("inputs"), saveInputs);
            workflow->insert(saveId, save);
        }
    }
    return filterId;
}

QString defaultInpaintRefinementUpscaleModel(const QString &refinementScaleMode)
{
    if (refinementScaleMode == QLatin1String("upscale_small"))
        return QStringLiteral("RealESRGAN_x2plus.pth");
    return QStringLiteral("RealESRGAN_x4plus.pth");
}

QString insertColorMatchAfterImage(QJsonObject *workflow,
                                          int *nextId,
                                          const QString &imageNodeId,
                                          const QString &referenceImageNodeId,
                                          const QString &maskNodeId,
                                          int maskNodeSlot)
{
    const QString matchId = QString::number((*nextId)++);
    QJsonObject inputs{{QStringLiteral("target"), QJsonArray{imageNodeId, 0}},
                       {QStringLiteral("reference"), QJsonArray{referenceImageNodeId, 0}},
                       {QStringLiteral("strength"), 1.0}};
    if (!maskNodeId.isEmpty())
        inputs.insert(QStringLiteral("exclude_mask"), QJsonArray{maskNodeId, maskNodeSlot});
    workflow->insert(matchId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("INPAINT_ColorMatch")},
                                 {QStringLiteral("inputs"), inputs}});
    return matchId;
}

/// Upstream `denoise_to_compositing_mask` + crop/apply_mask before `send_image` (inpaint / refine_region).
QString appendInpaintMaskedSaveOutput(QJsonObject *workflow,
                                             int *nextId,
                                             QString outputImageId,
                                             int outputImageSlot,
                                             QString compositingMaskId,
                                             int compositingMaskSlot,
                                             const QRect &targetBoundsRelative,
                                             int inputExtentWidth,
                                             int inputExtentHeight,
                                             int blendMaskBy)
{
    if (!targetBoundsRelative.isEmpty()) {
        const QSize inputSize(qMax(1, inputExtentWidth), qMax(1, inputExtentHeight));
        const QSize patchSize = targetBoundsRelative.size();
        if (patchSize.isValid() && inputSize != patchSize) {
            outputImageId =
                insertCropImage(workflow, nextId, outputImageId, outputImageSlot, targetBoundsRelative);
            outputImageSlot = 0;
            compositingMaskId =
                insertCropMask(workflow, nextId, compositingMaskId, compositingMaskSlot, targetBoundsRelative);
            compositingMaskSlot = 0;
        }
        compositingMaskId = insertThresholdMask(workflow, nextId, compositingMaskId, compositingMaskSlot, 0.0);
        compositingMaskSlot = 0;
        if (blendMaskBy > 0) {
            compositingMaskId = insertShrinkMask(workflow, nextId, compositingMaskId, compositingMaskSlot,
                                                 blendMaskBy / 2, blendMaskBy);
            compositingMaskSlot = 0;
        }
        outputImageId = insertApplyMaskToImage(workflow, nextId, outputImageId, outputImageSlot, compositingMaskId,
                                               compositingMaskSlot);
        outputImageSlot = 0;
    }

    QJsonObject save = workflow->value(QStringLiteral("10")).toObject();
    QJsonObject saveInputs = save.value(QStringLiteral("inputs")).toObject();
    saveInputs.insert(QStringLiteral("images"), QJsonArray{outputImageId, outputImageSlot});
    save.insert(QStringLiteral("inputs"), saveInputs);
    workflow->insert(QStringLiteral("10"), save);
    return outputImageId;
}

/// Scale decoded image from diffusion extent to native context (client composites mask).
void appendInpaintNativeScaledSaveOutput(QJsonObject *workflow,
                                         int *nextId,
                                         QString outputImageId,
                                         int outputImageSlot,
                                         int diffusionExtentWidth,
                                         int diffusionExtentHeight,
                                         int nativeExtentWidth,
                                         int nativeExtentHeight)
{
    const int nativeW = qMax(64, nativeExtentWidth);
    const int nativeH = qMax(64, nativeExtentHeight);
    const int diffW = qMax(64, diffusionExtentWidth);
    const int diffH = qMax(64, diffusionExtentHeight);
    if (nativeW != diffW || nativeH != diffH) {
        outputImageId = insertScaleImage(workflow, nextId, outputImageId, outputImageSlot, nativeW, nativeH);
        outputImageSlot = 0;
    }

    QJsonObject save = workflow->value(QStringLiteral("10")).toObject();
    QJsonObject saveInputs = save.value(QStringLiteral("inputs")).toObject();
    saveInputs.insert(QStringLiteral("images"), QJsonArray{outputImageId, outputImageSlot});
    save.insert(QStringLiteral("inputs"), saveInputs);
    workflow->insert(QStringLiteral("10"), save);
}

/// Scale decode + mask from diffusion extent to native context before crop/apply_mask (upstream scale_to_target).
void appendInpaintNativeScaledMaskedSaveOutput(QJsonObject *workflow,
                                                      int *nextId,
                                                      QString outputImageId,
                                                      int outputImageSlot,
                                                      QString compositingMaskId,
                                                      int compositingMaskSlot,
                                                      const QRect &nativeTargetBoundsRelative,
                                                      int diffusionExtentWidth,
                                                      int diffusionExtentHeight,
                                                      int nativeExtentWidth,
                                                      int nativeExtentHeight,
                                                      int blendMaskBy)
{
    const int nativeW = qMax(64, nativeExtentWidth);
    const int nativeH = qMax(64, nativeExtentHeight);
    const int diffW = qMax(64, diffusionExtentWidth);
    const int diffH = qMax(64, diffusionExtentHeight);
    if (nativeW != diffW || nativeH != diffH) {
        outputImageId = insertScaleImage(workflow, nextId, outputImageId, outputImageSlot, nativeW, nativeH);
        outputImageSlot = 0;
        compositingMaskId = insertScaleMask(workflow, nextId, compositingMaskId, compositingMaskSlot, nativeW, nativeH);
        compositingMaskSlot = 0;
    }
    appendInpaintMaskedSaveOutput(workflow,
                                  nextId,
                                  outputImageId,
                                  outputImageSlot,
                                  compositingMaskId,
                                  compositingMaskSlot,
                                  nativeTargetBoundsRelative,
                                  nativeW,
                                  nativeH,
                                  blendMaskBy);
}

QPair<QString, QString> applyInpaintControlNetConditioning(QJsonObject *workflow,
                                                           int *nextId,
                                                           const QString &positiveNodeId,
                                                           const QString &negativeNodeId,
                                                           const QString &vaeNodeId,
                                                           int vaeSlot,
                                                           const QString &imageNodeId,
                                                           int imageSlot,
                                                           const QString &maskNodeId,
                                                           int maskSlot,
                                                           const QString &controlNetFile,
                                                           ComfyResources::Arch arch)
{
    const QPair<QString, QString> unchanged{positiveNodeId, negativeNodeId};
    if (!workflow || !nextId || controlNetFile.isEmpty())
        return unchanged;
    const QString cnFile = controlNetFile;

    double strength = 1.0;
    double startPercent = 0.0;
    double endPercent = 1.0;
    if (arch == ComfyResources::Arch::Flux) {
        strength = 0.9;
        endPercent = 0.5;
    } else if (arch == ComfyResources::Arch::ZImage) {
        strength = 0.5;
    }

    const QString loaderId = QString::number((*nextId)++);
    workflow->insert(loaderId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ControlNetLoader")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("control_net_name"), cnFile}}}});
    const QString applyId = QString::number((*nextId)++);
    workflow->insert(applyId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ControlNetInpaintingAliMamaApply")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("positive"), QJsonArray{positiveNodeId, 0}},
                                              {QStringLiteral("negative"), QJsonArray{negativeNodeId, 0}},
                                              {QStringLiteral("control_net"), QJsonArray{loaderId, 0}},
                                              {QStringLiteral("vae"), QJsonArray{vaeNodeId, vaeSlot}},
                                              {QStringLiteral("image"), QJsonArray{imageNodeId, imageSlot}},
                                              {QStringLiteral("mask"), QJsonArray{maskNodeId, maskSlot}},
                                              {QStringLiteral("strength"), strength},
                                              {QStringLiteral("start_percent"), startPercent},
                                              {QStringLiteral("end_percent"), endPercent}}}});
    return {applyId, applyId};
}

/// `workflow.py::inpaint` refinement branch — decode → upscale model → VAE → 2nd sampler @ 0.4.
QString appendInpaintRefinementUpscalePass(QJsonObject *workflow,
                                                  int *nextId,
                                                  const QString &firstDecodeId,
                                                  const QString &vaeNodeId,
                                                  int vaeSlot,
                                                  const QString &modelSourceId,
                                                  QString positiveNodeId,
                                                  QString negativeNodeId,
                                                  int negativeNodeSlot,
                                                  const QString &maskNodeId,
                                                  int maskNodeSlot,
                                                  const ComfyWorkflowEngine::InpaintBuildParams &params,
                                                  ComfyResources::Arch arch)
{
    QString imageId = firstDecodeId;
    int imageSlot = 0;
    if (!params.initialBoundsRelative.isEmpty()) {
        imageId = insertCropImage(workflow, nextId, imageId, imageSlot, params.initialBoundsRelative);
        imageSlot = 0;
    }

    const int desiredW =
        qMax(64, params.desiredExtentWidth > 0 ? params.desiredExtentWidth : params.initialExtentWidth);
    const int desiredH =
        qMax(64, params.desiredExtentHeight > 0 ? params.desiredExtentHeight : params.initialExtentHeight);

    QString upscaleModel = params.upscaleModelName.trimmed();
    if (upscaleModel.isEmpty())
        upscaleModel = defaultInpaintRefinementUpscaleModel(params.refinementScaleMode);

    const QString loaderId = QString::number((*nextId)++);
    workflow->insert(loaderId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("UpscaleModelLoader")},
                                 {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("model_name"), upscaleModel}}}});

    const QString upscaledId = QString::number((*nextId)++);
    workflow->insert(upscaledId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageUpscaleWithModel")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("upscale_model"), QJsonArray{loaderId, 0}},
                                              {QStringLiteral("image"), QJsonArray{imageId, imageSlot}}}}});

    imageId = insertScaleImage(workflow, nextId, upscaledId, 0, desiredW, desiredH);
    imageSlot = 0;

    const QString latentId = QString::number((*nextId)++);
    workflow->insert(latentId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEEncode")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("pixels"), QJsonArray{imageId, imageSlot}},
                                              {QStringLiteral("vae"), QJsonArray{vaeNodeId, vaeSlot}}}}});

    QString upscaleMaskId = maskNodeId;
    int upscaleMaskSlot = maskNodeSlot;
    if (!params.targetBoundsRelative.isEmpty()) {
        upscaleMaskId = insertCropMask(workflow, nextId, maskNodeId, maskNodeSlot, params.targetBoundsRelative);
        upscaleMaskSlot = 0;
    }
    if (params.initialExtentWidth > 0 && params.initialExtentHeight > 0
        && (desiredW != params.initialExtentWidth || desiredH != params.initialExtentHeight)) {
        upscaleMaskId = insertScaleMask(workflow, nextId, upscaleMaskId, upscaleMaskSlot, desiredW, desiredH);
        upscaleMaskSlot = 0;
        upscaleMaskSlot = 0;
    }

    const QString latentMaskedId = QString::number((*nextId)++);
    workflow->insert(latentMaskedId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SetLatentNoiseMask")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("samples"), QJsonArray{latentId, 0}},
                                              {QStringLiteral("mask"), QJsonArray{upscaleMaskId, upscaleMaskSlot}}}}});

    if (params.useInpaintModel && !params.controlNetInpaintFile.isEmpty()) {
        QString pass2Positive = QStringLiteral("5");
        QString pass2Negative = QStringLiteral("6");
        int pass2NegativeSlot = 0;
        const auto patched = applyInpaintControlNetConditioning(workflow,
                                                                nextId,
                                                                pass2Positive,
                                                                pass2Negative,
                                                                vaeNodeId,
                                                                vaeSlot,
                                                                QStringLiteral("1"),
                                                                0,
                                                                upscaleMaskId,
                                                                upscaleMaskSlot,
                                                                params.controlNetInpaintFile,
                                                                arch);
        if (patched.first != QStringLiteral("5")) {
            pass2Positive = patched.first;
            pass2Negative = patched.second;
            pass2NegativeSlot = 1;
        }
        positiveNodeId = pass2Positive;
        negativeNodeId = pass2Negative;
        negativeNodeSlot = pass2NegativeSlot;
    }

    const QString diffModelId = insertDifferentialDiffusion(workflow, nextId, modelSourceId);
    const int steps = qMax(1, params.steps);
    const int startAtStep = static_cast<int>(qRound(steps * (1.0 - 0.4)));
    const QString samplerId = addSamplerCustomAdvanced(workflow,
                                                       nextId,
                                                       diffModelId,
                                                       positiveNodeId,
                                                       negativeNodeId,
                                                       latentMaskedId,
                                                       arch,
                                                       params.cfg,
                                                       steps,
                                                       startAtStep,
                                                       params.sampler,
                                                       params.scheduler.isEmpty() ? QStringLiteral("normal")
                                                                                  : params.scheduler,
                                                       params.seed,
                                                       desiredW,
                                                       desiredH,
                                                       negativeNodeSlot);

    const QString decodeId = QString::number((*nextId)++);
    workflow->insert(decodeId,
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEDecode")},
                                 {QStringLiteral("inputs"),
                                  QJsonObject{{QStringLiteral("samples"), QJsonArray{samplerId, 1}},
                                              {QStringLiteral("vae"), QJsonArray{vaeNodeId, vaeSlot}}}}});

    QString inputCroppedId = QStringLiteral("1");
    if (!params.initialBoundsRelative.isEmpty())
        inputCroppedId = insertCropImage(workflow, nextId, QStringLiteral("1"), 0, params.initialBoundsRelative);

    imageId = decodeId;
    if (params.colorMatch)
        imageId = insertColorMatchAfterImage(workflow, nextId, decodeId, inputCroppedId, upscaleMaskId, upscaleMaskSlot);

    return imageId;
}


QJsonObject parseDefaultWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(defaultWorkflow), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QJsonObject parseImg2ImgWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(img2imgWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QJsonObject parseInpaintingWorkflowTemplate()
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(inpaintingWorkflowTemplate), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

} // namespace detail

} // namespace ComfyWorkflowEngine
