/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyComboBox.h"
#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyLocalization.h"
#include "ComfyFormUi.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include "ComfySpinBox.h"
#include <QVBoxLayout>

namespace ComfyDockUiBuilderGenerateInternal {

void buildSeedSizeSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    const ComfyFormUi::SeedControls seedControls = ComfyFormUi::addSeedControls(dock);
    d->generate.seedControlRow = seedControls.row;
    d->generate.checkFixedSeed = seedControls.fixedSeedCheckBox;
    d->generate.spinSeed = seedControls.seedSpinBox;
    d->generate.btnRandomSeed = seedControls.randomSeedButton;
    d->generate.spinSeed->setValue(0);
    QObject::connect(d->generate.btnRandomSeed, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotRandomSeed);

    d->generate.seedRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *seedRow = new QHBoxLayout(d->generate.seedRowWidget);
    ComfyUiStyle::applyTightRowLayout(seedRow);
    seedRow->addWidget(new QLabel(ComfyTr::tr("Seed:"), d->generate.seedRowWidget));
    // Seed controls live in queue popup; row kept for Live workspace spin reparent.
    d->generate.seedRowWidget->hide();
    genContentLayout->addWidget(d->generate.seedRowWidget);

    d->generate.comboSizePreset = new ComfyComboBox();
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
    d->generate.spinWidth = new ComfySpinBox();
    d->generate.spinWidth->setRange(64, 8192);
    d->generate.spinWidth->setValue(512);
    d->generate.spinHeight = new ComfySpinBox();
    d->generate.spinHeight->setRange(64, 8192);
    d->generate.spinHeight->setValue(512);

    d->generate.sizeRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *sizeRow = new QHBoxLayout(d->generate.sizeRowWidget);
    ComfyUiStyle::applyTightRowLayout(sizeRow);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("Size:"), d->generate.sizeRowWidget));
    sizeRow->addWidget(d->generate.comboSizePreset, 1);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("W:"), d->generate.sizeRowWidget));
    sizeRow->addWidget(d->generate.spinWidth);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("H:"), d->generate.sizeRowWidget));
    sizeRow->addWidget(d->generate.spinHeight);
    genContentLayout->addWidget(d->generate.sizeRowWidget);

    d->generate.btnGenerate = new QPushButton(ComfyTr::tr("Generate"));
    d->generate.btnGenerate->setIcon(ComfyTheme::icon(QStringLiteral("generate")));
    ComfyUiStyle::applyPrimaryButton(d->generate.btnGenerate);
    QObject::connect(d->generate.btnGenerate, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotGenerate);
    d->generate.btnGenerate->installEventFilter(dock);

    d->inpaint.btnInpaint = new QPushButton(ComfyTr::tr("Inpaint (selection)"));
    d->inpaint.btnInpaint->setToolTip(ComfyTr::tr("Generate in selection."));
    QObject::connect(d->inpaint.btnInpaint, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotInpaint);
    genContentLayout->addWidget(d->inpaint.btnInpaint);
    d->inpaint.btnInpaint->setVisible(false);
}

} // namespace ComfyDockUiBuilderGenerateInternal
