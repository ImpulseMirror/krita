/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilder.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyControlLayerListWidget.h"
#include "ComfyUiStyle.h"

#include <QLabel>
#include <QAbstractScrollArea>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "ComfyUiLayoutDiagnostics.h"

namespace ComfyDockUiBuilder {

DockShell buildDockShell(const Context &ctx)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    Q_UNUSED(dock);
    DockShell shell;
    shell.rootWidget = new QWidget(dock);
    shell.rootLayout = new QVBoxLayout(shell.rootWidget);
    // §13.1 Dock stack: index 0 = Welcome, 1 = workspace content (Generate/Upscale/Live/Animation/Graph via combo)
    shell.mainStack = new QStackedWidget(shell.rootWidget);
    d->mainStack = shell.mainStack;
    return shell;
}

void finalizeContentScroll(DockShell &shell)
{
    shell.scroll->setWidget(shell.scrollContent);
    // Upper controls keep natural height; history below takes remaining docker space.
    shell.scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    shell.scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shell.scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    shell.scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    shell.contentLayout->addWidget(shell.scroll, 0);
}

void finalizeGenerateWorkspaceLayout(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock::Private *d = ctx.d;
    if (!d->generate.genContentContainer || !d->generate.regionPromptWidget)
        return;
    auto *lay = qobject_cast<QVBoxLayout *>(d->generate.genContentContainer->layout());
    if (!lay)
        return;

    if (d->generate.rootPromptColumnWidget) {
        d->generate.rootPromptColumnWidget->setVisible(false);
        lay->removeWidget(d->generate.rootPromptColumnWidget);
    }
    if (d->generate.negativePromptBlock) {
        d->generate.negativePromptBlock->setVisible(false);
        lay->removeWidget(d->generate.negativePromptBlock);
    }

    d->generate.regionPromptWidget->setParent(d->generate.genContentContainer);
    lay->removeWidget(d->generate.regionPromptWidget);
    const int insertAt = 0;
    lay->insertWidget(insertAt, d->generate.regionPromptWidget);

    if (d->history.histGroupBox && shell.contentLayout) {
        if (QWidget *oldParent = d->history.histGroupBox->parentWidget()) {
            if (QLayout *oldLay = oldParent->layout())
                oldLay->removeWidget(d->history.histGroupBox);
        }
        d->history.histGroupBox->setParent(shell.contentPage);
        const int statusIndex = shell.contentLayout->indexOf(d->labelStatus);
        if (statusIndex >= 0)
            shell.contentLayout->insertWidget(statusIndex, d->history.histGroupBox, 1);
        else
            shell.contentLayout->addWidget(d->history.histGroupBox, 1);
    }

    if (d->generate.regionsGroupBox && shell.scrollLayout) {
        shell.scrollLayout->removeWidget(d->generate.regionsGroupBox);
        d->generate.regionsGroupBox->setParent(shell.rootWidget);
        d->generate.regionsGroupBox->hide();
        d->generate.regionsGroupBox->setFixedHeight(0);
    }

    // FAITHFUL_PORT: compact Generate chrome sits directly on contentPage; history takes
    // remaining height below. The scroll wrapper only inflated sizeHint and hid history.
    if (d->generate.genGroupBox && shell.contentLayout && shell.scroll) {
        if (shell.scrollLayout)
            shell.scrollLayout->removeWidget(d->generate.genGroupBox);
        shell.contentLayout->removeWidget(shell.scroll);
        d->generate.genGroupBox->setParent(shell.contentPage);
        if (shell.contentLayout->indexOf(d->generate.genGroupBox) < 0)
            shell.contentLayout->insertWidget(0, d->generate.genGroupBox, 0);
        delete shell.scroll;
        shell.scroll = nullptr;
        shell.scrollContent = nullptr;
        shell.scrollLayout = nullptr;
    }

    if (d->live.livePreviewGroupBox && d->generate.genContentContainer) {
        ComfyUiLayoutDiagnostics::restoreLivePreviewPanelLayout(d, shell.contentPage);
    }

    if (d->generate.regionPromptWidget && d->generate.rootControlLayerList)
        d->generate.regionPromptWidget->embedRegionControlPanel(d->generate.rootControlLayerList);

    if (d->generate.regionsGroupBox)
        d->generate.regionsGroupBox->setVisible(false);

    if (shell.contentLayout)
        shell.contentLayout->activate();
    if (shell.contentPage)
        shell.contentPage->updateGeometry();
}

void attachContentPage(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    d->labelStatus = new QLabel(ComfyTr::tr("Use Settings to configure server URL and advanced options."));
    d->labelStatus->setWordWrap(true);
    ComfyUiStyle::styleDescription(d->labelStatus);
    shell.contentLayout->addWidget(d->labelStatus);
    d->mainStack->addWidget(shell.contentPage);
    shell.rootLayout->addWidget(d->mainStack);
    ComfyUiStyle::applyWidgetTree(shell.rootWidget);
    dock->setWidget(shell.rootWidget);
}

} // namespace ComfyDockUiBuilder
