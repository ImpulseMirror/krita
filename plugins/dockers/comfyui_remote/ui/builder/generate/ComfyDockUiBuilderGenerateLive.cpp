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
#include "ComfyUiStyle.h"

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
    ComfyUiStyle::applyIconToolButton(btn);
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
    ComfyUiStyle::applyTightRowLayout(liveParamsLayout);
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
    ComfyUiStyle::applyTightRowLayout(livePromptOuter);
    d->live.livePromptHostWidget = new QWidget(d->live.livePromptRowWidget);
    QVBoxLayout *livePromptHostLayout = new QVBoxLayout(d->live.livePromptHostWidget);
    ComfyUiStyle::applyTightRowLayout(livePromptHostLayout, 0);
    livePromptHostLayout->setAlignment(Qt::AlignTop);
    d->live.livePromptButtonsWidget = new QWidget(d->live.livePromptRowWidget);
    QVBoxLayout *livePromptButtonsLayout = new QVBoxLayout(d->live.livePromptButtonsWidget);
    ComfyUiStyle::applyTightRowLayout(livePromptButtonsLayout);
    livePromptButtonsLayout->setAlignment(Qt::AlignTop);
    livePromptOuter->setAlignment(Qt::AlignTop);
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
    ComfyUiStyle::applyTightRowLayout(previewLay);
    d->live.livePreviewRowWidget = new QWidget(d->live.livePreviewGroupBox);
    d->live.livePreviewRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QHBoxLayout *previewRowLay = new QHBoxLayout(d->live.livePreviewRowWidget);
    ComfyUiStyle::applyTightRowLayout(previewRowLay);
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
    if (!d->live.liveTopToolbarWidget)
        return;

    auto *liveToolbarLay = qobject_cast<QHBoxLayout *>(d->live.liveTopToolbarWidget->layout());
    if (!liveToolbarLay)
        return;

    if (d->live.btnLivePlay)
        liveToolbarLay->addWidget(d->live.btnLivePlay);
    if (d->live.btnLiveRecord)
        liveToolbarLay->addWidget(d->live.btnLiveRecord);
    if (d->live.btnLiveApply)
        liveToolbarLay->addWidget(d->live.btnLiveApply);
    if (d->live.btnLiveApplyLayer)
        liveToolbarLay->addWidget(d->live.btnLiveApplyLayer);

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE insertLiveToolbarIntoTopRow count=") << liveToolbarLay->count();
}

} // namespace ComfyDockUiBuilderGenerateInternal
