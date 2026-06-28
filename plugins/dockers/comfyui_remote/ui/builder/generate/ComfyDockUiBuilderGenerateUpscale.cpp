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

void buildUpscaleWidgetsSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    d->upscale.upscaleFactorRow = new QWidget(shell.genGroup);
    QVBoxLayout *upscaleFactorOuter = new QVBoxLayout(d->upscale.upscaleFactorRow);
    upscaleFactorOuter->setContentsMargins(0, 0, 0, 0);
    upscaleFactorOuter->setSpacing(2);
    QHBoxLayout *upscaleFactorLayout = new QHBoxLayout();
    upscaleFactorLayout->setContentsMargins(0, 0, 0, 0);
    d->upscale.sliderUpscaleFactor = new QSlider(Qt::Horizontal, d->upscale.upscaleFactorRow);
    d->upscale.sliderUpscaleFactor->setRange(10, 40);
    d->upscale.sliderUpscaleFactor->setValue(20);
    d->upscale.sliderUpscaleFactor->setToolTip(ComfyTr::tr("Upscale factor"));
    d->upscale.spinUpscaleFactor = new QDoubleSpinBox(d->upscale.upscaleFactorRow);
    d->upscale.spinUpscaleFactor->setRange(1.0, 4.0);
    d->upscale.spinUpscaleFactor->setValue(2.0);
    d->upscale.spinUpscaleFactor->setDecimals(2);
    d->upscale.spinUpscaleFactor->setSingleStep(0.1);
    d->upscale.spinUpscaleFactor->setPrefix(ComfyTr::tr("Scale") + QStringLiteral(": "));
    d->upscale.spinUpscaleFactor->setSuffix(QStringLiteral("x"));
    d->upscale.spinUpscaleFactor->setToolTip(ComfyTr::tr("Scale: X.XX×"));
    upscaleFactorLayout->addWidget(d->upscale.sliderUpscaleFactor, 1);
    upscaleFactorLayout->addWidget(d->upscale.spinUpscaleFactor);
    upscaleFactorOuter->addLayout(upscaleFactorLayout);
    d->upscale.labelUpscaleTargetSize = new QLabel(ComfyTr::tr("Target size: — × —"), d->upscale.upscaleFactorRow);
    d->upscale.labelUpscaleTargetSize->setToolTip(ComfyTr::tr("Target size: W x H (from document extent × scale)"));
    d->upscale.labelUpscaleTargetSize->setAlignment(Qt::AlignRight);
    upscaleFactorOuter->addWidget(d->upscale.labelUpscaleTargetSize);
    QObject::connect(d->upscale.sliderUpscaleFactor, &QSlider::valueChanged, dock, [dock, d](int v) {
        d->upscaleRt.upscaleFactor = v / 10.0;
        if (d->upscale.spinUpscaleFactor && qAbs(d->upscale.spinUpscaleFactor->value() - d->upscaleRt.upscaleFactor) > 0.005)
            d->upscale.spinUpscaleFactor->setValue(d->upscaleRt.upscaleFactor);
        dock->updateUpscaleTargetSize();
    });
    QObject::connect(d->upscale.spinUpscaleFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dock, [dock, d](double v) {
        d->upscaleRt.upscaleFactor = v;
        if (d->upscale.sliderUpscaleFactor)
            d->upscale.sliderUpscaleFactor->setValue(qRound(v * 10));
        dock->updateUpscaleTargetSize();
    });
    genContentLayout->addWidget(d->upscale.upscaleFactorRow);
    d->upscale.upscaleFactorRow->setVisible(d->comboWorkspace->currentIndex() == 1);

    d->upscale.upscaleRefineBlock = new QWidget(shell.genGroup);
    QVBoxLayout *refineBlockLay = new QVBoxLayout(d->upscale.upscaleRefineBlock);
    refineBlockLay->setContentsMargins(0, 0, 0, 0);
    refineBlockLay->setSpacing(4);
    d->upscale.checkUpscaleRefine = new QCheckBox(ComfyTr::tr("Refine upscaled image"), d->upscale.upscaleRefineBlock);
    refineBlockLay->addWidget(d->upscale.checkUpscaleRefine);
    d->upscale.upscaleRefineDetails = new QWidget(d->upscale.upscaleRefineBlock);
    QVBoxLayout *refineLay = new QVBoxLayout(d->upscale.upscaleRefineDetails);
    refineLay->setContentsMargins(0, 0, 0, 0);
    refineLay->setSpacing(4);
    {
        QHBoxLayout *styleRow = new QHBoxLayout();
        styleRow->setContentsMargins(0, 0, 0, 0);
        d->upscale.comboUpscaleRefinementModel = new QComboBox(d->upscale.upscaleRefineDetails);
        d->upscale.btnUpscaleRefineSettings = new QToolButton(d->upscale.upscaleRefineDetails);
        d->upscale.btnUpscaleRefineSettings->setIcon(ComfyTheme::icon(QStringLiteral("settings")));
        d->upscale.btnUpscaleRefineSettings->setToolTip(ComfyTr::tr("Style settings…"));
        d->upscale.btnUpscaleRefineSettings->setAutoRaise(true);
        QObject::connect(d->upscale.btnUpscaleRefineSettings, &QToolButton::clicked,
                         dock, &ComfyUIRemoteDock::slotConfigureHelp);
        styleRow->addWidget(d->upscale.comboUpscaleRefinementModel, 1);
        styleRow->addWidget(d->upscale.btnUpscaleRefineSettings);
        refineLay->addLayout(styleRow);
    }
    {
        QHBoxLayout *strLay = new QHBoxLayout();
        strLay->addWidget(new QLabel(ComfyTr::tr("Strength"), d->upscale.upscaleRefineDetails), 1);
        d->upscale.sliderUpscaleRefineStrength = new QSlider(Qt::Horizontal, d->upscale.upscaleRefineDetails);
        d->upscale.sliderUpscaleRefineStrength->setRange(1, 100);
        strLay->addWidget(d->upscale.sliderUpscaleRefineStrength, 3);
        d->upscale.labelUpscaleRefineStrength = new QLabel(d->upscale.upscaleRefineDetails);
        d->upscale.labelUpscaleRefineStrength->setMinimumWidth(40);
        strLay->addWidget(d->upscale.labelUpscaleRefineStrength);
        refineLay->addLayout(strLay);
    }
    {
        QHBoxLayout *gLay = new QHBoxLayout();
        gLay->addWidget(new QLabel(ComfyTr::tr("Image guidance"), d->upscale.upscaleRefineDetails), 1);
        d->upscale.sliderUpscaleRefineGuidance = new QSlider(Qt::Horizontal, d->upscale.upscaleRefineDetails);
        d->upscale.sliderUpscaleRefineGuidance->setRange(1, 100);
        gLay->addWidget(d->upscale.sliderUpscaleRefineGuidance, 3);
        d->upscale.labelUpscaleRefineGuidance = new QLabel(d->upscale.upscaleRefineDetails);
        d->upscale.labelUpscaleRefineGuidance->setMinimumWidth(40);
        gLay->addWidget(d->upscale.labelUpscaleRefineGuidance);
        refineLay->addLayout(gLay);
    }
    d->upscale.upscaleTileOverlapRow = new QWidget(d->upscale.upscaleRefineDetails);
    QHBoxLayout *tileOverlapLayout = new QHBoxLayout(d->upscale.upscaleTileOverlapRow);
    tileOverlapLayout->setContentsMargins(0, 0, 0, 0);
    tileOverlapLayout->addWidget(new QLabel(ComfyTr::tr("Tile Overlap"), d->upscale.upscaleTileOverlapRow), 2);
    d->upscale.comboTileOverlapMode = new QComboBox(d->upscale.upscaleTileOverlapRow);
    d->upscale.comboTileOverlapMode->addItem(ComfyTr::tr("Automatic"), 0);
    d->upscale.comboTileOverlapMode->addItem(ComfyTr::tr("Custom"), 1);
    d->upscale.comboTileOverlapMode->setCurrentIndex(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("TileOverlapMode", 0));
    d->upscaleRt.tileOverlapMode = d->upscale.comboTileOverlapMode->currentIndex();
    d->upscale.spinTileOverlap = new QSpinBox(d->upscale.upscaleTileOverlapRow);
    d->upscale.spinTileOverlap->setRange(0, 512);
    d->upscale.spinTileOverlap->setValue(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("TileOverlap", 32));
    d->upscaleRt.tileOverlap = d->upscale.spinTileOverlap->value();
    d->upscale.spinTileOverlap->setSuffix(ComfyTr::tr(" px"));
    d->upscale.spinTileOverlap->setToolTip(ComfyTr::tr("Tile overlap in pixels when Custom is selected."));
    tileOverlapLayout->addWidget(d->upscale.comboTileOverlapMode);
    tileOverlapLayout->addWidget(d->upscale.spinTileOverlap);
    refineLay->addWidget(d->upscale.upscaleTileOverlapRow);
    {
        QHBoxLayout *upscalePromptRow = new QHBoxLayout();
        upscalePromptRow->setContentsMargins(0, 0, 0, 0);
        upscalePromptRow->addWidget(new QLabel(ComfyTr::tr("Use Prompt"), d->upscale.upscaleRefineDetails));
        d->upscale.labelUpscaleUsePromptText = new QLabel(d->upscale.upscaleRefineDetails);
        d->upscale.labelUpscaleUsePromptText->setMinimumWidth(40);
        upscalePromptRow->addWidget(d->upscale.labelUpscaleUsePromptText, 1);
        d->upscale.checkUpscaleUsePrompt = new ComfySwitchWidget(d->upscale.upscaleRefineDetails);
        upscalePromptRow->addWidget(d->upscale.checkUpscaleUsePrompt);
        refineLay->addLayout(upscalePromptRow);
    }
    d->upscale.checkUpscaleUsePrompt->setToolTip(ComfyTr::tr("When refining, include the positive prompt in the diffusion pass (when supported)."));
    refineBlockLay->addWidget(d->upscale.upscaleRefineDetails);
    genContentLayout->addWidget(d->upscale.upscaleRefineBlock);
    d->upscale.upscaleRefineBlock->setVisible(d->comboWorkspace->currentIndex() == 1);
    d->upscale.spinTileOverlap->setVisible(d->upscaleRt.tileOverlapMode == 1);
    {
        KConfigGroup ucfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        d->upscale.checkUpscaleRefine->setChecked(ucfg.readEntry("UpscaleRefineEnabled", false));
        d->upscale.sliderUpscaleRefineStrength->setValue(ucfg.readEntry("UpscaleRefineStrength", 30));
        d->upscale.sliderUpscaleRefineGuidance->setValue(ucfg.readEntry("UpscaleRefineGuidance", 50));
        d->upscale.checkUpscaleUsePrompt->setChecked(ucfg.readEntry("UpscaleUsePrompt", false));
    }
    auto updateStrengthLabel = [dock, d]() {
        if (d->upscale.labelUpscaleRefineStrength && d->upscale.sliderUpscaleRefineStrength)
            d->upscale.labelUpscaleRefineStrength->setText(QString::number(d->upscale.sliderUpscaleRefineStrength->value()) + QLatin1Char('%'));
    };
    auto updateGuidanceLabel = [dock, d]() {
        if (d->upscale.labelUpscaleRefineGuidance && d->upscale.sliderUpscaleRefineGuidance)
            d->upscale.labelUpscaleRefineGuidance->setText(QString::number(d->upscale.sliderUpscaleRefineGuidance->value()) + QLatin1Char('%'));
    };
    updateStrengthLabel();
    updateGuidanceLabel();
    dock->updateUpscaleUsePromptLabel();
    QObject::connect(d->upscale.sliderUpscaleRefineStrength, &QSlider::valueChanged, dock, [dock, d, updateStrengthLabel](int) {
        updateStrengthLabel();
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineStrength", d->upscale.sliderUpscaleRefineStrength->value());
    });
    QObject::connect(d->upscale.sliderUpscaleRefineGuidance, &QSlider::valueChanged, dock, [dock, d, updateGuidanceLabel](int) {
        updateGuidanceLabel();
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineGuidance", d->upscale.sliderUpscaleRefineGuidance->value());
    });
    QObject::connect(d->upscale.checkUpscaleRefine, &QCheckBox::toggled, dock, [dock, d](bool on) {
        dock->syncUpscaleRefineControlsEnabled(on);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineEnabled", on);
    });
    QObject::connect(d->upscale.checkUpscaleUsePrompt, &QAbstractButton::toggled, dock, [dock](bool on) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleUsePrompt", on);
        dock->updateUpscaleUsePromptLabel();
    });
    dock->syncUpscaleRefineControlsEnabled(d->upscale.checkUpscaleRefine->isChecked());
    QObject::connect(d->upscale.comboTileOverlapMode, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int idx) {
        d->upscaleRt.tileOverlapMode = idx;
        if (d->upscale.spinTileOverlap) d->upscale.spinTileOverlap->setVisible(idx == 1);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("TileOverlapMode", idx);
        dock->updateUpscaleTargetSize();
    });
    QObject::connect(d->upscale.spinTileOverlap, QOverload<int>::of(&QSpinBox::valueChanged), dock, [dock, d](int v) {
        d->upscaleRt.tileOverlap = v;
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("TileOverlap", v);
        dock->updateUpscaleTargetSize();
    });
    dock->syncUpscaleRefinementModelFromPresetCombo();
    QObject::connect(d->upscale.comboUpscaleRefinementModel, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefinementModelIndex", d->upscale.comboUpscaleRefinementModel->currentIndex());
    });

    d->upscale.upscaleActionRowWidget = new QWidget(d->generate.genContentContainer);
    d->upscale.upscaleActionRowWidget->setVisible(false);
    QHBoxLayout *upscaleActionRow = new QHBoxLayout(d->upscale.upscaleActionRowWidget);
    upscaleActionRow->setContentsMargins(0, 0, 0, 0);
    upscaleActionRow->setSpacing(4);
    if (!d->upscale.btnUpscale) {
        d->upscale.btnUpscale = new QPushButton(ComfyTr::tr("Upscale"));
        d->upscale.btnUpscale->setToolTip(ComfyTr::tr(
            "Upscale the canvas at the scale factor above. With \"Refine upscaled image\" enabled, runs a diffusion pass after scaling."));
        QObject::connect(d->upscale.btnUpscale, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotUpscale);
    }
    upscaleActionRow->addWidget(d->upscale.btnUpscale, 1);
    genContentLayout->addWidget(d->upscale.upscaleActionRowWidget);
}

} // namespace ComfyDockUiBuilderGenerateInternal
