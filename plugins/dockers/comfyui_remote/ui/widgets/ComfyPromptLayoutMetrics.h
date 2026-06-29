/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PROMPT_LAYOUT_METRICS_H_
#define COMFY_PROMPT_LAYOUT_METRICS_H_

#include <QFontMetrics>
#include <QSize>

/// Shared prompt stack sizing (upstream ai_diffusion TextPromptWidget.line_count).
namespace ComfyPromptLayoutMetrics {

inline constexpr int kPromptHeightPadPx = 10;
inline constexpr int kNegativeLineCount = 1;
inline constexpr int kGeneratePositiveLinesDefault = 3;
inline constexpr int kGeneratePositiveLinesMinWithNegative = 3;
inline constexpr int kLivePositiveLinesDefault = 2;

int heightForLines(const QFontMetrics &fm, int lines);
int stackFrameHeightForLines(const QFontMetrics &fm, int positiveLines, bool showNegative);
int positiveLinesForGenerateWorkspace(bool showNegative, int settingsLineCount);

} // namespace ComfyPromptLayoutMetrics

#endif
