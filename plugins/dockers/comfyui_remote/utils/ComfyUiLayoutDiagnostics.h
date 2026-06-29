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
inline constexpr const char *kBuildMarker = "ui-diag-2025-06-28-r21";

QString maskShapeDescription(const QImage &maskGray);
QString imageAlphaCornerStats(const QImage &img);

void logWidget(const char *tag, const QWidget *widget);
void logLayoutChildren(const char *tag, QLayout *layout);

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

/// Implemented in .cpp; Private* is ComfyUIRemoteDock::Private (dock-only).
void dumpDockerLayoutForDock(void *dockPrivate, QWidget *dockerRoot, const char *reason);
/// One-line snapshot: contentPage children, gen/hist geometry, list items (logcat: COMFY_UI_DIAG genHistLayout).
void logGenerateHistoryLayout(void *dockPrivate, const char *reason);
/// Render-time chrome geometry chain (logcat: COMFY_UI_DIAG wsChrome).
void logWorkspaceChromeLayout(void *dockPrivate, QWidget *dockerRoot, const char *reason);
/// Live workspace layout snapshot (logcat: COMFY_UI_DIAG liveLayout).
void logLiveWorkspaceLayout(void *dockPrivate, QWidget *dockerRoot, const char *reason);
/// Reparent live preview below gen chrome on contentPage (Live workspace only).
void restoreLivePreviewPanelLayout(void *dockPrivate, QWidget *contentPage);

} // namespace ComfyUiLayoutDiagnostics

#endif
