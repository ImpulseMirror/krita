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
class QScrollArea;

namespace ComfyUiLayoutDiagnostics {

QString maskShapeDescription(const QImage &maskGray);
QString imageAlphaCornerStats(const QImage &img);

/// Sum visible essential Generate chrome inside scroll (style row + prompt + strength + CTA).
/// \a dockPrivate is ComfyUIRemoteDock::Private*.
int measureEssentialGenerateChromeHeight(void *dockPrivate, int contentWidth);
int measureEssentialGenerateContentHeight(void *dockPrivate, int contentWidth);
int measureCompactGenerateScrollHeight(void *dockPrivate, QScrollArea *scroll);
/// Y offset of workspace selector within genGroup (compact chrome top inset).
int measureWorkspaceTopChromeInset(void *dockPrivate);
/// genGroup top edge Y within contentPage (dock chrome offset from page top).
int measureGenGroupTopOnContentPage(void *dockPrivate);
/// Primary top-row control Y within docker root (workspace combo or mode combo).
int measurePrimaryChromeTopOnDocker(void *dockPrivate, QWidget *dockerRoot);
/// regionPrompt height when upscale workspace active (expect 0).
int measureRegionPromptHeightOnUpscale(void *dockPrivate);

void logHistoryThumbnailStage(const char *stage,
                              const QString &resultPath,
                              const QRect &contextBounds,
                              const QRect &targetBounds,
                              bool hasMask,
                              const QSize &imageSize,
                              const QSize &maskSize,
                              const QString &detail);

/// Log widget geometry + parent chain (logcat: COMFY_UI_DIAG SLIDER).
void logSliderMetrics(const char *reason, QWidget *widget);

/// Log strength-row hierarchy after dock layout settles.
void logStrengthRowMetrics(void *dockPrivate);

/// Reparent slider + icon buttons onto the Generate strength row (shared with Live workspace).
void ensureGenerateStrengthRowLayout(void *dockPrivate);

/// Reparent live preview below gen chrome on contentPage (Live workspace only).
void restoreLivePreviewPanelLayout(void *dockPrivate, QWidget *contentPage);

} // namespace ComfyUiLayoutDiagnostics

#endif
