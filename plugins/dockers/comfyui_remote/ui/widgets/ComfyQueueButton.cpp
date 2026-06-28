/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyQueueButton.h"

#include "ComfyTheme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

ComfyQueueButton::ComfyQueueButton(QWidget *parent)
    : QToolButton(parent)
{
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setPopupMode(QToolButton::InstantPopup);
    setAutoRaise(true);
    setDisplayState(DisplayState::Inactive, 0, QString());
}

void ComfyQueueButton::setDisplayState(DisplayState state, int displayCount, const QString &toolTip)
{
    m_state = state;
    m_displayCount = qMax(0, displayCount);
    setText(QString::number(m_displayCount));
    switch (state) {
    case DisplayState::Upload:
        setIcon(ComfyTheme::icon(QStringLiteral("queue-upload")));
        break;
    case DisplayState::Active:
        setIcon(ComfyTheme::icon(QStringLiteral("queue-active")));
        break;
    case DisplayState::Waiting:
        setIcon(ComfyTheme::icon(QStringLiteral("queue-waiting")));
        break;
    case DisplayState::Inactive:
        setIcon(ComfyTheme::icon(QStringLiteral("queue-inactive")));
        break;
    }
    if (!toolTip.isEmpty())
        setToolTip(toolTip);
    update();
}

QSize ComfyQueueButton::sizeHint() const
{
    const QSize original = QToolButton::sizeHint();
    const int textW = fontMetrics().horizontalAdvance(QStringLiteral("99")) + 8;
    const int w = static_cast<int>(original.height() * 0.75) + textW;
    return QSize(w, original.height());
}

void ComfyQueueButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter painter(this);
    QStyle *style = this->style();
    const Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter | Qt::AlignAbsolute;
    const QRect rect = this->rect();
    const QPixmap pixmap = icon().pixmap(static_cast<int>(rect.height() * 0.75));
    QStyle::PrimitiveElement element = QStyle::PE_Widget;
    if (opt.state & QStyle::State_MouseOver)
        element = QStyle::PE_PanelButtonCommand;
    style->drawPrimitive(element, &opt, &painter, this);
    style->drawItemPixmap(&painter, rect.adjusted(4, 0, 0, 0), static_cast<int>(align), pixmap);
    const QRect textRect = rect.adjusted(pixmap.width() + 4, 0, -18, 0);
    style->drawItemText(&painter, textRect, static_cast<int>(align), palette(), true, text());
    painter.translate(static_cast<int>(0.5 * rect.width() - 10), 0);
    style->drawPrimitive(QStyle::PE_IndicatorArrowDown, &opt, &painter, this);
}
