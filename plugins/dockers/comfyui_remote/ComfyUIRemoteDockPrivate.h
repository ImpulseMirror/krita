/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_REMOTE_DOCK_PRIVATE_H_
#define COMFYUI_REMOTE_DOCK_PRIVATE_H_

#include "ComfyUIRemoteDock.h"

#include <QPointer>
#include <QMap>
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
#include <QUrl>
#include <QJsonObject>
#include <QVariant>
#include <QDialog>
#include <QFormLayout>
#include <QToolButton>
#include <QSlider>
#include <QGroupBox>
#include <QButtonGroup>
#include <QRadioButton>
#include <QStackedWidget>
#include <QWidget>
#include <QImage>
#include <QVector>

class QCompleter;
class QStringListModel;

#include <kis_signal_auto_connection.h>
#include <KisViewManager.h>
#include <kis_canvas2.h>

struct ComfyUIRemoteDock::Private
{
    QPointer<KisViewManager> viewManager;
    QPointer<KisCanvas2> canvas;

    QLineEdit *editServerUrl = nullptr;
    QComboBox *comboCheckpoint = nullptr;
    QPushButton *btnRefreshCheckpoints = nullptr;
    QComboBox *comboPreset = nullptr;
    QComboBox *comboQuality = nullptr;
    QComboBox *comboSizePreset = nullptr;
    QPushButton *btnSaveAsPreset = nullptr;
    QPushButton *btnDeletePreset = nullptr;
    QPlainTextEdit *editPrompt = nullptr;
    QPlainTextEdit *editNegative = nullptr;
    QWidget *negativePromptBlock = nullptr;  // §4.7: label row + negative editor (hide when "Show negative" off)
    QWidget *stepsParametersWidget = nullptr;  // §4.7: Steps/CFG/Sampler row (hide when "Show steps" off)
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
    /// §13.56: KSampler scheduler (e.g. normal, karras) from quality sampler preset or "normal"
    QString ksamplerScheduler = QStringLiteral("normal");
    QSpinBox *spinSeed = nullptr;
    QCheckBox *checkFixedSeed = nullptr;
    QPushButton *btnRandomSeed = nullptr;
    QProgressBar *progressBar = nullptr;
    QComboBox *comboWorkspace = nullptr;
    int lastWorkspaceIndex = -1;  // §13.149: track for Live strength persist on leave
    QComboBox *comboQueueMode = nullptr;
    QSpinBox *spinBatchCount = nullptr;
    QLabel *labelQueueCount = nullptr;
    QPushButton *btnTest = nullptr;
    QPushButton *btnGenerate = nullptr;
    QPushButton *btnCancelQueue = nullptr;
    QPushButton *btnInpaint = nullptr;
    QToolButton *btnGenerateViewOperations = nullptr; // §13.29: main action menu (Generate / Refine / Edit / …)
    QComboBox *comboInpaintMode = nullptr;  // §13.206: Automatic | Fill | Expand (when not Automatic, overrides detectInpaintMode)
    QComboBox *comboFillMode = nullptr;    // §13.188: None | Neutral | Blur | Border | Inpaint (five options, replace/green internal only)
    QPushButton *btnUpscale = nullptr;
    QSpinBox *spinAnimationFrames = nullptr;
    QPushButton *btnGenerateAnimation = nullptr;
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
    QCheckBox *checkUseReferenceImage = nullptr;
    QCheckBox *checkLiveMode = nullptr;
    QWidget *liveSpinner = nullptr;  // §13.105: LiveSpinnerWidget for Live view progress
    QTimer *liveTimer = nullptr;
    QString liveUploadedImageName;
    QString livePromptId;
    QCheckBox *checkLiveRecord = nullptr;  // §13.45: when checked, save each live result to .live-frames/frame-N.webp
    int liveFrameIndex = 0;                // next frame index when recording
    int livePollCount = 0;
    static const int liveMaxPollCount = 120;
    QTimer *livePollTimer = nullptr;
    // §13.31: debounced save of custom workflow text to document annotation
    QTimer *customWorkflowDocumentSaveTimer = nullptr;
    // §13.19: debounced write of ai_diffusion/ui.json (history list + preserved keys)
    QTimer *documentUiJsonSaveTimer = nullptr;
    // §13.170: transient WebSocket + timer (opaque QObject child of dock when active)
    QObject *webWorkflowSwitchSession = nullptr;
    QLabel *labelStatus = nullptr;
    QListWidget *listHistory = nullptr;
    QPushButton *btnHistoryReRun = nullptr;
    QPushButton *btnHistoryApply = nullptr;
    QPlainTextEdit *editCustomWorkflow = nullptr;
    // §13.25: ETN_Parameter / ETN_KritaStyle — Configure → Workflow tab
    QGroupBox *customWorkflowParamsGroup = nullptr;
    QFormLayout *customWorkflowParamsForm = nullptr;
    QTimer *customWorkflowParamsRefreshTimer = nullptr;
    QMap<QString, QVariant> customWorkflowParamOverrides;
    // §13.48: Shared tag word list; separate completers for positive / negative prompts
    QStringListModel *tagKeywordModel = nullptr;
    QCompleter *promptTagCompleter = nullptr;
    QCompleter *negativePromptTagCompleter = nullptr;
    QPushButton *btnLoadWorkflow = nullptr;
    QCheckBox *checkConfirmDiscardImage = nullptr;  // §13.192: confirm_discard_image (Interface tab)

    // §13.44: Preview layer ID (QUuid string) from document annotation; restored on setCanvas
    QString previewLayerId;

    struct RegionEntry {
        QString name;
        QString prompt;
        QString maskSource; // "selection" or "layer:LayerName"
    };
    QList<RegionEntry> regionEntries;
    QList<RegionEntry> editRegionEntries;  // §13.125: edit_regions (Generate + Edit mode)
    /// Snapshot of active regions when "Generate regions" started (async chain must not follow live toggles).
    QList<RegionEntry> regionGenerationSnapshot;
    // §13.90: PromptHeader — full (title + description), icon (icon only), none (no header)
    int promptHeaderMode = 0;  // 0=full, 1=icon, 2=none; persisted in config
    QComboBox *regionHeaderCombo = nullptr;
    QLabel *regionHeaderLabel = nullptr;  // description or icon, visibility by mode
    QListWidget *listRegions = nullptr;   // §13.106: objectName InactiveRegionWidget (list of regions; selected = active)
    QPushButton *btnAddRegion = nullptr;
    QPushButton *btnRemoveRegion = nullptr;
    QPushButton *btnMoveRegionUp = nullptr;
    QPushButton *btnMoveRegionDown = nullptr;
    QPushButton *btnEditRegion = nullptr;
    QPushButton *btnGenerateRegions = nullptr;

    void refreshRegionsList();
    void loadRegionsFromConfig();
    void saveRegionsToConfig();

    struct HistoryEntry {
        QString jobId;            // §13.131/13.136: prompt_id for result identity (Item(job_id, index))
        QString prompt, negative, checkpoint;
        QString styleName;  // preset name for §13.28 Copy Style
        int width = 512, height = 512;
        int steps = 20;
        double cfg = 8.0;
        int strength = 100;  // 1–100%, for Copy Strength / denoise
        QString samplerName;
        qint64 seed = 0;
        QString resultImagePath;  // deprecated: use resultImagePaths; kept for single-image compat
        QStringList resultImagePaths;  // §13.131: one or more result images per job (in-place discard by index)
        QMap<int, bool> imageInUse;   // §13.19 / §13.28a: per sub-image applied state (list row index → star overlay)
        QStringList regionLayerNames;  // §13.184: when non-empty, Apply uses create_result_layer per region (layer name from maskSource "layer:X")
        int documentSlot = -1;         // §13.19: ai_diffusion/result{slot}.webp — -1 = not embedded in .kra yet
        QList<int> documentBlobEndOffsets; // §13.19: byte end offsets for segments inside the blob (same order as resultImagePaths)
        /// §13.74: document timeline frame when job was queued (Single Frame + animated doc); -1 if not tracked
        int animationSubmitTime = -1;
    };
    QList<HistoryEntry> historyEntries;
    QMap<QString, HistoryEntry> pendingHistoryByPromptId;
    static const int maxHistoryEntries = 20;

    static const int builtinPresetCount = 5; // None, Portrait, Landscape, Anime, Realistic

    void refreshHistoryList();

    QNetworkAccessManager *nam = nullptr;
    QTimer *pollTimer = nullptr;
    QString clientId;             // §13.59: one UUID per session; used in prompt/queue HTTP bodies (and WebSocket URL when WS implemented)
    QString currentPromptId;      // the one we're currently polling (§9.3 Job.id)
    QStringList jobQueue;         // prompt_ids waiting, first is running (§9.3 JobQueue)
    int pollCount = 0;
    static const int maxPollCount = 300; // 5 min at 1s
    KisSignalAutoConnectionsStore connections;

    // Batch submit state
    QStringList batchCollectIds;
    int batchSubmitIndex = 0;
    int batchCountTarget = 0;
    qint64 batchBaseSeed = 0;    // §13.212: base seed for batch (seed_i = batchBaseSeed + i * batchCountTarget)
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

    // Region generation state (used during slotGenerateRegions async chain)
    QString regionJobId;  // §13.184: UUID for this region run; used when adding to history with regionLayerNames
    QImage regionCurrentImage;
    int regionIndex = 0;
    QString regionUploadedImageName;
    QString regionUploadedImageSubfolder;
    QString regionPromptId;
    QString regionMaskUploadedName;
    QString regionMaskUploadedSubfolder;
    int regionPollCount = 0;
    static const int regionMaxPollCount = 300;

    // One-click inpaint state
    QString inpaintUploadedImageName;
    QString inpaintUploadedImageSubfolder;
    QString inpaintUploadedMaskName;
    QString inpaintUploadedMaskSubfolder;
    QString inpaintPromptId;
    QImage inpaintCurrentImage;
    int inpaintPollCount = 0;
    static const int inpaintMaxPollCount = 300;
    QTimer *inpaintPollTimer = nullptr;

    // Upscale state (§13.179 FactorWidget: slider + spinbox + target size; §13.147 TileOverlapMode)
    double upscaleFactor = 2.0;
    QSlider *sliderUpscaleFactor = nullptr;
    QDoubleSpinBox *spinUpscaleFactor = nullptr;
    QLabel *labelUpscaleTargetSize = nullptr;
    QWidget *upscaleFactorRow = nullptr;
    int tileOverlapMode = 0;  // §13.147: 0 = auto (overlap -1), 1 = custom (use tile_overlap px)
    int tileOverlap = 32;
    QComboBox *comboTileOverlapMode = nullptr;
    QSpinBox *spinTileOverlap = nullptr;
    QWidget *upscaleTileOverlapRow = nullptr;

    QString upscaleUploadedImageName;
    QString upscalePromptId;
    int upscalePollCount = 0;
    static const int upscaleMaxPollCount = 300;
    QTimer *upscalePollTimer = nullptr;

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
    QPointer<QLabel> stylesTabLoraWarningLabel;   // §13.166: Configure → Styles LoRA warning line
    QPointer<QListWidget> stylesTabLoraListWidget; // same tab, for refreshStylesTabLoraWarning
    int stylesTabLoraFilterMode = 0; // 0 = All, 1 = On server, 2 = Not on server (§4.5)
    QString comfyDeviceSummary;               // Last formatted line from system_stats (after Connect)
    QJsonObject lastComfySystemStats;         // §13.17: Parsed GET /system_stats body (empty if disconnected / failed)

    // Queue popup button (jobs, batch, seed, enqueue mode, cancel)
    QToolButton *btnQueuePopup = nullptr;
    QWidget *queueResolutionRow = nullptr; // §13.213: Resolution slider row (visible when perf preset = custom)
    QSlider *sliderResolutionMultiplier = nullptr;
    QLabel *labelResolutionMultiplier = nullptr;
    double resolutionMultiplier = 1.0;

    // Group pointers for workspace visibility toggling
    QGroupBox *genGroupBox = nullptr;
    QGroupBox *histGroupBox = nullptr;
    QGroupBox *regionsGroupBox = nullptr;
    QWidget *genContentContainer = nullptr;   // Generate view content (prompt, params, buttons)
    QWidget *graphPlaceholderWidget = nullptr; // Graph workspace: "Open Settings" placeholder

    // §5.1/5.2: Main stack (Welcome vs workspace content) and connection state (§13.73)
    QStackedWidget *mainStack = nullptr;
    QWidget *welcomePage = nullptr;
    // §13.190: Welcome layout order — AutoUpdateWidget, NewsWidget, ConnectionWidget (at most one visible)
    QWidget *welcomeUpdateWidget = nullptr;   // §13.37: visible when update available
    QWidget *welcomeNewsWidget = nullptr;     // §13.38: visible when client has news and digest ≠ last_news
    QLabel *welcomeNewsLabel = nullptr;       // §13.38: shows news.text (set when fetch returns)
    QWidget *welcomeConnectionWidget = nullptr;  // status + error + Configure
    bool hasUpdateAvailable = false;          // §13.37: set when update check reports available
    QString updateDownloadUrl;                // §13.160: url from plugin/latest response (for Download and Install)
    bool updateCheckRequested = false;        // §13.37: avoid duplicate update checks per session
    bool updateCheckInProgress = false;      // §13.37: true while GET plugin/latest is in flight (show "Checking for updates...")
    bool hasUnseenNews = false;               // §13.38: set when client.news digest ≠ settings last_news
    QString lastNewsDigest;                   // §13.38: digest of current news (saved to settings when user clicks Ok)
    QLabel *welcomeStatusLabel = nullptr;
    QLabel *welcomeErrorLabel = nullptr;  // Yellow error line when connection failed
    bool isConnected = false;
    bool isConnecting = false;
    bool connectionErrorOccurred = false;
    // §7.4a: error_kind for retry/UI — "network" | "missing_resources" | "unknown"; used so autostart/retry can prefer fallback only for network
    QString connectionErrorKind;

    // §13.33 InitialSetupWidget: first-time server choice (undefined → show setup; then show mode selector + panels)
    QPointer<QStackedWidget> connectionStack;       // 0 = initial setup, 1 = mode selector + panels
    QPointer<QStackedWidget> innerConnectionStack;  // 0 = cloud, 1 = managed, 2 = custom (external)
    QButtonGroup *connectionModeGroup = nullptr;   // 3 options: Online Service, Local Managed Server, Custom ComfyUI
};

// §13.125: active_regions — edit_regions when Generate workspace + Edit mode, else root regions.
inline QList<ComfyUIRemoteDock::Private::RegionEntry> &comfyActiveRegionEntries(ComfyUIRemoteDock::Private *d)
{
    const bool onGenerate = !d->comboWorkspace || d->comboWorkspace->currentIndex() == 0;
    const bool editMode = d->checkEditMode && d->checkEditMode->isChecked();
    if (onGenerate && editMode)
        return d->editRegionEntries;
    return d->regionEntries;
}

inline const QList<ComfyUIRemoteDock::Private::RegionEntry> &comfyActiveRegionEntries(const ComfyUIRemoteDock::Private *d)
{
    const bool onGenerate = !d->comboWorkspace || d->comboWorkspace->currentIndex() == 0;
    const bool editMode = d->checkEditMode && d->checkEditMode->isChecked();
    if (onGenerate && editMode)
        return d->editRegionEntries;
    return d->regionEntries;
}

#endif
