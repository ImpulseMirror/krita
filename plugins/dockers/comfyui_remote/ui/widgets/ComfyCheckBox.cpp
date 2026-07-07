/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyCheckPaint.h"
#include "ComfyUiStyle.h"

#include <QPainter>
#include <QStyleOptionButton>

ComfyCheckBox::ComfyCheckBox(QWidget *parent)
    : QCheckBox(parent)
{
    setStyleSheet(QString());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(this, &QCheckBox::toggled, this, [this](bool) { update(); });
}

ComfyCheckBox::ComfyCheckBox(const QString &text, QWidget *parent)
    : ComfyCheckBox(parent)
{
    setText(text);
}

QRect ComfyCheckBox::indicatorRect() const
{
    const int box = ComfyUiStyle::Spacing::checkboxSize;
    const int y = (height() - box) / 2;
    if (layoutDirection() == Qt::RightToLeft)
        return QRect(width() - box, y, box, box);
    return QRect(0, y, box, box);
}

QRect ComfyCheckBox::labelRect(const QRect &indicator) const
{
    const int gap = ComfyUiStyle::Spacing::rowGap;
    if (layoutDirection() == Qt::RightToLeft)
        return QRect(0, 0, qMax(0, indicator.left() - gap), height());
    return QRect(indicator.right() + gap + 1, 0, qMax(0, width() - indicator.right() - gap - 1), height());
}

void ComfyCheckBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect indicator = indicatorRect();
    ComfyCheckPaint::paintIndicator(painter, indicator, isChecked(), isEnabled(), ComfyUiStyle::colors());

    if (text().isEmpty())
        return;

    const QRect textArea = labelRect(indicator);
    const ComfyUiStyle::Colors style = ComfyUiStyle::colors();
    painter.setPen(isEnabled() ? QColor(style.primaryText) : QColor(style.disabledText));
    painter.setFont(font());
    painter.drawText(textArea, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text());
}

QSize ComfyCheckBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int box = ComfyUiStyle::Spacing::checkboxSize;
    const int gap = ComfyUiStyle::Spacing::rowGap;
    const int textW = text().isEmpty() ? 0 : fm.horizontalAdvance(text()) + gap;
    const int h = qMax(box, fm.height()) + 2;
    return QSize(box + textW + 4, h);
}

QSize ComfyCheckBox::minimumSizeHint() const
{
    return QSize(ComfyUiStyle::Spacing::checkboxSize, ComfyUiStyle::Spacing::checkboxSize);
}
