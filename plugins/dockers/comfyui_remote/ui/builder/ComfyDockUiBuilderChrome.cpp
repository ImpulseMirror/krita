/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilder.h"
#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyWorkspaceSelectButton.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyHistoryListWidget.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KSharedConfig>
#include <KConfigGroup>

#include <kis_annotation.h>
#include <kis_types.h>

using ComfyDockShellInternal::ComfyPromptPlainTextEdit;
using ComfyDockShellInternal::LiveSpinnerWidget;
using ComfyDockShellInternal::StrengthSpinBox;
using ComfyDockShellInternal::setComboCurrentItemData;



namespace ComfyDockUiBuilder {

void buildSharedChrome(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    shell.contentPage = new QWidget(shell.rootWidget);
    shell.contentLayout = new QVBoxLayout(shell.contentPage);
    shell.contentLayout->setContentsMargins(0, 0, 0, 0);
    shell.contentLayout->setSpacing(0);
    shell.scroll = new QScrollArea();
    shell.scroll->setWidgetResizable(true);
    shell.scroll->setFrameShape(QFrame::NoFrame);
    shell.scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shell.scrollContent = new QWidget();
    shell.scrollLayout = new QVBoxLayout(shell.scrollContent);
    shell.scrollLayout->setContentsMargins(0, 0, 0, 0);
    shell.scrollLayout->setSpacing(0);

    QGroupBox *connGroup = new QGroupBox(ComfyTr::tr("Connection"));
    QVBoxLayout *connLayout = new QVBoxLayout(connGroup);
    d->editServerUrl = new QLineEdit();
    d->editServerUrl->setText(ComfyUIUtils::savedServerUrl());
    d->editServerUrl->setPlaceholderText(ComfyTr::tr("e.g. 127.0.0.1:8188"));
    d->editServerUrl->setClearButtonEnabled(true);
    QObject::connect(d->editServerUrl, &QLineEdit::editingFinished, dock, [dock, d]() {
        QString url = d->editServerUrl->text().trimmed();
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ServerUrl", url);
        // §3.1: Persist to settings.json
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        settings.insert(QStringLiteral("server_url"), url);
        ComfyUIUtils::saveSettingsJson(settings);
    });

    d->generate.comboCheckpoint = new QComboBox();
    d->generate.comboCheckpoint->setEditable(true);
    d->generate.comboCheckpoint->setInsertPolicy(QComboBox::NoInsert);
    d->generate.comboCheckpoint->addItem("v1-5-pruned-emaonly.safetensors");
    d->generate.btnRefreshCheckpoints = new QPushButton(ComfyTr::tr("Refresh"));
    d->generate.btnRefreshCheckpoints->setToolTip(ComfyTr::tr("Load checkpoint list from server"));
    QObject::connect(d->generate.btnRefreshCheckpoints, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotRefreshCheckpoints);
    QObject::connect(d->generate.comboCheckpoint, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        dock->schedulePersistDocumentDefaults();
    });
    if (d->generate.comboCheckpoint->lineEdit()) {
        QObject::connect(d->generate.comboCheckpoint->lineEdit(), &QLineEdit::editingFinished, dock, [dock, d]() {
            dock->schedulePersistDocumentDefaults();
        });
    }

    d->generate.comboPreset = new QComboBox();
    dock->rebuildPresetComboItems();
    QObject::connect(d->generate.comboPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, &ComfyUIRemoteDock::slotPresetChanged);
    d->upscale.comboUpscaleModel = new QComboBox();
    d->upscale.comboUpscaleModel->setVisible(false);
    dock->refreshUpscaleModelCombo();
    QObject::connect(d->upscale.comboUpscaleModel, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        if (d->upscale.comboUpscaleModel)
            d->upscaleRt.upscalerModel = d->upscale.comboUpscaleModel->currentData().toString();
        if (d->canvas && d->canvas->image())
            dock->scheduleDocumentUiJsonSave();
    });
    d->generate.btnSaveAsPreset = new QPushButton(ComfyTr::tr("Save as preset"));
    d->generate.btnDeletePreset = new QPushButton(ComfyTr::tr("Delete preset"));
    QObject::connect(d->generate.btnSaveAsPreset, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotSaveAsPreset);
    QObject::connect(d->generate.btnDeletePreset, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotDeletePreset);
    // §13.34: SamplingQuality (fast/quality) — affects steps; animation uses same spinSteps via slotGenerate
    d->generate.comboQuality = new QComboBox();
    d->generate.comboQuality->addItem(ComfyTr::tr("Fast"));
    d->generate.comboQuality->addItem(ComfyTr::tr("Quality"));
    d->generate.comboQuality->setCurrentIndex(1);
    d->generate.comboQuality->setToolTip(ComfyTr::tr("Fast: fewer steps, quicker results. Quality: more steps, better details."));
    QObject::connect(d->generate.comboQuality, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int idx) {
        if (!d->generate.spinSteps) return;
        if (idx == 0) { // Fast
            int v = d->generate.spinSteps->value();
            d->generate.spinSteps->setValue(qMax(1, v / 2));
        } else { // Quality
            if (d->generate.spinSteps->value() < 20)
                d->generate.spinSteps->setValue(20);
        }
        if (d->canvas && d->canvas->image())
            dock->scheduleDocumentUiJsonSave();
    });

    QHBoxLayout *presetRow = new QHBoxLayout();
    presetRow->addWidget(d->generate.btnSaveAsPreset);
    presetRow->addWidget(d->generate.btnDeletePreset);
    presetRow->addStretch();
    connLayout->addLayout(presetRow);
    d->generate.btnDeletePreset->setEnabled(false);

    // Widgets for advanced configuration (shown in settings dialog instead of main dock)
    d->live.checkUseReferenceImage = new QCheckBox(ComfyTr::tr("Use current layer as reference (replace REFERENCE_IMAGE in workflow)"));
    d->live.checkUseReferenceImage->setToolTip(ComfyTr::tr("Export current layer, upload to server, and replace REFERENCE_IMAGE in your workflow JSON with the uploaded filename."));
    d->editCustomWorkflow = new QPlainTextEdit();
    d->editCustomWorkflow->setPlaceholderText(
        ComfyTr::tr("Paste ComfyUI workflow: API export (File → Export), or saved UI JSON (nodes/links) after connecting to the server."));
    d->editCustomWorkflow->setMaximumHeight(80);
    d->customWorkflowParamsGroup = new QGroupBox(ComfyTr::tr("Workflow parameters (ETN)"));
    d->customWorkflowParamsForm = new QFormLayout(d->customWorkflowParamsGroup);
    d->customWorkflowParamsGroup->setLayout(d->customWorkflowParamsForm);
    d->customWorkflowParamsGroup->setVisible(false);
    d->customWorkflowParamsGroup->setParent(nullptr);
    d->customWorkflowDocumentSaveTimer = new QTimer(dock);
    d->customWorkflowDocumentSaveTimer->setSingleShot(true);
    QObject::connect(d->customWorkflowDocumentSaveTimer, &QTimer::timeout, dock, [dock, d]() {
        dock->saveEmbeddedCustomWorkflowToDocument();
    });
    QObject::connect(d->editCustomWorkflow, &QPlainTextEdit::textChanged, dock, &ComfyUIRemoteDock::scheduleSaveEmbeddedCustomWorkflowToDocument);
    d->customWorkflowParamsRefreshTimer = new QTimer(dock);
    d->customWorkflowParamsRefreshTimer->setSingleShot(true);
    QObject::connect(d->customWorkflowParamsRefreshTimer, &QTimer::timeout, dock, &ComfyUIRemoteDock::refreshCustomWorkflowParameterPanel);
    QObject::connect(d->editCustomWorkflow, &QPlainTextEdit::textChanged, dock, [dock, d]() {
        if (d->customWorkflowParamsRefreshTimer)
            d->customWorkflowParamsRefreshTimer->start(450);
    });

    // Open settings dialog (connection + workflow) instead of exposing config directly
    QPushButton *btnSettings = new QPushButton(ComfyTr::tr("Settings…"));
    btnSettings->setIcon(ComfyTheme::icon(QStringLiteral("settings")));
    QObject::connect(btnSettings, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotConfigureHelp);
    connLayout->addWidget(btnSettings);
    connGroup->setParent(shell.rootWidget);
    connGroup->hide();
    connGroup->setFixedHeight(0);

    // FAITHFUL_PORT: flatten the Generate groupbox into the scroll column so the
    // visible UI matches upstream — no "Generate" title bar / frame around the
    // prompt + strength + Generate button stack.
    shell.genGroup = new QGroupBox();
    d->generate.genGroupBox = shell.genGroup;
    shell.genGroup->setFlat(true);
    shell.genGroup->setStyleSheet(QStringLiteral("QGroupBox{border:0;margin:0;padding:0;}"));
    shell.genLayout = new QVBoxLayout(shell.genGroup);
    shell.genLayout->setContentsMargins(0, 0, 0, 0);
    shell.genLayout->setSpacing(0);

    // §5.3 Workspace selector: Generate (sparkle/magic icon), Upscale, Live, Animation, Graph; order and labels per spec
    d->comboWorkspace = new ComfyWorkspaceSelectButton(shell.genGroup);
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        int storedWorkspace = cfg.readEntry("WorkspaceIndex", 0);
        if (storedWorkspace < 0 || storedWorkspace >= d->comboWorkspace->count()) {
            storedWorkspace = 0;
        }
        d->comboWorkspace->setCurrentIndex(storedWorkspace);
        d->lastWorkspaceIndex = storedWorkspace;
    }
    QObject::connect(d->comboWorkspace, &ComfyWorkspaceSelectButton::currentIndexChanged, dock, [dock, d](int idx) {
        // Toggle visibility of mode-specific controls
        if (!d->generate.genGroupBox) return;
        const bool isGenerate = (idx == 0);
        const bool isUpscale = (idx == 1);
        const bool isLive = (idx == 2);
        const bool isAnimation = (idx == 3);
        const bool isGraph = (idx == 4);

        // §13.149: LiveWorkspace persistence — save live strength when leaving Live, restore when entering
        KisImageSP img = d->canvas ? d->canvas->image().toStrongRef() : KisImageSP();
        const int prevWorkspace = d->lastWorkspaceIndex;
        const QString liveKey = ComfyUIUtils::liveWorkspaceAnnotationKey();
        if (d->lastWorkspaceIndex == 2 && img && d->generate.spinStrength) {
            QJsonObject o;
            o.insert(QStringLiteral("strength"), d->generate.spinStrength->value() / 100.0);
            img->removeAnnotation(liveKey);
            img->addAnnotation(KisAnnotationSP(new KisAnnotation(liveKey, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("live")), QJsonDocument(o).toJson(QJsonDocument::Compact))));
            dock->scheduleDocumentUiJsonSave();
        }
        // §13.169: CustomInpaint — save inpaint UI to document when leaving Generate
        if (prevWorkspace == 0 && idx != 0 && img)
            dock->saveInpaintWorkspaceToDocument();
        if (idx == 2 && img && d->generate.spinStrength) {
            KisAnnotationSP liveAnn = img->annotation(liveKey);
            if (liveAnn && !liveAnn->annotation().isEmpty()) {
                QJsonObject o = QJsonDocument::fromJson(liveAnn->annotation()).object();
                double s = o.value(QStringLiteral("strength")).toDouble(0.75);
                d->generate.spinStrength->setValue(qBound(1, qRound(s * 100.0), 100));
            }
        }
        d->lastWorkspaceIndex = idx;
        if (idx == 0 && img)
            dock->loadInpaintWorkspaceFromDocument();

        if (d->generate.btnGenerate) d->generate.btnGenerate->setVisible(isGenerate || isGraph);
        // FAITHFUL_PORT: inpaint mode / fill / context / Seamless / Focus controls
        // map to upstream's `CustomInpaintWidget`, which is only visible in
        // Custom inpaint mode. In the compact view we keep them hidden; final
        // visibility is enforced by dock->applyInterfaceAppearanceSettings() below.
        if (d->inpaint.btnInpaint) d->inpaint.btnInpaint->setVisible(false);
        if (d->inpaint.comboInpaintMode) d->inpaint.comboInpaintMode->setVisible(false);
        if (d->inpaint.comboFillMode) d->inpaint.comboFillMode->setVisible(false);
        if (d->inpaint.comboInpaintContext) d->inpaint.comboInpaintContext->setVisible(false);
        if (d->inpaint.checkInpaintUseModel) d->inpaint.checkInpaintUseModel->setVisible(false);
        if (d->inpaint.checkInpaintUsePromptFocus) d->inpaint.checkInpaintUsePromptFocus->setVisible(false);
        if (d->upscale.btnUpscale) d->upscale.btnUpscale->setVisible(isUpscale);
        if (d->upscale.upscaleActionRowWidget) d->upscale.upscaleActionRowWidget->setVisible(isUpscale);
        if (d->generate.comboPreset) d->generate.comboPreset->setVisible(isGenerate || isLive || isAnimation || isGraph);
        if (d->upscale.comboUpscaleModel) d->upscale.comboUpscaleModel->setVisible(isUpscale);
        if (isUpscale && d->upscale.upscaleActionRowWidget && d->generate.btnQueuePopup) {
            if (auto *row = qobject_cast<QHBoxLayout *>(d->upscale.upscaleActionRowWidget->layout()))
                row->addWidget(d->generate.btnQueuePopup);
            if (d->upscale.btnUpscale && d->generate.btnQueuePopup) {
                d->generate.btnQueuePopup->setFixedHeight(qMax(28, d->upscale.btnUpscale->sizeHint().height() - 2));
                d->generate.btnQueuePopup->setMinimumWidth(d->generate.btnQueuePopup->sizeHint().width());
            }
            dock->updateQueueStatus();
        } else if (d->generate.generateActionRowWidget && d->generate.btnQueuePopup) {
            if (auto *row = qobject_cast<QHBoxLayout *>(d->generate.generateActionRowWidget->layout()))
                row->addWidget(d->generate.btnQueuePopup);
            if (d->generate.btnGenerate && d->generate.btnQueuePopup)
                d->generate.btnQueuePopup->setFixedHeight(qMax(28, d->generate.btnGenerate->sizeHint().height() - 2));
        }
        if (d->generate.btnGenerateAnimation) d->generate.btnGenerateAnimation->setVisible(isAnimation);
        if (d->animFramesRowWidget) d->animFramesRowWidget->setVisible(isAnimation);
        if (d->live.checkLiveMode) d->live.checkLiveMode->setVisible(isLive);
        if (d->live.checkLiveRecord) d->live.checkLiveRecord->setVisible(isLive);
        if (!isLive) dock->stopLiveSpinner();
        if (d->batchModeRow) d->batchModeRow->setVisible(isLive || isAnimation);
        if (isAnimation)
            dock->refreshAnimationTargetLayerCombo();
        dock->updateAnimationTargetLayerRowVisibility();
        if (d->btnImportAnimation) d->btnImportAnimation->setVisible(isLive || isAnimation);
        if (d->generate.regionsGroupBox) d->generate.regionsGroupBox->setVisible(false);
        if (d->generate.controlPreviewGroupBox) d->generate.controlPreviewGroupBox->setVisible(isGenerate);
        if (!isGenerate)
            dock->stopControlPreviewPolling();
        else {
            dock->syncControlPreviewRangeFromSettings();
            dock->syncPoseGuidePeopleCountFromSettings();
        }
        if (isGenerate)
            dock->refreshRegionsList();  // §13.125: show active region set when returning to Generate
        if (d->upscale.upscaleFactorRow) d->upscale.upscaleFactorRow->setVisible(isUpscale);
        if (d->upscale.upscaleRefineBlock) d->upscale.upscaleRefineBlock->setVisible(isUpscale);
        if (isUpscale) dock->updateUpscaleTargetSize();
        dock->updateAnimationButtonLabel();

        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("WorkspaceIndex", idx);
        if (d->generate.genContentContainer) d->generate.genContentContainer->setVisible(!isGraph);
        if (d->graphPlaceholderWidget) d->graphPlaceholderWidget->setVisible(isGraph);
        dock->reparentCustomWorkflowEditor(isGraph);
        if (d->history.histGroupBox) d->history.histGroupBox->setVisible(isGenerate || isGraph);
        if (d->generate.queueButtonRowWidget)
            d->generate.queueButtonRowWidget->setVisible(isGenerate || isAnimation || isGraph);
        if (d->upscale.upscaleActionRowWidget)
            d->upscale.upscaleActionRowWidget->setVisible(isUpscale);
        dock->refreshQueuePopupSupportsBatch();
        dock->updateQueueStatus();
        dock->applyInterfaceAppearanceSettings(); // §3.5: prompt_line_count_live when Live workspace
    });
    dock->updateAnimationButtonLabel();  // §5.7: initial label from persisted FullAnimation
    // FAITHFUL_PORT: top row mirrors python GenerationWidget — WorkspaceSelectWidget +
    // StyleSelectWidget side-by-side. `comboPreset` is the style/model picker
    // ("Nova XL ★" in upstream); without dock row it was never displayed. We
    // also tuck a small gear button on the right so the user can still reach
    // the Settings dialog after first-time setup (hiding the Connection group
    // removed the only previous entry point).
    {
        QHBoxLayout *topRow = new QHBoxLayout();
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->addWidget(d->comboWorkspace);
        topRow->addWidget(d->generate.comboPreset, 1);
        topRow->addWidget(d->upscale.comboUpscaleModel, 1);
        QToolButton *btnTopSettings = new QToolButton(shell.genGroup);
        btnTopSettings->setIcon(ComfyTheme::icon(QStringLiteral("settings")));
        btnTopSettings->setToolTip(ComfyTr::tr("Settings…"));
        btnTopSettings->setAutoRaise(true);
        QObject::connect(btnTopSettings, &QToolButton::clicked,
                dock, &ComfyUIRemoteDock::slotConfigureHelp);
        topRow->addWidget(btnTopSettings);
        shell.genLayout->insertLayout(0, topRow);
    }
}



} // namespace ComfyDockUiBuilder
