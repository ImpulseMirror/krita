/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUIRemoteDockPrivate.h"

#include "ComfyControlLayer.h"

#include <QImage>
#include <QList>

class ComfyUIRemoteDock;

namespace ComfyUpscaleRunnerInternal {

QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForUpscale(const ComfyUIRemoteDock::Private *d);
QList<ComfyControlLayerEntry> controlLayersForUpscale(const ComfyUIRemoteDock::Private *d);
QImage maskPngForComfyUpload(const QImage &maskGray, int targetW, int targetH);

} // namespace ComfyUpscaleRunnerInternal
