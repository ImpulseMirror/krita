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

void buildStrengthSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    // §5.4: Strength (1–100%, denoise = strength/100). §13.32: stepBy snaps to valid step boundaries.
    d->generate.spinStrength = new StrengthSpinBox(d->generate.spinSteps, dock);
    d->generate.spinStrength->setRange(1, 100);
    d->generate.spinStrength->setValue(100);
    d->generate.spinStrength->setSuffix(QStringLiteral("%"));
    d->generate.spinStrength->setToolTip(ComfyTr::tr("Strength: 100% = full generation, lower = more preserved (refine)."));
    d->inpaint.strengthRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *strengthRow = new QHBoxLayout(d->inpaint.strengthRowWidget);
    strengthRow->setContentsMargins(0, 0, 0, 0);
    d->inpaint.sliderStrength = new QSlider(Qt::Horizontal, d->inpaint.strengthRowWidget);
    d->inpaint.sliderStrength->setRange(1, 100);
    d->inpaint.sliderStrength->setSingleStep(5);
    d->inpaint.sliderStrength->setValue(d->generate.spinStrength->value());
    d->generate.spinStrength->setPrefix(ComfyTr::tr("Strength") + QStringLiteral(": "));
    strengthRow->addWidget(d->inpaint.sliderStrength, 1);
    strengthRow->addWidget(d->generate.spinStrength);
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int storedStrength =
            qBound(1, cfg.readEntry("GenerateStrength", cfg.readEntry("Strength", 100)), 100);
        d->generate.spinStrength->setValue(storedStrength);
        if (d->inpaint.sliderStrength)
            d->inpaint.sliderStrength->setValue(storedStrength);
    }
    QObject::connect(d->inpaint.sliderStrength, &QSlider::valueChanged, dock, &ComfyUIRemoteDock::onGenerateStrengthChanged);
    QObject::connect(d->generate.spinStrength, QOverload<int>::of(&QSpinBox::valueChanged), dock,
            &ComfyUIRemoteDock::onGenerateStrengthChanged);

    // §5.4: Region-only toggle; when set, only active region mask and prompt are used
    d->generate.checkRegionOnly = new QCheckBox(ComfyTr::tr("Region-only"));
    d->generate.checkRegionOnly->setToolTip(ComfyTr::tr("Limit generation to the active region only."));
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        d->generate.checkRegionOnly->setChecked(cfg.readEntry("RegionOnly", false));
    }
    QObject::connect(d->generate.checkRegionOnly, &QCheckBox::toggled, dock, [dock, d](bool checked) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("RegionOnly", checked);
        dock->updateGenerateOptions();
    });
    genContentLayout->addWidget(d->generate.checkRegionOnly);
    // FAITHFUL_PORT: upstream exposes region-only via the region-mask icon button in
    // the Generate-button row, not as a loose top-level checkbox. Hide here and
    // keep the model state available to the rest of the code.
    d->generate.checkRegionOnly->setVisible(false);

    // §5.4: Edit mode toggle (instruction-based editing; uses linked_edit_style when set)
    d->generate.checkEditMode = new QCheckBox(ComfyTr::tr("Edit"));
    d->generate.checkEditMode->setToolTip(
        ComfyTr::tr("Use instruction-based editing (alternative style when set). On Generate, the Regions list switches to a separate set while Edit is checked, so normal and edit workflows do not share the same regions."));
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        d->generate.checkEditMode->setChecked(cfg.readEntry("EditMode", false));
    }
    QObject::connect(d->generate.checkEditMode, &QCheckBox::toggled, dock, [dock, d](bool checked) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("EditMode", checked);
        dock->refreshRegionsList();  // §13.125: switch between root and edit region lists
        dock->updateGenerateOptions();
    });
    genContentLayout->addWidget(d->generate.checkEditMode);
    // FAITHFUL_PORT: upstream selects Edit via the Generate-button dropdown menu,
    // not a loose checkbox in the docker. Hide here; the state is still wired.
    d->generate.checkEditMode->setVisible(false);

    // §5.4: Layer count (1–8); visible only when style architecture is Qwen Layered (Arch.qwen_l)
    d->generate.layerCountRow = new QWidget(shell.genGroup);
    QHBoxLayout *layerCountLayout = new QHBoxLayout(d->generate.layerCountRow);
    layerCountLayout->setContentsMargins(0, 0, 0, 0);
    d->generate.spinLayerCount = new QSpinBox(d->generate.layerCountRow);
    d->generate.spinLayerCount->setRange(1, 8);
    d->generate.spinLayerCount->setValue(1);
    d->generate.spinLayerCount->setToolTip(ComfyTr::tr("Number of output layers for Qwen Layered generation."));
    layerCountLayout->addWidget(new QLabel(ComfyTr::tr("Layer count:"), d->generate.layerCountRow));
    layerCountLayout->addWidget(d->generate.spinLayerCount);
    layerCountLayout->addStretch();
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        d->generate.spinLayerCount->setValue(qBound(1, cfg.readEntry("LayerCount", 1), 8));
    }
    QObject::connect(d->generate.spinLayerCount, QOverload<int>::of(&QSpinBox::valueChanged), dock, [dock, d](int v) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("LayerCount", v);
    });
    strengthRow->addWidget(d->generate.layerCountRow);
    d->generate.layerCountRow->setVisible(false);  // Shown only when current style arch is qwen_l; no arch in presets yet
    d->generate.btnAddControlIcon = new QToolButton(d->inpaint.strengthRowWidget);
    d->generate.btnAddControlIcon->setToolButtonStyle(Qt::ToolButtonIconOnly);
    d->generate.btnAddControlIcon->setIcon(ComfyTheme::icon(QStringLiteral("control-add")));
    d->generate.btnAddControlIcon->setToolTip(ComfyTr::tr("Add Control Layer"));
    d->generate.btnAddControlIcon->setAutoRaise(true);
    QObject::connect(d->generate.btnAddControlIcon, &QToolButton::clicked, dock, [dock, d]() {
        if (d->activeRegionIndex >= 0
            && d->activeRegionIndex < comfyActiveRegionEntries(d).size())
            dock->slotAddRegionControlLayer();
        else
            dock->slotAddControlLayer();
    });
    d->generate.btnAddRegionIcon = new QToolButton(d->inpaint.strengthRowWidget);
    d->generate.btnAddRegionIcon->setToolButtonStyle(Qt::ToolButtonIconOnly);
    d->generate.btnAddRegionIcon->setIcon(ComfyTheme::icon(QStringLiteral("region-add")));
    d->generate.btnAddRegionIcon->setToolTip(ComfyTr::tr("Add Region"));
    d->generate.btnAddRegionIcon->setAutoRaise(true);
    QObject::connect(d->generate.btnAddRegionIcon, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotAddRegion);
    strengthRow->addWidget(d->generate.btnAddControlIcon);
    strengthRow->addWidget(d->generate.btnAddRegionIcon);
    genContentLayout->addWidget(d->inpaint.strengthRowWidget);

}

} // namespace ComfyDockUiBuilderGenerateInternal
