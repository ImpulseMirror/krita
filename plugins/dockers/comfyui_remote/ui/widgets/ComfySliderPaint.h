/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUiStyle.h"

#include <QColor>
#include <QPainter>
#include <QRect>

#include <QtGlobal>

namespace ComfySliderPaint {

inline QRect horizontalTrackRect(const QRect &widgetRect)
{
    const int margin = ComfyUiStyle::Spacing::sliderMargin;
    const int h = ComfyUiStyle::Spacing::sliderTrack;
    return QRect(widgetRect.left() + margin,
                 widgetRect.top() + (widgetRect.height() - h) / 2,
                 qMax(1, widgetRect.width() - margin * 2),
                 h);
}

inline int xFromValue(int value, int minValue, int maxValue, const QRect &track)
{
    if (maxValue <= minValue || track.width() <= 1)
        return track.left();
    const double ratio = (value - minValue) / static_cast<double>(maxValue - minValue);
    return track.left() + qRound(ratio * track.width());
}

inline int valueFromX(int x, int minValue, int maxValue, const QRect &track)
{
    if (track.width() <= 1 || maxValue <= minValue)
        return minValue;
    const int clamped = qBound(track.left(), x, track.right());
    const double ratio = (clamped - track.left()) / static_cast<double>(track.width());
    return qBound(minValue, qRound(minValue + ratio * static_cast<double>(maxValue - minValue)), maxValue);
}

inline QRect squareHandleRect(int centerX, int centerY, int handleSize)
{
    const int half = handleSize / 2;
    return QRect(centerX - half, centerY - half, handleSize, handleSize);
}

inline void paintHorizontalTrack(QPainter &p,
                                 const QRect &track,
                                 const QColor &base,
                                 const QColor &fill,
                                 int fillRightX)
{
    const int radius = ComfyUiStyle::Spacing::sliderTrack / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(base);
    p.drawRoundedRect(track, radius, radius);

    if (fillRightX > track.left()) {
        const QRect filled(track.left(), track.top(), fillRightX - track.left(), track.height());
        p.setBrush(fill);
        p.drawRoundedRect(filled, radius, radius);
    }
}

inline void paintSquareHandle(QPainter &p,
                              const QRect &handle,
                              const QColor &fill,
                              const QColor &border,
                              bool active)
{
    const int corner = qMax(2, handle.width() / 5);
    p.setPen(QPen(border, active ? 2 : 1));
    p.setBrush(fill);
    p.drawRoundedRect(handle.adjusted(1, 1, -1, -1), corner, corner);
}

inline void paintTickMarks(QPainter &p,
                           const QRect &track,
                           int minValue,
                           int maxValue,
                           int tickInterval,
                           const QColor &color)
{
    if (tickInterval <= 0 || maxValue <= minValue)
        return;
    const int tickLen = qMax(2, ComfyUiStyle::Spacing::sliderTrack + 1);
    p.setPen(QPen(color, 1));
    for (int v = minValue; v <= maxValue; v += tickInterval) {
        const int x = xFromValue(v, minValue, maxValue, track);
        p.drawLine(x, track.bottom() + 1, x, track.bottom() + 1 + tickLen);
        p.drawLine(x, track.top() - 1, x, track.top() - 1 - tickLen);
    }
}

} // namespace ComfySliderPaint
