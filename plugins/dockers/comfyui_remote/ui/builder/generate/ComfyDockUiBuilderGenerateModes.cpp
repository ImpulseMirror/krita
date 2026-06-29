/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyLiveRunner.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIRemoteDockShellInternal.h"
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


namespace ComfyDockUiBuilderGenerateInternal {

void buildModeWorkspaceSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    d->upscale.btnUpscale = new QPushButton(ComfyTr::tr("Upscale"));
    d->upscale.btnUpscale->setToolTip(ComfyTr::tr(
        "Upscale the canvas at the scale factor above (ComfyUI ImageScale). With \"Refine upscaled image\" enabled, runs a diffusion pass after scaling."));
    QObject::connect(d->upscale.btnUpscale, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotUpscale);
    // btnUpscale is placed on upscaleActionRowWidget in buildUpscaleWidgetsSection (runs earlier).

    // §5.6 Live / §5.7 Animation: Full Animation / Single Frame radio (batch_mode); visible when workspace is Live or Animation
    d->batchModeRow = new QWidget(shell.genGroup);
    QHBoxLayout *batchModeLayout = new QHBoxLayout(d->batchModeRow);
    d->batchModeRow->setContentsMargins(0, 0, 0, 0);
    d->radioSingleFrame = new QRadioButton(ComfyTr::tr("Single Frame"), d->batchModeRow);
    d->radioFullAnimation = new QRadioButton(ComfyTr::tr("Full Animation"), d->batchModeRow);
    d->radioSingleFrame->setToolTip(ComfyTr::tr("Generate a single image at current time."));
    d->radioFullAnimation->setToolTip(ComfyTr::tr("Generate multiple frames (animation)."));
    d->batchModeGroup = new QButtonGroup(d->batchModeRow);
    d->batchModeGroup->addButton(d->radioSingleFrame);
    d->batchModeGroup->addButton(d->radioFullAnimation);
    bool fullAnimation = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("FullAnimation", false);
    d->radioFullAnimation->setChecked(fullAnimation);
    d->radioSingleFrame->setChecked(!fullAnimation);
    batchModeLayout->addWidget(d->radioSingleFrame);
    batchModeLayout->addWidget(d->radioFullAnimation);
    batchModeLayout->addStretch();
    genContentLayout->addWidget(d->batchModeRow);
    d->batchModeRow->setVisible(false);

    // §13.74: Single Frame — choose paint layer that receives output (persisted in ui.json animation.target_layer)
    d->animationTargetRow = new QWidget(shell.genGroup);
    QHBoxLayout *animTargetLayout = new QHBoxLayout(d->animationTargetRow);
    d->animationTargetRow->setContentsMargins(0, 0, 0, 0);
    // §5.7: dropdown lists each paint layer as "Target layer: {name}" (no separate label — text is per item)
    d->comboAnimationTargetLayer = new QComboBox(d->animationTargetRow);
    d->comboAnimationTargetLayer->setAccessibleName(ComfyTr::tr("Target layer"));
    d->comboAnimationTargetLayer->setToolTip(
        ComfyTr::tr("Paint layer that receives Single Frame generation output (Animation workspace)."));
    QObject::connect(d->comboAnimationTargetLayer, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        if (d->canvas && d->canvas->image())
            dock->scheduleDocumentUiJsonSave();
        if (d->animationPreviewRow && d->animationPreviewRow->isVisible() && d->animationPreviewDebounce) {
            d->animationPreviewDebounce->stop();
            d->animationPreviewDebounce->start();
        }
    });
    animTargetLayout->addWidget(d->comboAnimationTargetLayer, 1);
    genContentLayout->addWidget(d->animationTargetRow);
    d->animationTargetRow->setVisible(false);

    // §13.74: preview of last Single Frame result (Animation workspace)
    d->animationPreviewRow = new QWidget(shell.genGroup);
    QVBoxLayout *animPreviewLayout = new QVBoxLayout(d->animationPreviewRow);
    d->animationPreviewRow->setContentsMargins(0, 0, 0, 0);
    animPreviewLayout->addWidget(new QLabel(ComfyTr::tr("Frame preview:"), d->animationPreviewRow));
    d->labelAnimationPreview = new QLabel(d->animationPreviewRow);
    d->labelAnimationPreview->setAlignment(Qt::AlignCenter);
    d->labelAnimationPreview->setMinimumHeight(96);
    d->labelAnimationPreview->setMaximumHeight(220);
    d->labelAnimationPreview->setScaledContents(false);
    d->labelAnimationPreview->setFrameShape(QFrame::StyledPanel);
    d->labelAnimationPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    animPreviewLayout->addWidget(d->labelAnimationPreview);
    genContentLayout->addWidget(d->animationPreviewRow);
    d->animationPreviewRow->setVisible(false);

    QObject::connect(d->batchModeGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), dock, [dock, d](QAbstractButton *) {
        const bool full = d->radioFullAnimation && d->radioFullAnimation->isChecked();
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("FullAnimation", full);
        dock->updateAnimationButtonLabel();
        dock->updateAnimationTargetLayerRowVisibility();
        if (d->canvas && d->canvas->image())
            dock->scheduleDocumentUiJsonSave();
    });

    // FAITHFUL_PORT: wrap the Animation Frames row in a QWidget so the orphan
    // "Frames:" label hides as a unit alongside btnGenerateAnimation; the row
    // only appears in the Animation workspace.
    d->animFramesRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *animRow = new QHBoxLayout(d->animFramesRowWidget);
    animRow->setContentsMargins(0, 0, 0, 0);
    d->spinAnimationFrames = new QSpinBox(d->animFramesRowWidget);
    d->spinAnimationFrames->setRange(2, 16);
    d->spinAnimationFrames->setValue(4);
    d->spinAnimationFrames->setToolTip(ComfyTr::tr("Number of frames (seeds: seed, seed+1, …)"));
    d->generate.btnGenerateAnimation = new QPushButton(ComfyTr::tr("Generate animation"), d->animFramesRowWidget);
    d->generate.btnGenerateAnimation->setToolTip(ComfyTr::tr("Generate N images with sequential seeds as new layers."));
    QObject::connect(d->generate.btnGenerateAnimation, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotGenerateAnimation);
    animRow->addWidget(new QLabel(ComfyTr::tr("Frames:"), d->animFramesRowWidget));
    animRow->addWidget(d->spinAnimationFrames);
    animRow->addWidget(d->generate.btnGenerateAnimation);
    genContentLayout->addWidget(d->animFramesRowWidget);
    d->animFramesRowWidget->setVisible(false);

    // §13.45: Import Animation — import frames from .animation or .live-frames into document
    d->btnImportAnimation = new QPushButton(ComfyTr::tr("Import Animation"), shell.genGroup);
    d->btnImportAnimation->setToolTip(ComfyTr::tr("Import frame images from the document's .animation or .live-frames folder as keyframes."));
    QObject::connect(d->btnImportAnimation, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotImportAnimation);
    genContentLayout->addWidget(d->btnImportAnimation);
    d->btnImportAnimation->setVisible(false);

    d->live.checkLiveMode = new QCheckBox(ComfyTr::tr("Live (periodic img2img from canvas)"));
    d->live.checkLiveMode->setToolTip(ComfyTr::tr("Continuously watches the canvas and runs img2img when you paint or change prompts."));
    QObject::connect(d->live.checkLiveMode, &QCheckBox::toggled, dock, [dock, d](bool checked) {
        if (checked)
            ComfyLiveRunner::startLivePollLoop(dock);
        else {
            ComfyLiveRunner::stopLivePollLoop(dock);
            d->liveRt.livePollTimer->stop();
            dock->stopLiveSpinner();
        }
    });
    genContentLayout->addWidget(d->live.checkLiveMode);
    // FAITHFUL_PORT: Live-only checkbox; default-hide so it doesn't leak into the
    // Generate compact view (workspace lambda re-enables when switching to Live).
    d->live.checkLiveMode->setVisible(false);
    // §13.45: Record — save each live result to .live-frames/frame-N.webp for later Import Animation
    d->live.checkLiveRecord = new QCheckBox(ComfyTr::tr("Record (save frames to .live-frames)"));
    d->live.checkLiveRecord->setToolTip(ComfyTr::tr("When enabled, each live result is saved to the document's .live-frames folder as frame-N.webp. Use Import Animation to add them to the document."));
    QObject::connect(d->live.checkLiveRecord, &QCheckBox::toggled, dock, [dock, d](bool checked) {
        if (checked) {
            d->liveRt.liveFrameIndex = 0;
        } else {
            // §13.149: When recording stops, import_animation is called (frames from .live-frames)
            dock->slotImportAnimation();
        }
    });
    genContentLayout->addWidget(d->live.checkLiveRecord);
    // FAITHFUL_PORT: Record is Live-only too; default-hide.
    d->live.checkLiveRecord->setVisible(false);
    // §13.105: progress indicator — parented beside live preview in buildLiveSection().
    d->live.liveSpinner = new LiveSpinnerWidget(dock);
    d->live.liveSpinner->hide();

}

} // namespace ComfyDockUiBuilderGenerateInternal
