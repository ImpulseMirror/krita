/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

class ComfyUIRemoteDock;

namespace ComfyControlRunner {

void stopPreviewPolling(ComfyUIRemoteDock *dock);
void syncPreviewRangeFromSettings(ComfyUIRemoteDock *dock);
void syncPoseGuidePeopleCountFromSettings(ComfyUIRemoteDock *dock);
void onPreviewRun(ComfyUIRemoteDock *dock);
void onPreviewPollTimer(ComfyUIRemoteDock *dock);
void stopLayerJobPolling(ComfyUIRemoteDock *dock);
void refreshLayerJobGenerateButtons(ComfyUIRemoteDock *dock);
void onLayerJobRun(ComfyUIRemoteDock *dock, bool forRegion, int entryIndex);
void onLayerJobPollTimer(ComfyUIRemoteDock *dock);

} // namespace ComfyControlRunner
