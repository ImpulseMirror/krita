/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>

class ComfyUIRemoteDock;

namespace ComfyInpaintRunner {

void onPollTimer(ComfyUIRemoteDock *dock);

/// Inpaint click — prepare, canvas/mask upload, workflow build (was slotInpaint).
void onInpaint(ComfyUIRemoteDock *dock);

void beginUploadPipeline(ComfyUIRemoteDock *dock);
void submitWorkflow(ComfyUIRemoteDock *dock, QJsonObject workflow);

} // namespace ComfyInpaintRunner
