/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyComboBox.h"
#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyFormUi.h"
#include "ComfySlider.h"

#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
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
#include <QAbstractSlider>
#include "ComfySpinBox.h"
#include "ComfyDoubleSpinBox.h"
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

    d->upscale.upscaleFactorRow = new QWidget(d->generate.genContentContainer);
    d->upscale.upscaleFactorRow->setMinimumHeight(ComfyUiStyle::Spacing::rowHeight);
    QVBoxLayout *upscaleFactorOuter = new QVBoxLayout(d->upscale.upscaleFactorRow);
    ComfyUiStyle::applyTightRowLayout(upscaleFactorOuter);
    upscaleFactorOuter->setContentsMargins(0, 0, 0, ComfyUiStyle::Spacing::settingsSectionGap);
    QWidget *factorSliderRow = new QWidget(d->upscale.upscaleFactorRow);
    QHBoxLayout *factorSliderLayout = new QHBoxLayout(factorSliderRow);
    ComfyUiStyle::applyTightRowLayout(factorSliderLayout);
    factorSliderLayout->setAlignment(Qt::AlignVCenter);
    d->upscale.spinUpscaleFactor = new ComfyDoubleSpinBox(factorSliderRow);
    d->upscale.spinUpscaleFactor->setRange(1.0, 4.0);
    d->upscale.spinUpscaleFactor->setValue(2.0);
    d->upscale.spinUpscaleFactor->setDecimals(2);
    d->upscale.spinUpscaleFactor->setSingleStep(0.1);
    d->upscale.spinUpscaleFactor->setPrefix(ComfyTr::tr("Scale") + QStringLiteral(": "));
    d->upscale.spinUpscaleFactor->setSuffix(QStringLiteral("x"));
    d->upscale.spinUpscaleFactor->setToolTip(ComfyTr::tr("Scale: X.XX×"));
    {
        const QFontMetrics fm(d->upscale.spinUpscaleFactor->font());
        d->upscale.spinUpscaleFactor->setMinimumWidth(
            fm.horizontalAdvance(d->upscale.spinUpscaleFactor->prefix() + QStringLiteral("4.00")
                                 + d->upscale.spinUpscaleFactor->suffix())
            + ComfyUiStyle::Spacing::spinButtonWidth + ComfyUiStyle::Spacing::nestedPanel * 2);
    }
    ComfySlider *factorSliderWidget = ComfyFormUi::makeExpandingSlider(
        10, 40, QString(), factorSliderRow, false);
    d->upscale.sliderUpscaleFactor = factorSliderWidget->slider();
    d->upscale.sliderUpscaleFactor->setValue(20);
    d->upscale.sliderUpscaleFactor->setToolTip(ComfyTr::tr("Upscale factor"));
    factorSliderLayout->addWidget(factorSliderWidget, 1);
    factorSliderLayout->addWidget(d->upscale.spinUpscaleFactor);
    upscaleFactorOuter->addWidget(factorSliderRow);
    d->upscale.labelUpscaleTargetSize = new QLabel(ComfyTr::tr("Target size: — × —"), d->upscale.upscaleFactorRow);
    d->upscale.labelUpscaleTargetSize->setToolTip(ComfyTr::tr("Target size: W x H (from document extent × scale)"));
    d->upscale.labelUpscaleTargetSize->setAlignment(Qt::AlignRight);
    d->upscale.labelUpscaleTargetSize->hide();
    upscaleFactorOuter->addWidget(d->upscale.labelUpscaleTargetSize);
    QObject::connect(d->upscale.sliderUpscaleFactor, &QAbstractSlider::sliderPressed, dock, [dock, d]() {
        dock->updateUpscaleTargetSize();
        if (d->upscale.labelUpscaleTargetSize)
            d->upscale.labelUpscaleTargetSize->show();
    });
    QObject::connect(d->upscale.sliderUpscaleFactor, &QAbstractSlider::sliderReleased, dock, [d]() {
        if (d->upscale.labelUpscaleTargetSize)
            d->upscale.labelUpscaleTargetSize->hide();
    });
    QObject::connect(d->upscale.sliderUpscaleFactor, &QAbstractSlider::valueChanged, dock, [dock, d](int v) {
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

    d->upscale.upscaleRefineBlock = new QWidget(d->generate.genContentContainer);
    QVBoxLayout *refineBlockLay = new QVBoxLayout(d->upscale.upscaleRefineBlock);
    ComfyUiStyle::applyTightRowLayout(refineBlockLay);
    d->upscale.checkUpscaleRefine = new ComfyCheckBox(ComfyTr::tr("Refine upscaled image"), d->upscale.upscaleRefineBlock);
    refineBlockLay->addWidget(d->upscale.checkUpscaleRefine);
    auto *refinePanel = new QFrame(d->upscale.upscaleRefineBlock);
    refinePanel->setObjectName(QStringLiteral("UpscaleRefinePanel"));
    refinePanel->setFrameShape(QFrame::NoFrame);
    refinePanel->setStyleSheet(ComfyUiStyle::outlinedPanelStyleSheet(QStringLiteral("UpscaleRefinePanel")));
    d->upscale.upscaleRefineDetails = refinePanel;
    QVBoxLayout *refineLay = new QVBoxLayout(refinePanel);
    ComfyUiStyle::applyTightRowLayout(refineLay);
    refineLay->setContentsMargins(ComfyUiStyle::Spacing::nestedPanel,
                                  ComfyUiStyle::Spacing::nestedPanel,
                                  ComfyUiStyle::Spacing::nestedPanel,
                                  ComfyUiStyle::Spacing::nestedPanel);
    {
        QWidget *styleRow = new QWidget(d->upscale.upscaleRefineDetails);
        QHBoxLayout *styleLay = new QHBoxLayout(styleRow);
        ComfyUiStyle::applyTightRowLayout(styleLay);
        styleLay->setAlignment(Qt::AlignVCenter);
        d->upscale.comboUpscaleRefinementModel = new ComfyComboBox(styleRow);
        d->upscale.comboUpscaleRefinementModel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ComfyUiStyle::applyComboBox(d->upscale.comboUpscaleRefinementModel);
        d->upscale.btnUpscaleRefineSettings = new QToolButton(styleRow);
        d->upscale.btnUpscaleRefineSettings->setIcon(ComfyTheme::icon(QStringLiteral("settings")));
        d->upscale.btnUpscaleRefineSettings->setToolTip(ComfyTr::tr("Style settings…"));
        d->upscale.btnUpscaleRefineSettings->setAutoRaise(true);
        ComfyUiStyle::applyIconToolButton(d->upscale.btnUpscaleRefineSettings);
        QObject::connect(d->upscale.btnUpscaleRefineSettings, &QToolButton::clicked,
                         dock, &ComfyUIRemoteDock::slotConfigureHelp);
        styleLay->addWidget(d->upscale.comboUpscaleRefinementModel, 1);
        styleLay->addWidget(d->upscale.btnUpscaleRefineSettings);
        refineLay->addWidget(styleRow);
    }
    {
        const ComfyFormUi::InlineSliderSpinRow strengthRow = ComfyFormUi::addInlineSliderSpinRow(
            d->upscale.upscaleRefineDetails, ComfyTr::tr("Strength"), 1, 100);
        d->upscale.sliderUpscaleRefineStrength = strengthRow.qtSlider();
        d->upscale.spinUpscaleRefineStrength = strengthRow.spin;
        refineLay->addWidget(strengthRow.row);
    }
    {
        const ComfyFormUi::InlineSliderSpinRow guidanceRow = ComfyFormUi::addInlineSliderSpinRow(
            d->upscale.upscaleRefineDetails, ComfyTr::tr("Image guidance"), 1, 100);
        d->upscale.sliderUpscaleRefineGuidance = guidanceRow.qtSlider();
        d->upscale.spinUpscaleRefineGuidance = guidanceRow.spin;
        refineLay->addWidget(guidanceRow.row);
    }
    d->upscale.upscaleTileOverlapRow = new QWidget(d->upscale.upscaleRefineDetails);
    QHBoxLayout *tileOverlapLay = new QHBoxLayout(d->upscale.upscaleTileOverlapRow);
    ComfyUiStyle::applyTightRowLayout(tileOverlapLay);
    tileOverlapLay->setAlignment(Qt::AlignVCenter);
    tileOverlapLay->addWidget(new QLabel(ComfyTr::tr("Tile Overlap"), d->upscale.upscaleTileOverlapRow));
    tileOverlapLay->addStretch(1);
    d->upscale.comboTileOverlapMode = new ComfyComboBox(d->upscale.upscaleTileOverlapRow);
    d->upscale.comboTileOverlapMode->addItem(ComfyTr::tr("Automatic"), 0);
    d->upscale.comboTileOverlapMode->addItem(ComfyTr::tr("Custom"), 1);
    ComfyUiStyle::applyComboBox(d->upscale.comboTileOverlapMode);
    d->upscale.comboTileOverlapMode->setMinimumWidth(100);
    d->upscale.comboTileOverlapMode->setCurrentIndex(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("TileOverlapMode", 0));
    d->upscaleRt.tileOverlapMode = d->upscale.comboTileOverlapMode->currentIndex();
    d->upscale.spinTileOverlap = new ComfySpinBox(d->upscale.upscaleTileOverlapRow);
    d->upscale.spinTileOverlap->setRange(0, 512);
    d->upscale.spinTileOverlap->setValue(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("TileOverlap", 32));
    d->upscaleRt.tileOverlap = d->upscale.spinTileOverlap->value();
    d->upscale.spinTileOverlap->setSuffix(ComfyTr::tr(" px"));
    d->upscale.spinTileOverlap->setToolTip(ComfyTr::tr("Tile overlap in pixels when Custom is selected."));
    d->upscale.spinTileOverlap->setEnabled(d->upscaleRt.tileOverlapMode == 1);
    tileOverlapLay->addWidget(d->upscale.comboTileOverlapMode);
    tileOverlapLay->addWidget(d->upscale.spinTileOverlap);
    refineLay->addWidget(d->upscale.upscaleTileOverlapRow);
    {
        QWidget *usePromptRow = ComfyFormUi::addLabeledRow(
            d->upscale.upscaleRefineDetails, ComfyTr::tr("Use Prompt"), nullptr);
        auto *usePromptLayout = qobject_cast<QHBoxLayout *>(usePromptRow->layout());
        d->upscale.labelUpscaleUsePromptText = new QLabel(d->upscale.upscaleRefineDetails);
        d->upscale.labelUpscaleUsePromptText->setMinimumWidth(40);
        d->upscale.checkUpscaleUsePrompt = new ComfySwitchWidget(d->upscale.upscaleRefineDetails);
        if (usePromptLayout) {
            usePromptLayout->addWidget(d->upscale.labelUpscaleUsePromptText, 1);
            usePromptLayout->addWidget(d->upscale.checkUpscaleUsePrompt);
        }
        refineLay->addWidget(usePromptRow);
    }
    d->upscale.checkUpscaleUsePrompt->setToolTip(ComfyTr::tr("When refining, include the positive prompt in the diffusion pass (when supported)."));
    refineBlockLay->addWidget(d->upscale.upscaleRefineDetails);
    refineBlockLay->setSpacing(ComfyUiStyle::Spacing::nestedPanel);
    genContentLayout->addWidget(d->upscale.upscaleRefineBlock);
    d->upscale.upscaleRefineBlock->setVisible(d->comboWorkspace->currentIndex() == 1);
    {
        KConfigGroup ucfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        d->upscale.checkUpscaleRefine->setChecked(ucfg.readEntry("UpscaleRefineEnabled", false));
        d->upscale.sliderUpscaleRefineStrength->setValue(ucfg.readEntry("UpscaleRefineStrength", 30));
        d->upscale.sliderUpscaleRefineGuidance->setValue(ucfg.readEntry("UpscaleRefineGuidance", 50));
        if (d->upscale.spinUpscaleRefineStrength)
            d->upscale.spinUpscaleRefineStrength->setValue(d->upscale.sliderUpscaleRefineStrength->value());
        if (d->upscale.spinUpscaleRefineGuidance)
            d->upscale.spinUpscaleRefineGuidance->setValue(d->upscale.sliderUpscaleRefineGuidance->value());
        d->upscale.checkUpscaleUsePrompt->setChecked(ucfg.readEntry("UpscaleUsePrompt", false));
    }
    dock->updateUpscaleUsePromptLabel();
    QObject::connect(d->upscale.sliderUpscaleRefineStrength, &QAbstractSlider::valueChanged, dock, [d](int v) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineStrength", v);
    });
    QObject::connect(d->upscale.spinUpscaleRefineStrength, QOverload<int>::of(&QSpinBox::valueChanged), dock, [d](int v) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineStrength", v);
    });
    QObject::connect(d->upscale.sliderUpscaleRefineGuidance, &QAbstractSlider::valueChanged, dock, [d](int v) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineGuidance", v);
    });
    QObject::connect(d->upscale.spinUpscaleRefineGuidance, QOverload<int>::of(&QSpinBox::valueChanged), dock, [d](int v) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineGuidance", v);
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
        if (d->upscale.spinTileOverlap)
            d->upscale.spinTileOverlap->setEnabled(idx == 1);
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
    ComfyUiStyle::applyTightRowLayout(upscaleActionRow);
    if (!d->upscale.btnUpscale) {
        d->upscale.btnUpscale = new QPushButton(ComfyTr::tr("Upscale"));
        ComfyUiStyle::applyPrimaryButton(d->upscale.btnUpscale);
        d->upscale.btnUpscale->setToolTip(ComfyTr::tr(
            "Upscale the canvas at the scale factor above. With \"Refine upscaled image\" enabled, runs a diffusion pass after scaling."));
        QObject::connect(d->upscale.btnUpscale, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotUpscale);
    }
    upscaleActionRow->addWidget(d->upscale.btnUpscale, 1);
    genContentLayout->addWidget(d->upscale.upscaleActionRowWidget);
}

} // namespace ComfyDockUiBuilderGenerateInternal
