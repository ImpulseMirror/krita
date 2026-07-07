/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUiStyle.h"

#include <QColor>
#include <QPainter>
#include <QRect>

namespace ComfyCheckPaint {

inline void paintCheckmark(QPainter &p, const QRect &rect, const QColor &color)
{
    const qreal inset = rect.width() * 0.22;
    const QPointF p1(rect.left() + inset, rect.center().y());
    const QPointF p2(rect.left() + rect.width() * 0.40, rect.bottom() - inset);
    const QPointF p3(rect.right() - inset, rect.top() + inset);

    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(QVector<QPointF>{p1, p2, p3});
}

inline void paintIndicator(QPainter &p,
                           const QRect &rect,
                           bool checked,
                           bool enabled,
                           const ComfyUiStyle::Colors &style)
{
    const QColor border(style.border);
    const QColor interior = enabled ? QColor(style.inputBg) : QColor(style.disabledBg);
    const QRect box = rect.adjusted(0, 0, -1, -1);

    p.setPen(QPen(border, 1));
    p.setBrush(interior);
    p.drawRoundedRect(box, 2, 2);

    if (checked) {
        const QColor mark = enabled ? QColor(style.primaryText) : QColor(style.disabledText);
        paintCheckmark(p, box, mark);
    }
}

} // namespace ComfyCheckPaint
