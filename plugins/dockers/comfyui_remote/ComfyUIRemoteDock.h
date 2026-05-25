/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_REMOTE_DOCK_H_
#define COMFYUI_REMOTE_DOCK_H_

#include <QDockWidget>
#include <QJsonObject>
#include <QScopedPointer>
#include <kis_mainwindow_observer.h>

class QPlainTextEdit;
class QComboBox;
class QVBoxLayout;
class ComfyControlLayerListWidget;
struct ComfyControlLayerEntry;

#include <kis_types.h>
#include <QList>

class ComfyUIRemoteDock : public QDockWidget, public KisMainwindowObserver
{
    Q_OBJECT
public:
    /// Opaque implementation (definition in ComfyUIRemoteDockPrivate.h); must be public so helpers can use ComfyUIRemoteDock::Private *.
    struct Private;
    ComfyUIRemoteDock();
    ~ComfyUIRemoteDock() override;

    /// §13.17: Performance preset "Automatic" — align dock batch/resolution with inferred tier (after connect or when user selects Automatic).
    void syncPerformanceFromAutoPreset();

    /// §13.31 / §13.170: Save current custom workflow editor text to the open document (used after remote publish).
    void persistOpenCustomWorkflowToDocument();

    /// §13.48: Reload tag CSV list for prompt autocomplete (after Interface settings change).
    void refreshPromptTagCompleter();

    /// §13.213: Queue popup resolution multiplier row visible only when performance preset is Custom.
    void refreshQueueResolutionRowVisibility();

    QString observerName() override { return "ComfyUIRemoteDock"; }
    void setViewManager(KisViewManager *viewManager) override;
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

    // §13.27: ErrorBox-style status: red for error, yellow for warning, default for normal.
    // Public so helper free functions in implementation TUs can surface validation errors
    // without becoming friends of the dock.
    void setStatusMessage(const QString &msg, bool isError = false, bool isWarning = false);

private:
    void startPolling();
    void updateQueueStatus();
    void updateWelcomeVisibility();
    void refreshHistoryList();
    // §13.131/13.136: Resolve current history row to entry index + image index; returns path for selected image or empty
    QString pathForCurrentHistoryRow(int *outEntryIndex = nullptr, int *outImageIndex = nullptr) const;
    void refreshRegionsList();
    // §13.18: ProgressKind — upload = amber (progress_alt), generation = default
    void setProgressBarKind(bool isUpload);
    void loadRegionsFromConfig();
    void saveRegionsToConfig();
    void setupRootControlLayersUi(QWidget *parent, QVBoxLayout *layout);
    void setupRegionControlLayersUi(QWidget *parent, QVBoxLayout *layout);
    void wireControlLayerList(ComfyControlLayerListWidget *list,
                              QList<ComfyControlLayerEntry> *layers,
                              bool forRegion);
    void refreshRootControlLayersList();

    // §13.44: Persist preview layer ID to document annotation (call when user sets preview layer)
    void savePreviewLayerIdToDocument(const QString &layerId);
    // §13.179: Update "Target size: W x H" from document extent × upscale factor
    void updateUpscaleTargetSize();
    // §5.6, §5.7: Set Animation button label/tooltip from batch_mode (Single Frame → "Generate Frame", Full Animation → "Generate Animation")
    void updateAnimationButtonLabel();
    // §13.74: AnimationWorkspace — ui.json `animation` { batch_mode, sampling_quality, target_layer }
    QJsonObject animationWorkspaceToJson() const;
    // §13.189: ui.json top-level keys (upscale, custom, regions, model fields) for Python / .kra parity
    void mergeDocumentModelIntoUiJson(QJsonObject *ui, KisImageSP img) const;
    void loadRegionsPersistedForDocument(KisImageSP img);
    void applyModelFieldsFromUiJson(const QJsonObject &ui);
    void refreshAnimationTargetLayerCombo();
    void updateAnimationTargetLayerRowVisibility();
    void loadAnimationWorkspaceFromDocument();
    void updateAnimationResultPreview(const QString &imagePath);
    /// §13.93 / §13.74: live thumbnail of target paint layer at current timeline (no saved result path yet)
    void refreshAnimationTargetLayerLivePreview();
    // §13.90: Apply PromptHeader (full / icon / none) to region UI
    void applyPromptHeader();
    void updateNegativePromptAlertVisibility();  // §13.143: show alert when style does not use negative prompt
    // §13.145: Active history memory tracking — total bytes of result images, prune when over limit, update Performance tab label
    qint64 historyResultStorageBytes() const;
    void pruneHistoryToStorageLimit();
    void updateHistoryUsageLabel();
    // §13.19: Document-embedded history (slots, ui.json debounce, prune vs history_document_storage_mb)
    void persistTopHistoryEntryToDocument(bool skipForAnimationFrame);
    void scheduleDocumentUiJsonSave();
    void flushDocumentUiJsonNow();
    void loadDocumentHistoryFromAnnotations();
    void removeDocumentHistoryBlobForSlot(KisImageSP image, int slot);
    void pruneDocumentEmbeddedHistoryIfNeeded();
    void evictDocumentEmbeddedSlotIfAny(int documentSlot);
    // §13.19: After multi-image discard, re-encode remaining files to a new document slot (slots not reused)
    void reEmbedHistoryEntryAtIndex(int entryIndex);
    // §4.7 / §5.4: Apply a finished result file using Interface "Apply behavior" (replace / layer / layer_active)
    bool applyResultFileWithBehavior(const QString &localPath, const QString &applyBehavior);
    /// §13.74: Animation + Single Frame — copy result into selected target paint layer (avoids duplicate apply via generation_finished_action).
    bool tryApplyAnimationSingleFrameToTargetLayer(const QString &localPath, bool timelineMismatch = false);
    // §4.7: After a job completes — history selection + optional preview/apply from generation_finished_action
    void handleGenerationFinished(const QString &resultImagePath, bool skipAutoActions = false);
    // §4.7: Prompt lines, negative prompt visibility, steps row — from settings.json
    void applyInterfaceAppearanceSettings();
    // §4.5: Rebuild preset dropdown (None, optional built-ins, custom) from show_builtin_styles + KConfig
    void rebuildPresetComboItems();
    // §5.5: Upscale refinement model list mirrors main style preset combo
    void syncUpscaleRefinementModelFromPresetCombo();
    int firstCustomPresetIndex() const;
    int legacyKConfigPresetCount() const;
    void applyComfyStyleEntry(const struct ComfyStyleEntry &style);
    // §4.5: Rename a custom preset in KConfig (Styles tab Name field).
    bool renameCustomPreset(const QString &oldName, const QString &newName);
    // §4.5 / §13.56: Apply quality_sampler_preset from settings.json to dock + ksamplerScheduler.
    void applyQualitySamplerPresetFromSettings();
    void applyQualitySamplerPresetKey(const QString &presetName);
    // §13.166: Update Configure → Styles LoRA warning when server list or selection changes.
    void refreshStylesTabLoraWarning();
    // §4.5: Re-apply Styles tab LoRA list filter after object_info refresh.
    void applyStylesTabLoraListFilter();
    // §13.184: create_result_layer — apply result image to each region layer per ApplyRegionBehavior (replace, layer_group, transparency_mask, no_hide)
    bool applyResultToRegions(const QString &resultPath, int entryIndex, const QString &regionApplyBehavior);
    // §13.37 / §13.160: Run update check (GET plugin/latest?version=current); drives PluginUpdateState and optional download/install
    void startUpdateCheck(bool manualRequest = false);
    // §13.37: Download verified package (SHA-256), extract when KArchive is available, then restart_required
    void startPluginUpdateDownload();
    // §13.37: Manual check from Plugin tab "Check for Updates" button (works even when auto-update is off)
    void slotCheckForUpdates();
    // §13.38: Fetch news from API; if digest != last_news, show NewsWidget with text
    void startNewsFetch();
    // §5.4 / §13.209: Persist fixed seed and seed value to KConfig.
    void persistSeedToConfig();
    // §5.4: Copy dock seed row into queue popup duplicates (avoids reparenting shared widgets).
    void syncQueueSeedWidgetsFromMain();
    /// §5.7 / §13.92: Hide batch + enqueue mode in queue popup on Animation (supports_batch=False).
    void refreshQueuePopupSupportsBatch();
    /// §13.81: When `server_mode` is undefined, probe `settings.server_url` then `127.0.0.1:8000`.
    void tryAutostartServerFallback();
    /// §13.89: Connect vs Disconnect button label (Configure dialog).
    void refreshConnectionActionButton();

protected:
    // §13.196: Shift+Enter in prompt widget triggers Generate
    bool eventFilter(QObject *obj, QEvent *event) override;

private Q_SLOTS:
    void slotTestConnection();
    /// §13.89: Clear client connection state (Python Connection.disconnect() equivalent for custom ComfyUI).
    void slotDisconnect();
    void slotRefreshCheckpoints();
    void slotRefreshSamplers();
    void slotRandomSeed();
    void slotLoadWorkflowFromFile();
    /// §4.8: Main-thread continuation after optional background read in slotLoadWorkflowFromFile.
    void applyImportedWorkflowBytes(const QByteArray &raw, const QString &openError);
    void slotPresetChanged(int index);
    void slotSaveAsPreset();
    void slotSaveCurrentPreset();
    void slotDeletePreset();
    void slotGenerate();
    void slotBatchSubmitNext();
    void slotCancelQueue();
    void slotHistoryReRun();
    void slotHistoryItemSelected();
    void slotHistoryApply();
    void slotHistoryContextMenu(QPoint pos);
    void slotHistoryCopyPrompt();
    void slotHistoryCopyPromptEvaluated();
    void slotHistoryCopyStrength();
    void slotHistoryCopyStyle();
    void slotHistoryCopySeed();
    void slotHistoryCopyInfo();
    void slotHistorySaveImage();
    void slotHistoryDiscard();
    void slotHistoryClear();
    void slotInpaint();
    void slotUpscale();
    void slotUpscalePoll();
    void slotGenerateAnimation();
    void slotImportAnimation();  // §13.45: Import frames from .animation or .live-frames into document
    void slotConfigureHelp();
    void slotRestoreDefaults();
    void slotLiveTick();
    void slotLivePoll();
    void beginLiveUploadPipeline();
    void uploadNextLiveLoraFile();
    void continueLiveAfterLoraUploads();
    void uploadLiveCanvasAndPrompt();
    void setLiveProgress(int percent);
    void startLiveSpinner();
    void stopLiveSpinner();
    void slotAddRegion();
    void slotRemoveRegion();
    void slotMoveRegionUp();
    void slotMoveRegionDown();
    void slotEditRegion();
    void slotGenerateRegions();
    void slotInpaintPoll();
    /// §13.195: ai_diffusion_toggle_workspace — same cycle as Python Workspace enum
    void slotAiDiffusionToggleWorkspace();
    /// §13.195: ai_diffusion_toggle_edit_mode — flip edit_mode boolean (Generate + instruction styles)
    void slotAiDiffusionToggleEditMode();
    /// §10.1: Krita actions — same IDs / semantics as Python ai_diffusion.action
    void slotAiDiffusionGenerateAction();
    void slotAiDiffusionCancelCurrent();
    void slotAiDiffusionCancelQueued();
    void slotAiDiffusionCancelAll();
    void slotAiDiffusionTogglePreview();
    void slotAiDiffusionApply();
    void slotAiDiffusionApplyAlternative();
    void slotAiDiffusionCreateRegion();
    void slotAiDiffusionSwitchWorkspaceGeneration();
    void slotAiDiffusionSwitchWorkspaceUpscaling();
    void slotAiDiffusionSwitchWorkspaceLive();
    void slotAiDiffusionSwitchWorkspaceGraph();
    /// §13.93: QTimer ~20 ms — selection bounds + timeline vs. Python KritaDocument._poll
    void slotDocumentSyncPoll();
    /// §13.93: debounced refresh after current_time_changed (Animation Single Frame target preview)
    void slotDebouncedAnimationTargetPreview();
    /// §13.53: Upload canvas → control preprocessor workflow → show thumbnail
    void slotControlPreviewRun();
    void slotControlPreviewPoll();
    /// §13.98: Insert default pose SVG into active KisShapeLayer; register for 500 ms polling
    void slotAddPoseGuideToVectorLayer();
    void slotAddControlLayer();
    void slotRemoveControlLayerAt(int row);
    void slotControlLayerSelectionChanged();
    void refreshRegionControlLayersList();
    void slotAddRegionControlLayer();
    void slotRemoveRegionControlLayerAt(int row);
    void slotRegionControlLayerSelectionChanged();
    void slotAddPoseForControlLayer(bool forRegion, int index);
    void slotControlLayerJobPoll();
    void refreshControlLayerGenerateButtons();
    void beginControlLayerGenerateJob(bool forRegion, int entryIndex);

private:
    void beginGenerateUploadPipeline();
    void uploadNextGenerateLoraFile();
    void continueGenerateAfterLoraUploads();
    void uploadNextGenerateControlImage();
    void uploadNextGenerateRegionMask();
    void continueGenerateAfterControlUploads();
    void beginInpaintUploadPipeline();
    void uploadNextInpaintLoraFile();
    void continueInpaintAfterLoraUploads();
    void finalizeGenerateWorkflowAndSubmit(QJsonObject workflow);
    void continueUpscaleAfterCanvasUpload(int canvasW, int canvasH, int w2, int h2);
    void beginUpscaleConditioningUploadPipeline();
    void uploadNextUpscaleControlImage();
    void uploadNextUpscaleRegionMask();
    void finalizeUpscaleWorkflowAndSubmit();
    void submitUpscaleWorkflow(const QJsonObject &workflow, bool wantRefine, bool useTiledRefine);
    void uploadNextInpaintControlImage();
    void submitInpaintWorkflow(QJsonObject workflow);
    bool tryStartRefineFromGenerate();
    void uploadCanvasForRefineGenerate();
    // §13.194 / §13.137: RecentlyUsedSync — document_defaults in settings.json; skip layer_bounds on fresh docs
    void persistDocumentDefaultsToSettings();
    void tryApplyDocumentDefaultsForNewDocument(KisImageSP image);
    void schedulePersistDocumentDefaults();
    QString encodeStyleIdForDocumentDefaults() const;
    QString encodeStyleIdFromPresetCombo(const QComboBox *cb) const;
    void applyStyleIdToPresetCombo(QComboBox *cb, const QString &styleId);
    void applyStyleIdFromDocumentDefaults(const QString &styleId);
    /// §5.5: Checkpoint filename for upscale refinement preset (built-in → current Styles checkpoint; custom → preset KConfig).
    QString checkpointNameForUpscaleRefinementPreset() const;
    /// Sampling fields from refinement preset / dock defaults (custom presets override from KConfig).
    void readUpscaleRefinementSampling(int *outSteps, double *outCfg, QString *outSampler, QString *outScheduler) const;
    /// §10.1: cancel running main generate job and promote next in local queue (false if none)
    bool cancelCurrentGenerateJob();
    /// §10.1: remove jobs waiting in local queue (does not interrupt current)
    void cancelQueuedGenerateJobs();
    // §13.169 / §13.31: document annotations (ui.json equivalent) using current canvas image
    void saveInpaintWorkspaceToDocument();
    void loadInpaintWorkspaceFromDocument();
    void updateInpaintControlsForArch();
    void saveEmbeddedCustomWorkflowToDocument();
    void loadEmbeddedCustomWorkflowFromDocument();
    void scheduleSaveEmbeddedCustomWorkflowToDocument();
    /// §13.25: Rebuild ETN parameter widgets from current custom workflow JSON (Configure → Workflow).
    void refreshCustomWorkflowParameterPanel();
    /// §13.101: UI workflow (nodes/links) → prompt API using lastObjectInfoRoot; on failure sets status and returns false.
    bool tryResolveCustomWorkflowInPlace(QJsonObject *workflow);
    void reparentCustomWorkflowEditor(bool toGraphWorkspace);
    // §13.170: after Open Web UI — ETN subscribe + WebSocket listen (when Qt WebSockets is available)
    void beginWebWorkflowSwitch();
    void endWebWorkflowSwitch();
    void showPromptTagCompletion(QPlainTextEdit *editor);
    void insertPromptTagCompletion(QPlainTextEdit *editor, const QString &completion);
    /// Build POST /prompt for one batch index (shared by slotBatchSubmitNext and per-frame reference upload completion).
    void dispatchBatchPromptRequest(QJsonObject workflow, int submitIndex);
    /// §13.49: IntervalSlider + settings keys control_layer_timing_{low,high}_pct (0–100)
    void syncControlPreviewRangeFromSettings();
    void syncPoseGuidePeopleCountFromSettings();
    /// §13.53: Stop control preprocessor preview poll (separate from main generate / live queues)
    void stopControlPreviewPolling();
    void stopControlLayerJobPolling();
    void startControlLayerJobWebSocketListen();
    bool importControlLayerFromOpenPoseJson(const QJsonValue &openPoseJson,
                                            const QString &resultLayerName,
                                            const QString &anchorLayerName,
                                            bool forRegion,
                                            int regionRow,
                                            int entryIndex);

    void fetchComfyModelsLorasMergeAndRefreshStylesTab();
    void clearObjectInfoDerivedServerCaches();
    // §13.123 / §13.58: Parse full GET /object_info JSON — LoRA names + spec §13.58 class_types present as top-level keys.
    void syncFromObjectInfoRoot(const QJsonObject &objectInfoRoot);
    /// §13.97 / §13.75: Apply filtered checkpoint list; keep selection if valid, else first item; invalid custom preset → None.
    void applyServerCheckpointList(const QStringList &filteredNames, bool serverReturnedEmptyList);
    /// §13.75 ETN filter then applyServerCheckpointList. If \p noOpWhenNamesEmpty and names empty, leave combo unchanged.
    void fetchFilteredCheckpointListAndApply(const QStringList &namesFromObjectInfo, const QString &baseUrlStr, bool noOpWhenNamesEmpty);
    /// §4.9 Configure → Plugin tab: Latest version line + Download and Install enablement
    void refreshPluginInformationTabUpdateUi();
    void refreshWelcomeAutoUpdatePanel();
    void syncPluginUpdateUi();
    QScopedPointer<Private> m_d;
};

#endif
