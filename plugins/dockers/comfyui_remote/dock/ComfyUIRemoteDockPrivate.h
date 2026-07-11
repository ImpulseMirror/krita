/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_REMOTE_DOCK_PRIVATE_H_
#define COMFYUI_REMOTE_DOCK_PRIVATE_H_

#include "ComfyUIRemoteDock.h"
#include "ComfyControlLayer.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyLiveScheduler.h"
#include "ComfyRegionProcess.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyWorkspaceSelectButton.h"
#include "ComfyHistoryListWidget.h"
#include "ComfyUIUtils.h"

#include <QPointer>
#include <QScopedPointer>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QWebSocket;
class ComfySpinBox;
#include <QTemporaryFile>
#include <QUrl>
#include <QRect>
#include <QSize>
#include <QDateTime>
#include <QJsonObject>
#include <QVariant>
#include <QDialog>
#include <QFormLayout>
#include <QToolButton>
#include <QAbstractSlider>
#include <QGroupBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QStackedWidget>
#include <QWidget>

class QHBoxLayout;
class ComfyUIIntervalSlider;
class ComfyStyleLoraListWidget;
#include <QRect>
#include <QImage>
#include <QVector>
#include <limits>

class QCompleter;
class QStringListModel;

#include <kis_signal_auto_connection.h>
#include <KisViewManager.h>
#include <kis_canvas2.h>
#include <kis_image.h>
#include <kis_node.h>


/// Completed job metadata (alias `ComfyUIRemoteDock::Private::HistoryEntry` for port parity).
struct ComfyHistoryEntry {
    QString jobId;
    QString prompt, negative, checkpoint;
    QString styleName;
    int width = 512, height = 512;
    int steps = 20;
    double cfg = 8.0;
    int strength = 100;
    QString samplerName;
    qint64 seed = 0;
    QString resultImagePath;
    QStringList resultImagePaths;
    QMap<int, bool> imageInUse;
    QStringList regionLayerNames;
    int documentSlot = -1;
    QList<int> documentBlobEndOffsets;
    int animationSubmitTime = -1;
    bool hasMask = false;
    QString inpaintMode;
    QRect contextBounds;
    QRect targetBounds;
    QJsonObject customWorkflowMetadata;
    QDateTime finishedAt;
};

struct GenerateUi
{
    QComboBox *comboCheckpoint = nullptr;
    QPushButton *btnRefreshCheckpoints = nullptr;
    QComboBox *comboPreset = nullptr;
    QComboBox *comboQuality = nullptr;
    QComboBox *comboSizePreset = nullptr;
    QPushButton *btnSaveAsPreset = nullptr;
    QPushButton *btnDeletePreset = nullptr;
    QPlainTextEdit *editPrompt = nullptr;
    QPlainTextEdit *editNegative = nullptr;
    QWidget *rootPromptColumnWidget = nullptr;
    QWidget *promptResizeHandle = nullptr;   // §13.54
    QWidget *negativeResizeHandle = nullptr; // §13.54
    QWidget *negativePromptBlock = nullptr;  // §4.7: label row + negative editor (hide when "Show negative" off)
    QWidget *stepsParametersWidget = nullptr;  // §4.7: Steps/CFG/Sampler row (hide when "Show steps" off)
    QWidget *seedRowWidget = nullptr;
    QWidget *sizeRowWidget = nullptr;
    QWidget *presetRowWidget = nullptr;
    QLabel *labelNegativePromptAlert = nullptr;  // §13.143: alert icon when style does not use negative prompt
    QSpinBox *spinWidth = nullptr;
    QSpinBox *spinHeight = nullptr;
    QSpinBox *spinSteps = nullptr;
    QDoubleSpinBox *spinCfg = nullptr;
    QSpinBox *spinStrength = nullptr;  // §5.4: 1–100%, denoise = strength/100
    QCheckBox *checkRegionOnly = nullptr;   // §5.4: limit generation to active region only
    QCheckBox *checkEditMode = nullptr;    // §5.4: instruction-based Edit vs normal Generate
    QWidget *layerCountRow = nullptr;     // §5.4: visible only when style arch is Qwen Layered
    QSpinBox *spinLayerCount = nullptr;   // §5.4: 1–8 for Qwen Layered
    QComboBox *comboSampler = nullptr;
    QPushButton *btnRefreshSamplers = nullptr;
    QSpinBox *spinSeed = nullptr;
    QCheckBox *checkFixedSeed = nullptr;
    QPushButton *btnRandomSeed = nullptr;
    QWidget *seedControlRow = nullptr;
    QLabel *labelQueueDocumentCount = nullptr;
    QLabel *labelQueueTotalCount = nullptr;
    QComboBox *comboQueueMode = nullptr;
    QSpinBox *spinBatchCount = nullptr;
    QAbstractSlider *sliderBatchCount = nullptr;
    QLabel *labelBatchCount = nullptr;
    QPushButton *btnGenerate = nullptr;
    QToolButton *btnCancelActive = nullptr;
    QToolButton *btnCancelQueued = nullptr;
    QToolButton *btnCancelAll = nullptr;
    QToolButton *btnCancelQueue = nullptr; // §compat: points at btnCancelAll
    QToolButton *btnAddControlIcon = nullptr;
    QToolButton *btnAddRegionIcon = nullptr;
    QWidget *generateActionRowWidget = nullptr;
    QMenu *menuGenerate = nullptr;
    QMenu *menuRefine = nullptr;
    QMenu *menuRefineSelection = nullptr;
    QMenu *menuGenerateRegion = nullptr;
    QMenu *menuRefineRegion = nullptr;
    QMenu *menuEdit = nullptr;
    QGroupBox *controlLayersGroupBox = nullptr;
    class ComfyControlLayerListWidget *rootControlLayerList = nullptr;
    QGroupBox *regionControlLayersGroupBox = nullptr;
    QLabel *labelRegionControlLayers = nullptr;
    class ComfyControlLayerListWidget *regionControlLayerList = nullptr;
    QComboBox *regionHeaderCombo = nullptr;
    QLabel *regionHeaderLabel = nullptr;  // description or icon, visibility by mode
    class ComfyRegionPromptWidget *regionPromptWidget = nullptr;
    class ComfyQueueButton *btnQueuePopup = nullptr;
    QWidget *queueButtonRowWidget = nullptr;
    QWidget *queueBatchOptionsRow = nullptr;
    QLabel *queueBatchLabel = nullptr;
    QWidget *queueEnqueueModeRow = nullptr;
    QLabel *queueEnqueueLabel = nullptr;
    QWidget *queueResolutionRow = nullptr; // §13.213: Resolution slider row (visible when perf preset = custom)
    QAbstractSlider *sliderResolutionMultiplier = nullptr;
    QLabel *labelResolutionMultiplier = nullptr;
    double resolutionMultiplier = 1.0;
    QGroupBox *genGroupBox = nullptr;
    QGroupBox *regionsGroupBox = nullptr;
    QGroupBox *controlPreviewGroupBox = nullptr;
    QComboBox *comboControlPreviewMode = nullptr;
    ComfyUIIntervalSlider *controlPreviewRangeSlider = nullptr;
    QPushButton *btnControlPreviewRun = nullptr;
    QSpinBox *spinPoseGuidePeopleCount = nullptr;
    QPushButton *btnAddPoseGuide = nullptr;
    QLabel *labelControlPreviewImage = nullptr;
    QWidget *genContentContainer = nullptr;   // Generate view content (prompt, params, buttons)
    QPushButton *btnGenerateAnimation = nullptr;

};

struct GenerateRuntime
{
    QString ksamplerScheduler = QStringLiteral("normal");
    QList<ComfyControlLayerEntry> generateControlLayersActive;
    QString generateRefineUploadedImageName;
    ComfyPrepareGenerateWorkflow::Result generateRefinePrepared;
    bool generateRefineAfterRegions = false;
    ComfyWorkflowEngine::RefineParams generateStashedRefineParams;
    QTimer *controlLayerJobPollTimer = nullptr;
    QString controlLayerJobPromptId;
    int controlLayerJobPollCount = 0;
    QString controlLayerJobMode;
    QString controlLayerJobResultLayerName;
    QString controlLayerJobAnchorLayerName;
    bool controlLayerJobForRegion = false;
    int controlLayerJobRegionRow = -1;
    int controlLayerJobEntryIndex = -1;
    bool controlLayerJobHandsCompositeBack = false;
    QRect controlLayerJobCompositeLocalRect;
    QSize controlLayerJobCompositeFullSize;
    QJsonValue controlLayerJobOpenPoseJson;
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    class QWebSocket *controlLayerJobWebSocket = nullptr;
#endif
    bool generateAwaitingLoraUploads = false;
    int generateLoraUploadIndex = 0;
    QStringList generateLoraUploadPaths;
    bool generateAwaitingControlUploads = false;
    int generateControlUploadIndex = 0;
    QStringList generateControlUploadedNames;
    QString generateStashedCustomJson;
    int generateStashedBatch = 1;
    double generateStashedMul = 1.0;
    QJsonObject generatePendingBaseWorkflow;
    ComfyResources::Arch generatePendingArch = ComfyResources::Arch::Sd15;
    int generatePendingLayerCount = 1;
    int generatePendingRefineWidth = 0;
    int generatePendingRefineHeight = 0;
    int generateOneShotQueueMode = -1;
    QList<ComfyWorkflowEngine::RegionalPromptInput> generateRegionalInputs;
    QList<ComfyRegionProcess::ProcessedRegionEntry> generateProcessedRegions;
    bool generateAwaitingRegionMaskUploads = false;
    int generateRegionMaskUploadIndex = 0;
    QString generateStyleVae;
    int generateStyleClipSkip = 0;
    ComfyResources::Arch generateStyleArch = ComfyResources::Arch::Sd15;
    QTimer *controlPreviewPollTimer = nullptr;
    QString controlPreviewPromptId;
    int controlPreviewPollCount = 0;
    bool controlPreviewHandsCompositeBack = false;
    QRect controlPreviewCompositeLocalRect;
    QSize controlPreviewCompositeFullSize;

};

struct LiveUi
{
    QCheckBox *checkUseReferenceImage = nullptr;
    QCheckBox *checkLiveMode = nullptr;
    QWidget *liveSpinner = nullptr;  // §13.105: LiveSpinnerWidget for Live view progress
    QCheckBox *checkLiveRecord = nullptr;  // §13.45: when checked, save each live result to .live-frames/frame-N.webp
    // FAITHFUL_PORT: ai_diffusion/ui/live.py LiveWidget toolbar + params + prompt rows
    QToolButton *btnLivePlay = nullptr;
    QToolButton *btnLiveRecord = nullptr;
    QToolButton *btnLiveApply = nullptr;
    QToolButton *btnLiveApplyLayer = nullptr;
    QToolButton *btnLiveEditToggle = nullptr;
    QToolButton *btnLiveRandomSeed = nullptr;
    QWidget *liveTopToolbarWidget = nullptr;
    QWidget *liveParamsRowWidget = nullptr;
    QWidget *livePromptRowWidget = nullptr;
    QWidget *livePromptHostWidget = nullptr;
    QWidget *livePromptButtonsWidget = nullptr;
    /// FAITHFUL_PORT: ai_diffusion/ui/live.py LivePreviewArea — bottom docker preview, not canvas layer.
    QWidget *livePreviewGroupBox = nullptr;
    QWidget *livePreviewRowWidget = nullptr;
    QLabel *livePreviewArea = nullptr;
};

struct LiveRuntime
{
    QTimer *liveTimer = nullptr;
    bool liveAwaitingLoraUploads = false;
    int liveLoraUploadIndex = 0;
    QStringList liveLoraUploadPaths;
    QString liveUploadedImageName;
    QString liveUploadedMaskName;
    ComfyPrepareLiveWorkflow::Result livePrepared;
    bool livePipelineBusy = false;
    bool liveApplyInProgress = false;
    ComfyLiveRunnerInternal::LiveSchedulerState liveScheduler;
    bool liveAwaitingControlUploads = false;
    int liveControlUploadIndex = 0;
    QStringList liveControlUploadedNames;
    QList<ComfyControlLayerEntry> liveControlLayersActive;
    QList<ComfyControlLayerEntry> livePromptReferenceLayers;
    QList<ComfyWorkflowEngine::RegionalPromptInput> liveRegionalInputs;
    bool liveAwaitingRegionMaskUploads = false;
    int liveRegionMaskUploadIndex = 0;
    QString livePreparedPositive;
    QString livePreparedNegative;
    quint32 livePreparedSeed = 0;
    QString livePromptId;
    int liveFrameIndex = 0;                // next frame index when recording
    int livePollCount = 0;
    static const int liveMaxPollCount = 120;
    QTimer *livePollTimer = nullptr;
    QString lastLiveResultImagePath;
    QString lastLiveRawResultImagePath;
    QString lastLiveResultCompositionPath;
    QString liveSamplerLoraBlockMessage;
    /// LIVE_DIAG: carried from workflow build through poll for verdict logging.
    QString liveDiagLatentPath;
    QString liveDiagArchKey;
    double liveDiagDenoise = -1.0;
};

struct InpaintUi
{
    QWidget *seamlessRowWidget = nullptr;
    QWidget *focusRowWidget = nullptr;
    QPushButton *btnInpaint = nullptr;
    QAbstractSlider *sliderStrength = nullptr;
    QWidget *strengthSliderWidget = nullptr;
    QWidget *strengthRowWidget = nullptr;
    QWidget *customInpaintRowWidget = nullptr;
    QToolButton *btnInpaintMode = nullptr;
    QToolButton *btnRegionMask = nullptr;
    QMenu *menuInpaint = nullptr;
    QComboBox *comboInpaintMode = nullptr;  // §13.206 / P4.1: all InpaintMode values (automatic … custom)
    QComboBox *comboFillMode = nullptr;    // §13.188: None | Neutral | Blur | Border | Inpaint (five options, replace/green internal only)
    QComboBox *comboInpaintContext = nullptr; // §13.169 / §13.194: selection/crop context (ETN_KritaSelection parity)
    class ComfySwitchWidget *checkInpaintUseModel = nullptr;      // §13.107: Seamless (use_inpaint)
    class ComfySwitchWidget *checkInpaintUsePromptFocus = nullptr; // §13.107: Focus (use_prompt_focus)
    class ComfySwitchWidget *editModeSwitch = nullptr;           // Python CustomInpaint edit_mode_switch
};

struct InpaintRuntime
{
    bool inpaintPersistUseModel = true;     // §13.194: CustomInpaint use_inpaint (inpaint annotation)
    bool inpaintPersistUsePromptFocus = false; // §13.194: use_prompt_focus
    QString inpaintContextKey = QStringLiteral("automatic");
    QString inpaintContextLayerId;
    QJsonObject inpaintPendingWorkflow;
    QList<ComfyControlLayerEntry> inpaintControlLayersActive;
    bool inpaintAwaitingLoraUploads = false;
    int inpaintLoraUploadIndex = 0;
    QStringList inpaintLoraUploadPaths;
    int inpaintControlUploadIndex = 0;
    QStringList inpaintControlUploadedNames;
    ComfyResources::Arch inpaintPendingArch = ComfyResources::Arch::Sd15;
    QString inpaintUploadedImageName;
    QString inpaintUploadedImageSubfolder;
    QString inpaintUploadedMaskName;
    QString inpaintUploadedMaskSubfolder;
    QString inpaintPromptId;
    QImage inpaintCurrentImage;
    QImage inpaintFullCanvasImage;
    QImage inpaintCompositingMaskCropped;
    QRect inpaintContextBounds;
    QRect inpaintTargetBounds;
    QImage inpaintNativeContextImage;
    QImage inpaintNativeCompositingMask;
    QSize inpaintNativeContextSize;
    QSize inpaintDiffusionExtent;
    bool inpaintUseRefineRegionWorkflow = false;
    bool inpaintServerMaskedOutput = false;
    /// INPAINT_DIAG: carried from workflow build through poll for verdict logging.
    QString inpaintDiagLatentPath;
    QString inpaintDiagArchKey;
    double inpaintDiagDenoise = -1.0;
    int inpaintPreprocessGrow = 0;
    int inpaintPreprocessFeather = 0;
    int inpaintPreprocessBlend = 0;
    bool inpaintFromRegionLayer = false;
    int inpaintPollCount = 0;
    static const int inpaintMaxPollCount = 300;
    QTimer *inpaintPollTimer = nullptr;
    ComfyHistoryEntry inpaintPendingEntry;
    ComfyPrepareGenerateWorkflow::Result inpaintPrepared;
};

struct UpscaleUi
{
    QComboBox *comboUpscaleModel = nullptr;
    QPushButton *btnUpscale = nullptr;
    QAbstractSlider *sliderUpscaleFactor = nullptr;
    QDoubleSpinBox *spinUpscaleFactor = nullptr;
    QLabel *labelUpscaleTargetSize = nullptr;
    QWidget *upscaleFactorRow = nullptr;
    QComboBox *comboTileOverlapMode = nullptr;
    QSpinBox *spinTileOverlap = nullptr;
    QWidget *upscaleTileOverlapRow = nullptr;
    QWidget *upscaleRefineBlock = nullptr;
    QCheckBox *checkUpscaleRefine = nullptr;
    QWidget *upscaleRefineDetails = nullptr;
    QComboBox *comboUpscaleRefinementModel = nullptr;
    QToolButton *btnUpscaleRefineSettings = nullptr;
    QAbstractSlider *sliderUpscaleRefineStrength = nullptr;
    ComfySpinBox *spinUpscaleRefineStrength = nullptr;
    QAbstractSlider *sliderUpscaleRefineGuidance = nullptr;
    ComfySpinBox *spinUpscaleRefineGuidance = nullptr;
    class ComfySwitchWidget *checkUpscaleUsePrompt = nullptr;
    QLabel *labelUpscaleUsePromptText = nullptr;
    QWidget *upscaleActionRowWidget = nullptr;
};

struct UpscaleRuntime
{
    bool upscaleAwaitingControlUploads = false;
    int upscaleControlUploadIndex = 0;
    QStringList upscaleControlUploadedNames;
    QList<ComfyControlLayerEntry> upscaleControlLayersActive;
    ComfyWorkflowEngine::UpscaleTiledParams upscaleStashedTiledParams;
    int upscaleStashedW2 = 0;
    int upscaleStashedH2 = 0;
    int upscaleStashedCanvasW = 0;
    ComfyResources::Arch upscaleStashedArch = ComfyResources::Arch::Sd15;
    QList<ComfyWorkflowEngine::RegionalPromptInput> upscaleRegionalInputs;
    QList<ComfyRegionProcess::ProcessedRegionEntry> upscaleProcessedRegions;
    bool upscaleAwaitingRegionMaskUploads = false;
    int upscaleRegionMaskUploadIndex = 0;
    bool upscalePendingIsTiled = false;
    bool upscalePendingWantRefine = false;
    double upscaleFactor = 2.0;
    QString upscalerModel;
    int tileOverlapMode = 0;  // §13.147: 0 = auto (overlap -1), 1 = custom (use tile_overlap px)
    int tileOverlap = 32;
    QString upscaleUploadedImageName;
    QString upscalePromptId;
    int upscalePollCount = 0;
    bool upscaleLastSubmitUsedRefine = false;
    qint64 upscaleSeed = 0;
    int upscaleResultW = 0;
    int upscaleResultH = 0;
    static const int upscaleMaxPollCount = 300;
    QTimer *upscalePollTimer = nullptr;
};

struct HistoryState
{
    ComfyHistoryListWidget *listHistory = nullptr;
    QPushButton *btnHistoryReRun = nullptr;
    QPushButton *btnHistoryApply = nullptr;
    QWidget *historyButtonsRowWidget = nullptr;
    QString previewHistoryJobId;
    int previewHistoryImageIndex = -1;
    bool historyPreviewUpdateBlocked = false;
    QHash<QString, QImage> historyPreviewImageCache;
    QList<ComfyHistoryEntry> historyEntries;
    QMap<QString, ComfyHistoryEntry> pendingHistoryByPromptId;
    static const int maxHistoryEntries = 20;
    QGroupBox *histGroupBox = nullptr;
};


struct ComfyUIRemoteDock::Private
{
    GenerateUi generate;
    GenerateRuntime generateRt;
    LiveUi live;
    LiveRuntime liveRt;
    InpaintUi inpaint;
    InpaintRuntime inpaintRt;
    UpscaleUi upscale;
    UpscaleRuntime upscaleRt;
    HistoryState history;

    using HistoryEntry = ComfyHistoryEntry;
    using RegionEntry = ComfyRegionEntry;

    QPointer<KisViewManager> viewManager;
    QPointer<KisCanvas2> canvas;

    QLineEdit *editServerUrl = nullptr;
    // FAITHFUL_PORT: wrapper widgets for rows that are hidden by default to match
    // upstream krita-ai-diffusion's compact docker layout (advanced controls live
    // in the Settings dialog or per-style preset, not on the main docker).
    // Settings dialog Styles tab — must outlive slotConfigureHelp() because
    // the dialog (and its long-lived signal/slot lambdas) keep references to
    // these. Previously stack-locals → dangling-reference SIGSEGV on Android
    // when the user clicked the Styles nav tab after the function returned.
    bool stylesTabSyncing = false;
    QString stylesTabPresetNameBaselineMember;
    bool stylesTabPersistingAdvanced = false;
    bool stylesTabPersistingLoras = false;
    bool stylesTabPendingNewPreset = false;
    // FAITHFUL_PORT: wrap the Seamless/Focus/Frames rows so their orphan labels
    // are hidden together with the controls in compact view.
    QWidget *animFramesRowWidget = nullptr;
    // (Settings-dialog Styles tab locals lifted to members above.)
    QProgressBar *progressBar = nullptr;
    ComfyWorkspaceSelectButton *comboWorkspace = nullptr;
    QHBoxLayout *workspaceTopRowLayout = nullptr;  // workspace + live toolbar + style + settings
    int lastWorkspaceIndex = -1;  // §13.149: track for Live strength persist on leave
    QPushButton *btnTest = nullptr;
    /// §13.81: one-shot auto-connect when a server URL is saved in settings
    bool autostartServerProbeDone = false;
    bool connectionAutostartActive = false;
    int connectionAutostartRetryAttempt = 0;
    static constexpr int connectionAutostartMaxRetries = 5;
    QTimer *connectionRetryTimer = nullptr;
    uint connectionSessionId = 0;
    QSet<QString> documentDefaultsAppliedDocIds; // §13.194: apply document_defaults once per document id
    QSet<QString> warnedFutureUiJsonVersionDocIds; // §13.199: one user-visible warning per document_id
    QSet<QString> warnedUiJsonParseFailDocIds;     // §13.140: one parse-failure dialog per document_id
    QTimer *documentDefaultsSaveTimer = nullptr; // debounce persist (checkpoint text edits)
    QSpinBox *spinAnimationFrames = nullptr;
    QPushButton *btnImportAnimation = nullptr;  // §13.45: Import frames from .animation or .live-frames
    QWidget *batchModeRow = nullptr;       // §5.6, §5.7: Full Animation / Single Frame radio row (visible in Live and Animation)
    QRadioButton *radioSingleFrame = nullptr;
    QRadioButton *radioFullAnimation = nullptr;
    QButtonGroup *batchModeGroup = nullptr;
    /// §13.74: Target paint layer for Single Frame output (QUuid string in item data); row visible when Animation + Single Frame
    QWidget *animationTargetRow = nullptr;
    QComboBox *comboAnimationTargetLayer = nullptr;
    /// §13.74: Single Frame preview (target_image_changed parity)
    QWidget *animationPreviewRow = nullptr;
    QLabel *labelAnimationPreview = nullptr;
    // §13.31: debounced save of custom workflow text to document annotation
    QTimer *customWorkflowDocumentSaveTimer = nullptr;
    // §13.19: debounced write of ai_diffusion/ui.json (history list + preserved keys)
    QTimer *documentUiJsonSaveTimer = nullptr;
    // §13.170: transient WebSocket + timer (opaque QObject child of dock when active)
    QObject *webWorkflowSwitchSession = nullptr;
    QLabel *labelStatus = nullptr;
    QPlainTextEdit *editCustomWorkflow = nullptr;
    QVBoxLayout *graphWorkflowEditorLayout = nullptr;
    // §13.25: ETN_Parameter / ETN_KritaStyle / ETN_KritaImageLayer / ETN_KritaMaskLayer — Configure → Workflow tab (layer slots keyed by Comfy node id)
    QGroupBox *customWorkflowParamsGroup = nullptr;
    QFormLayout *customWorkflowParamsForm = nullptr;
    QTimer *customWorkflowParamsRefreshTimer = nullptr;
    QMap<QString, QVariant> customWorkflowParamOverrides;
    /// Graph workspace CustomGenerationMode.live — re-capture each tick (upstream `custom.is_live`).
    bool customGraphLiveActive = false;
    QByteArray customGraphLiveLastFingerprint;
    ComfyUIUtils::CustomWorkflowExpandState lastCustomWorkflowExpandState;
    QCheckBox *checkCustomGraphLive = nullptr;
    QTimer *customGraphLiveTimer = nullptr;
    // §13.48: Shared tag word list; separate completers for positive / negative prompts
    QStringListModel *tagKeywordModel = nullptr;
    QCompleter *promptTagCompleter = nullptr;
    QCompleter *negativePromptTagCompleter = nullptr;
    QCheckBox *checkConfirmDiscardImage = nullptr;  // §13.192: KConfig only (not on Interface tab)
    QPointer<QComboBox> settingsPromptTranslationCombo;

    // §13.44: Preview layer ID (QUuid string) from document annotation; restored on setCanvas
    QString previewLayerId;

    int activeRegionIndex = -1;

    QList<ComfyControlLayerEntry> rootControlLayers;
    /// P2.4: per-row generate_control_layer job (separate from §13.53 preview panel poll)

    /// Full-canvas img2img refine — canvas pixel size (img2img template has no EmptyLatentImage).
    /// FAITHFUL_PORT: Ctrl+click Generate → QueueMode.replace for one submit only (-1 = use combo).

    QList<RegionEntry> regionEntries;
    QList<RegionEntry> editRegionEntries;  // §13.125: edit_regions (Generate + Edit mode)
    // §13.90: PromptHeader — full (title + description), icon (icon only), none (no header)
    int promptHeaderMode = 0;  // 0=full, 1=icon, 2=none; persisted in config

    static const int builtinPresetCount = 5; // None, Portrait, Landscape, Anime, Realistic

    QNetworkAccessManager *nam = nullptr;
    QTimer *pollTimer = nullptr;
    // §13.93: document poll (selection_bounds + current_time), ~20 ms while canvas open
    QTimer *documentSyncPoller = nullptr;
    QTimer *animationPreviewDebounce = nullptr;
    bool documentPollInitialized = false;
    QRect lastPolledSelectionBounds;
    bool lastPolledHadSelection = false;
    int lastPolledCurrentTime = (std::numeric_limits<int>::min)();
    QString lastPolledActiveLayerId;
    QString clientId;             // §13.59: one UUID per session; used in prompt/queue HTTP bodies (and WebSocket URL when WS implemented)
    QString currentPromptId;      // the one we're currently polling (§9.3 Job.id)
    QStringList jobQueue;         // prompt_ids waiting, first is running (§9.3 JobQueue)
    int pollCount = 0;
    /// -1 = unknown (buffer +2/tick); [0,1] = known fraction from server/upload.
    double jobProgressFraction = -1.0;
    static const int maxPollCount = 300; // 5 min at 1s
    KisSignalAutoConnectionsStore connections;

    // Batch submit state
    QStringList batchCollectIds;
    int batchSubmitIndex = 0;
    int batchCountTarget = 0;
    /// §13.212: performance batch_size (seed_i = batchBaseSeed + i * batchSeedStep), not capped job count
    int batchSeedStep = 1;
    qint64 batchBaseSeed = 0;
    // §13.45 / §13.74: Full Animation batch — each prompt_id writes .animation/frame-{time}.png; import at playback start
    bool isFullAnimationBatch = false;
    QMap<QString, int> animationBatchPromptIdToIndex;  // prompt_id → timeline frame index (filename)
    /// §13.74: Ordered timeline frames for this batch (empty = use 0…N-1 filenames)
    QVector<int> animationBatchFrameTimes;
    int animationImportStartFrame = 0;   // firstFrame passed to KisAnimationImporter
    int animationBatchRangeStart = 0;    // inclusive, for "[Generated] start-end" rename
    int animationBatchRangeEnd = 0;      // inclusive
    QString animationBatchGroupId;       // §13.74: client-side batch UUID (extra_data + grouping)
    /// §13.74: timeline frame index → Comfy result cache path; finalize import compares adjacent frames for path reuse
    QMap<int, QString> animationBatchSourcePathByFrame;
    int batchQueueMode = 0;
    QUrl batchBaseUrl;
    bool batchUseCustomWorkflow = false;
    QJsonObject batchCustomWorkflow;
    /// §13.74: Full Animation + custom workflow + reference — upload current frame after each timeline seek, then replace REFERENCE_IMAGE
    bool batchNeedsPerFrameReference = false;
    /// P4/D7: built-in Animation workspace — per-frame canvas/layer upload before buildRefine (strength < 100% or edit mode)
    bool batchNeedsPerFrameAnimationRefine = false;
    /// P7: stashed at batch start for history save composite + region apply metadata
    QStringList batchStashedRegionLayerNames;
    QRect batchStashedContextBounds;
    QRect batchStashedTargetBounds;
    QImage batchStashedCompositingMask;
    bool batchStashedHasMask = false;

    // One-click inpaint state
    /// P4.3: active built-in/user style checkpoint options applied to template workflows
    /// Server workflow includes crop + ETN_ApplyMaskToImage (upstream send_image path).

    // Upscale state (§13.179 FactorWidget: slider + spinbox + target size; §13.147 TileOverlapMode)
    // §5.5 Upscale view — refine block (persistence + ui.json + Comfy upscaleRefineWorkflowTemplate when refine enabled)


    // Settings dialog (connection & workflow configuration)
    QPointer<QDialog> settingsDialog;
    QLabel *labelConnectionStatus = nullptr;  // Status line in Configure → Connection tab
    QLabel *labelHistoryUsageMb = nullptr;    // §13.145: "Currently using X.X MB" in Performance tab
    QLabel *labelStoredHistoryMb = nullptr; // §4.8: document-embedded history size (ai_diffusion/result*)
    QLabel *labelPerfDevice = nullptr;        // §4.8: "Device: …" from ComfyUI /system_stats
    QLabel *labelDetectedModels = nullptr;    // §7.4: Detected base models list (Connection tab)
    /// §4.5 / §13.148: LoRA names from last successful GET /object_info (LoraLoader*, etc.)
    QStringList comfyServerLoraFilenames;
    /// §13.58: Top-level object_info keys that match Technical_Specification.md §13.58 class_type list (last successful fetch).
    QStringList objectInfoSpec58NodesPresent;
    /// Avoid repeating the same §13.58 object_info summary on every Refresh (log only when the set changes).
    QString objectInfoSpec58LastLoggedSignature;
    QPointer<ComfyStyleLoraListWidget> stylesTabLoraListWidget;
    QString comfyDeviceSummary;               // Last formatted line from system_stats (after Connect)
    QJsonObject lastComfySystemStats;         // §13.17: Parsed GET /system_stats body (empty if disconnected / failed)
    /// §13.101 / §13.123: Last GET /object_info root (empty until a successful fetch) — UI workflow → API conversion
    QJsonObject lastObjectInfoRoot;

    // Queue popup button (jobs, batch, seed, enqueue mode, cancel)
    /// §5.7 / §13.92: Queue button on Generate + Animation; batch/enqueue rows hidden when not supports_batch.

    // Group pointers for workspace visibility toggling
    /// §13.49 / §13.53: Control timing range + server-side preprocessor preview (Generate workspace)
    /// §13.98: Add default pose skeleton to active vector layer (Pose.create_default extent + people_count)
    /// §13.53 hands: after server returns crop output, composite onto full canvas extent for preview
    QWidget *graphPlaceholderWidget = nullptr; // Graph workspace body
    QWidget *graphWorkflowSelectWidgets = nullptr; // combo + import/save/delete (top row)
    QComboBox *comboGraphWorkflow = nullptr;
    QToolButton *btnGraphImportWorkflow = nullptr;
    QToolButton *btnGraphSaveWorkflow = nullptr;
    QToolButton *btnGraphDeleteWorkflow = nullptr;
    QWidget *graphActionRowHost = nullptr; // holds Generate+queue when Graph active

    // §5.1/5.2: Main stack (Welcome vs workspace content) and connection state (§13.73)
    QStackedWidget *mainStack = nullptr;
    QWidget *welcomePage = nullptr;
    // §13.190: Welcome layout order — AutoUpdateWidget, NewsWidget, ConnectionWidget (at most one visible)
    QWidget *welcomeUpdateWidget = nullptr;   // §13.37: visible when update available
    QLabel *welcomeUpdateTitleLabel = nullptr;
    QLabel *welcomeUpdateVersionLabel = nullptr; // §5.2: version text below headline when update available
    QProgressBar *welcomeUpdateProgressBar = nullptr;
    QCheckBox *welcomeCheckAutoUpdate = nullptr; // §5.2: same setting as Plugin tab (auto_update)
    QPushButton *welcomeUpdateButton = nullptr;
    QWidget *welcomeNewsWidget = nullptr;     // §13.38: visible when client has news and digest ≠ last_news
    QLabel *welcomeNewsLabel = nullptr;       // §13.38: shows news.text (set when fetch returns)
    QWidget *welcomeConnectionWidget = nullptr;  // status + error + Configure
    /// §13.37 UpdateState — unknown, checking, latest, available, failed_check, downloading, installing, restart_required, failed_update
    enum class PluginUpdateState {
        Unknown,
        Checking,
        Latest,
        Available,
        FailedCheck,
        Downloading,
        Installing,
        RestartRequired,
        FailedUpdate,
    };
    PluginUpdateState pluginUpdateState = PluginUpdateState::Unknown;
    QString updateDownloadUrl;                // §13.160: url from plugin/latest response
    QString updatePackageSha256;              // §13.160: required when a newer version is advertised
    QString updateRemoteVersion;              // §13.160: server "version" when an update exists
    QString updateExtractPath;                // folder with extracted/saved package (restart message)
    QPointer<QNetworkReply> pluginUpdateDownloadReply;
    QScopedPointer<QTemporaryFile> pluginUpdateSaveFile;
    bool updateCheckRequested = false;        // §13.37: avoid duplicate auto update checks per session
    bool updateCheckInProgress = false;      // §13.37: true while GET plugin/latest is in flight (show "Checking for updates...")
    /// §4.9 Plugin tab: last successful plugin/latest "version" string (empty until first successful check).
    QString lastReportedLatestPluginVersion;
    /// §4.9: show "Update failed" after network/JSON error until the next check starts.
    bool pluginUpdateCheckHadFailure = false;
    QPointer<QLabel> pluginTabLatestVersionLabel;
    QPointer<QPushButton> pluginTabDownloadInstallButton;
    bool hasUnseenNews = false;               // §13.38: set when client.news digest ≠ settings last_news
    QString lastNewsDigest;                   // §13.38: digest of current news (saved to settings when user clicks Ok)
    QLabel *welcomeStatusLabel = nullptr;
    QLabel *welcomeErrorLabel = nullptr;  // Yellow error line when connection failed
    bool shellLayoutReady = false;
    bool isConnected = false;
    bool isConnecting = false;
    bool connectionErrorOccurred = false;
    // §7.4a: error_kind for retry/UI — "network" | "missing_resources" | "unknown"; used so autostart/retry can prefer fallback only for network
    QString connectionErrorKind;

    // Connection settings page for the supported external ComfyUI URL mode.
    QPointer<QStackedWidget> connectionStack;
};

// §13.125: active_regions — edit_regions when Generate workspace + Edit mode, else root regions.
inline QList<ComfyUIRemoteDock::Private::RegionEntry> &comfyActiveRegionEntries(ComfyUIRemoteDock::Private *d)
{
    const bool onGenerate = !d->comboWorkspace || d->comboWorkspace->currentIndex() == 0;
    const bool editMode = d->generate.checkEditMode && d->generate.checkEditMode->isChecked();
    if (onGenerate && editMode)
        return d->editRegionEntries;
    return d->regionEntries;
}

inline const QList<ComfyUIRemoteDock::Private::RegionEntry> &comfyActiveRegionEntries(const ComfyUIRemoteDock::Private *d)
{
    const bool onGenerate = !d->comboWorkspace || d->comboWorkspace->currentIndex() == 0;
    const bool editMode = d->generate.checkEditMode && d->generate.checkEditMode->isChecked();
    if (onGenerate && editMode)
        return d->editRegionEntries;
    return d->regionEntries;
}

inline int comfyActiveRegionRow(const ComfyUIRemoteDock::Private *d)
{
    if (!d->generate.regionPromptWidget)
        return -1;
    const int i = d->activeRegionIndex;
    return i >= 0 ? i : -1;
}

inline QList<ComfyControlLayerEntry> mergedJobControlLayers(const QList<ComfyControlLayerEntry> &root,
                                                            const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions)
{
    QList<ComfyControlLayerEntry> out = root;
    for (const ComfyUIRemoteDock::Private::RegionEntry &r : regions) {
        for (const ComfyControlLayerEntry &c : r.controlLayers)
            out.append(c);
    }
    return out;
}

#endif
