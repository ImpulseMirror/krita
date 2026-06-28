/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilder.h"
#include "ComfySettingsDialogBuilderStylesInternal.h"

namespace ComfySettingsDialogBuilder {

StylesTabResult buildStylesTab(const Context &ctx, QStackedWidget *stack)
{
    ComfySettingsDialogBuilderStylesInternal::StylesWorkspace ws;
    ws.dock = ctx.dock;
    ws.d = ctx.d;
    ws.dialog = ctx.dialog;
    ws.stack = stack;
    ComfySettingsDialogBuilderStylesInternal::buildStylesTabWidgets(ws);
    return ComfySettingsDialogBuilderStylesInternal::wireStylesTabSync(ws);
}

} // namespace ComfySettingsDialogBuilder
