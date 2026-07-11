/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyComboBox.h"
#include "ComfyDockUiBuilder.h"
#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyLocalization.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace ComfyDockUiBuilder {

void buildGraphWorkspace(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;

    // Top-row workflow library chrome (upstream CustomWorkflowWidget header, minus Open Web UI).
    d->graphWorkflowSelectWidgets = new QWidget(shell.genGroup);
    auto *selectLay = new QHBoxLayout(d->graphWorkflowSelectWidgets);
    ComfyUiStyle::applyTightRowLayout(selectLay, 2);

    d->comboGraphWorkflow = new ComfyComboBox(d->graphWorkflowSelectWidgets);
    d->comboGraphWorkflow->setMinimumContentsLength(16);
    d->comboGraphWorkflow->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
    d->comboGraphWorkflow->setToolTip(ComfyTr::tr("Select a custom workflow JSON from the local library."));
    ComfyUiStyle::applyComboBox(d->comboGraphWorkflow);
    selectLay->addWidget(d->comboGraphWorkflow, 1);

    auto makeTool = [d](const QString &iconStem, const QString &tip) {
        auto *btn = new QToolButton(d->graphWorkflowSelectWidgets);
        btn->setIcon(ComfyTheme::icon(iconStem));
        btn->setToolTip(tip);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        btn->setAutoRaise(true);
        ComfyUiStyle::applyIconToolButton(btn);
        return btn;
    };
    d->btnGraphImportWorkflow =
        makeTool(QStringLiteral("import"), ComfyTr::tr("Import workflow from file"));
    d->btnGraphSaveWorkflow =
        makeTool(QStringLiteral("save"), ComfyTr::tr("Save workflow to file"));
    d->btnGraphDeleteWorkflow =
        makeTool(QStringLiteral("discard"), ComfyTr::tr("Delete the currently selected workflow"));
    selectLay->addWidget(d->btnGraphImportWorkflow);
    selectLay->addWidget(d->btnGraphSaveWorkflow);
    selectLay->addWidget(d->btnGraphDeleteWorkflow);

    QObject::connect(d->comboGraphWorkflow, QOverload<int>::of(&QComboBox::currentIndexChanged), dock,
                     &ComfyUIRemoteDock::slotGraphWorkflowSelected);
    QObject::connect(d->btnGraphImportWorkflow, &QToolButton::clicked, dock,
                     &ComfyUIRemoteDock::slotLoadWorkflowFromFile);
    QObject::connect(d->btnGraphSaveWorkflow, &QToolButton::clicked, dock,
                     &ComfyUIRemoteDock::slotSaveWorkflowToLibrary);
    QObject::connect(d->btnGraphDeleteWorkflow, &QToolButton::clicked, dock,
                     &ComfyUIRemoteDock::slotDeleteWorkflowFromLibrary);

    if (d->workspaceTopRowLayout) {
        // After workspace select / live toolbar, before style combo — Graph swaps style for this.
        const int insertAt = d->workspaceTopRowLayout->indexOf(d->generate.comboPreset);
        if (insertAt >= 0)
            d->workspaceTopRowLayout->insertWidget(insertAt, d->graphWorkflowSelectWidgets, 1);
        else
            d->workspaceTopRowLayout->addWidget(d->graphWorkflowSelectWidgets, 1);
    }
    d->graphWorkflowSelectWidgets->hide();

    d->graphPlaceholderWidget = new QWidget(shell.genGroup);
    d->graphWorkflowEditorLayout = new QVBoxLayout(d->graphPlaceholderWidget);
    d->graphWorkflowEditorLayout->setContentsMargins(0, ComfyUiStyle::Spacing::sectionGap, 0, 0);
    d->graphWorkflowEditorLayout->setSpacing(ComfyUiStyle::Spacing::rowGap);

    // Upstream CustomWorkflowWidget: params (when ETN) + Generate. No paste JSON /
    // reference / continuous-preview chrome on the Graph surface — JSON stays
    // hidden storage filled by import / library select.
    if (d->editCustomWorkflow) {
        d->editCustomWorkflow->setParent(d->graphPlaceholderWidget);
        d->editCustomWorkflow->hide();
        d->editCustomWorkflow->setMaximumHeight(0);
    }
    if (d->live.checkUseReferenceImage) {
        d->live.checkUseReferenceImage->setParent(d->graphPlaceholderWidget);
        d->live.checkUseReferenceImage->hide();
    }
    // checkCustomGraphLive intentionally not created — Graph live loop stays off for first pass.

    if (d->customWorkflowParamsGroup) {
        d->customWorkflowParamsGroup->setParent(d->graphPlaceholderWidget);
        d->graphWorkflowEditorLayout->addWidget(d->customWorkflowParamsGroup);
    }
    d->graphWorkflowEditorLayout->addStretch(1);

    d->graphActionRowHost = new QWidget(d->graphPlaceholderWidget);
    auto *actionHostLay = new QHBoxLayout(d->graphActionRowHost);
    actionHostLay->setContentsMargins(0, 0, 0, 0);
    actionHostLay->setSpacing(0);
    d->graphWorkflowEditorLayout->addWidget(d->graphActionRowHost);

    d->graphPlaceholderWidget->setVisible(false);
    shell.genLayout->addWidget(d->graphPlaceholderWidget);

    dock->refreshGraphWorkflowCombo();
}

} // namespace ComfyDockUiBuilder
