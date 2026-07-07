/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyComboBox.h"
#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyFormUi.h"
#include "ComfySlider.h"
#include "ComfyUiStyle.h"

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
#include <QGridLayout>
#include <QSlider>
#include <QToolButton>
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
#include <QAbstractSlider>
#include "ComfySpinBox.h"
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

namespace {

QToolButton *makeQueueCancelButton(const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setText(text);
    button->setIcon(ComfyTheme::icon(QStringLiteral("cancel")));
    button->setEnabled(false);
    button->setAutoRaise(true);
    return button;
}

QLabel *makeQueueCountLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("0"), parent);
    label->setStyleSheet(
        QStringLiteral("color: %1; font-weight: bold;").arg(ComfyUiStyle::colors().highlight));
    return label;
}

} // namespace

namespace {

void ensureSeedSpinInQueueRow(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->generate.seedControlRow || !d->generate.spinSeed)
        return;
    if (d->generate.spinSeed->parentWidget() == d->generate.seedControlRow)
        return;
    if (auto *lay = qobject_cast<QHBoxLayout *>(d->generate.seedControlRow->layout())) {
        if (QWidget *oldParent = d->generate.spinSeed->parentWidget()) {
            if (QLayout *oldLay = oldParent->layout())
                oldLay->removeWidget(d->generate.spinSeed);
        }
        d->generate.spinSeed->setParent(d->generate.seedControlRow);
        d->generate.spinSeed->setPrefix(QString());
        if (lay->indexOf(d->generate.spinSeed) < 0)
            lay->insertWidget(1, d->generate.spinSeed, 1);
        d->generate.spinSeed->show();
    }
}

} // namespace

void buildQueueActionsSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    d->generate.comboQueueMode = new ComfyComboBox();
    d->generate.comboQueueMode->addItem(ComfyTr::tr("at the Back"), 0);
    d->generate.comboQueueMode->addItem(ComfyTr::tr("in Front (new jobs first)"), 1);
    d->generate.comboQueueMode->addItem(ComfyTr::tr("Replace Queue"), 2);
    d->generate.comboQueueMode->setToolTip(ComfyTr::tr("at the Back: add after current jobs. in Front: new jobs run first. Replace Queue: clear queue then add."));
    d->generate.spinBatchCount = new ComfySpinBox();
    d->generate.spinBatchCount->setRange(1, 10);
    d->generate.spinBatchCount->setToolTip(ComfyTr::tr("Number of jobs to enqueue at once"));
    d->generate.spinBatchCount->setValue(1);
    d->generate.spinBatchCount->hide();

    d->generate.btnQueuePopup = new ComfyQueueButton();
    d->generate.btnQueuePopup->setToolTip(ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));

    QMenu *queueMenu = new QMenu(d->generate.btnQueuePopup);
    QWidget *queueWidget = new QWidget(queueMenu);
    queueWidget->setObjectName(QStringLiteral("ComfyQueuePopup"));
    auto *grid = new QGridLayout(queueWidget);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    grid->addWidget(new QLabel(ComfyTr::tr("Jobs"), queueWidget), 0, 0);
    {
        auto *countsLayout = new QHBoxLayout();
        countsLayout->setContentsMargins(0, 0, 0, 0);
        countsLayout->addWidget(new QLabel(ComfyTr::tr("Document:"), queueWidget));
        d->generate.labelQueueDocumentCount = makeQueueCountLabel(queueWidget);
        countsLayout->addWidget(d->generate.labelQueueDocumentCount);
        countsLayout->addWidget(new QLabel(ComfyTr::tr("Total:"), queueWidget));
        d->generate.labelQueueTotalCount = makeQueueCountLabel(queueWidget);
        countsLayout->addWidget(d->generate.labelQueueTotalCount);
        countsLayout->addStretch();
        grid->addLayout(countsLayout, 0, 1);
    }

    auto *batchLabel = new QLabel(ComfyTr::tr("Batches"), queueWidget);
    d->generate.queueBatchLabel = batchLabel;
    grid->addWidget(batchLabel, 1, 0);
    auto *batchSliderWidget = new ComfySlider(1,
                                            10,
                                            QString::number(d->generate.spinBatchCount->value()),
                                            ComfySlider::Layout::Expanding,
                                            queueWidget);
    d->generate.sliderBatchCount = batchSliderWidget->slider();
    d->generate.labelBatchCount = batchSliderWidget->valueLabel();
    d->generate.sliderBatchCount->setSingleStep(1);
    d->generate.sliderBatchCount->setPageStep(1);
    d->generate.sliderBatchCount->setToolTip(ComfyTr::tr("Number of jobs to enqueue at once"));
    grid->addWidget(batchSliderWidget, 1, 1);
    d->generate.queueBatchOptionsRow = batchSliderWidget;

    auto *seedLabel = new QLabel(ComfyTr::tr("Seed"), queueWidget);
    grid->addWidget(seedLabel, 2, 0);
    if (d->generate.seedControlRow) {
        if (d->generate.checkFixedSeed)
            d->generate.checkFixedSeed->setText(ComfyTr::tr("Fixed"));
        grid->addWidget(d->generate.seedControlRow, 2, 1);
    }

    auto *resolutionSliderWidget = new ComfySlider(3,
                                                   15,
                                                   QStringLiteral("1.0 x"),
                                                   ComfySlider::Layout::Expanding,
                                                   queueWidget);
    d->generate.sliderResolutionMultiplier = resolutionSliderWidget->slider();
    resolutionSliderWidget->setValueLabelVisible(false);
    d->generate.labelResolutionMultiplier = new QLabel(QStringLiteral("1.0 x"), queueWidget);
    d->generate.labelResolutionMultiplier->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    d->generate.labelResolutionMultiplier->setMinimumWidth(20);
    d->generate.queueResolutionRow = resolutionSliderWidget;
    grid->addWidget(new QLabel(ComfyTr::tr("Resolution"), queueWidget), 3, 0);
    {
        auto *resolutionLayout = new QHBoxLayout();
        resolutionLayout->setContentsMargins(0, 0, 0, 0);
        resolutionLayout->addWidget(resolutionSliderWidget, 1);
        resolutionLayout->addWidget(d->generate.labelResolutionMultiplier);
        grid->addLayout(resolutionLayout, 3, 1);
    }

    auto *enqueueLabel = new QLabel(ComfyTr::tr("Enqueue"), queueWidget);
    d->generate.queueEnqueueLabel = enqueueLabel;
    grid->addWidget(enqueueLabel, 4, 0);
    grid->addWidget(d->generate.comboQueueMode, 4, 1);
    d->generate.queueEnqueueModeRow = d->generate.comboQueueMode;

    grid->addWidget(new QLabel(ComfyTr::tr("Cancel"), queueWidget), 5, 0);
    {
        auto *cancelLayout = new QHBoxLayout();
        cancelLayout->setContentsMargins(0, 0, 0, 0);
        d->generate.btnCancelActive = makeQueueCancelButton(ComfyTr::tr("Active"), queueWidget);
        d->generate.btnCancelQueued = makeQueueCancelButton(ComfyTr::tr("Queued"), queueWidget);
        d->generate.btnCancelAll = makeQueueCancelButton(ComfyTr::tr("All"), queueWidget);
        cancelLayout->addWidget(d->generate.btnCancelActive);
        cancelLayout->addWidget(d->generate.btnCancelQueued);
        cancelLayout->addWidget(d->generate.btnCancelAll);
        grid->addLayout(cancelLayout, 5, 1);
    }

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
        d->generate.labelResolutionMultiplier->setText(
            QStringLiteral("%1 x").arg(QString::number(d->generate.resolutionMultiplier, 'f', 1)));
        const bool fixedSeed = cfg.readEntry("FixedSeed", false);
        const qint64 seedValue = cfg.readEntry("SeedValue", qint64(0));
        d->generate.checkFixedSeed->setChecked(fixedSeed);
        d->generate.spinSeed->setValue(static_cast<int>(seedValue));
        d->generate.sliderBatchCount->setValue(d->generate.spinBatchCount->value());
        d->generate.labelBatchCount->setText(QString::number(d->generate.spinBatchCount->value()));
    }

    QObject::connect(d->generate.sliderResolutionMultiplier, &QAbstractSlider::valueChanged, dock, [dock, d](int v) {
        d->generate.resolutionMultiplier = qMax(0.3, v / 10.0);
        d->generate.labelResolutionMultiplier->setText(
            QStringLiteral("%1 x").arg(QString::number(d->generate.resolutionMultiplier, 'f', 1)));
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ResolutionMultiplier", d->generate.resolutionMultiplier);
    });
    QObject::connect(d->generate.sliderBatchCount, &QAbstractSlider::valueChanged, dock, [dock, d](int v) {
        if (d->generate.spinBatchCount)
            d->generate.spinBatchCount->setValue(v);
        if (d->generate.labelBatchCount)
            d->generate.labelBatchCount->setText(QString::number(v));
        dock->schedulePersistDocumentDefaults();
    });
    QObject::connect(d->generate.spinBatchCount, QOverload<int>::of(&QSpinBox::valueChanged), dock, [d](int v) {
        if (d->generate.sliderBatchCount) {
            QSignalBlocker block(d->generate.sliderBatchCount);
            d->generate.sliderBatchCount->setValue(v);
        }
        if (d->generate.labelBatchCount)
            d->generate.labelBatchCount->setText(QString::number(v));
    });
    dock->applyRecentlyUsedSyncFromSettings();
    QObject::connect(d->generate.comboQueueMode, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int index) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("QueueMode", index);
    });
    QObject::connect(d->generate.checkFixedSeed, &QCheckBox::toggled, dock, [dock](bool) {
        dock->persistSeedToConfig();
    });
    QObject::connect(d->generate.spinSeed, QOverload<int>::of(&QSpinBox::valueChanged), dock, [dock](int) {
        dock->persistSeedToConfig();
    });
    QObject::connect(d->generate.btnCancelActive, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotAiDiffusionCancelCurrent);
    QObject::connect(d->generate.btnCancelQueued, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotAiDiffusionCancelQueued);
    QObject::connect(d->generate.btnCancelAll, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotAiDiffusionCancelAll);

    QWidgetAction *queueAction = new QWidgetAction(queueMenu);
    queueAction->setDefaultWidget(queueWidget);
    queueMenu->addAction(queueAction);
    d->generate.btnQueuePopup->setMenu(queueMenu);

    QObject::connect(queueMenu, &QMenu::aboutToShow, dock, [dock]() {
        ensureSeedSpinInQueueRow(dock->m_d.data());
        dock->refreshQueuePopupSupportsBatch();
        dock->updateQueueStatus();
    });

    dock->setupGenerateInpaintMenus();

    d->generate.generateActionRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *actionRow = new QHBoxLayout(d->generate.generateActionRowWidget);
    ComfyUiStyle::applyTightRowLayout(actionRow);

    QWidget *genCluster = new QWidget(d->generate.generateActionRowWidget);
    QHBoxLayout *genClusterLay = new QHBoxLayout(genCluster);
    ComfyUiStyle::applyTightRowLayout(genClusterLay, 0); // Generate button cluster
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
    if (d->generate.btnQueuePopup && d->generate.btnGenerate) {
        d->generate.btnQueuePopup->setFixedHeight(ComfyUiStyle::Spacing::primaryButtonHeight - 2);
        d->generate.btnQueuePopup->setMinimumWidth(d->generate.btnQueuePopup->sizeHint().width());
    }
    genContentLayout->addWidget(d->generate.generateActionRowWidget);
    d->generate.queueButtonRowWidget = d->generate.generateActionRowWidget;

    d->generate.btnCancelQueue = d->generate.btnCancelAll;
    if (d->generate.btnCancelAll)
        d->generate.btnCancelAll->setEnabled(false);

    d->progressBar = new QProgressBar();
    d->progressBar->setObjectName(QStringLiteral("ComfyGenerateProgressBar"));
    d->progressBar->setMinimum(0);
    d->progressBar->setMaximum(1000);
    d->progressBar->setTextVisible(false);
    d->progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ComfyUiStyle::applyProgressBar(d->progressBar, false);
    dock->setProgressBarKind(false);  // §13.18: default = generation
    dock->resetProgressBarToIdle();
    genContentLayout->addWidget(d->progressBar);

    dock->setupRootControlLayersUi(d->generate.genContentContainer, genContentLayout);

}

} // namespace ComfyDockUiBuilderGenerateInternal
