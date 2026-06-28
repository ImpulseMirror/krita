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

void buildSeedSizeSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    DockShell &shell = *ws.shell;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    d->generate.checkFixedSeed = new QCheckBox(ComfyTr::tr("Fixed seed"));
    d->generate.spinSeed = new QSpinBox();
    d->generate.spinSeed->setRange(0, 2147483647);  // §13.209: 32-bit non-negative (0 to 2^31−1)
    d->generate.spinSeed->setValue(0);
    d->generate.btnRandomSeed = new QPushButton();
    d->generate.btnRandomSeed->setIcon(
        ComfyTheme::icon(QStringLiteral("random")));  // §5.4: dice icon for random seed
    d->generate.btnRandomSeed->setToolTip(ComfyTr::tr("Pick a new random seed."));
    d->generate.btnRandomSeed->setAccessibleName(ComfyTr::tr("Random seed"));
    QObject::connect(d->generate.btnRandomSeed, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotRandomSeed);
    // FAITHFUL_PORT: wrap Seed row in a widget so it can be hidden as a unit. Upstream
    // krita-ai-diffusion keeps seed controls in the Settings/Queue popup, not on
    // the main docker; gated by settings.show_seed (default false).
    d->generate.seedRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *seedRow = new QHBoxLayout(d->generate.seedRowWidget);
    seedRow->setContentsMargins(0, 0, 0, 0);
    seedRow->addWidget(new QLabel(ComfyTr::tr("Seed:"), d->generate.seedRowWidget));
    seedRow->addWidget(d->generate.checkFixedSeed);
    seedRow->addWidget(d->generate.spinSeed);
    seedRow->addWidget(d->generate.btnRandomSeed);
    genContentLayout->addWidget(d->generate.seedRowWidget);

    d->generate.comboSizePreset = new QComboBox();
    d->generate.comboSizePreset->addItem(ComfyTr::tr("512×512 (default)"), QSize(512, 512));
    d->generate.comboSizePreset->addItem(ComfyTr::tr("768×768"), QSize(768, 768));
    d->generate.comboSizePreset->addItem(ComfyTr::tr("1024×1024"), QSize(1024, 1024));
    d->generate.comboSizePreset->addItem(ComfyTr::tr("2048×2048 (4k)"), QSize(2048, 2048));
    d->generate.comboSizePreset->addItem(ComfyTr::tr("4096×4096 (8k)"), QSize(4096, 4096));
    QObject::connect(d->generate.comboSizePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dock, [dock, d](int idx) {
        QSize s = d->generate.comboSizePreset->itemData(idx).toSize();
        if (s.isValid()) {
            d->generate.spinWidth->setValue(s.width());
            d->generate.spinHeight->setValue(s.height());
        }
    });
    d->generate.spinWidth = new QSpinBox();
    d->generate.spinWidth->setRange(64, 8192);
    d->generate.spinWidth->setValue(512);
    d->generate.spinHeight = new QSpinBox();
    d->generate.spinHeight->setRange(64, 8192);
    d->generate.spinHeight->setValue(512);

    // FAITHFUL_PORT: wrap Size row in a widget so it can be hidden as a unit. Upstream
    // derives generation size from the document extent and the style preset, so the
    // main docker has no Size/W/H controls. Gated by settings.show_size (default false).
    d->generate.sizeRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *sizeRow = new QHBoxLayout(d->generate.sizeRowWidget);
    sizeRow->setContentsMargins(0, 0, 0, 0);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("Size:"), d->generate.sizeRowWidget));
    sizeRow->addWidget(d->generate.comboSizePreset, 1);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("W:"), d->generate.sizeRowWidget));
    sizeRow->addWidget(d->generate.spinWidth);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("H:"), d->generate.sizeRowWidget));
    sizeRow->addWidget(d->generate.spinHeight);
    genContentLayout->addWidget(d->generate.sizeRowWidget);

    d->generate.btnGenerate = new QPushButton(ComfyTr::tr("Generate"));
    d->generate.btnGenerate->setIcon(
        ComfyTheme::icon(QStringLiteral("generate")));  // §5.4: sparkle / magic-style icon
    QObject::connect(d->generate.btnGenerate, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotGenerate);
    d->generate.btnGenerate->installEventFilter(dock);
    // Added to generateActionRowWidget after queue button is created.

    d->inpaint.btnInpaint = new QPushButton(ComfyTr::tr("Inpaint (selection)"));
    d->inpaint.btnInpaint->setToolTip(ComfyTr::tr("Generate in selection."));
    QObject::connect(d->inpaint.btnInpaint, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotInpaint);
    genContentLayout->addWidget(d->inpaint.btnInpaint);
    // FAITHFUL_PORT: upstream merges Inpaint into the Generate-button dropdown
    // (inpaint_mode_button). Hide the standalone button; the slot stays available
    // for the dropdown / actions.
    d->inpaint.btnInpaint->setVisible(false);
}

} // namespace ComfyDockUiBuilderGenerateInternal
