/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <kis_types.h>

class KisViewManager;

namespace ComfyControlRunnerInternal {

KisNodeSP findNodeByName(KisNodeSP root, const QString &name);
QImage canvasImageForControlLayerJob(KisImageSP image, KisViewManager *viewManager, const QString &mode);
QRect selectionCropLocalRect(KisImageSP image, KisViewManager *viewManager, const QSize &fullExportSize, QImage *canvasImg);

} // namespace ComfyControlRunnerInternal
