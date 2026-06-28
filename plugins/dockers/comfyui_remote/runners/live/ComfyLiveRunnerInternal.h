/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyPrepareLiveWorkflow.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>

namespace ComfyLiveRunnerInternal {

QImage cropLiveResultToTarget(const QImage &image, const ComfyPrepareLiveWorkflow::Result &prep);
QUrl comfyImageUploadUrl(const QString &serverUrl);

} // namespace ComfyLiveRunnerInternal
