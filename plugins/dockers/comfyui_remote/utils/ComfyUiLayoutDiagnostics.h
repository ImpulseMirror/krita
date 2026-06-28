/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_UI_LAYOUT_DIAGNOSTICS_H_
#define COMFY_UI_LAYOUT_DIAGNOSTICS_H_

#include <QRect>
#include <QString>
#include <QSize>

class QImage;

class QWidget;
class QLayout;
class QScrollArea;

namespace ComfyUiLayoutDiagnostics {

/// Bump when changing diagnostic probes (grep logcat for COMFY_UI_DIAG).
inline constexpr const char *kBuildMarker = "ui-diag-2025-06-27-r8";

QString maskShapeDescription(const QImage &maskGray);
QString imageAlphaCornerStats(const QImage &img);

void logWidget(const char *tag, const QWidget *widget);
void logLayoutChildren(const char *tag, QLayout *layout);

/// Sum visible essential Generate chrome inside scroll (style row + prompt + strength + CTA).
/// \a dockPrivate is ComfyUIRemoteDock::Private*.
int measureCompactGenerateScrollHeight(void *dockPrivate, QScrollArea *scroll);

void logHistoryThumbnailStage(const char *stage,
                              const QString &resultPath,
                              const QRect &contextBounds,
                              const QRect &targetBounds,
                              bool hasMask,
                              const QSize &imageSize,
                              const QSize &maskSize,
                              const QString &detail);

/// Implemented in .cpp; Private* is ComfyUIRemoteDock::Private (dock-only).
void dumpDockerLayoutForDock(void *dockPrivate, QWidget *dockerRoot, const char *reason);

} // namespace ComfyUiLayoutDiagnostics

#endif
