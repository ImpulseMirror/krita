/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilderGenerateInternal.h"

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


namespace ComfyDockUiBuilderGenerateInternal {

void buildQueueActionsSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    // Queue popup (similar to krita-ai Queue button)
    d->generate.labelQueueCount = new QLabel(ComfyTr::tr("Queue: 0"));
    d->generate.comboQueueMode = new QComboBox();
    d->generate.comboQueueMode->addItem(ComfyTr::tr("at the Back"), 0);
    d->generate.comboQueueMode->addItem(ComfyTr::tr("in Front (new jobs first)"), 1);
    d->generate.comboQueueMode->addItem(ComfyTr::tr("Replace Queue"), 2);
    d->generate.comboQueueMode->setToolTip(ComfyTr::tr("at the Back: add after current jobs. in Front: new jobs run first. Replace Queue: clear queue then add."));
    d->generate.spinBatchCount = new QSpinBox();
    d->generate.spinBatchCount->setRange(1, 10);
    d->generate.spinBatchCount->setToolTip(ComfyTr::tr("Number of images to generate per click"));
    d->generate.spinBatchCount->setValue(1);

    d->generate.btnQueuePopup = new ComfyQueueButton();
    d->generate.btnQueuePopup->setToolTip(ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));

    QMenu *queueMenu = new QMenu(d->generate.btnQueuePopup);
    QWidget *queueWidget = new QWidget(queueMenu);
    QVBoxLayout *queueLayout = new QVBoxLayout(queueWidget);
    queueLayout->setContentsMargins(8, 8, 8, 8);

    QHBoxLayout *countsLayout = new QHBoxLayout();
    countsLayout->addWidget(new QLabel(ComfyTr::tr("Jobs:"), queueWidget));
    countsLayout->addWidget(d->generate.labelQueueCount, 1);
    queueLayout->addLayout(countsLayout);

    d->generate.queueBatchOptionsRow = new QWidget(queueWidget);
    QHBoxLayout *batchLayout = new QHBoxLayout(d->generate.queueBatchOptionsRow);
    batchLayout->setContentsMargins(0, 0, 0, 0);
    batchLayout->addWidget(new QLabel(ComfyTr::tr("Batch:"), d->generate.queueBatchOptionsRow));
    batchLayout->addWidget(d->generate.spinBatchCount, 1);
    queueLayout->addWidget(d->generate.queueBatchOptionsRow);

    // Resolution multiplier (similar to krita-ai)
    d->generate.sliderResolutionMultiplier = new QSlider(Qt::Horizontal, queueWidget);
    d->generate.sliderResolutionMultiplier->setRange(3, 15); // §13.213: 3–15 → 0.3–1.5
    d->generate.labelResolutionMultiplier = new QLabel(queueWidget);
    d->generate.labelResolutionMultiplier->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int storedQueueMode = cfg.readEntry("QueueMode", 0);
        if (storedQueueMode >= 0 && storedQueueMode < d->generate.comboQueueMode->count()) {
            d->generate.comboQueueMode->setCurrentIndex(storedQueueMode);
        } else {
            d->generate.comboQueueMode->setCurrentIndex(0);
        }
        const double storedMul = cfg.readEntry("ResolutionMultiplier", 1.0);
        d->generate.resolutionMultiplier = storedMul <= 0.0 ? 1.0 : storedMul;
        int sliderValue = qRound(d->generate.resolutionMultiplier * 10.0);
        sliderValue = qBound(3, sliderValue, 15);
        d->generate.sliderResolutionMultiplier->setValue(sliderValue);
        d->generate.labelResolutionMultiplier->setText(QString::number(d->generate.resolutionMultiplier, 'f', 1) + QLatin1String("×"));
        const bool fixedSeed = cfg.readEntry("FixedSeed", false);
        const qint64 seedValue = cfg.readEntry("SeedValue", qint64(0));
        d->generate.checkFixedSeed->setChecked(fixedSeed);
        d->generate.spinSeed->setValue(static_cast<int>(seedValue));
    }
    QObject::connect(d->generate.sliderResolutionMultiplier, &QSlider::valueChanged, dock, [dock, d](int v) {
        d->generate.resolutionMultiplier = qMax(0.3, v / 10.0);
        d->generate.labelResolutionMultiplier->setText(QString::number(d->generate.resolutionMultiplier, 'f', 1) + QLatin1String("×"));
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ResolutionMultiplier", d->generate.resolutionMultiplier);
    });
    QObject::connect(d->generate.spinBatchCount, QOverload<int>::of(&QSpinBox::valueChanged), dock, [dock, d](int) {
        dock->schedulePersistDocumentDefaults();
    });
    dock->applyRecentlyUsedSyncFromSettings();
    QObject::connect(d->generate.comboQueueMode, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int index) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("QueueMode", index);
    });
    QObject::connect(d->generate.checkFixedSeed, &QCheckBox::toggled, dock, [dock, d](bool) {
        dock->persistSeedToConfig();
        dock->syncQueueSeedWidgetsFromMain();
    });
    QObject::connect(d->generate.spinSeed, QOverload<int>::of(&QSpinBox::valueChanged), dock, [dock, d](int) {
        dock->persistSeedToConfig();
        dock->syncQueueSeedWidgetsFromMain();
    });
    d->generate.queueResolutionRow = new QWidget(queueWidget);
    QHBoxLayout *resLayout = new QHBoxLayout(d->generate.queueResolutionRow);
    resLayout->setContentsMargins(0, 0, 0, 0);
    resLayout->addWidget(new QLabel(ComfyTr::tr("Resolution:"), d->generate.queueResolutionRow));
    resLayout->addWidget(d->generate.sliderResolutionMultiplier, 1);
    resLayout->addWidget(d->generate.labelResolutionMultiplier);
    queueLayout->addWidget(d->generate.queueResolutionRow);

    d->generate.queueCheckFixedSeed = new QCheckBox(ComfyTr::tr("Fixed seed"), queueWidget);
    d->generate.queueSpinSeed = new QSpinBox(queueWidget);
    d->generate.queueSpinSeed->setRange(0, 2147483647);
    d->generate.queueSpinSeed->setValue(d->generate.spinSeed->value());
    d->generate.queueCheckFixedSeed->setChecked(d->generate.checkFixedSeed->isChecked());
    d->generate.queueBtnRandomSeed = new QPushButton(queueWidget);
    d->generate.queueBtnRandomSeed->setIcon(
        ComfyTheme::icon(QStringLiteral("random")));
    d->generate.queueBtnRandomSeed->setToolTip(ComfyTr::tr("Pick a new random seed."));
    d->generate.queueBtnRandomSeed->setAccessibleName(ComfyTr::tr("Random seed"));
    QObject::connect(d->generate.queueBtnRandomSeed, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotRandomSeed);
    auto copyQueueSeedToMain = [dock, d]() {
        if (!d->generate.queueSpinSeed || !d->generate.spinSeed || !d->generate.checkFixedSeed || !d->generate.queueCheckFixedSeed)
            return;
        d->generate.checkFixedSeed->blockSignals(true);
        d->generate.spinSeed->blockSignals(true);
        d->generate.checkFixedSeed->setChecked(d->generate.queueCheckFixedSeed->isChecked());
        d->generate.spinSeed->setValue(d->generate.queueSpinSeed->value());
        d->generate.checkFixedSeed->blockSignals(false);
        d->generate.spinSeed->blockSignals(false);
        dock->persistSeedToConfig();
    };
    QObject::connect(d->generate.queueSpinSeed, QOverload<int>::of(&QSpinBox::valueChanged), dock, [copyQueueSeedToMain](int) {
        copyQueueSeedToMain();
    });
    QObject::connect(d->generate.queueCheckFixedSeed, &QCheckBox::toggled, dock, [copyQueueSeedToMain](bool) {
        copyQueueSeedToMain();
    });

    QHBoxLayout *seedLayout = new QHBoxLayout();
    seedLayout->addWidget(d->generate.queueCheckFixedSeed);
    seedLayout->addWidget(d->generate.queueSpinSeed, 1);
    seedLayout->addWidget(d->generate.queueBtnRandomSeed);
    queueLayout->addLayout(seedLayout);

    QObject::connect(queueMenu, &QMenu::aboutToShow, dock, [dock, d]() {
        dock->syncQueueSeedWidgetsFromMain();
        dock->refreshQueuePopupSupportsBatch();
    });

    d->generate.queueEnqueueModeRow = new QWidget(queueWidget);
    QHBoxLayout *modeLayout = new QHBoxLayout(d->generate.queueEnqueueModeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->addWidget(new QLabel(ComfyTr::tr("Enqueue:"), d->generate.queueEnqueueModeRow));
    modeLayout->addWidget(d->generate.comboQueueMode, 1);
    queueLayout->addWidget(d->generate.queueEnqueueModeRow);

    QPushButton *popupCancel = new QPushButton(ComfyTr::tr("Cancel all"), queueWidget);
    popupCancel->setIcon(ComfyTheme::icon(QStringLiteral("cancel")));
    popupCancel->setToolTip(ComfyTr::tr("Stop the running job and clear the queue (Cancel All)."));
    QObject::connect(popupCancel, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotCancelQueue);
    queueLayout->addWidget(popupCancel);

    QWidgetAction *queueAction = new QWidgetAction(queueMenu);
    queueAction->setDefaultWidget(queueWidget);
    queueMenu->addAction(queueAction);
    d->generate.btnQueuePopup->setMenu(queueMenu);

    dock->setupGenerateInpaintMenus();

    d->generate.generateActionRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *actionRow = new QHBoxLayout(d->generate.generateActionRowWidget);
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(4);

    QWidget *genCluster = new QWidget(d->generate.generateActionRowWidget);
    QHBoxLayout *genClusterLay = new QHBoxLayout(genCluster);
    genClusterLay->setContentsMargins(0, 0, 0, 0);
    genClusterLay->setSpacing(0);
    if (d->generate.btnGenerate)
        genClusterLay->addWidget(d->generate.btnGenerate);

    d->inpaint.btnInpaintMode = new QToolButton(genCluster);
    d->inpaint.btnInpaintMode->setArrowType(Qt::DownArrow);
    d->inpaint.btnInpaintMode->setAutoRaise(true);
    QObject::connect(d->inpaint.btnInpaintMode, &QToolButton::clicked, dock, &ComfyUIRemoteDock::showInpaintModeMenu);

    d->inpaint.btnRegionMask = new QToolButton(genCluster);
    d->inpaint.btnRegionMask->setCheckable(true);
    d->inpaint.btnRegionMask->setIcon(ComfyTheme::icon(QStringLiteral("region-alpha")));
    d->inpaint.btnRegionMask->setToolTip(
        ComfyTr::tr("Generate the active layer region only (use layer transparency as mask)"));
    if (d->generate.checkRegionOnly)
        d->inpaint.btnRegionMask->setChecked(d->generate.checkRegionOnly->isChecked());
    QObject::connect(d->inpaint.btnRegionMask, &QToolButton::toggled, d->generate.checkRegionOnly, &QCheckBox::setChecked);
    QObject::connect(d->generate.checkRegionOnly, &QCheckBox::toggled, d->inpaint.btnRegionMask, &QToolButton::setChecked);

    genClusterLay->addWidget(d->inpaint.btnInpaintMode);
    genClusterLay->addWidget(d->inpaint.btnRegionMask);
    actionRow->addWidget(genCluster, 1);
    actionRow->addWidget(d->generate.btnQueuePopup);
    if (d->generate.btnQueuePopup && d->generate.btnGenerate)
        d->generate.btnQueuePopup->setFixedHeight(qMax(28, d->generate.btnGenerate->sizeHint().height() - 2));
    genContentLayout->addWidget(d->generate.generateActionRowWidget);
    d->generate.queueButtonRowWidget = d->generate.generateActionRowWidget;

    d->generate.btnCancelQueue = popupCancel;
    d->generate.btnCancelQueue->setEnabled(false);

    d->progressBar = new QProgressBar();
    d->progressBar->setMinimum(0);
    d->progressBar->setMaximum(100);
    d->progressBar->setValue(0);
    d->progressBar->setTextVisible(false);
    d->progressBar->setFixedHeight(6);
    dock->setProgressBarKind(false);  // §13.18: default = generation
    genContentLayout->addWidget(d->progressBar);

    dock->setupRootControlLayersUi(d->generate.genContentContainer, genContentLayout);

}

} // namespace ComfyDockUiBuilderGenerateInternal
