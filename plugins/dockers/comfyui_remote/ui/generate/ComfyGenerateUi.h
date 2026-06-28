/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

class ComfyUIRemoteDock;

namespace ComfyGenerateUi {

void setupInpaintMenus(ComfyUIRemoteDock *dock);
void showInpaintModeMenu(ComfyUIRemoteDock *dock);
void updateOptions(ComfyUIRemoteDock *dock);
void reEnableUi(ComfyUIRemoteDock *dock);

} // namespace ComfyGenerateUi
