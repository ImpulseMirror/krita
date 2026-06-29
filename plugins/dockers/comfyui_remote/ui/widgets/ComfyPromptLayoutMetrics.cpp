/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPromptLayoutMetrics.h"

#include <QtGlobal>

namespace ComfyPromptLayoutMetrics {

int heightForLines(const QFontMetrics &fm, int lines)
{
    return qBound(20, fm.lineSpacing() * lines + kPromptHeightPadPx, 800);
}

int stackFrameHeightForLines(const QFontMetrics &fm, int positiveLines, bool showNegative)
{
    int h = heightForLines(fm, qBound(1, positiveLines, 10));
    if (showNegative)
        h += heightForLines(fm, kNegativeLineCount);
    return h;
}

int positiveLinesForGenerateWorkspace(bool showNegative, int settingsLineCount)
{
    const int lines = qBound(1, settingsLineCount, 10);
    if (showNegative)
        return qMax(kGeneratePositiveLinesMinWithNegative, lines);
    return lines;
}

} // namespace ComfyPromptLayoutMetrics
