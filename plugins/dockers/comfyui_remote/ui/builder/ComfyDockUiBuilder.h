/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"

#include <QGroupBox>
#include <QObject>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace ComfyDockUiBuilder {

struct Context {
    ComfyUIRemoteDock *dock = nullptr;
    ComfyUIRemoteDock::Private *d = nullptr;
};

struct DockShell {
    QWidget *rootWidget = nullptr;
    QVBoxLayout *rootLayout = nullptr;
    QStackedWidget *mainStack = nullptr;
    QWidget *contentPage = nullptr;
    QVBoxLayout *contentLayout = nullptr;
    QScrollArea *scroll = nullptr;
    QWidget *scrollContent = nullptr;
    QVBoxLayout *scrollLayout = nullptr;
    QGroupBox *genGroup = nullptr;
    QVBoxLayout *genLayout = nullptr;
};

DockShell buildDockShell(const Context &ctx);
void buildWelcomePage(const Context &ctx, DockShell &shell);
void buildSharedChrome(const Context &ctx, DockShell &shell);
void buildGenerateWorkspace(const Context &ctx, DockShell &shell);
void buildGraphWorkspace(const Context &ctx, DockShell &shell);
void buildHistoryPanel(const Context &ctx, QVBoxLayout *scrollLayout);
void buildRegionsPanel(const Context &ctx, QVBoxLayout *scrollLayout);
void finalizeContentScroll(DockShell &shell);
void finalizeGenerateWorkspaceLayout(const Context &ctx, DockShell &shell);
void attachContentPage(const Context &ctx, DockShell &shell);

} // namespace ComfyDockUiBuilder
