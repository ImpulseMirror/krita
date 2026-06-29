/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilderGenerateInternal.h"

#include "ComfyLocalization.h"
#include "ComfyLiveRunner.h"
#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLoggingCategory>
#include <QToolButton>
#include <QVBoxLayout>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

using ComfyDockShellInternal::LiveSpinnerWidget;

namespace ComfyDockUiBuilderGenerateInternal {

namespace {

QToolButton *makeLiveToolButton(const QString &iconName, const QString &tooltip, QWidget *parent)
{
    auto *btn = new QToolButton(parent);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    btn->setIcon(ComfyTheme::icon(iconName));
    btn->setAutoRaise(true);
    btn->setToolTip(tooltip);
    btn->setVisible(false);
    return btn;
}

} // namespace

void buildLiveSection(Workspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    QVBoxLayout *genContentLayout = ws.genContentLayout;

    d->live.btnLivePlay = makeLiveToolButton(
        QStringLiteral("play"),
        ComfyTr::tr("Start/stop live preview"),
        d->generate.genContentContainer);
    d->live.btnLiveRecord = makeLiveToolButton(
        QStringLiteral("record"),
        ComfyTr::tr("Start live generation and insert images as keyframes into an animation"),
        d->generate.genContentContainer);
    d->live.btnLiveApply = makeLiveToolButton(
        QStringLiteral("apply"),
        ComfyTr::tr("Copy the current result to the active layer"),
        d->generate.genContentContainer);
    d->live.btnLiveApply->setEnabled(false);
    d->live.btnLiveApplyLayer = makeLiveToolButton(
        QStringLiteral("apply-layer"),
        ComfyTr::tr("Create a new layer with the current result"),
        d->generate.genContentContainer);
    d->live.btnLiveApplyLayer->setEnabled(false);

    QObject::connect(d->live.btnLivePlay, &QToolButton::clicked, dock, [dock, d]() {
        if (!d->live.checkLiveMode)
            return;
        const bool next = !d->live.checkLiveMode->isChecked();
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_LIVE toolbar play clicked active=") << next;
        d->live.checkLiveMode->setChecked(next);
    });
    QObject::connect(d->live.btnLiveRecord, &QToolButton::clicked, dock, [dock, d]() {
        if (!d->live.checkLiveRecord)
            return;
        const bool next = !d->live.checkLiveRecord->isChecked();
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_LIVE toolbar record clicked recording=") << next;
        d->live.checkLiveRecord->setChecked(next);
    });
    QObject::connect(d->live.btnLiveApply, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotAiDiffusionApply);
    QObject::connect(d->live.btnLiveApplyLayer, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotAiDiffusionApplyAlternative);

    d->live.liveParamsRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *liveParamsLayout = new QHBoxLayout(d->live.liveParamsRowWidget);
    liveParamsLayout->setContentsMargins(0, 0, 0, 0);
    d->live.liveParamsRowWidget->setVisible(false);
    genContentLayout->insertWidget(0, d->live.liveParamsRowWidget);

    d->live.btnLiveRandomSeed = makeLiveToolButton(
        QStringLiteral("random"),
        ComfyTr::tr("Generate a random seed value to get a variation of the image."),
        d->live.liveParamsRowWidget);
    d->live.btnLiveRandomSeed->setVisible(true);
    QObject::connect(d->live.btnLiveRandomSeed, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotRandomSeed);

    d->live.btnLiveEditToggle = makeLiveToolButton(
        QStringLiteral("edit"),
        ComfyTr::tr("Switch to edit mode"),
        d->live.liveParamsRowWidget);
    d->live.btnLiveEditToggle->setVisible(true);
    QObject::connect(d->live.btnLiveEditToggle, &QToolButton::clicked, dock, [dock, d]() {
        if (!d->generate.checkEditMode)
            return;
        const bool next = !d->generate.checkEditMode->isChecked();
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_LIVE edit toggle clicked editMode=") << next;
        d->generate.checkEditMode->setChecked(next);
    });
    if (d->generate.checkEditMode) {
        QObject::connect(d->generate.checkEditMode, &QCheckBox::toggled, dock, [dock](bool) {
            dock->updateLiveToolbarState();
        });
    }

    d->live.livePromptRowWidget = new QWidget(d->generate.genContentContainer);
    QHBoxLayout *livePromptOuter = new QHBoxLayout(d->live.livePromptRowWidget);
    livePromptOuter->setContentsMargins(0, 0, 0, 0);
    d->live.livePromptHostWidget = new QWidget(d->live.livePromptRowWidget);
    QVBoxLayout *livePromptHostLayout = new QVBoxLayout(d->live.livePromptHostWidget);
    livePromptHostLayout->setContentsMargins(0, 0, 0, 0);
    livePromptHostLayout->setSpacing(0);
    d->live.livePromptButtonsWidget = new QWidget(d->live.livePromptRowWidget);
    QVBoxLayout *livePromptButtonsLayout = new QVBoxLayout(d->live.livePromptButtonsWidget);
    livePromptButtonsLayout->setContentsMargins(0, 0, 0, 0);
    livePromptButtonsLayout->setSpacing(2);
    livePromptButtonsLayout->setAlignment(Qt::AlignTop);
    livePromptOuter->addWidget(d->live.livePromptHostWidget, 1);
    livePromptOuter->addWidget(d->live.livePromptButtonsWidget);
    d->live.livePromptRowWidget->setVisible(false);
    genContentLayout->insertWidget(1, d->live.livePromptRowWidget);

    if (d->live.checkLiveMode) {
        QObject::connect(d->live.checkLiveMode, &QCheckBox::toggled, dock, [dock](bool on) {
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_LIVE mode toggled active=") << on;
            dock->updateLiveToolbarState();
        });
    }
    if (d->live.checkLiveRecord) {
        QObject::connect(d->live.checkLiveRecord, &QCheckBox::toggled, dock, [dock](bool on) {
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_LIVE record toggled recording=") << on;
            dock->updateLiveToolbarState();
        });
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote() << QStringLiteral("COMFY_LIVE buildLiveSection done");

    d->live.livePreviewGroupBox = new QWidget(d->generate.genContentContainer);
    d->live.livePreviewGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d->live.livePreviewGroupBox->setVisible(false);
    QVBoxLayout *previewLay = new QVBoxLayout(d->live.livePreviewGroupBox);
    previewLay->setContentsMargins(0, 0, 0, 0);
    d->live.livePreviewRowWidget = new QWidget(d->live.livePreviewGroupBox);
    d->live.livePreviewRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QHBoxLayout *previewRowLay = new QHBoxLayout(d->live.livePreviewRowWidget);
    previewRowLay->setContentsMargins(0, 0, 0, 0);
    previewRowLay->setSpacing(4);
    d->live.livePreviewArea = new QLabel(d->live.livePreviewRowWidget);
    d->live.livePreviewArea->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    d->live.livePreviewArea->setMinimumSize(128, 128);
    d->live.livePreviewArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d->live.livePreviewArea->setScaledContents(false);
    previewRowLay->addWidget(d->live.livePreviewArea, 1);
    if (d->live.liveSpinner) {
        d->live.liveSpinner->setParent(d->live.livePreviewRowWidget);
        previewRowLay->addWidget(d->live.liveSpinner, 0, Qt::AlignTop);
    }
    previewLay->addWidget(d->live.livePreviewRowWidget, 1);
}

void insertLiveToolbarIntoTopRow(Workspace &ws)
{
    ComfyUIRemoteDock::Private *d = ws.d;
    if (!d->workspaceTopRowLayout)
        return;

    const int styleIndex = d->workspaceTopRowLayout->indexOf(d->generate.comboPreset);
    int insertAt = styleIndex >= 0 ? styleIndex : d->workspaceTopRowLayout->count();
    if (d->live.btnLivePlay)
        d->workspaceTopRowLayout->insertWidget(insertAt++, d->live.btnLivePlay);
    if (d->live.btnLiveRecord)
        d->workspaceTopRowLayout->insertWidget(insertAt++, d->live.btnLiveRecord);
    if (d->live.btnLiveApply)
        d->workspaceTopRowLayout->insertWidget(insertAt++, d->live.btnLiveApply);
    if (d->live.btnLiveApplyLayer)
        d->workspaceTopRowLayout->insertWidget(insertAt++, d->live.btnLiveApplyLayer);

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE insertLiveToolbarIntoTopRow insertAt=") << insertAt;
}

} // namespace ComfyDockUiBuilderGenerateInternal
