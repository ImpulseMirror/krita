/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_WORKFLOW_ENGINE_INTERNAL_H_
#define COMFY_WORKFLOW_ENGINE_INTERNAL_H_

#include "ComfyWorkflowEngine.h"

#include <functional>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QRect>
#include <QString>

namespace ComfyWorkflowEngine {
namespace detail {

struct PromptOutput {
    QString positiveId;
    QString negativeId;
};

QJsonValue clipEncodeTextInput(const QString &prompt,
                               const QString &translationLang,
                               QJsonObject *workflow,
                               int *nextInjectId);

void patchClipTextEncodeNode(QJsonObject &workflow,
                             const QString &nodeKey,
                             const QString &prompt,
                             const QString &translationLang,
                             int *nextInjectId);

QString addClipTextEncode(const std::function<QString(const QString &, const QJsonObject &)> &addNode,
                          const QJsonArray &clipLink,
                          const QString &prompt,
                          const QString &translationLang);

QJsonObject parseDefaultWorkflowTemplate();
QJsonObject parseImg2ImgWorkflowTemplate();
QJsonObject parseInpaintingWorkflowTemplate();

void replaceInputLink(QJsonObject *workflow, const QJsonArray &from, const QJsonArray &to);
QString findCheckpointNodeId(const QJsonObject &workflow);
QString findNodeIdByClassType(const QJsonObject &workflow, const QString &classType);
QString ipAdapterWeightType(const QString &mode);
void replaceAllLinksFromNode(QJsonObject *workflow, const QString &fromNode, int fromSlot, const QString &toNode,
                             int toSlot);

QString insertInpaintExpandMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int grow, int feather,
                                int maskSlot = 1);
QString insertImageToMask(QJsonObject *workflow, int *nextId, const QString &imageNodeId, const QString &channel);
QString insertDifferentialDiffusion(QJsonObject *workflow, int *nextId, const QString &modelNodeId);
QString insertCropMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, const QRect &bounds);
QString insertCropImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
                        const QRect &bounds);
QString insertScaleImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot, int width,
                         int height);
QString insertScaleMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, int width,
                        int height);
QString insertShrinkMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot, int shrink,
                         int blur);
QString insertThresholdMask(QJsonObject *workflow, int *nextId, const QString &maskNodeId, int maskSlot,
                            double threshold);
QString insertApplyMaskToImage(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
                               const QString &maskNodeId, int maskSlot);
QString insertMaskedFill(QJsonObject *workflow, int *nextId, const QString &imageNodeId, int imageSlot,
                         const QString &maskNodeId, int maskSlot, const QString &fillKind);

void appendColorMatchAfterDecode(QJsonObject *workflow,
                                 const QString &decodeNodeId,
                                 const QString &referenceImageNodeId,
                                 const QString &maskNodeId,
                                 int maskNodeSlot,
                                 int *nextId);
QString appendNsfwFilterAfterDecode(QJsonObject *workflow, const QString &decodeNodeId, double sensitivity, int *nextId);
QString insertColorMatchAfterImage(QJsonObject *workflow,
                                   int *nextId,
                                   const QString &imageNodeId,
                                   const QString &referenceImageNodeId,
                                   const QString &maskNodeId,
                                   int maskNodeSlot);

QString appendInpaintMaskedSaveOutput(QJsonObject *workflow,
                                      int *nextId,
                                      QString outputImageId,
                                      int outputImageSlot,
                                      QString compositingMaskId,
                                      int compositingMaskSlot,
                                      const QRect &targetBoundsRelative,
                                      int inputExtentWidth,
                                      int inputExtentHeight,
                                      int blendMaskBy);
void appendInpaintNativeScaledSaveOutput(QJsonObject *workflow,
                                         int *nextId,
                                         QString outputImageId,
                                         int outputImageSlot,
                                         int diffusionExtentWidth,
                                         int diffusionExtentHeight,
                                         int nativeExtentWidth,
                                         int nativeExtentHeight);
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
                                               int blendMaskBy);

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
                                                           ComfyResources::Arch arch);

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
                                           const InpaintBuildParams &params,
                                           ComfyResources::Arch arch);

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
                                               const QList<IpAdapterLayerInput> &ipLayers,
                                               bool editReference,
                                               ComfyResources::Arch arch);

QString insertConditioningImageNode(QJsonObject *workflow,
                                    int *nextId,
                                    const QString &imageName,
                                    const ConditioningGraphRef &graph);

QList<RegionalPromptInput> filterRegionalPromptsForTile(const QList<RegionalPromptInput> &regions,
                                                        const QRect &tileBounds);

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
                                 int negativeSlot = 0,
                                 int latentSlot = 0);

void finishBuilderWithSamplerCustom(QJsonObject *workflow,
                                    const QString &samplerNodeId,
                                    ComfyResources::Arch arch,
                                    int extentW,
                                    int extentH,
                                    double denoise,
                                    int *nextNodeIdInOut);

} // namespace detail
} // namespace ComfyWorkflowEngine

#endif
