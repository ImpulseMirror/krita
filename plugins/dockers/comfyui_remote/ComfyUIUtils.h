/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_UTILS_H_
#define COMFYUI_UTILS_H_

#include <QString>
#include <QStringList>
#include <QPair>
#include <QRect>
#include <QPoint>
#include <QSize>
#include <QNetworkRequest>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QByteArray>
#include <QUrl>
#include <QtGlobal>  // qBound

#include <KSharedConfig>

#include <utility>

#include <kis_types.h>

class QNetworkAccessManager;
class QNetworkReply;
class QObject;

class KisViewManager;

namespace ComfyUIUtils {

// §13.42: Document color mode — only RGBA and 8-bit are supported for generation/upscale
std::pair<bool, QString> checkColorMode(KisImageSP image);

QString historyCacheDir();
QString pluginUserDataDir();
QString pluginLogDir();
// §13.148: FileLibrary storage — database_dir = user_data_dir/database; LoRAs persist in database/loras.json, checkpoints in-memory from server
QString pluginDatabaseDir();
// §13.31: Local custom workflows — user_data_dir/workflows (*.json); directory created on first use
QString workflowsStorageDir();
QStringList listLocalWorkflowJsonFilenames();
QString lorasJsonPath();
// §13.148 / §4.5: LoRA library JSON at database/loras.json (array of { filename, strength_percent, enabled }).
QJsonArray loadLorasJsonArray();
bool saveLorasJsonArray(const QJsonArray &arr);
// §8.5: Append `<lora:filename:weight>` for each enabled library entry (weight = strength_percent / 100).
QString mergeLibraryLoraTagsIntoPositivePrompt(const QString &positivePrompt);

// §13.165: On load, check plugin is in expected location (e.g. under dockers/ or .git); if not, log warning. Call once from plugin constructor.
void checkPluginInstallationPath();
// §10.2: KisMainWindow saves layout under MainWindow/DockWidget {factory id}; migrate pre–§10.2 id ComfyUIRemote → imageDiffusion once.
void migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion();
// Same migration for an explicit config (e.g. in-memory SimpleConfig in unit tests).
void migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(const KSharedConfigPtr &cfg);

// §3.1: Settings path and persistence — user_data_dir/settings.json, load/save with // line comments
QString settingsFilePath();
QJsonObject loadSettingsJson();
bool saveSettingsJson(const QJsonObject &obj);
// §4.8: Performance → Multi-Threading (subset of operations honor this flag).
inline bool multiThreadingEnabled()
{
    return loadSettingsJson().value(QStringLiteral("multi_threading")).toBool(true);
}
// §4.7: When dump_workflow is enabled, write latest POST /prompt JSON body for debugging
void dumpComfyPromptPayloadIfEnabled(const QJsonObject &payload);
// §4.8 / ComfyUI GET /system_stats — human-readable device line for Performance tab
QString formatComfySystemStatsDeviceLine(const QJsonObject &systemStatsRoot);
// §4.8: When max_pixel_auto is false, scale down w×h to fit within max_pixel_count_mp (megapixels)
void clampExtentToMaxMegapixels(int *width, int *height);

// §13.17: Map ComfyUI /system_stats JSON to a preset key when Performance preset is Automatic
QString inferAutoPerformancePresetKey(const QJsonObject &systemStatsRoot);
// §13.17: Effective batch count + resolution multiplier for generation (custom → use dock values; else table)
void generationPerformanceBatchResolution(const QJsonObject &settingsJson,
                                        const QJsonObject &systemStatsRoot,
                                        int dockBatch,
                                        double dockResolutionMultiplier,
                                        int *outBatch,
                                        double *outResolutionMultiplier);

// §13.23: ScaleMode strings (resolution.py). Invalid/empty → "resize".
QString normalizeDiffusionScaleMode(const QString &rawFromSettings);
// After generationPerformanceBatchResolution, apply scale mode: "none" forces multiplier 1.0.
void adjustEffectiveResolutionMultiplierForDiffusionScaleMode(const QJsonObject &settingsRoot, double *resolutionMultiplier);
// §13.23: ComfyUI ImageScale upscale_method for the simple Upscale workspace graph.
QString comfyImageScaleMethodForDiffusionScaleMode(const QString &normalizedScaleMode);

// §13.24 TileLayout (resolution.py): uniform grid over an extent for tiled upscale/refine planning.
class DiffusionTileLayout {
public:
    QSize imageExtent;
    int tileExtent = 512;
    int minSize = 64;
    int padding = 0; ///< Overlap between adjacent tiles (px); negative requests auto overlap in layout
    int blending = 0; ///< Blend width (px); kept for spec parity; uniform estimator uses 0
    int tileCount = 0;

    DiffusionTileLayout() = default;

    static DiffusionTileLayout fromUniformGrid(int width, int height, int tileExtentPx, int overlapPx,
                                               int minTileSize = 64, int blendPx = 0);
    /// Strength 0–1 (e.g. KSampler denoise): higher strength → smaller tiles; aligned to \p multiple (e.g. 8).
    static DiffusionTileLayout fromDenoiseStrength(QSize extent, int minTileSize, double strength0to1, int multiple,
                                                   int overlapPx = -1);

    int totalTiles() const { return tileCount; }
    QPoint coord(int index) const;
    int tileIndex(QPoint tileCoord) const;
    QRect bounds(int index) const;
    QPoint start(QPoint tileCoord) const;
    QPoint end(QPoint tileCoord) const;

private:
    int gridW = 0;
    int gridH = 0;
    int step = 1;
    void initGridFromExtent();
    QRect boundsAtTileCoord(QPoint tileCoord) const;
};

// §13.24: Uniform tile grid count (overlap padding between tiles); overlap < 0 uses auto overlap from tile extent.
int estimateUniformTileGridCount2D(int width, int height, int tileExtent, int overlapPx);
// §13.24: tile_extent for Upscale target label tile-count estimate (settings.json upscale_tile_estimate_extent, default 512).
int diffusionUpscaleTileEstimateExtentPx(const QJsonObject &settingsRoot);

// §13.204: Single source of truth for plugin version (footer, Plugin tab, diagnostics)
QString pluginVersion();
// §13.159 / §13.160: INTERSTICE_URL overrides; default https://api.interstice.cloud (plugin/latest, plugin/news).
QString intersticeApiBaseUrl();
// §13.159: INTERSTICE_WEB_URL for sign-in / account / website links; default https://www.interstice.cloud
QString intersticeWebBaseUrl();

// §13.14 / §13.152 / §13.69 / §13.79: Document annotations — name = ai_diffusion/ + key, description = "AI Diffusion Plugin: " + key
inline QString documentAnnotationKey(const QString &logicalKey) {
    return QStringLiteral("ai_diffusion/") + logicalKey;
}
inline QString documentAnnotationDescription(const QString &logicalKey) {
    return QStringLiteral("AI Diffusion Plugin: ") + logicalKey;
}

// §13.15: Document identity — use this key for model–document binding when implementing annotation persistence
inline QString documentIdAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("document_id"));
}

// §13.44: Preview layer — Krita layer used for live-preview or generation preview; persist/restore layer ID
inline QString previewLayerAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("preview_layer"));
}

// §13.149: Live workspace state in ui.json equivalent — persist live strength per document
inline QString liveWorkspaceAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("live"));
}

// §13.169: CustomInpaint / ui.json inpaint key — mode, fill, use_inpaint, use_prompt_focus, context, context_layer_id
inline QString inpaintWorkspaceAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("inpaint"));
}

// §13.31: Custom workflow graph embedded in document (ui.json equivalent)
inline QString customWorkflowAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("custom_workflow"));
}

// §9.7 / §13.19: Main document state blob (history list, version; other sections merged on save)
inline QString documentUiJsonAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("ui.json"));
}

// Resolve ComfyUI base URL + relative API path (e.g. api/etn/workflow/subscribe) for HTTP requests
QUrl comfyResolveApiUrl(const QString &baseUrlTrimmed, const QString &relativeApiPath);
// Build ws/wss URL for ComfyUI /ws endpoint (scheme/host/port from HTTP URL)
QUrl comfyWebSocketUrlForClient(const QString &httpUrlTrimmed, const QString &clientId);

// §13.39: When reading history result annotations, try key with extension then without (for older documents)
inline std::pair<QString, QString> documentAnnotationKeysWithFallback(const QString &baseName, const QString &extension) {
    QString withExt = baseName + QLatin1Char('.') + extension;
    return {documentAnnotationKey(withExt), documentAnnotationKey(baseName)};
}

// §4.8 / §13.19: Sum byte size of embedded history blobs (ai_diffusion/result* annotations in .kra)
qint64 documentEmbeddedHistoryStorageBytes(KisImageSP image);
// §13.19: Max embedded history bytes from settings (history_document_storage_mb or legacy history_storage)
qint64 documentEmbeddedHistoryLimitBytes(const QJsonObject &settings);
// §13.199 / §9.7: Load merged ui.json; future version → defaults + flag (callers may show a warning).
struct DocumentUiJsonLoadOutcome {
    QJsonObject object;
    int rawVersionFromFile = 1;
    bool resetToDefaultsDueToFutureVersion = false;
    /// §13.140: Non-empty ui.json annotation existed but JSON parse failed (caller shows QMessageBox).
    bool parseFailed = false;
    QString parseError;
};
DocumentUiJsonLoadOutcome loadDocumentUiJsonWithMeta(KisImageSP image);
// §9.7: Load merged ui.json object (defaults to { "version": 1 } if missing)
QJsonObject loadDocumentUiJsonObject(KisImageSP image);
/// True when the image has a non-empty ai_diffusion/ui.json annotation (document already has embedded plugin state).
bool documentHasStoredUiJsonPayload(KisImageSP image);
// §13.194: `document_defaults` object inside settings.json (RecentlyUsedSync parity).
QJsonObject documentDefaultsFromSettingsRoot(const QJsonObject &settingsRoot);
// §13.137: When applying document_defaults to a fresh document, never restore InpaintContext.layer_bounds.
QString inpaintContextForFreshDocumentDefaults(const QString &storedContext);
// §13.19: Highest slot index from ui.json history[] and result* annotations (+1 = next slot)
int maxHistorySlotFromDocument(KisImageSP image, const QJsonObject &uiRoot);
// §13.19 / §3.5: Concatenate one or more result files into a single blob; offsets are segment end positions [end0, end1, …, total]
struct HistoryImageEncodeResult {
    QByteArray data;
    QList<int> offsets;
};
HistoryImageEncodeResult encodeHistoryImagesFromPaths(const QStringList &paths, const QJsonObject &settings);
// Logical annotation basename for slot N (always .webp in key per §9.7)
QString historyResultLogicalKey(int slot);

// §4.8: When tiled_vae_mode is "always", replace each VAEDecode node with VAEDecodeTiled (ComfyUI core nodes.py).
void applyTiledVaePreferenceToWorkflow(QJsonObject &workflow);
// §4.8: When dynamic_caching is on, enable common toggle inputs on First Block / Tea / FBCache-style nodes if present.
void applyDynamicCachingPreferenceToWorkflow(QJsonObject &workflow);
// §4.8: Apply tiled VAE and dynamic caching preferences to the API workflow object.
void applyPerformancePreferencesToWorkflow(QJsonObject &workflow);
// §13.147 Upscale refine: VAEDecode → VAEDecodeTiled; auto overlap matches DiffusionTileLayout (-1 → tileExtent/8 clamped); custom uses tile_overlap px.
void applyUpscaleRefineVaedecodeTiling(QJsonObject &workflow,
                                      const QString &decodeNodeId,
                                      int tileOverlapMode,
                                      int customOverlapPx,
                                      const QJsonObject &settingsRoot);

// §4.5 / §13.148: Parse LoRA filenames from GET /object_info (LoraLoader*, lora_name / model_name lists).
void extractLoraFilenamesFromObjectInfo(const QJsonObject &objectInfoRoot, QStringList *outSortedUnique);
// True if library basename matches a server object_info entry (exact, basename, or subpath suffix).
bool loraFilenameKnownOnServer(const QString &libraryBasename, const QStringList &serverEntries);

// §13.75 / §1209 CheckpointInfo: intersect CheckpointLoaderSimple names with ETN model_info metadata filters
QStringList filterCheckpointNamesWithEtnModelInfo(const QStringList &checkpointLoaderNames, const QJsonDocument &modelInfoResponse);

// §13.56: Built-in sampler presets (same object shape as ai_diffusion/presets/samplers.json).
QJsonObject builtinSamplerPresetsRoot();
// Re-read presets/samplers.json and merge with built-ins (e.g. when opening Styles settings).
void reloadSamplerPresetsCache();
bool samplerPresetLookup(const QJsonObject &root,
                         const QString &presetName,
                         QString *outSampler,
                         QString *outScheduler,
                         int *outSteps,
                         int *outMinimumSteps,
                         double *outCfg);
struct ResolvedSamplerInputs {
    QString sampler;
    QString scheduler;
    int steps = 20;
    double cfg = 8.0;
};
// §4.5: Live uses live_sampler_preset when set; otherwise dock sampler row.
ResolvedSamplerInputs resolveSamplerForLive(const QJsonObject &settings,
                                            const QString &dockSamplerText,
                                            int dockSteps,
                                            double dockCfg);

// §13.55: Control layer default strength/range presets (same structure as ai_diffusion/presets/control.json).
struct ControlLayerPreset {
    double strength = 1.0;
    double start = 0.0;
    double end = 1.0;
};
QJsonObject builtinControlPresetsRoot();
void reloadControlPresetsCache();
/// Presets for `controlMode` (e.g. "default"); uses `archKey` array if present, else "all".
QList<ControlLayerPreset> controlPresetsForMode(const QJsonObject &root,
                                                const QString &controlMode,
                                                const QString &archKey = QString());
// §13.55 / §13.124: settings key control_layer_default_preset_index (0-based, up to 4 presets) → resolved preset for new layers.
bool resolveDefaultControlLayerPreset(const QJsonObject &settings, ControlLayerPreset *out,
                                      const QString &archKey = QString());

// §8.5: Remove text after '#' unless escaped as '\#'
QString stripPromptComments(QString text);

// §13.201: Attention weight — segment range for cursor (parenthesis/angle block or current word); returns (start, length) or (-1, 0)
std::pair<int, int> attentionSegmentRange(const QString &text, int cursorPos);
// §13.201: Adjust weight in segment by delta (±0.1), clamp [−2.0, 2.0]; returns new segment text
QString editAttentionWeight(const QString &segment, double delta);

// §13.132: Safe short name for job/layer names and history labels (first 40 chars, alphanumeric + " _-" only)
QString sanitizePrompt(const QString &prompt);

// §13.94 / §3.5: save_image_file_name_format — placeholders {document_name}, {job_timestamp}, {job_index}, {prompt}
QString formatSaveImageFileName(const QString &templateStr, const QString &documentName, const QString &jobTimestamp,
                                int jobIndex1Based, const QString &promptTrimmed);

// §3.5: save_image_quality_jpeg (default 85), save_image_quality_webp (default 80); legacy save_image_jpeg_quality
int saveImageQualityJpeg(const QJsonObject &settings);
int saveImageQualityWebp(const QJsonObject &settings);

// §13.127: InpaintParams grow/feather range for workflow (mask preprocessing)
constexpr int inpaintGrowFeatherMin = 0;
constexpr int inpaintGrowFeatherMax = 499;
inline int clampInpaintGrowFeather(int value) {
    return qBound(inpaintGrowFeatherMin, value, inpaintGrowFeatherMax);
}

// §13.135: For settings/workflow JSON: strip whole lines whose stripped form starts with "//" (not #)
QByteArray stripJsonLineComments(QByteArray data);

// §13.101: Convert ComfyUI UI workflow JSON (version + nodes + links) to prompt API graph. Requires GET /object_info.
QPair<bool, QString> convertComfyUiWorkflowUiToApi(const QJsonObject &uiWorkflow, const QJsonObject &objectInfoRoot, QJsonObject *outApi);
/// If \p inOut looks like UI workflow (nodes + links arrays), replaces with API graph; otherwise no-op. Returns false on error.
bool tryResolveCustomWorkflowJsonToApi(QJsonObject *inOut, const QJsonObject &objectInfoRoot, QString *errorOut);

// §13.150: Persistence format version for ui.json; increment on breaking changes
constexpr int persistenceFormatVersion = 1;

// §13.45 / §13.193: Frame output paths for Live and Animation. documentPath = full path to .kra file.
QString liveFramesDirectory(const QString &documentPath);       // {document_directory}/{document_stem}.live-frames
QString animationFramesDirectory(const QString &documentPath);  // {document_directory}/{document_stem}.animation
QString liveFramePath(const QString &documentPath, int frameIndex);    // .../frame-N.webp
QString animationFramePath(const QString &documentPath, int frameIndex); // .../frame-N.png
/// §13.74: True if both files exist and have identical contents (size + byte-wise compare).
bool filesContentsEqual(const QString &pathA, const QString &pathB);

// §13.153: ai_diffusion theme.icon(name) stems → Krita/Breeze icon names (functional parity without bundling SVGs)
QString kritaIconNameForThemeStem(const QString &stem);

// §13.141: Avoid ngrok browser warning when connecting to arbitrary ComfyUI URLs
void setComfyUIRequestHeaders(QNetworkRequest &req);
// §13.113: ComfyClient uploads LoRAs via HTTP PUT api/etn/upload/loras/<file_id> (streaming body). Returns nullptr if the file cannot be opened.
QNetworkReply *tryUploadLoraFileViaEtnApi(QNetworkAccessManager *nam,
                                          const QString &baseUrlTrimmed,
                                          const QString &localFilePath,
                                          QObject *parentForReply);

// §13.142: When server error indicates LCM unsupported, return the spec message; otherwise return error as-is
QString formatServerErrorMessage(const QString &serverError);

// §8.5/13.35: Wildcards {option1|option2|option3} — pick one option per occurrence using deterministic RNG(seed)
QString evalWildcards(QString text, quint32 seed);

// §13.35: Layer placeholders <layer:name> → "Picture {n}"; returns (modifiedPrompt, ordered layer names for workflow binding)
QStringList extractLayerPlaceholders(QString &prompt);

// §13.103: Custom workflow validation — at most one node of type ETN_KritaStyleAndPrompt. Returns (true, "") if valid, else (false, localized error message).
QPair<bool, QString> validateCustomWorkflowStyleAndPromptNodes(const QJsonObject &workflow);

// §13.25: ETN_Parameter / ETN_KritaStyle — discover slots for UI; apply user values to API workflow (Parameter uses inputs["default"]).
struct CustomWorkflowParamSlot {
    QString nodeId;
    QString paramName; ///< inputs["name"] (group/leaf path like Python CustomParam.name)
    QString typeStr;     ///< ETN_Parameter inputs["type"], empty for other node kinds
    enum class Kind {
        ParameterInt,
        ParameterFloat,
        ParameterBool,
        ParameterText,
        ParameterPromptPositive,
        ParameterPromptNegative,
        ParameterChoice,
        KritaStyleSampler,
        KritaImageLayer,
        KritaMaskLayer,
        Unsupported
    } kind = Kind::Unsupported;
    QVariant defaultValue;
    double minV = 0.0;
    double maxV = 0.0;
    QStringList choices;
};
QList<CustomWorkflowParamSlot> discoverCustomWorkflowParameterSlots(const QJsonObject &workflowRoot);
/// Resolve a paint layer UUID string (no braces) to its Krita layer name; empty if not found.
QString paintLayerNameByUuid(KisImageSP image, const QString &uuidWithoutBraces);
/// Merges Configure → Workflow overrides into API workflow JSON. Parameter / style keys match `CustomParam.name`;
/// ETN_KritaImageLayer and ETN_KritaMaskLayer overrides use the ComfyUI **node id** as map key (Python LayerSelect parity).
void applyCustomWorkflowParameterValues(QJsonObject &workflowRoot,
                                        const QMap<QString, QVariant> &valuesByKey,
                                        KisImageSP layerResolutionImage = KisImageSP());
// §13.53: Build a control-image preprocessing workflow (LoadImage -> preprocessor -> optional invert -> SaveImage).
// \p resolution is the **shortest side** of the input extent in pixels (cf. spec: shortest side of extent/bounds);
// normalized inside to a 64-multiple, with floor 512 for non-hands modes.
// Returns empty object when controlMode is unsupported by this port.
QJsonObject buildControlImageWorkflow(const QString &inputImageName,
                                      const QString &controlMode,
                                      int resolution = 1024,
                                      bool invertOutput = false);
// §13.53: ControlMode.is_lines parity helper (scribble/line_art/soft_edge/canny_edge).
bool isControlModeLines(const QString &controlMode);
// §13.53 hands + bounds: composite preprocessor output (crop-sized) back onto a full-extent image at \p cropInExtentCoords.
QImage compositeControlImageOntoExtent(const QImage &processedCrop,
                                       const QSize &fullExtentSize,
                                       const QRect &cropInExtentCoords);
// §13.58: class_type strings listed in Technical_Specification.md (ETN tooling, INPAINT, preprocessors, GrowMask/ImageUpscaleWithModel, common core nodes this port emits).
const QStringList &comfyUiSpecSection58NodeClassTypes();
// §13.58: Subset of the above that appear as keys in GET /object_info (ComfyObjectInfo node registry).
QStringList specSection58NodesPresentInObjectInfo(const QJsonObject &objectInfoRoot);

// §13.154: True when selection covers (0,0) to (width, height) and all pixels are fully selected (0xff)
bool isSelectionEntireDocument(KisImageSP image, KisViewManager *viewManager);

// §13.214: Effective batch count from extent — constrains latent samples so effective extent does not exceed capacity
inline int computeBatchSize(int extentWidth, int extentHeight, int minSize = 512, int maxBatches = 8) {
    if (extentWidth <= 0 || extentHeight <= 0 || maxBatches < 1) return 1;
    qint64 area = static_cast<qint64>(extentWidth) * extentHeight;
    if (area <= 0) return 1;
    qint64 refArea = static_cast<qint64>(minSize) * minSize;
    int batch = qMax(1, static_cast<int>((refArea * maxBatches) / area));
    return qMin(maxBatches, batch);
}

// §13.205: When InpaintMode is automatic: expand if selection touches/exceeds doc in at least one dimension, else fill
inline QString detectInpaintMode(int extentWidth, int extentHeight, int areaX, int areaY, int areaWidth, int areaHeight) {
    if (extentWidth <= 0 || extentHeight <= 0) return QStringLiteral("fill");
    if (areaWidth >= extentWidth || areaHeight >= extentHeight) return QStringLiteral("expand");
    return QStringLiteral("fill");
}

// §13.206: Classify checkpoint name for arch (use_inpaint_model thresholds: sd15 >0.5, sdxl >0.8, flux ==1.0). Returns "sd15", "sdxl", "flux", "flux2_4b", "flux_k", "qwen_e", or "unknown"
QString classifyCheckpointArch(const QString &ckptName);

// §13.206: True when arch is an edit model (flux_k, qwen_e, qwen_e_p, qwen_l) — result.mode = custom, result.fill = none
bool isArchEdit(const QString &ckptName);

// §13.206: InpaintParams derived from mode, arch, strength, and conditioning (detect_inpaint equivalent)
struct InpaintParams {
    QString fillKind;       // "blur" | "border" | "neutral" | "inpaint" | "replace" | "green" | "none"
    bool useInpaintModel = false;
    bool useReference = false;
    bool useConditionMask = false;
    bool isEditMode = false;
};
InpaintParams detectInpaintParams(const QString &mode, const QString &arch, double strength0to1,
                                  bool positiveEmpty, bool hasStructuralControl, bool editReference = false);

// §13.43: calc_selection_pre_process grow (pixels). Uses extent/area diagonal, strength, and modifier defaults. Returns grow clamped to 0–499 for grow_mask_by.
int calcSelectionPreProcessGrow(int extentWidth, int extentHeight, int areaWidth, int areaHeight, double strength0to1,
                                int selectionFeatherPercent = 50, double selectionMinTransition = 0, int selectionGrowOffset = 0);

// §13.43: Read selection modifier settings from settings.json (selection_feather, selection_min_transition, selection_grow_offset). Defaults 50, 0, 0.
void getSelectionModifierSettings(int *selectionFeatherPercent, double *selectionMinTransition, int *selectionGrowOffset);
// §13.102: SelectionModifiers.square and .invert — for create_mask_from_selection and bounds
bool getSelectionModifiersInvert();
bool getSelectionModifiersSquare();
// §13.102: When square is true, return a square rect (max of w,h) centered and clamped to image bounds
QRect makeRectSquare(const QRect &rect, int extentWidth, int extentHeight);

QImage getCanvasAsQImage(KisImageSP image);
// maskSource: "selection" or "layer:<name>". §13.102: when invertSelection true, invert mask (white↔black) after reading selection.
QImage getMaskAsQImage(KisImageSP image, KisViewManager *viewManager, const QString &maskSource, bool invertSelection = false);

// Composite result over current using mask (mask white = use result pixel).
void compositeWithMask(QImage &current, const QImage &result, const QImage &mask);

// §13.47: Build diagnostics string for "Collect Diagnostics" (version, platform, paths, settings redacted, log tails)
QString collectDiagnostics(const QString &pluginVersion, bool redactUser = true,
                           const QStringList *objectInfoSpec58NodesPresent = nullptr);

// §13.36: A1111-style metadata string for PNG "parameters". `prompt` should include library LoRA tags (mergeLibraryLoraTagsIntoPositivePrompt after stripPromptComments) for parity with JobParams.
QString createImgMetadata(const QString &prompt, const QString &negative, int steps, double cfg, qint64 seed,
                          int width, int height, int strength, const QString &samplerName, const QString &checkpoint);

// §13.37: Extract verified update ZIP to an empty or cleared directory (requires COMFYUI_HAVE_KARCHIVE at build time).
bool extractZipToDirectory(const QString &zipPath, const QString &destDir, QString *errorOut = nullptr);

// §13.215: Tag CSV format — columns tag, type, count, aliases; returns list of tag strings for autocomplete
QStringList loadTagCsvTags(const QString &filePath);
// §13.48: `user_data_dir/tags` (created on demand); CSVs named `{stem}.csv` per settings tag_files
QString tagsStorageDir();
// §13.48: Merge tags from tag_directory (or default tags dir) + stems in tag_files (default Danbooru, e621)
QStringList tagKeywordsForAutocomplete(const QJsonObject &settings = QJsonObject());

// §13.4: Prompt import from image file — read A1111 "parameters" from PNG (tEXt chunk); returns (positive, negative) or (empty, empty)
QPair<QString, QString> readPromptFromImageFile(const QString &filePath);

// §13.125: Edit mode + linked_edit_style — KSampler/checkpoint/style/negative from named custom preset (settings.json + KConfig)
struct LinkedEditStyleOverride {
    bool active = false;
    QString checkpoint;
    int steps = 20;
    double cfg = 8.0;
    double denoise = 1.0;
    QString sampler = QStringLiteral("euler");
    QString scheduler = QStringLiteral("normal");
    QString stylePositiveTemplate;
    QString styleNegative;
};
LinkedEditStyleOverride linkedEditStyleOverride(bool editModeEnabled, const QString &dockCkpt, int dockSteps, double dockCfg,
                                                double dockDenoise, const QString &dockSampler, const QString &dockScheduler);

// §13.125: Merge style template with user/region instruction; {prompt} placeholder per spec style JSON
QString mergeStylePromptWithInstruction(const QString &styleTemplate, const QString &userInstruction);

}

#endif
