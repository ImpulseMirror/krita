/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilder.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionPromptWidget.h"

#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

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
    shell.scrollLayout->addStretch();
    shell.scroll->setWidget(shell.scrollContent);
    shell.contentLayout->addWidget(shell.scroll);
}

void finalizeGenerateWorkspaceLayout(const Context &ctx)
{
    ComfyUIRemoteDock::Private *d = ctx.d;
    if (!d->generate.genContentContainer || !d->generate.regionPromptWidget)
        return;
    auto *lay = qobject_cast<QVBoxLayout *>(d->generate.genContentContainer->layout());
    if (!lay)
        return;

    if (d->inpaint.labelPrompt)
        d->inpaint.labelPrompt->setVisible(false);
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

    if (d->history.histGroupBox && d->progressBar) {
        if (d->history.histGroupBox->parentWidget() != d->generate.genContentContainer) {
            if (QWidget *oldParent = d->history.histGroupBox->parentWidget()) {
                if (QLayout *oldLay = oldParent->layout())
                    oldLay->removeWidget(d->history.histGroupBox);
            }
            d->history.histGroupBox->setParent(d->generate.genContentContainer);
        }
        lay->removeWidget(d->history.histGroupBox);
        const int afterProgress = lay->indexOf(d->progressBar) + 1;
        lay->insertWidget(afterProgress, d->history.histGroupBox);
    }

    if (d->generate.regionsGroupBox)
        d->generate.regionsGroupBox->setVisible(false);
}

void attachContentPage(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    d->labelStatus = new QLabel(ComfyTr::tr("Use Settings to configure server URL and advanced options."));
    d->labelStatus->setWordWrap(true);
    shell.contentLayout->addWidget(d->labelStatus);
    d->mainStack->addWidget(shell.contentPage);
    shell.rootLayout->addWidget(d->mainStack);
    dock->setWidget(shell.rootWidget);
}

} // namespace ComfyDockUiBuilder
