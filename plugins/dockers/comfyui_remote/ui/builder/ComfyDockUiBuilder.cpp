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
#include <QAbstractScrollArea>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "ComfyUiLayoutDiagnostics.h"

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

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
    ComfyUiLayoutDiagnostics::logWidget("finalizeContentScroll.scroll", shell.scroll);
    ComfyUiLayoutDiagnostics::logLayoutChildren("finalizeContentScroll.contentLayout", shell.contentLayout);
}

void finalizeGenerateWorkspaceLayout(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock::Private *d = ctx.d;
    if (!d->generate.genContentContainer || !d->generate.regionPromptWidget) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG finalizeGenerateWorkspaceLayout EARLY_RETURN genContent=")
            << (d->generate.genContentContainer != nullptr) << QStringLiteral("regionPrompt=")
            << (d->generate.regionPromptWidget != nullptr);
        return;
    }
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

    if (d->generate.regionsGroupBox)
        d->generate.regionsGroupBox->setVisible(false);

    // Progress sits flush under controls, directly above history (FAITHFUL_PORT).
    if (d->progressBar && shell.contentLayout) {
        if (QWidget *oldParent = d->progressBar->parentWidget()) {
            if (QLayout *oldLay = oldParent->layout())
                oldLay->removeWidget(d->progressBar);
        }
        d->progressBar->setParent(shell.contentPage);
        const int histIndex = d->history.histGroupBox
                                  ? shell.contentLayout->indexOf(d->history.histGroupBox)
                                  : -1;
        if (histIndex >= 0)
            shell.contentLayout->insertWidget(histIndex, d->progressBar, 0);
        else
            shell.contentLayout->addWidget(d->progressBar, 0);
    }
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG finalizeGenerateWorkspaceLayout histParent=")
        << (d->history.histGroupBox && d->history.histGroupBox->parentWidget()
                ? d->history.histGroupBox->parentWidget()->objectName()
                : QStringLiteral("null"))
        << QStringLiteral("progressParent=")
        << (d->progressBar && d->progressBar->parentWidget() ? d->progressBar->parentWidget()->objectName()
                                                             : QStringLiteral("null"));
    ComfyUiLayoutDiagnostics::logLayoutChildren("finalizeGenerateWorkspaceLayout.contentLayout", shell.contentLayout);
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
