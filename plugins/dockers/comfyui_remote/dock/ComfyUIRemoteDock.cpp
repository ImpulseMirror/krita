/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyHistoryListWidget.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUiLayoutDiagnostics.h"

#include <QProgressBar>
#include <QPointer>
#include <QScrollArea>
#include "ComfyStyleCollection.h"
#include "ComfyStyleLoraListWidget.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyFileLibrary.h"
#include "ComfyControlLayer.h"
#include "ComfyUIUtils.h"
#include "ComfyResources.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyUIPoseLayers.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfyPromptClient.h"
#include "ComfyDockUiBuilder.h"
#include "ComfyGenerateRunner.h"
#include "ComfyUiLayoutDiagnostics.h"

#include <QLoggingCategory>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QSignalBlocker>
#include <QToolTip>
#include <QCursor>
// FAITHFUL_PORT: dedicated Android-visible category so `adb logcat -s
// org.krita.debug:V` shows every status change and generate-path decision.
// All comfyui_remote slots/lambdas funnel through setStatusMessage() and
// slotGenerate(); both emit on this category at qWarning level so they show
// in logcat without enabling QT_LOGGING_RULES on the device.
Q_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE, "krita.comfyui_remote")

#include <kis_shape_layer.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QMap>
#include <QSharedPointer>
#include <QTemporaryFile>
#include <QMessageBox>
#include <QTimer>
#include <QDateTime>
#include <QRandomGenerator>
#include <QProgressBar>
#include <QInputDialog>
#include <QListView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QCompleter>
#include <QStringListModel>
#include <QTextCursor>
#include <QScrollArea>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QHttpMultiPart>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QSlider>
#include <QRadioButton>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QEvent>
#include <QPaintEvent>
#include <QMenu>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QApplication>
#include <QFontMetrics>
#include <QClipboard>
#include <QTabWidget>
#include <QStackedWidget>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>
#include <QDesktopServices>
#include <QToolButton>
#include <QWidgetAction>
#include <QUuid>
#include <QCryptographicHash>
#include <QSet>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>

#include <QMouseEvent>
#include <kis_action.h>
#include <kis_action_manager.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>
#include <kis_icon_utils.h>
#include <KisViewManager.h>
#include <kis_canvas2.h>
#include <kis_signal_auto_connection.h>
#include <kis_image_manager.h>
#include <kis_selection.h>
#include <kis_types.h>
#include <KoUpdater.h>
#include <kis_animation_importer.h>
#include <kis_annotation.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <kis_node.h>
#include <kis_mask.h>
#include <KisPart.h>
#include <KisDocument.h>
#include <KisImportExportErrorCode.h>
#include <commands/KisNodeRenameCommand.h>
#include <kis_layer_properties_icons.h>
#include <kis_layer_utils.h>
#include <KisImageBarrierLock.h>
#include <kis_undo_adapter.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorProfile.h>
#include <KoColorConversionTransformation.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

ComfyUIRemoteDock::ComfyUIRemoteDock()
    : QDockWidget()
    , m_d(new Private)
{
    m_d->nam = new QNetworkAccessManager(this);
    m_d->pollTimer = new QTimer(this);
    m_d->pollTimer->setSingleShot(true);
    m_d->inpaintRt.inpaintPollTimer = new QTimer(this);
    m_d->inpaintRt.inpaintPollTimer->setSingleShot(true);
    connect(m_d->inpaintRt.inpaintPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotInpaintPoll);
    m_d->upscaleRt.upscalePollTimer = new QTimer(this);
    m_d->upscaleRt.upscalePollTimer->setSingleShot(true);
    connect(m_d->upscaleRt.upscalePollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotUpscalePoll);
    m_d->liveRt.liveTimer = new QTimer(this);
    m_d->liveRt.liveTimer->setInterval(100);
    connect(m_d->liveRt.liveTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotLiveTick);
    m_d->liveRt.livePollTimer = new QTimer(this);
    m_d->liveRt.livePollTimer->setSingleShot(true);
    connect(m_d->liveRt.livePollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotLivePoll);
    m_d->generateRt.controlPreviewPollTimer = new QTimer(this);
    m_d->generateRt.controlPreviewPollTimer->setSingleShot(true);
    connect(m_d->generateRt.controlPreviewPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotControlPreviewPoll);
    m_d->generateRt.controlLayerJobPollTimer = new QTimer(this);
    m_d->generateRt.controlLayerJobPollTimer->setSingleShot(true);
    connect(m_d->generateRt.controlLayerJobPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotControlLayerJobPoll);
    m_d->documentSyncPoller = new QTimer(this);
    m_d->documentSyncPoller->setInterval(20);
    connect(m_d->documentSyncPoller, &QTimer::timeout, this, &ComfyUIRemoteDock::slotDocumentSyncPoll);
    m_d->animationPreviewDebounce = new QTimer(this);
    m_d->animationPreviewDebounce->setSingleShot(true);
    m_d->animationPreviewDebounce->setInterval(100);
    connect(m_d->animationPreviewDebounce, &QTimer::timeout, this, &ComfyUIRemoteDock::slotDebouncedAnimationTargetPreview);
    m_d->documentDefaultsSaveTimer = new QTimer(this);
    m_d->documentDefaultsSaveTimer->setSingleShot(true);
    connect(m_d->documentDefaultsSaveTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::persistDocumentDefaultsToSettings);
    connect(m_d->pollTimer, &QTimer::timeout, this, [this]() { ComfyGenerateRunner::onPollTimer(this); });
    ComfyDockUiBuilder::Context uiCtx{this, m_d.data()};
    ComfyDockUiBuilder::DockShell uiShell = ComfyDockUiBuilder::buildDockShell(uiCtx);
    ComfyDockUiBuilder::buildWelcomePage(uiCtx, uiShell);
    ComfyDockUiBuilder::buildSharedChrome(uiCtx, uiShell);

    ComfyDockUiBuilder::buildGenerateWorkspace(uiCtx, uiShell);
    ComfyDockUiBuilder::buildGraphWorkspace(uiCtx, uiShell);

    uiShell.scrollLayout->addWidget(uiShell.genGroup);

    ComfyDockUiBuilder::buildHistoryPanel(uiCtx, uiShell.scrollLayout);
    ComfyDockUiBuilder::buildRegionsPanel(uiCtx, uiShell.scrollLayout);

    // §13.90: Apply PromptHeader from config (full / icon / none)
    m_d->promptHeaderMode = qBound(0, KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("PromptHeader", 0), 2);
    if (m_d->generate.regionHeaderCombo) m_d->generate.regionHeaderCombo->setCurrentIndex(m_d->promptHeaderMode);
    applyPromptHeader();

    ComfyDockUiBuilder::finalizeContentScroll(uiShell);
    ComfyDockUiBuilder::attachContentPage(uiCtx, uiShell);
    setWindowTitle(ComfyTr::tr("AI Image Generation"));
    setEnabled(false);

    loadRegionsFromConfig();
    refreshRegionsList();
    ComfyDockUiBuilder::finalizeGenerateWorkspaceLayout(uiCtx, uiShell);

    int ws = m_d->comboWorkspace->currentIndex();
    const bool isGraph = (ws == 4);
    const bool isGenerate = (ws == 0);
    const bool isUpscale = (ws == 1);
    if (ws == 2 && m_d->generate.spinStrength) {
        QSignalBlocker bSpin(m_d->generate.spinStrength);
        m_d->generate.spinStrength->setValue(30);
        if (m_d->inpaint.sliderStrength) {
            QSignalBlocker bSlider(m_d->inpaint.sliderStrength);
            m_d->inpaint.sliderStrength->setValue(30);
        }
    }
    if (m_d->generate.genContentContainer) m_d->generate.genContentContainer->setVisible(!isGraph);
    if (m_d->graphPlaceholderWidget) m_d->graphPlaceholderWidget->setVisible(isGraph);
    reparentCustomWorkflowEditor(isGraph);
    syncHistoryPanelWorkspaceVisibility();
    if (m_d->generate.queueButtonRowWidget)
        m_d->generate.queueButtonRowWidget->setVisible(isGenerate || ws == 3 || isGraph);
    if (m_d->upscale.upscaleActionRowWidget)
        m_d->upscale.upscaleActionRowWidget->setVisible(isUpscale);
    if (m_d->generate.comboPreset)
        m_d->generate.comboPreset->setVisible(isGenerate || ws == 2 || ws == 3 || isGraph);
    if (m_d->upscale.comboUpscaleModel)
        m_d->upscale.comboUpscaleModel->setVisible(isUpscale);
    if (isUpscale && m_d->upscale.upscaleActionRowWidget && m_d->generate.btnQueuePopup) {
        if (auto *row = qobject_cast<QHBoxLayout *>(m_d->upscale.upscaleActionRowWidget->layout()))
            row->addWidget(m_d->generate.btnQueuePopup);
    }
    if (isUpscale)
        updateUpscaleTargetSize();
    refreshQueuePopupSupportsBatch();
    if (m_d->generate.btnQueuePopup) {
        const bool animWs = (ws == 3);
        m_d->generate.btnQueuePopup->setToolTip(animWs
            ? ComfyTr::tr("Idle. Click to adjust seed or cancel jobs (Animation has no batch enqueue options).")
            : ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));
    }

    updateWelcomeVisibility();
    updateLiveWorkspaceUi();
    m_d->shellLayoutReady = true;
    refreshInlineControlLayersList();
    applyInterfaceAppearanceSettings();
    updateGenerateOptions();
    applyQualitySamplerPresetFromSettings();
    refreshQueueResolutionRowVisibility();
    refreshAnimationTargetLayerCombo();
    updateAnimationTargetLayerRowVisibility();

    // §13.81: deferred autostart probe for unset or legacy skipped server modes
    QTimer::singleShot(400, this, &ComfyUIRemoteDock::tryAutostartServerFallback);
}
ComfyUIRemoteDock::~ComfyUIRemoteDock()
{
    if (m_d->pluginUpdateDownloadReply) {
        m_d->pluginUpdateDownloadReply->disconnect(this);
        m_d->pluginUpdateDownloadReply->abort();
        m_d->pluginUpdateDownloadReply.clear();
    }
    m_d->pluginUpdateSaveFile.reset();
    endWebWorkflowSwitch();
}
bool ComfyUIRemoteDock::eventFilter(QObject *obj, QEvent *event)
{
    const auto isPromptEditor = [this](QObject *o) -> bool {
        if (o == m_d->generate.editPrompt || o == m_d->generate.editNegative)
            return true;
        if (!m_d->generate.regionPromptWidget)
            return false;
        return o == m_d->generate.regionPromptWidget->positivePromptEditor()
               || o == m_d->generate.regionPromptWidget->negativePromptEditor();
    };
    const auto isPositivePromptEditor = [this](QObject *o) -> bool {
        if (o == m_d->generate.editPrompt)
            return true;
        return m_d->generate.regionPromptWidget
               && o == m_d->generate.regionPromptWidget->positivePromptEditor();
    };
    // §13.196: Ctrl+Backspace — accept ShortcutOverride so the editor receives a normal Key_Backspace (word delete)
    if (event->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (isPromptEditor(obj) && ke->key() == Qt::Key_Backspace && (ke->modifiers() & Qt::ControlModifier)) {
            ke->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (isPromptEditor(obj) && (ke->modifiers() & Qt::ControlModifier) && ke->key() == Qt::Key_Space) {
            showPromptTagCompletion(static_cast<QPlainTextEdit *>(obj));
            return true;
        }
    }
    // §13.196: Ctrl+click Generate → replace queue for one job (upstream generate_replace).
    if (obj == m_d->generate.btnGenerate && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::ControlModifier)) {
            slotGenerateReplace();
            return true;
        }
    }
    if (isPositivePromptEditor(obj) && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ShiftModifier)) {
            slotGenerate();
            return true;
        }
    }
    // §8.5 / §13.35 / §13.201: Ctrl+Up / Ctrl+Down — attention weight in positive and negative prompt fields
    if (isPromptEditor(obj) && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) && (ke->modifiers() & Qt::ControlModifier)) {
            auto *edit = static_cast<QPlainTextEdit *>(obj);
            QString text = edit->toPlainText();
            int cursorPos = edit->textCursor().position();
            auto range = ComfyUIUtils::attentionSegmentRange(text, cursorPos);
            if (range.first >= 0 && range.second > 0) {
                QString segment = text.mid(range.first, range.second);
                double delta = (ke->key() == Qt::Key_Up) ? 0.1 : -0.1;
                QString newSegment = ComfyUIUtils::editAttentionWeight(segment, delta);
                if (newSegment != segment) {
                    QTextCursor cur = edit->textCursor();
                    cur.setPosition(range.first);
                    cur.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
                    cur.insertText(newSegment);
                }
                return true;
            }
        }
    }
    return QDockWidget::eventFilter(obj, event);
}
void ComfyUIRemoteDock::setViewManager(KisViewManager *viewManager)
{
    m_d->connections.clear();
    m_d->viewManager = viewManager;
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->setViewManager(viewManager);
    if (!viewManager) {
        if (m_d->documentSyncPoller)
            m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        return;
    }
    // §10.1 / §13.151: action IDs and display strings match Python ai_diffusion.action (activation flags in XML)
    KisActionManager *am = viewManager->actionManager();
    auto reg = [this, am](const QString &id, void (ComfyUIRemoteDock::*slot)()) {
        KisAction *a = am->createAction(id);
        m_d->connections.addConnection(a, &KisAction::triggered, this, slot);
    };
    reg(QStringLiteral("ai_diffusion_settings"), &ComfyUIRemoteDock::slotConfigureHelp);
    reg(QStringLiteral("ai_diffusion_generate"), &ComfyUIRemoteDock::slotAiDiffusionGenerateAction);
    reg(QStringLiteral("ai_diffusion_cancel"), &ComfyUIRemoteDock::slotAiDiffusionCancelCurrent);
    reg(QStringLiteral("ai_diffusion_cancel_queued"), &ComfyUIRemoteDock::slotAiDiffusionCancelQueued);
    reg(QStringLiteral("ai_diffusion_cancel_all"), &ComfyUIRemoteDock::slotAiDiffusionCancelAll);
    reg(QStringLiteral("ai_diffusion_toggle_preview"), &ComfyUIRemoteDock::slotAiDiffusionTogglePreview);
    reg(QStringLiteral("ai_diffusion_apply"), &ComfyUIRemoteDock::slotAiDiffusionApply);
    reg(QStringLiteral("ai_diffusion_apply_alternative"), &ComfyUIRemoteDock::slotAiDiffusionApplyAlternative);
    reg(QStringLiteral("ai_diffusion_create_region"), &ComfyUIRemoteDock::slotAiDiffusionCreateRegion);
    reg(QStringLiteral("ai_diffusion_switch_workspace_generation"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceGeneration);
    reg(QStringLiteral("ai_diffusion_switch_workspace_upscaling"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceUpscaling);
    reg(QStringLiteral("ai_diffusion_switch_workspace_live"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceLive);
    reg(QStringLiteral("ai_diffusion_switch_workspace_graph"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceGraph);
    reg(QStringLiteral("ai_diffusion_toggle_workspace"), &ComfyUIRemoteDock::slotAiDiffusionToggleWorkspace);
    reg(QStringLiteral("ai_diffusion_toggle_edit_mode"), &ComfyUIRemoteDock::slotAiDiffusionToggleEditMode);
}

// §13.15 / §13.52: document_id annotation; when missing, create; when duplicate across open docs, assign new ID to current (copy handling).
static void ensureDocumentId(KisImageSP image, KisCanvas2 *canvas)
{
    if (!image)
        return;
    const QString key = ComfyUIUtils::documentIdAnnotationKey();
    KisDocument *currentDoc = (canvas && canvas->imageView()) ? canvas->imageView()->document() : nullptr;

    KisAnnotationSP ann = image->annotation(key);
    if (ann) {
        const QString existingId = QString::fromUtf8(ann->annotation());
        if (!existingId.isEmpty() && currentDoc) {
            const QList<QPointer<KisDocument>> docs = KisPart::instance()->documents();
            for (const QPointer<KisDocument> &doc : docs) {
                if (!doc || doc == currentDoc)
                    continue;
                KisImageSP otherImg = doc->image().toStrongRef();
                if (!otherImg)
                    continue;
                KisAnnotationSP otherAnn = otherImg->annotation(key);
                if (otherAnn && QString::fromUtf8(otherAnn->annotation()) == existingId) {
                    const QString newUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    image->removeAnnotation(key);
                    image->addAnnotation(KisAnnotationSP(new KisAnnotation(
                        key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("document_id")), newUuid.toUtf8())));
                    return;
                }
            }
        }
        return;
    }

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    image->addAnnotation(KisAnnotationSP(new KisAnnotation(
        key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("document_id")), uuid.toUtf8())));
}

void ComfyUIRemoteDock::setCanvas(KoCanvasBase *canvas)
{
    KisCanvas2 *c = dynamic_cast<KisCanvas2 *>(canvas);
    m_d->canvas = c;
    setEnabled(canvas != nullptr);
    if (c) {
        KisImageSP img = c->image().toStrongRef();
        if (img) {
            m_d->documentPollInitialized = false;
            if (m_d->documentSyncPoller)
                m_d->documentSyncPoller->start();
            ensureDocumentId(img, c);
            QString docIdWarn;
            if (KisAnnotationSP idAnn = img->annotation(ComfyUIUtils::documentIdAnnotationKey())) {
                docIdWarn = QString::fromUtf8(idAnn->annotation()).trimmed();
            }
            QString docPathLabel;
            if (KisDocument *doc = c->imageView() ? c->imageView()->document() : nullptr)
                docPathLabel = doc->path();
            if (docPathLabel.isEmpty())
                docPathLabel = ComfyTr::tr("(unsaved document)");
            const ComfyUIUtils::DocumentUiJsonLoadOutcome uiMeta = ComfyUIUtils::loadDocumentUiJsonWithMeta(img);
            // §13.140: Persistence load failure — warning; continue with empty/default embedded state
            const QString parseFailDedupeKey =
                !docIdWarn.isEmpty() ? docIdWarn
                                     : (QStringLiteral("img:") + QString::number(reinterpret_cast<quintptr>(img.data())));
            if (uiMeta.parseFailed && !m_d->warnedUiJsonParseFailDocIds.contains(parseFailDedupeKey)) {
                m_d->warnedUiJsonParseFailDocIds.insert(parseFailDedupeKey);
                QMessageBox::warning(this, ComfyTr::tr("AI Diffusion Plugin"),
                                     ComfyTr::tr("Failed to load state from %1: %2", docPathLabel, uiMeta.parseError));
            }
            // §13.199: future ui.json version → defaults; warn once per document
            if (uiMeta.resetToDefaultsDueToFutureVersion && !docIdWarn.isEmpty()
                && !m_d->warnedFutureUiJsonVersionDocIds.contains(docIdWarn)) {
                m_d->warnedFutureUiJsonVersionDocIds.insert(docIdWarn);
                QMessageBox::warning(
                    this,
                    ComfyTr::trc("@title:window", "AI Image Generation data"),
                    ComfyTr::tr(
                        "This document's embedded AI data (format version %1) is newer than this Krita build supports (version %2). "
                        "Embedded plugin state was reset to defaults; saving the document may discard fields this build does not understand.",
                        uiMeta.rawVersionFromFile,
                        ComfyUIUtils::persistenceFormatVersion));
            }
            tryApplyDocumentDefaultsForNewDocument(img);
            loadRegionsPersistedForDocument(img);
            applyModelFieldsFromUiJson(uiMeta.object);
            // §13.44 / §13.189: preview_layer from annotation or ui.json
            const QString previewKey = ComfyUIUtils::previewLayerAnnotationKey();
            KisAnnotationSP previewAnn = img->annotation(previewKey);
            m_d->previewLayerId = previewAnn ? QString::fromUtf8(previewAnn->annotation()).trimmed() : QString();
            if (m_d->previewLayerId.isEmpty()) {
                m_d->previewLayerId = uiMeta.object.value(QStringLiteral("preview_layer")).toString().trimmed();
            }
            // §13.149 / §13.189: Live strength from annotation or ui.json
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2 && m_d->generate.spinStrength) {
                const QString liveKey = ComfyUIUtils::liveWorkspaceAnnotationKey();
                QJsonObject liveObj;
                if (KisAnnotationSP liveAnn = img->annotation(liveKey)) {
                    if (!liveAnn->annotation().isEmpty())
                        liveObj = QJsonDocument::fromJson(liveAnn->annotation()).object();
                }
                if (liveObj.isEmpty()) {
                    liveObj = uiMeta.object.value(QStringLiteral("live")).toObject();
                }
                int strengthPct = 30;
                if (!liveObj.isEmpty()) {
                    strengthPct =
                        qBound(1, qRound(liveObj.value(QStringLiteral("strength")).toDouble(0.3) * 100.0), 100);
                }
                QSignalBlocker bSpin(m_d->generate.spinStrength);
                m_d->generate.spinStrength->setValue(strengthPct);
                if (m_d->inpaint.sliderStrength) {
                    QSignalBlocker bSlider(m_d->inpaint.sliderStrength);
                    m_d->inpaint.sliderStrength->setValue(strengthPct);
                }
            }
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0) {
                loadInpaintWorkspaceFromDocument();
                loadEmbeddedCustomWorkflowFromDocument();
            }
            loadDocumentHistoryFromAnnotations();
            loadAnimationWorkspaceFromDocument();
            tryBindPreviewLayerFromDocument();
            refreshInpaintContextLayers();
            updateGenerateOptions();
        } else {
            if (m_d->documentSyncPoller)
                m_d->documentSyncPoller->stop();
            m_d->documentPollInitialized = false;
            m_d->previewLayerId.clear();
            m_d->history.historyEntries.clear();
            if (m_d->history.listHistory)
                m_d->history.listHistory->clear();
            updateAnimationResultPreview(QString());
        }
        // §13.4: Prompt import from image file — when document is opened from PNG/JPG/WebP and prompts are empty, fill from A1111 parameters
        KisDocument *doc = c->imageView() ? c->imageView()->document() : nullptr;
        if (doc && m_d->generate.editPrompt && m_d->generate.editNegative) {
            QString path = doc->path();
            if (!path.isEmpty()) {
                const QString lower = path.toLower();
                if (lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".jpg")) || lower.endsWith(QLatin1String(".jpeg")) || lower.endsWith(QLatin1String(".webp"))) {
                    if (m_d->generate.editPrompt->toPlainText().trimmed().isEmpty() && m_d->generate.editNegative->toPlainText().trimmed().isEmpty()) {
                        QPair<QString, QString> prompts = ComfyUIUtils::readPromptFromImageFile(path);
                        if (!prompts.first.isEmpty() || !prompts.second.isEmpty()) {
                            m_d->generate.editPrompt->setPlainText(prompts.first);
                            m_d->generate.editNegative->setPlainText(prompts.second);
                        }
                    }
                }
            }
        }
    } else {
        if (m_d->documentSyncPoller)
            m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        m_d->previewLayerId.clear();
        m_d->history.historyEntries.clear();
        if (m_d->history.listHistory)
            m_d->history.listHistory->clear();
        updateAnimationResultPreview(QString());
    }
    updateWelcomeVisibility();
    updateHistoryUsageLabel();
}
void ComfyUIRemoteDock::unsetCanvas()
{
    setCanvas(nullptr);
}
void ComfyUIRemoteDock::setProgressBarKind(bool isUpload)
{
    if (!m_d->progressBar)
        return;
    ComfyUiStyle::applyProgressBar(m_d->progressBar, isUpload);
}

namespace {

constexpr int kJobProgressScale = 1000;

void applyJobProgressToBar(QProgressBar *bar, double jobProgressFraction)
{
    if (!bar)
        return;
    if (jobProgressFraction >= 0.0) {
        bar->setValue(qBound(0, int(jobProgressFraction * kJobProgressScale), kJobProgressScale));
    } else {
        int v = bar->value();
        if (v >= 100)
            bar->setValue(0);
        bar->setValue(qMin(99, v + 2));
    }
}

} // namespace

void ComfyUIRemoteDock::beginJobProgress()
{
    m_d->jobProgressFraction = -1.0;
    if (!m_d->progressBar)
        return;
    m_d->progressBar->setMaximum(kJobProgressScale);
    m_d->progressBar->setValue(0);
}

void ComfyUIRemoteDock::setJobProgressFraction(double fraction)
{
    m_d->jobProgressFraction = qBound(0.0, fraction, 1.0);
    applyJobProgressToBar(m_d->progressBar, m_d->jobProgressFraction);
}

void ComfyUIRemoteDock::tickJobProgressBuffer()
{
    applyJobProgressToBar(m_d->progressBar, m_d->jobProgressFraction);
}

void ComfyUIRemoteDock::finishJobProgress()
{
    m_d->jobProgressFraction = 1.0;
    if (m_d->progressBar)
        m_d->progressBar->setValue(kJobProgressScale);
}

void ComfyUIRemoteDock::resetProgressBarToIdle()
{
    m_d->jobProgressFraction = -1.0;
    if (!m_d->progressBar)
        return;
    m_d->progressBar->setMaximum(kJobProgressScale);
    m_d->progressBar->setValue(m_d->progressBar->maximum());
}

#ifdef COMFYUI_ENABLE_TEST_HOOKS
ComfyUIRemoteDock::LayoutTestAccess ComfyUIRemoteDock::layoutTestAccess() const
{
    LayoutTestAccess access;
    access.progressBar = m_d->progressBar;
    access.historyGroup = m_d->history.histGroupBox;
    access.generateChrome = m_d->generate.genGroupBox;
    access.contentPage = m_d->history.histGroupBox ? m_d->history.histGroupBox->parentWidget() : nullptr;
    if (!access.contentPage && m_d->progressBar)
        access.contentPage = m_d->progressBar->parentWidget();
    if (access.contentPage) {
        for (QObject *child : access.contentPage->children()) {
            if (auto *scroll = qobject_cast<QScrollArea *>(child)) {
                access.generateScroll = scroll;
                break;
            }
        }
    }
    return access;
}

void *ComfyUIRemoteDock::testDockPrivate() const
{
    return m_d.data();
}
#endif

void ComfyUIRemoteDock::setLiveProgress(int percent)
{
    setLiveSpinnerProgress(m_d->live.liveSpinner, percent);
}
void ComfyUIRemoteDock::startLiveSpinner()
{
    startLiveSpinnerWidget(m_d->live.liveSpinner);
}
void ComfyUIRemoteDock::stopLiveSpinner()
{
    stopLiveSpinnerWidget(m_d->live.liveSpinner);
}
void ComfyUIRemoteDock::setStatusMessage(const QString &msg, bool isError, bool isWarning)
{
    // FAITHFUL_PORT: every status change goes to logcat too so silent early
    // returns in slotGenerate() / slotInpaint() / etc. are visible without
    // having to see the on-dock label (which can scroll out of view on tablet).
    if (isError)
        qCWarning(KIS_COMFYUI_REMOTE) << "STATUS[ERROR]" << msg;
    else if (isWarning)
        qCWarning(KIS_COMFYUI_REMOTE) << "STATUS[WARN]" << msg;
    else
        qCWarning(KIS_COMFYUI_REMOTE) << "STATUS" << msg;
    if (!m_d->labelStatus) return;
    if (isError || isWarning)
        m_d->labelStatus->setVisible(true);
    m_d->labelStatus->setText(msg);
    // §13.27: theme colors — red for error, yellow for warning
    if (isError) {
        ComfyUiStyle::styleStatusLabel(m_d->labelStatus, ComfyUiStyle::StatusTone::Error);
        // FAITHFUL_PORT: on Android the dock's status label is at the bottom
        // of a scroll area and frequently sits below the visible viewport, so
        // error-class messages were invisible — the user would click Generate,
        // hit a silent early-return path, and see "nothing happen". Scroll the
        // status label into view AND show a transient tooltip at the cursor so
        // the user gets an immediate visual signal regardless of scroll state.
        if (m_d->labelStatus->parentWidget()) {
            if (QScrollArea *sa = m_d->labelStatus->parentWidget()->findChild<QScrollArea *>())
                sa->ensureWidgetVisible(m_d->labelStatus);
        }
        QToolTip::showText(QCursor::pos(), msg, m_d->labelStatus, QRect(), 4000);
    } else if (isWarning) {
        ComfyUiStyle::styleStatusLabel(m_d->labelStatus, ComfyUiStyle::StatusTone::Warning);
    } else {
        ComfyUiStyle::resetLabelStyle(m_d->labelStatus);
    }
}
void ComfyUIRemoteDock::startPolling()
{
    setProgressBarKind(false);  // §13.18: generation progress
    beginJobProgress();
    setStatusMessage(ComfyTr::tr("Generating… %1", m_d->pollCount));
    m_d->pollTimer->start(1000);
}
void ComfyUIRemoteDock::updateQueueStatus()
{
    int running = 0;
    if (!m_d->currentPromptId.isEmpty())
        ++running;
    if (!m_d->upscaleRt.upscalePromptId.isEmpty())
        ++running;
    if (!m_d->inpaintRt.inpaintPromptId.isEmpty())
        ++running;
    if (!m_d->liveRt.livePromptId.isEmpty())
        ++running;
    const int queued = m_d->jobQueue.size();
    const int total = running + queued;
    m_d->generate.labelQueueCount->setText(ComfyTr::tr("Queue: %1", total));
    if (m_d->generate.btnQueuePopup) {
        const bool animWorkspace = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3;
        QString tip;
        if (total > 0) {
            tip = queued > 0
                ? ComfyTr::tr("Generating image. %1 jobs queued. Click to adjust queue or cancel.", queued)
                : ComfyTr::tr("Generating image. Click to adjust queue or cancel.");
            m_d->generate.btnQueuePopup->setDisplayState(ComfyQueueButton::DisplayState::Active, total, tip);
        } else {
            tip = animWorkspace
                ? ComfyTr::tr("Idle. Click to adjust seed or cancel jobs (Animation has no batch enqueue options).")
                : ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs.");
            m_d->generate.btnQueuePopup->setDisplayState(ComfyQueueButton::DisplayState::Inactive, 0, tip);
        }
    }
    if (running + queued > 0) {
        if (queued > 0) {
            setStatusMessage(ComfyTr::tr("Queue: 1 running, %1 queued.", queued));
        } else {
            setStatusMessage(ComfyTr::tr("Generating… %1", m_d->pollCount));
        }
    } else {
        setStatusMessage(ComfyTr::tr("Ready."));
    }
    m_d->generate.btnCancelQueue->setEnabled(running + queued > 0);
}

void ComfyUIRemoteDock::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    updateQueueStatus();
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        syncCompactGenerateLayoutRows(ws == 0 || ws == 1 || ws == 2);
        QTimer::singleShot(0, this, [this]() {
            if (!m_d || !m_d->comboWorkspace)
                return;
            const int ws = m_d->comboWorkspace->currentIndex();
            syncCompactGenerateLayoutRows(ws == 0 || ws == 1 || ws == 2);
        });
    }
}

void ComfyUIRemoteDock::resizeEvent(QResizeEvent *event)
{
    QDockWidget::resizeEvent(event);
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        syncCompactGenerateLayoutRows(ws == 0 || ws == 1 || ws == 2);
    }
}
