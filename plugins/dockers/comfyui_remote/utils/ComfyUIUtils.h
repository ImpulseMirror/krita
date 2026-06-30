/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_UTILS_H_
#define COMFYUI_UTILS_H_

#include <QString>
#include <QStringList>
#include <QPair>
#include <functional>
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
#include <QUuid>

namespace ComfyWorkflowEngine {
struct RefineParams;
}
#include <QtGlobal>  // qBound
#include <optional>

#include "ComfyStyleCollection.h"

#include "ComfyResources.h"

#include "ComfyControlLayer.h"

#include <KSharedConfig>

#include <utility>

#include <kis_types.h>
#include <kis_image.h>
#include <kis_node.h>

class QNetworkAccessManager;
class QNetworkReply;
class QObject;

class KisViewManager;

namespace ComfyUIUtils {

// §13.42: Document color mode — only RGBA and 8-bit are supported for generation/upscale
std::pair<bool, QString> checkColorMode(KisImageSP image);

QString historyCacheDir();
/// Installed plugin data (styles, presets, tags) — sibling to kritacomfyuiremote module or source tree in dev.
QString pluginInstallDataDir();
void ensureBundledPluginDataInstalled();
/// ai_diffusion theme.icon(): bundled SVG/PNG under data/icons ({stem}-{themeSuffix}.svg).
QString findBundledThemeIconFile(const QString &stem, const QString &themeSuffix);
QString pluginUserDataDir();
#ifdef COMFYUI_ENABLE_TEST_HOOKS
namespace ComfyUITestHooks {
void setPluginUserDataDirOverride(const QString &path);
void clearPluginUserDataDirOverride();
} // namespace ComfyUITestHooks
#endif
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
/// Python v1.23+: append `lora_triggers` metadata for enabled style LoRAs (workflow applies weights via LoraLoader).
QString mergeStyleLoraTriggersIntoPositivePrompt(const QString &positivePrompt, const QJsonArray &styleLoras);

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
/// Persisted ComfyUI server URL from settings.json or KConfig (empty when never configured).
QString savedServerUrl();
// §4.8: Performance → Multi-Threading (subset of operations honor this flag).
inline bool multiThreadingEnabled()
{
    return loadSettingsJson().value(QStringLiteral("multi_threading")).toBool(true);
}
// §4.7: When dump_workflow is enabled, write latest POST /prompt JSON body for debugging
void dumpComfyPromptPayloadIfEnabled(const QJsonObject &payload);
/// Extract prompt_id from ComfyUI GET /queue response (running + pending).
QStringList promptIdsFromComfyQueueResponse(const QJsonObject &queueRoot);
/// GET /queue, delete all known server jobs + local ids, then POST /interrupt.
void requestComfyClearAllQueueJobs(QNetworkAccessManager *nam,
                                   const QString &baseUrlTrimmed,
                                   const QStringList &localPromptIds,
                                   QObject *context);
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

/// §13.24 / GAP-J: Python TileLayout + prepare upscale_tiled tile_size (resolution.py, workflow.py).
struct UpscaleTiledLayoutSpec {
    int minTileSize = 512;
    int padding = 0;
    int blending = 0;
    int totalTiles = 1;
};
int computeUpscaleTiledMinTileSizePx(int targetWidth,
                                     int targetHeight,
                                     ComfyResources::Arch arch,
                                     int stylePreferredResolution = 0);
UpscaleTiledLayoutSpec computeUpscaleTiledLayoutSpec(int imageWidth,
                                                     int imageHeight,
                                                     ComfyResources::Arch arch,
                                                     int stylePreferredResolution,
                                                     double denoiseStrength0to1,
                                                     int customOverlapPx);

// §13.204: Single source of truth for plugin version (footer, Plugin tab, diagnostics)
QString pluginVersion();
// §13.159 / §13.160: INTERSTICE_URL overrides; default https://api.interstice.cloud (plugin/latest, plugin/news).
QString intersticeApiBaseUrl();

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

/// CustomInpaint fields mirrored in per-document `inpaint` annotation and `ui.json`.
struct InpaintWorkspaceSnapshot {
    QString mode = QStringLiteral("automatic");
    QString fill = QStringLiteral("blur");
    bool useInpaint = true;
    bool usePromptFocus = false;
    QString context = QStringLiteral("automatic");
    QString contextLayerId;
};

/// Accept upstream enum int or name string (Python `serialize` uses `.value`).
QString inpaintModeKeyFromJson(const QJsonValue &value);
QString fillModeKeyFromJson(const QJsonValue &value);
/// Accept upstream `InpaintContext` enum int (0–3) or name string.
QString inpaintContextKeyFromJson(const QJsonValue &value);
/// Normalize `context_layer_id` from Python `QUuid.toString()` or bare UUID.
QString inpaintContextLayerIdFromJson(const QJsonValue &value);
QJsonObject inpaintWorkspaceToJson(const InpaintWorkspaceSnapshot &state);
bool inpaintWorkspaceFromJson(const QJsonObject &object, InpaintWorkspaceSnapshot *out);

// §13.31: Custom workflow graph embedded in document (ui.json equivalent)
inline QString customWorkflowAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("custom_workflow"));
}

// §9.7 / §13.19: Main document state blob (history list, version; other sections merged on save)
inline QString documentUiJsonAnnotationKey() {
    return documentAnnotationKey(QStringLiteral("ui.json"));
}

/// Ensure host:port or partial URL has http/https scheme for QNetworkRequest.
QString normalizeComfyServerBaseUrl(const QString &hostOrUrl);
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

/// RecentlyUsedSync fields mirrored in `settings.json` → `document_defaults` (not KSharedConfig).
struct RecentlyUsedSyncSnapshot {
    QString inpaintMode = QStringLiteral("automatic");
    QString inpaintFill = QStringLiteral("blur");
    bool inpaintUseModel = true;
    bool inpaintUsePromptFocus = false;
    QString inpaintContext = QStringLiteral("automatic");
    int batchCount = 1;
};

/// Load `document_defaults`; one-time migrate missing keys from legacy KSharedConfig `ComfyUIRemote` group.
RecentlyUsedSyncSnapshot recentlyUsedSyncFromSettings(bool *migratedOut = nullptr);
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

// §13.123: CheckpointLoaderSimple ckpt_name list from GET /object_info.
QStringList parseCheckpointNamesFromObjectInfoRoot(const QJsonObject &objectInfoRoot);
// §4.5 / §13.148: Parse LoRA filenames from GET /object_info (LoraLoader*, lora_name / model_name lists).
void extractLoraFilenamesFromObjectInfo(const QJsonObject &objectInfoRoot, QStringList *outSortedUnique);
// True if library basename matches a server object_info entry (exact, basename, or subpath suffix).
bool loraFilenameKnownOnServer(const QString &libraryBasename, const QStringList &serverEntries);
/// object_info list + remote entries from ComfyFileLibrary (after updateRemoteLoras).
QStringList mergedServerLoraFilenames(const QStringList &serverLoraFilenames);
/// Pick exact server list entry for workflow lora_name (subdir paths, not basename only).
QString preferServerLoraEntry(const QString &resolvedName, const QStringList &serverLoraFilenames);

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
                         double *outCfg,
                         QString *outLora = nullptr);
/// Preset names for sampler combos (non-hidden, plus ensureIncluded and presets referenced by styles).
QStringList visibleSamplerPresetNames(const QString &ensureIncluded = QString());
/// Python StylePresets._set_checkpoint_warning messages (best-effort without full workload API).
QStringList styleCheckpointWarnings(const struct ComfyStyleEntry &style,
                                    const QStringList &serverCheckpointNames,
                                    const QJsonObject &objectInfoRoot);
struct ResolvedSamplerInputs {
    QString sampler;
    QString scheduler;
    int steps = 20;
    int minSteps = 1;
    double cfg = 8.0;
};
/// Result of resolving a sampler preset's optional LoRA (Python workflow._get_sampling_lora).
struct SamplerPresetLoraResult {
    bool ok = true;
    QString loraFilename;
    QString errorMessage;
};
/// Upstream `workflow.sampling_from_style` + `apply_strength` for refine / inpaint / live.
struct ResolvedSamplingInputs {
    QString sampler;
    QString scheduler;
    int totalSteps = 20;
    int startAtStep = 0;
    double denoiseStrength = 1.0;
    double cfg = 8.0;
};
ResolvedSamplingInputs resolveSamplingFromStyle(const ComfyStyleEntry *styleEntry,
                                                const QJsonObject &settings,
                                                const QString &dockSamplerText,
                                                int dockSteps,
                                                double dockCfg,
                                                double strength0to1,
                                                bool isLive);
/// Apply `sampling_from_style` + `apply_strength` to img2img/refine builder params.
void applyStrengthResolvedSamplingToRefine(ComfyWorkflowEngine::RefineParams *refine,
                                           const ComfyStyleEntry *styleEntry,
                                           const QJsonObject &settings,
                                           const QString &dockSampler,
                                           int dockSteps,
                                           double dockCfg,
                                           double strength0to1,
                                           bool isLive = false);
// §4.5: Live uses live_sampler_preset when set; otherwise dock sampler row.
ResolvedSamplerInputs resolveSamplerForLive(const ComfyStyleEntry *styleEntry,
                                            const QJsonObject &settings,
                                            const QString &dockSamplerText,
                                            int dockSteps,
                                            double dockCfg);
/// Live/quality sampler preset name from style + settings (Python style.live_sampler / style.sampler).
QString liveSamplerPresetName(const ComfyStyleEntry *styleEntry, const QJsonObject &settings);
/// Match server model list against search-path patterns (Python comfy_client._find_model).
QString findModelOnServerBySearchPaths(const QStringList &serverModels, const QStringList &searchPaths);
/// Resolve sampler-preset LoRA for workflow; errors when required file missing on server and locally.
SamplerPresetLoraResult resolveSamplerPresetLora(const QString &presetName,
                                                 ComfyResources::Arch arch,
                                                 const QStringList &serverLoraFilenames);
/// Append preset LoRA (weight 1.0) when not already in style loras.
QJsonArray appendSamplerPresetLora(const QJsonArray &styleLoras, const QString &loraFilename);

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

/// One region row in ui.json `regions[]` / `root.regions[]` (Python positive + layer_ids + control[]).
struct ComfyRegionUiStateEntry {
    QString name;
    QString positive;
    QString maskSource = QStringLiteral("selection");
    QString layerIds;
    QList<ComfyControlLayerEntry> controlLayers;
};

QJsonObject regionUiStateEntryToJson(const ComfyRegionUiStateEntry &entry);
ComfyRegionUiStateEntry regionUiStateEntryFromJson(const QJsonObject &o);
QJsonArray regionUiStateEntriesToJsonArray(const QList<ComfyRegionUiStateEntry> &entries);
QList<ComfyRegionUiStateEntry> regionUiStateEntriesFromJsonArray(const QJsonArray &arr);
/// Python `root` / `edit` wrapper: positive, negative, nested regions[].
QJsonObject rootRegionUiWrapToJson(const QString &positive,
                                   const QString &negative,
                                   const QList<ComfyRegionUiStateEntry> &regions);
bool rootRegionUiWrapFromJson(const QJsonObject &wrap,
                              QString *positive,
                              QString *negative,
                              QList<ComfyRegionUiStateEntry> *regions);
/// Top-level `regions` array (legacy/alternate) + `control` root list helpers.
QJsonArray readRegionUiArrayFromDocumentUi(const QJsonObject &ui, bool *foundInDocument = nullptr);

// §13.45 / §13.193: Frame output paths for Live and Animation. documentPath = full path to .kra file.
QString liveFramesDirectory(const QString &documentPath);       // {document_directory}/{document_stem}.live-frames
QString animationFramesDirectory(const QString &documentPath);  // {document_directory}/{document_stem}.animation
QString liveFramePath(const QString &documentPath, int frameIndex);    // .../frame-N.webp
QString animationFramePath(const QString &documentPath, int frameIndex); // .../frame-N.png
/// §13.74: True if both files exist and have identical contents (size + byte-wise compare).
bool filesContentsEqual(const QString &pathA, const QString &pathB);

// §13.153: ai_diffusion theme.icon(name) stems → Krita/Breeze icon names (functional parity without bundling SVGs)
// §13.141: Avoid ngrok browser warning when connecting to arbitrary ComfyUI URLs
void setComfyUIRequestHeaders(QNetworkRequest &req);
/// Non-empty when a /history/{prompt_id} entry reports ComfyUI execution failure (status.messages execution_error).
QString comfyHistoryExecutionError(const QJsonObject &historyEntry);
/// GET api/etn/translate/{lang}/{text} (Python ComfyClient.translate). \p onDone(ok, translatedText).
void requestEtnPromptTranslation(QNetworkAccessManager *nam,
                                 const QString &baseUrlTrimmed,
                                 const QString &langCode,
                                 const QString &text,
                                 QObject *context,
                                 std::function<void(bool ok, const QString &translated)> onDone);
// §13.113: ComfyClient uploads LoRAs via HTTP PUT api/etn/upload/loras/<file_id> (streaming body). Returns nullptr if the file cannot be opened.
QNetworkReply *tryUploadLoraFileViaEtnApi(QNetworkAccessManager *nam,
                                          const QString &baseUrlTrimmed,
                                          const QString &localFilePath,
                                          QObject *parentForReply);

// §13.142: When server error indicates LCM unsupported, return the spec message; otherwise return error as-is
QString formatServerErrorMessage(const QString &serverError);

// ComfyUI /prompt 400 responses return either {"error": "<string>"} or the more
// common structured form {"error": {"type":..., "message":..., "details":...},
// "node_errors": {"<id>": {"errors":[{...}], "class_type": "..."}}}. The plain
// `obj["error"].toString()` we were using returned an empty string for the
// structured form, which is what caused the "Generate did nothing — STATUS[ERROR] """
// silent-failure on Android. Centralise the extraction so every reply handler
// gives the user the actual server-side reason for the rejection.
QString extractServerErrorFromBody(const QByteArray &responseBody);

// §8.5/13.35: Wildcards {option1|option2|option3} — pick one option per occurrence using deterministic RNG(seed)
QString evalWildcards(QString text, quint32 seed);

/// P4.2 / Python settings.prompt_translation when translation_enabled is true in settings.json.
QString activePromptTranslationLanguage(const QJsonObject &settingsRoot = QJsonObject());

/// Python text.merge_prompt: wrap non-empty prompt with lang:xx … lang:en for ETN_Translate nodes.
QString wrapPromptWithTranslationLanguage(const QString &prompt, const QString &languageCode);

/// Python `text.replace_layers` / `prepare_prompts` layer_replace — arch-specific placeholder text.
QString layerPlaceholderReplacementForArch(ComfyResources::Arch arch);
// §13.35: Layer placeholders <layer:name> → replacement template (default "Picture {}").
QStringList extractLayerPlaceholders(QString &prompt,
                                     const QString &replacementTemplate = QStringLiteral("Picture {}"));

// §13.103: Custom workflow validation — at most one node of type ETN_KritaStyleAndPrompt. Returns (true, "") if valid, else (false, localized error message).
QPair<bool, QString> validateCustomWorkflowStyleAndPromptNodes(const QJsonObject &workflow);

/// M8: API workflow shape + optional object_info class_type presence check.
QPair<bool, QString> validateCustomWorkflowApiGraph(const QJsonObject &workflow,
                                                    const QJsonObject &objectInfoRoot = QJsonObject());

/// True when workflow contains an output node (SaveImage / ETN_ReturnImage / ETN_SendImage).
bool validateCustomWorkflowHasOutputNode(const QJsonObject &workflow);

bool settingsColorMatchEnabled();
double settingsNsfwFilterSensitivity();

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
        KritaStylePicker,
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
/// VAELoader vae_name options from GET /object_info (empty if not connected).
QStringList vaeNamesFromObjectInfo(const QJsonObject &objectInfoRoot);
/// Combo options for a loader node input (e.g. ControlNetLoader `control_net_name`).
QStringList loaderComboNamesFromObjectInfo(const QJsonObject &objectInfoRoot, const QString &nodeClass,
                                           const QString &inputName);
/// Upstream `comfy_client._find_model`: first server filename matching all `*` segments in a pattern.
QString findModelOnServer(const QStringList &available, const QStringList &searchPatterns);
/// Resolved inpaint ControlNet on server, or empty if unavailable (upstream `models.find_control(inpaint)`).
QString findControlNetInpaintOnServer(const QJsonObject &objectInfoRoot, ComfyResources::Arch arch);
struct ResolvedFooocusInpaint {
    QString head;
    QString patch;
    bool isValid() const { return !head.isEmpty() && !patch.isEmpty(); }
};
/// Upstream `models.fooocus_inpaint` from INPAINT_LoadFooocusInpaint model lists.
ResolvedFooocusInpaint resolveFooocusInpaintOnServer(const QJsonObject &objectInfoRoot);
struct ResolvedInpaintServerModels {
    QString controlNetInpaintFile;
    QString fooocusInpaintHead;
    QString fooocusInpaintPatch;
};
/// Resolve inpaint ControlNet + Fooocus filenames from object_info when useInpaintModel is set.
ResolvedInpaintServerModels resolveInpaintServerModels(const QJsonObject &objectInfoRoot,
                                                       ComfyResources::Arch arch,
                                                       bool useInpaintModel);
QStringList specSection58NodesPresentInObjectInfo(const QJsonObject &objectInfoRoot);

// §13.154: True when selection covers (0,0) to (width, height) and all pixels are fully selected (0xff)
bool isSelectionEntireDocument(KisImageSP image, KisViewManager *viewManager);

// §13.214: SD checkpoint min pixel extent (resolution.prepare_diffusion_input parity)
struct DiffusionPreparedExtent {
    QSize initial;
    double scaleFromInput = 1.0;
};
DiffusionPreparedExtent prepareDiffusionInputExtent(const QSize &inputExtent, ComfyResources::Arch arch);

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

/// Python DocumentModel.can_edit — linked instruction-edit style configured.
bool hasLinkedEditStyle(const QString &linkedEditStyleId);

/// Python DocumentModel.can_toggle_edit — dropdown visible on full canvas (non-edit arch + linked edit).
bool canToggleEditMode(const QString &ckptName, const QString &linkedEditStyleId);

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

/// Python workflow.build_instructions — prepend mode-specific edit instructions when arch supports edit.
QString buildInpaintPromptInstructions(const QString &mode, const QString &archKey);
QString prependInpaintPromptInstructions(const QString &prompt, const QString &mode, const QString &archKey);

/// Default fill kind for an explicit inpaint mode (for syncing fill combo UI).
QString defaultFillKindForInpaintMode(const QString &mode);

// §13.43: calc_selection_pre_process — grow/feather (denoise mask) + blend (compositing)
struct SelectionPreProcess {
    int grow = 0;
    int feather = 0;
    int blend = 0;
};

/// Upstream `document.py::SelectionModifiers`.
struct SelectionModifiers {
    double featherRel = 0.0;
    int featherMinPx = 0;
    double padRel = 0.0;
    int padOffsetPx = 0;
    int sizeMinPx = 256;
    int multiple = 16;
    bool square = false;
    bool invert = false;
};

/// Upstream `model.py::get_selection_modifiers` — `modifierMode` is combo value, not auto-detected mode.
SelectionModifiers getSelectionModifiers(const QString &archKey, const QString &modifierMode, double strength0to1,
                                         int minSize = 256);

/// Upstream `model.py::resolve_inpaint_mode` — workflow/detect_inpaint only.
QString resolveInpaintMode(const QString &modifierMode, int extentWidth, int extentHeight,
                           const QRect &originalSelectionBounds);

struct MaskFromSelectionResult {
    QImage maskGray;
    QRect originalBounds;
    QRect paddedBounds;
    bool valid = false;
};

/// Fraction of grayscale mask pixels brighter than threshold (default 20).
double maskNonWhiteFraction(const QImage &maskGray);

/// Which decode path `chooseSelectionMaskRead` picked (Android oval regression).
enum class SelectionMaskReadSource {
    None,
    ConvertToQImage,
    ReadBytes,
    ReadBytesOverSolidConvert,
    ConvertToQImageFallback,
    ReadBytesOnly,
};

struct SelectionMaskReadResult {
    QImage mask;
    SelectionMaskReadSource source = SelectionMaskReadSource::None;
};

/// Prefer `readBytes` when `convertToQImage` returns a solid selectedExactRect (Android oval bug).
SelectionMaskReadResult chooseSelectionMaskRead(const QImage &fromConvert, const QImage &fromBytes);

/// Embed selection-sized mask into padded-bounds canvas (black outside selection rect).
/// Used by createMaskFromSelection and regression tests (Android convertToQImage fix).
QImage assembleSelectionMaskInPaddedBounds(const QImage &selectionMaskGray,
                                           const QRect &selectionBoundsInDoc,
                                           const QRect &paddedBoundsInDoc);

/// Upstream `document.py::create_mask_from_selection` — duplicate, optional invert, pad, export padded mask.
MaskFromSelectionResult createMaskFromSelection(KisImageSP image, KisViewManager *viewManager,
                                                const SelectionModifiers &mod);

/// Upstream `resolution.py::compute_bounds` — `refineWorkflowKind` when strength < 1 or editing.
QRect computeInpaintDiffusionBounds(int extentWidth, int extentHeight, const QRect &maskPaddedBounds,
                                    bool refineWorkflowKind);
/// Upstream `Bounds.pad` — pad/clamp bounds for region inpaint masks (`get_region_inpaint_mask`).
QRect padMaskBounds(const QRect &bounds, const QRect &docExtent, int padding, int minSize, int multiple, bool square);

SelectionPreProcess calcSelectionPreProcess(int extentWidth, int extentHeight, int areaWidth, int areaHeight,
                                            double strength0to1, int selectionFeatherPercent,
                                            double selectionMinTransition, int selectionGrowOffset,
                                            int selectionBlendPixels, bool invertSelection);
/// Upstream `calc_selection_pre_process` using `selection_bounds` + `SelectionModifiers`.
SelectionPreProcess calcSelectionPreProcessFromModifiers(const QRect &selectionBounds, int extentWidth,
                                                         int extentHeight, const SelectionModifiers &mods);
int getSelectionBlendPixels();
QImage rasterExpandMask(const QImage &maskGray, int grow, int feather);
QImage denoiseToCompositingMask(const QImage &maskGray, int grow, int feather, int blend);

// §13.43: Read selection modifier settings from settings.json (selection_feather, selection_min_transition, selection_grow_offset). Defaults 50, 0, 0.
void getSelectionModifierSettings(int *selectionFeatherPercent, double *selectionMinTransition, int *selectionGrowOffset);
// §13.43 / §3.5: selection_padding (0–25%, default 6) — expands mask bounds around selection
int getSelectionPaddingPercent();
// Upstream create_mask_from_selection + Bounds.pad (feather/min-transition/grow-offset/padding, min 256, multiple 16)
QRect computePaddedSelectionBounds(const QRect &originalSelection, const QRect &docBounds, double strength0to1,
                                   int selectionFeatherPercent, double selectionMinTransition,
                                   int selectionGrowOffset, int selectionPaddingPercent,
                                   const QString &inpaintMode = QString(), bool square = false);
/// Decode context combo data: preset enum string or mask layer UUID → (`context`, `context_layer_id`).
void decodeInpaintContextComboData(const QVariant &comboData, QString *contextOut, QString *contextLayerIdOut);

/// Port `CustomInpaint.get_context` — custom mode only. Empty optional → keep `compute_bounds` result.
std::optional<QRect> customInpaintGetContext(KisImageSP image, const QString &contextKey,
                                             const QString &contextLayerId, const QRect &maskPaddedBounds);

/// Port `CustomInpaint.get_params` — custom mode fill / seamless / focus.
InpaintParams customInpaintGetParams(const QString &customFillKind, bool useInpaintModel, bool usePromptFocus,
                                     bool isEditing);

/// P2: `custom_workflow.py::get_inpaint_context` — read `context` from ETN_KritaSelection inputs.
QString getInpaintContextFromSelectionNode(const QJsonObject &selectionNodeInputs);
/// P2: `get_selection_modifiers(ctx, InpaintMode.fill, strength)` for custom web workflow.
SelectionModifiers getSelectionModifiersForContext(const QString &contextKey, double strength0to1, int minSize = 256);
QString findFirstWorkflowNodeIdByClassType(const QJsonObject &workflow, const QString &classType);
bool workflowContainsKritaInjectionNodes(const QJsonObject &workflow);
/// True when workflow has any ETN node expanded client-side before `/prompt` (`expand_custom` parity).
bool workflowNeedsCustomKritaExpansion(const QJsonObject &workflow);
/// `model.py::_generate_custom` — `exclude_internal = not is_live` where is_live = CustomGenerationMode.live.
bool customWorkflowCaptureExcludesInternal(bool customGenerationModeLive);
/// ETN_KritaStyle / ETN_KritaStyleAndPrompt `sampler_preset` (auto|regular|live).
bool customWorkflowNodeUsesLiveSampling(const QString &nodeSamplerPreset, bool customGenerationModeLive);

struct CustomWorkflowStyleBundle {
    QString checkpoint;
    QString positivePrompt;
    QString negativePrompt;
    QString sampler;
    QString scheduler;
    int steps = 20;
    double cfg = 7.0;
    QJsonArray styleLoras;
    bool ok = false;
    QString errorMessage;
};
/// Resolve style checkpoint, LoRAs, prompts, and sampler for custom workflow expand.
CustomWorkflowStyleBundle resolveCustomWorkflowStyleBundle(const ComfyStyleEntry *styleEntry,
                                                           const QJsonObject &settings,
                                                           const QString &dockSamplerText,
                                                           int dockSteps,
                                                           double dockCfg,
                                                           const QString &nodeSamplerPreset,
                                                           bool customGenerationModeLive);

struct ExtractedPromptLora {
    QString name;
    double strength = 1.0;
};
struct CustomWorkflowEvaluatedPrompts {
    QString positiveFinal;
    QString negativeFinal;
    QList<ExtractedPromptLora> promptLoras;
    QJsonObject metadata;
    bool ok = true;
    QString errorMessage;
};
/// `text.py::extract_loras` — strip `<lora:name:weight>` and resolve against file library.
struct ExtractLorasFromPromptResult {
    QString cleanedPrompt;
    QList<ExtractedPromptLora> loras;
    QString errorMessage;
};
ExtractLorasFromPromptResult extractLorasFromPrompt(QString prompt);
/// `workflow.py::prepare_prompts` for ETN_KritaStyleAndPrompt expand.
CustomWorkflowEvaluatedPrompts prepareCustomWorkflowStyleAndPrompts(const QString &userPositive,
                                                                      const QString &userNegative,
                                                                      const ComfyStyleEntry *styleEntry,
                                                                      qint64 seed,
                                                                      double effectiveCfg,
                                                                      const QString &translationLanguage = QString(),
                                                                      ComfyResources::Arch arch = ComfyResources::Arch::Sd15);

struct CustomWorkflowMaskPrepareResult {
    QRect captureBounds;
    QImage maskInCaptureCoords;
    bool hasSelectionMask = false;
    bool ok = true;
    QString errorMessage;
};
/// ETN_KritaSelection contexts accepted by `custom_workflow.prepare_mask`.
bool isValidCustomWorkflowSelectionContext(const QString &contextKey);
/// P2: `CustomWorkspace.prepare_mask` — bounds + mask relative to capture rect.
CustomWorkflowMaskPrepareResult prepareCustomWorkflowMask(const QJsonObject &selectionNodeInputs,
                                                          const MaskFromSelectionResult &maskResult,
                                                          const QRect &docBounds);

struct CustomWorkflowExpandState {
    QByteArray inputFingerprint;
    QJsonObject promptMetadata;
    QRect captureBounds;
    bool hasSelectionMask = false;
};
/// Resolve layer name from workflow inputs, param overrides (UUID), or first image/mask layer.
QString resolveCustomWorkflowLayerName(KisImageSP image,
                                       const QString &nodeId,
                                       const QString &paramName,
                                       const QString &nameFromWorkflow,
                                       const QMap<QString, QVariant> &paramOverrides,
                                       bool maskLayer = false);
QImage exportCustomWorkflowLayerImage(KisImageSP image,
                                      KisViewManager *viewManager,
                                      const QString &layerName,
                                      const QRect &exportBoundsInDoc,
                                      bool maskLayer);
struct CustomWorkflowKritaCapture {
    QRect captureBounds;
    QImage canvasImage;
    QImage maskImage;
    bool hasSelectionMask = false;
    bool ok = false;
    QString errorMessage;
};
/// Fingerprint for `_generate_custom` `input == previous_input` live dedup.
QByteArray computeCustomWorkflowInputFingerprint(const QJsonObject &workflow,
                                                 const CustomWorkflowKritaCapture &capture,
                                                 qint64 seed,
                                                 const QString &positivePrompt,
                                                 const QString &negativePrompt,
                                                 const QJsonArray &loraMetadata,
                                                 const QMap<QString, QVariant> &paramOverrides);
/// P2: capture canvas + mask for ETN_KritaCanvas / ETN_KritaSelection (`exclude_internal` when not live).
CustomWorkflowKritaCapture captureCustomWorkflowKritaInput(KisImageSP image,
                                                           KisViewManager *viewManager,
                                                           const QJsonObject &workflow,
                                                           double strength0to1,
                                                           bool excludeInternal,
                                                           const QList<ComfyControlLayerEntry> &rootControlLayers,
                                                           const QString &previewLayerId);

/// Upload PNG to ComfyUI `/upload/image`; returns server `name` or empty on failure.
QString uploadImageToComfySync(::QNetworkAccessManager *nam,
                               const QString &serverBaseUrl,
                               const QImage &image,
                               const QString &filenameHint,
                               QString *errorOut = nullptr);

/// Upstream `layers.masks` filter — transparency + selection masks only.
bool isInpaintContextMaskNode(KisNodeSP node);

KisNodeSP findDocumentNodeByUuid(KisImageSP image, const QUuid &layerId);
QImage cropImageToDocumentRect(const QImage &image, const QRect &cropInDocCoords, const QRect &docBounds);
void blitImageInto(QImage &dest, const QImage &src, QPoint topLeft);
/// Port `model.py::_save_job_result` — composite job result onto excluded document canvas before export.
QImage compositeJobResultOnDocument(KisImageSP image,
                                    const QList<KisNodeSP> &excludeNodes,
                                    const QImage &result,
                                    const QRect &placementBounds,
                                    bool hasMask);
// §13.188: Pre-fill masked pixels before upload (neutral, blur, …); noop for none/inpaint
// §13.102: When square is true, return a square rect (max of w,h) centered and clamped to image bounds
QRect makeRectSquare(const QRect &rect, int extentWidth, int extentHeight);

/// Place padded grayscale mask into full-document coordinates for context cropping.
QImage embedGrayMaskInDocument(const QImage &maskGray, const QRect &maskDocBounds, const QRect &docBounds);

struct DocumentImageResult {
    QImage image;
    QString errorMessage;
    explicit operator bool() const { return errorMessage.isEmpty() && !image.isNull(); }
};

/// Port `KritaDocument.get_image` — temporarily hides excludeNodes, refreshes projection, exports bounds.
DocumentImageResult getDocumentImage(KisImageSP image, const QRect &boundsIn, const QList<KisNodeSP> &excludeNodes);

/// `LiveWorkspace.set_result` — capture context, optional DiagCross multiply overlay on
/// static areas, then SourceOver the new server patch at `resultPlacementInDoc`.
/// Same compositing as compositeLiveResultPreview but uses an already-captured context image
/// (live docker preview must not re-capture the document — that toggles layer visibility).
QImage compositeLiveResultPreviewFromContext(const QImage &contextCapture,
                                             const QRect &contextBoundsInDoc,
                                             const QRect &resultPlacementInDoc,
                                             const QImage &result,
                                             bool drawGeneratingOverlay = true,
                                             const QImage &selectionMaskGray = QImage());

/// Port `model._get_current_image` exclude list — root control layers (not isPartOfImage) + preview layer.
QList<KisNodeSP> collectInpaintExcludeNodes(KisImageSP image,
                                          bool excludeInternal,
                                          const QList<ComfyControlLayerEntry> &rootControlLayers,
                                          const QString &previewLayerId);

QImage getCanvasAsQImage(KisImageSP image);
/// ComfyUI mask upload: grayscale → ARGB PNG (alpha = inverted luminance).
QImage maskPngForComfyUpload(const QImage &maskGray);
/// Export a paint layer by name (full image bounds); empty if not found.
QImage getLayerProjectionAsQImage(KisImageSP image, const QString &layerName);
/// Export a paint/mask layer by Krita layer UUID (full image bounds); empty if not found.
QImage getLayerProjectionByUuid(KisImageSP image, const QString &layerId);
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
/// Stems of `*.csv` tag files in plugin install `tags/` and `tagsStorageDir()` (Python `_tag_files()`).
QStringList discoverTagFileStems();
// §13.48: Merge tags from tag_directory (or default tags dir) + stems in tag_files (default Danbooru, e621)
QStringList tagKeywordsForAutocomplete(const QJsonObject &settings = QJsonObject());

// §13.4: Prompt import from image file — read A1111 "parameters" from PNG (tEXt chunk); returns (positive, negative) or (empty, empty)
QPair<QString, QString> readPromptFromImageFile(const QString &filePath);

// Edit mode + per-style linked_edit_style — KSampler/checkpoint/style/negative from linked style entry
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
LinkedEditStyleOverride linkedEditStyleOverride(bool editModeEnabled, const QString &linkedStyleId, const QString &dockCkpt,
                                                int dockSteps, double dockCfg, double dockDenoise, const QString &dockSampler,
                                                const QString &dockScheduler);

// §13.125: Merge style template with user/region instruction; {prompt} placeholder per spec style JSON
QString mergeStylePromptWithInstruction(const QString &styleTemplate, const QString &userInstruction);

}

#endif
