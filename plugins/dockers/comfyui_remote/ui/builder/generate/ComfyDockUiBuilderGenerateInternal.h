/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyDockUiBuilder.h"

class QVBoxLayout;

namespace ComfyDockUiBuilderGenerateInternal {

using ComfyDockUiBuilder::DockShell;

struct Workspace {
    ComfyUIRemoteDock *dock = nullptr;
    ComfyUIRemoteDock::Private *d = nullptr;
    DockShell *shell = nullptr;
    QVBoxLayout *genContentLayout = nullptr;
};

void buildUpscaleWidgetsSection(Workspace &ws);
void buildPromptSection(Workspace &ws);
void buildStrengthSection(Workspace &ws);
void buildSeedSizeSection(Workspace &ws);
void buildInpaintSection(Workspace &ws);
void buildModeWorkspaceSection(Workspace &ws);
void buildLiveSection(Workspace &ws);
void insertLiveToolbarIntoTopRow(Workspace &ws);
void buildQueueActionsSection(Workspace &ws);
void buildControlPreviewSection(Workspace &ws);

} // namespace ComfyDockUiBuilderGenerateInternal
