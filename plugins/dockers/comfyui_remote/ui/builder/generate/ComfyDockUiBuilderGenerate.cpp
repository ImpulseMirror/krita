/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilder.h"
#include "ComfyDockUiBuilderGenerateInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"

#include <QVBoxLayout>
#include <QWidget>

using ComfyDockUiBuilderGenerateInternal::Workspace;

namespace ComfyDockUiBuilder {

void buildGenerateWorkspace(const Context &ctx, DockShell &shell)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    d->generate.genContentContainer = new QWidget(shell.genGroup);
    QVBoxLayout *genContentLayout = new QVBoxLayout(d->generate.genContentContainer);
    genContentLayout->setContentsMargins(0, 0, 0, 0);

    Workspace ws{dock, d, &shell, genContentLayout};
    ComfyDockUiBuilderGenerateInternal::buildUpscaleWidgetsSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildPromptSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildStrengthSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildSeedSizeSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildInpaintSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildModeWorkspaceSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildQueueActionsSection(ws);
    ComfyDockUiBuilderGenerateInternal::buildControlPreviewSection(ws);

    shell.genLayout->addWidget(d->generate.genContentContainer);
}

} // namespace ComfyDockUiBuilder
