/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUiStyle.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRect>

namespace ComfySpinPaint {

inline int buttonColumnWidth()
{
    return ComfyUiStyle::Spacing::spinButtonWidth;
}

inline QRect buttonColumnRect(const QRect &widgetRect)
{
    const int w = buttonColumnWidth();
    return QRect(widgetRect.right() - w + 1, widgetRect.top() + 1, w - 1, widgetRect.height() - 2);
}

inline QRect upButtonRect(const QRect &column)
{
    return QRect(column.left(), column.top(), column.width(), column.height() / 2);
}

inline QRect downButtonRect(const QRect &column)
{
    const int half = column.height() / 2;
    return QRect(column.left(), column.top() + half, column.width(), column.height() - half);
}

inline QRect textRect(const QRect &widgetRect, const QRect &column)
{
    return QRect(widgetRect.left() + ComfyUiStyle::Spacing::nestedPanel,
                 widgetRect.top(),
                 qMax(1, column.left() - widgetRect.left() - ComfyUiStyle::Spacing::nestedPanel),
                 widgetRect.height());
}

inline void paintPanelGradient(QPainter &p, const QRect &rect, const QColor &panel, int radius)
{
    const QColor top = panel.lighter(108);
    const QColor bottom = panel.darker(108);
    QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
    grad.setColorAt(0.0, top);
    grad.setColorAt(1.0, bottom);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRoundedRect(rect, radius, radius);
}

inline void paintTriangle(QPainter &p, const QPointF &center, bool up, const QColor &color)
{
    const qreal halfW = 3.5;
    const qreal halfH = 2.5;
    QPolygonF poly;
    if (up) {
        poly << center + QPointF(0, -halfH) << center + QPointF(-halfW, halfH) << center + QPointF(halfW, halfH);
    } else {
        poly << center + QPointF(0, halfH) << center + QPointF(-halfW, -halfH) << center + QPointF(halfW, -halfH);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(poly);
}

struct SpinPaintState
{
    bool enabled = true;
    bool focused = false;
    bool hoverUp = false;
    bool hoverDown = false;
    bool pressedUp = false;
    bool pressedDown = false;
};

inline void paintSpinBox(QPainter &p,
                         const QRect &widgetRect,
                         const QString &text,
                         const QFont &font,
                         const SpinPaintState &state)
{
    const ComfyUiStyle::Colors style = ComfyUiStyle::colors();
    const QColor panel(style.secondaryPanel);
    const QColor borderColor = state.focused ? QColor(style.highlight) : QColor(style.border);
    const int radius = ComfyUiStyle::Spacing::comboRadius;
    const QRect inner = widgetRect.adjusted(1, 1, -1, -1);

    paintPanelGradient(p, inner, panel, radius);

    p.setPen(QPen(borderColor, state.focused ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(inner, radius, radius);

    const QRect column = buttonColumnRect(widgetRect);
    const QColor btnBase = panel.darker(105);
    const QColor btnHover = btnBase.lighter(112);
    const QColor btnPressed = btnBase.darker(108);

    auto paintButtonHalf = [&](const QRect &btn, bool hover, bool pressed) {
        QColor fill = btnBase;
        if (!state.enabled)
            fill = QColor(style.disabledBg);
        else if (pressed)
            fill = btnPressed;
        else if (hover)
            fill = btnHover;
        p.fillRect(btn, fill);
    };

    const QRect up = upButtonRect(column);
    const QRect down = downButtonRect(column);
    paintButtonHalf(up, state.hoverUp, state.pressedUp);
    paintButtonHalf(down, state.hoverDown, state.pressedDown);

    const QColor divider(style.border);
    p.setPen(QPen(divider, 1));
    p.drawLine(column.left(), column.top(), column.left(), column.bottom());
    p.drawLine(column.left(), up.bottom(), column.right(), up.bottom());

    const QColor arrowColor = state.enabled ? QColor(style.secondaryText) : QColor(style.disabledText);
    paintTriangle(p, up.center(), true, arrowColor);
    paintTriangle(p, down.center(), false, arrowColor);

    const QRect labelRect = textRect(widgetRect, column);
    p.setPen(state.enabled ? QColor(style.primaryText) : QColor(style.disabledText));
    p.setFont(font);
    p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, text);
}

} // namespace ComfySpinPaint
