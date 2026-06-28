/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_WORKFLOW_ENGINE_H_
#define COMFY_WORKFLOW_ENGINE_H_

#include "ComfyResources.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>

namespace ComfyWorkflowEngine {

struct ControlNetLayerInput;
struct IpAdapterLayerInput;
struct RegionalPromptInput;

/// Parameters for basic text-to-image (WorkflowKind.generate, no regions/controls yet).
struct TextToImageParams {
    QString checkpoint;
    int width = 512;
    int height = 512;
    int batchSize = 1;
    int layerCount = 1;
    QString positivePrompt;
    QString negativePrompt;
    /// P4.2: 2-letter code when translation_enabled (ETN_Translate via lang:xx directives); empty = off.
    QString promptTranslationLanguage;
    qint64 seed = 0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
    double denoise = 1.0;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    /// Enabled LoRAs from the active style preset (Python Style.get_models → CheckpointInput.loras).
    QJsonArray styleLoras;
};

/// Build ComfyUI API workflow JSON (CheckpointLoaderSimple + EmptyLatent + sampler path).
/// When usesSamplerCustomAdvanced(arch), KSampler is replaced with SamplerCustomAdvanced (Python generate()).
QJsonObject buildTextToImage(const TextToImageParams &params);

/// Python workflow.generate() uses SamplerCustomAdvanced for all arches.
bool usesSamplerCustomAdvanced(ComfyResources::Arch arch);

/// Replace template KSampler with SamplerCustomAdvanced (after conditioning is applied).
void finishWorkflowWithSamplerCustom(QJsonObject *workflow,
                                     const QString &samplerNodeId,
                                     ComfyResources::Arch arch,
                                     int extentWidth,
                                     int extentHeight,
                                     double denoiseStrength,
                                     int *nextNodeId = nullptr);

/// Insert ETN_NSFWFilter before SaveImage when `settings.nsfw_filter` > 0 (all generate/inpaint/refine paths).
void applyNsfwFilterToWorkflowOutput(QJsonObject *workflow, double sensitivity);

/// Python pack_latent_layers — LatentCutToBatch before VAEDecode when Qwen Layered layer_count > 1.
void packLatentLayersAfterSampler(QJsonObject *workflow, int layerCount, int batchSlice);

/// Control / IP-Adapter / regional inputs applied after base graph (Python Conditioning).
struct GenerationConditioningParams {
    QList<IpAdapterLayerInput> ipLayers;
    QList<ControlNetLayerInput> controlLayers;
    QList<RegionalPromptInput> regions;
    bool editReference = false;
};

/// Node ids for applyGenerationConditioning (text2img vs img2img/refine).
struct WorkflowGraphContext {
    QString samplerNodeId = QStringLiteral("3");
    QString modelNodeId = QStringLiteral("4");
    QString positiveNodeId = QStringLiteral("6");
    QString negativeNodeId = QStringLiteral("7");
    QString clipSourceNodeId = QStringLiteral("4");
    QString latentImageNodeId;
    QString canvasImageNodeId;
    int extentWidth = 512;
    int extentHeight = 512;
};

struct GenerationConditioningResult {
    QString modelNodeId;
    QString positiveNodeId;
    QString negativeNodeId;
};

/// IP → regional → control → optional ReferenceLatent (edit arches). Order matches Python generate().
GenerationConditioningResult applyGenerationConditioning(QJsonObject *workflow,
                                                         const GenerationConditioningParams &params,
                                                         const WorkflowGraphContext &ctx,
                                                         ComfyResources::Arch arch);

/// Infer graph context from a text2img or img2img template workflow.
WorkflowGraphContext discoverWorkflowGraphContext(const QJsonObject &workflow);

/// LoadImage(1) + VAEEncode(2) template used by buildRefine (full-canvas img2img).
bool isImg2imgRefineWorkflow(const QJsonObject &workflow);

/// Text2img + style options + conditioning + SamplerCustomAdvanced (GAP-A build_generate).
struct GenerateParams : TextToImageParams {
    GenerationConditioningParams conditioning;
    QString styleVae;
    int styleClipSkip = 0;
};

QJsonObject buildGenerate(const GenerateParams &params);

/// Python create_control_image — delegates to ComfyUIUtils preprocessor graph.
struct ControlPreviewParams {
    QString uploadedImageName;
    QString mode;
    int resolutionBase = 512;
    bool returnPreprocessed = false;
};

QJsonObject buildControlPreview(const ControlPreviewParams &params);

/// Insert CLIPSetLastLayer / VAELoader when style JSON specifies clip_skip or non-default VAE.
void applyCheckpointStyleOptions(QJsonObject *workflow,
                                   const QString &vaeName,
                                   int clipSkip,
                                   ComfyResources::Arch arch);

/// img2img refine (WorkflowKind.refine): canvas already uploaded; LoadImage node 1 uses imageName.
struct RefineParams {
    QString checkpoint;
    QString imageName;
    QString positivePrompt;
    QString negativePrompt;
    /// P4.2: 2-letter code when translation_enabled (ETN_Translate via lang:xx directives); empty = off.
    QString promptTranslationLanguage;
    qint64 seed = 0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
    double denoise = 0.75;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    QJsonArray styleLoras;
    double nsfwFilterSensitivity = 0.0;
};

QJsonObject buildRefine(const RefineParams &params);

/// Regional refine with mask (inpaint graph + sampler custom).
struct RefineRegionParams {
    RefineParams refine;
    QString maskImageName;
    int extentWidth = 512;
    int extentHeight = 512;
    int contextExtentWidth = 0;
    int contextExtentHeight = 0;
    int growMaskBy = 6;
    int featherMaskBy = 0;
    int blendMaskBy = 0;
    QRect targetBoundsRelative;
    QRect nativeTargetBoundsRelative;
    bool colorMatch = false;
    bool useInpaintModel = false;
    QString fooocusInpaintHead;
    QString fooocusInpaintPatch;
    /// Pre-resolved on server; empty skips ControlNetInpaintingAliMamaApply (upstream find_control).
    QString controlNetInpaintFile;
    double nsfwFilterSensitivity = 0.0;
};

QJsonObject buildRefineRegion(const RefineRegionParams &params);

/// One LoRA applied after checkpoint load (Python load_checkpoint_with_lora).
struct CheckpointLoraWeight {
    QString name;
    double strengthModel = 1.0;
    double strengthClip = 1.0;
};

struct CheckpointLoadParams {
    QString checkpoint;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    QList<CheckpointLoraWeight> loras;
    bool dynamicCaching = false;
};

struct CheckpointGraphRefs {
    QString modelNodeId;
    QString clipNodeId;
    QString vaeNodeId;
    int vaeNodeSlot = 2;
    int nextNodeId = 500;
};

/// Replace/patch checkpoint node: CheckpointLoaderSimple, Nunchaku*, UNET+DualCLIP, + LoraLoader chain.
bool loadCheckpointWithLora(QJsonObject *workflow, const CheckpointLoadParams &params, CheckpointGraphRefs *out = nullptr);

/// Enabled entries from style JSON `loras` array (Python LoraInput.from_dict + enabled filter).
QList<CheckpointLoraWeight> checkpointLorasFromStyle(const QJsonArray &styleLoras);
/// Upstream `unique(..., key=lambda l: l.name)` when merging style + prompt LoRAs.
QList<CheckpointLoraWeight> mergeCheckpointLorasUnique(const QList<CheckpointLoraWeight> &base,
                                                       const QList<CheckpointLoraWeight> &extra);

/// Inpaint (selection mask uploaded): LoadImage + VAEEncodeForInpaint + KSampler path.
struct InpaintBuildParams {
    QString imageName;
    QString maskImageName;
    QString checkpoint;
    QString positivePrompt;
    QString negativePrompt;
    /// P4.2: 2-letter code when translation_enabled (ETN_Translate via lang:xx directives); empty = off.
    QString promptTranslationLanguage;
    qint64 seed = 0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
    double denoise = 1.0;
    int growMaskBy = 6;
    int featherMaskBy = 0;
    int blendMaskBy = 0;
    QRect targetBoundsRelative;
    QRect nativeTargetBoundsRelative;
    int contextExtentWidth = 512;
    int contextExtentHeight = 512;
    QString fillKind = QStringLiteral("none");
    bool useConditionMask = false;
    QString backgroundPrompt;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    QJsonArray styleLoras;
    double nsfwFilterSensitivity = 0.0;
    bool useInpaintModel = false;
    bool useReference = false;
    bool colorMatch = false;
    QString fooocusInpaintHead;
    QString fooocusInpaintPatch;
    /// Pre-resolved on server; empty skips ControlNetInpaintingAliMamaApply (upstream find_control).
    QString controlNetInpaintFile;
    /// `workflow.py::inpaint` 2-pass path when `refinement_scaling` is upscale_small/quality.
    bool refinementUpscale = false;
    QString refinementScaleMode;
    QString upscaleModelName;
    int initialExtentWidth = 0;
    int initialExtentHeight = 0;
    int desiredExtentWidth = 0;
    int desiredExtentHeight = 0;
    QRect initialBoundsRelative;
};

QJsonObject buildInpaint(const InpaintBuildParams &params);

/// §13.206: SD1.5 prompt focus — ETN_BackgroundRegion + ETN_DefineRegion + ETN_AttentionMask on model.
bool applyInpaintPromptFocus(QJsonObject *workflow, int *nextNodeId, const QString &modelNodeId,
                             const QString &positiveClipNodeId, const QString &clipSourceNodeId,
                             const QString &maskNodeId, int maskNodeSlot, const QString &backgroundPrompt);

/// Live preview (WorkflowKind.live): img2img from uploaded canvas; same graph as refine.
using LiveParams = RefineParams;
QJsonObject buildLive(const LiveParams &params);

/// Per-frame text2img for animation batch (WorkflowKind.generate, batch_count=1 per prompt).
struct AnimationFrameParams {
    TextToImageParams base;
    qint64 batchBaseSeed = 0;
    int frameIndex = 0;
    int batchSeedStep = 1;
};

/// seed = batchBaseSeed + frameIndex * batchSeedStep (§13.212).
qint64 animationFrameSeed(qint64 batchBaseSeed, int frameIndex, int batchSeedStep);

QJsonObject buildAnimationFrame(const AnimationFrameParams &params);

/// Lanczos/ImageScale upscale (WorkflowKind.upscale_simple when no diffusion model).
struct UpscaleSimpleParams {
    QString imageName;
    int targetWidth = 1024;
    int targetHeight = 1024;
    QString upscaleMethod = QStringLiteral("lanczos");
};

QJsonObject buildUpscaleSimple(const UpscaleSimpleParams &params);

/// Single-pass upscale + diffusion refine (partial parity with upscale_tiled for one tile).
struct UpscaleRefineParams {
    QString imageName;
    int scaleWidth = 1024;
    int scaleHeight = 1024;
    QString upscaleMethod = QStringLiteral("lanczos");
    QString checkpoint;
    QString positivePrompt;
    QString negativePrompt;
    /// P4.2: 2-letter code when translation_enabled (ETN_Translate via lang:xx directives); empty = off.
    QString promptTranslationLanguage;
    qint64 seed = 0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 8.0;
    double denoise = 0.35;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
};

QJsonObject buildUpscaleRefine(const UpscaleRefineParams &params);

/// Multi-tile diffusion upscale (WorkflowKind.upscale_tiled; requires ETN tooling nodes).
struct UpscaleTiledParams {
    QString imageName;
    QString checkpoint;
    QString positivePrompt;
    QString negativePrompt;
    /// P4.2: 2-letter code when translation_enabled (ETN_Translate via lang:xx directives); empty = off.
    QString promptTranslationLanguage;
    qint64 seed = 0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
    double denoise = 0.35;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    int scaledWidth = 1024;
    int scaledHeight = 1024;
    int targetWidth = 1024;
    int targetHeight = 1024;
    int tileOverlapPx = -1;
    int minTileSize = 512;
    int stylePreferredResolution = 0;
    QString upscaleModelName;
    QList<ControlNetLayerInput> controlLayers;
    QList<IpAdapterLayerInput> ipAdapterLayers;
    QList<RegionalPromptInput> regionalPrompts;
    double upscaleFactor = 1.0;
    /// Python cond.edit_reference — ReferenceLatent from tile image/latent on edit arches.
    bool editReference = false;
};

QJsonObject buildUpscaleTiled(const UpscaleTiledParams &params);

/// One ControlNet layer ready for graph injection (image already uploaded to ComfyUI).
struct ControlNetLayerInput {
    QString mode;
    QString imageName;
    double strength = 1.0;
    double startPercent = 0.0;
    double endPercent = 1.0;
};

/// IP-Adapter layer (reference / style / composition / face) with uploaded image.
struct IpAdapterLayerInput {
    QString mode;
    QString imageName;
    double strength = 1.0;
    double startPercent = 0.0;
    double endPercent = 1.0;
};

/// Node ids for conditioning injection (generate, upscale tiled, etc.).
struct ConditioningGraphRef {
    QString samplerNodeId = QStringLiteral("3");
    QString modelNodeId;
    QString positiveNodeId;
    QString negativeNodeId;
    QString clipSourceNodeId = QStringLiteral("4");
    int nextNodeId = 50;
    QString tileLayoutNodeId;
    int tileIndex = -1;
    int scaledWidth = 0;
    int scaledHeight = 0;
};

/// Apply IP-Adapter to KSampler model input (SD1.5 / SDXL). Returns false if unsupported or no layers.
bool applyIpAdapterLayers(QJsonObject *workflow,
                          const QList<IpAdapterLayerInput> &layers,
                          ComfyResources::Arch arch,
                          ConditioningGraphRef *graph = nullptr);

/// Chain ControlNetApplyAdvanced nodes onto positive conditioning (P1.3).
/// Skips IP-Adapter modes; returns false if no structural layers were applied.
bool applyControlNetLayers(QJsonObject *workflow,
                             const QList<ControlNetLayerInput> &layers,
                             ComfyResources::Arch arch,
                             ConditioningGraphRef *graph = nullptr);

/// One region for ETN_BackgroundRegion / ETN_DefineRegion / ETN_AttentionMask (mask on ComfyUI server).
struct RegionalPromptInput {
    QString positivePrompt;
    QString maskImageName;
    bool isBackground = false;
    QString promptTranslationLanguage;
    /// Upscaled-space mask bounds for tiled upscale region filtering (empty = no filter).
    QRect maskBoundsUpscaled;
};

/// Result node ids for KSampler wiring after regional conditioning.
struct RegionalWorkflowNodes {
    QString modelNodeId;
    QString positiveNodeId;
    QString negativeNodeId;
    bool applied = false;
};

/// Build ETN regional graph (requires comfyui-tooling-nodes). Use when region count >= 2.
RegionalWorkflowNodes applyRegionalGeneration(QJsonObject *workflow,
                                              const QList<RegionalPromptInput> &regions,
                                              const QString &modelSourceNode,
                                              const QString &rootPositiveNode,
                                              const QString &rootNegativeNode,
                                              const QString &clipSourceNode = QStringLiteral("4"),
                                              ConditioningGraphRef *graph = nullptr);

/// Resolve arch from checkpoint + optional style architecture string ("auto", "sdxl", …).
ComfyResources::Arch resolveArch(const QString &checkpoint, const QString &styleArchitecture = QString());

/// Checkpoint + prompts injected for one ETN_KritaStyle / ETN_KritaStyleAndPrompt node.
struct CustomWorkflowStyleExpandInput {
    QString checkpoint;
    QList<CheckpointLoraWeight> loras;
    QString positivePrompt;
    QString negativePrompt;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
};

/// P2: `expand_custom` — replace ETN Krita injection nodes with Comfy core nodes + literals.
struct ExpandCustomKritaWorkflowParams {
    QJsonObject workflow;
    QString canvasImageName;
    QString maskImageName;
    QRect captureBounds;
    bool hasSelectionMask = false;
    qint64 seed = 0;
    /// ETN_KritaImageLayer / ETN_KritaMaskLayer node id → uploaded Comfy filename.
    QHash<QString, QString> layerUploadByNodeId;
    QString checkpoint;
    ComfyResources::Arch arch = ComfyResources::Arch::Sd15;
    QList<CheckpointLoraWeight> loras;
    QString positivePrompt;
    QString negativePrompt;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    int steps = 20;
    double cfg = 7.0;
    /// Per ETN_KritaStyle node id — resolved style from ComfyStyleCollection.
    QHash<QString, CustomWorkflowStyleExpandInput> kritaStyleByNodeId;
};
QJsonObject expandCustomKritaWorkflowNodes(const ExpandCustomKritaWorkflowParams &params);

} // namespace ComfyWorkflowEngine

#endif
