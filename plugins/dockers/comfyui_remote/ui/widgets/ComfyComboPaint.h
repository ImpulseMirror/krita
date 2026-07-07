/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfySpinPaint.h"
#include "ComfyUiStyle.h"

#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QRect>

namespace ComfyComboPaint {

inline QRect arrowColumnRect(const QRect &widgetRect)
{
    const int w = ComfyUiStyle::Spacing::comboArrowWidth;
    return QRect(widgetRect.right() - w + 1, widgetRect.top() + 1, w - 1, widgetRect.height() - 2);
}

inline QRect textRect(const QRect &widgetRect, const QRect &column)
{
    return QRect(widgetRect.left() + ComfyUiStyle::Spacing::nestedPanel,
                 widgetRect.top(),
                 qMax(1, column.left() - widgetRect.left() - ComfyUiStyle::Spacing::nestedPanel),
                 widgetRect.height());
}

struct ComboPaintState
{
    bool enabled = true;
    bool focused = false;
    bool hover = false;
    bool open = false;
};

inline void paintComboBox(QPainter &p,
                          const QRect &widgetRect,
                          const QString &text,
                          const QFont &font,
                          const QIcon &icon,
                          const ComboPaintState &state)
{
    const ComfyUiStyle::Colors style = ComfyUiStyle::colors();
    const QColor panel(style.secondaryPanel);
    QColor borderColor = QColor(style.border);
    if (state.open || state.focused)
        borderColor = QColor(style.highlight);
    else if (state.hover)
        borderColor = panel.lighter(112);

    const int radius = ComfyUiStyle::Spacing::comboRadius;
    const QRect inner = widgetRect.adjusted(1, 1, -1, -1);

    ComfySpinPaint::paintPanelGradient(p, inner, panel, radius);

    p.setPen(QPen(borderColor, (state.open || state.focused) ? 2 : 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(inner, radius, radius);

    const QRect column = arrowColumnRect(widgetRect);
    const QColor btnBase = panel.darker(105);
    const QColor btnFill = state.hover || state.open ? btnBase.lighter(112) : btnBase;
    p.fillRect(column, state.enabled ? btnFill : QColor(style.disabledBg));

    p.setPen(QPen(QColor(style.border), 1));
    p.drawLine(column.left(), column.top(), column.left(), column.bottom());

    const QColor arrowColor = state.enabled ? QColor(style.secondaryText) : QColor(style.disabledText);
    ComfySpinPaint::paintTriangle(p, column.center(), false, arrowColor);

    QRect labelRect = textRect(widgetRect, column);
    if (!icon.isNull()) {
        const int iconSide = qMin(labelRect.height() - 4, 16);
        const QRect iconRect(labelRect.left(),
                             labelRect.top() + (labelRect.height() - iconSide) / 2,
                             iconSide,
                             iconSide);
        p.drawPixmap(iconRect, icon.pixmap(iconSide, iconSide));
        labelRect.setLeft(iconRect.right() + 4);
    }

    if (!text.isEmpty()) {
        p.setPen(state.enabled ? QColor(style.primaryText) : QColor(style.disabledText));
        p.setFont(font);
        p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

} // namespace ComfyComboPaint
