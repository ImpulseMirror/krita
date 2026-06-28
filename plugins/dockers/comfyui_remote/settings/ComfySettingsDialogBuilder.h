/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"

#include <QDialog>
#include <QObject>
#include <QStackedWidget>

namespace ComfySettingsDialogBuilder {

struct Context {
    ComfyUIRemoteDock *dock = nullptr;
    ComfyUIRemoteDock::Private *d = nullptr;
    QDialog *dialog = nullptr;
};

struct PerformanceTabResult {
    std::function<void()> syncSlidersFromDock;
};

struct StylesTabResult {
    std::function<void()> syncFromDock;
};

void buildConnectionTab(const Context &ctx, QStackedWidget *stack);
StylesTabResult buildStylesTab(const Context &ctx, QStackedWidget *stack);
void buildDiffusionTab(const Context &ctx, QStackedWidget *stack);
void buildInterfaceTab(const Context &ctx, QStackedWidget *stack);
PerformanceTabResult buildPerformanceTab(const Context &ctx, QStackedWidget *stack);

} // namespace ComfySettingsDialogBuilder
