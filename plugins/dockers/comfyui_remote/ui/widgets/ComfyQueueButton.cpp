/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyQueueButton.h"

#include "ComfySpinPaint.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"

#include <QColor>
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
    const QFontMetrics fm(font());
    const int pad = ComfyUiStyle::Spacing::iconPadding;
    const int iconSide = ComfyUiStyle::Spacing::iconSmall;
    const int arrowW = ComfyUiStyle::Spacing::comboArrowWidth;
    const QString label = text().isEmpty() ? QStringLiteral("0") : text();
    const int textW = fm.horizontalAdvance(label);
    const int h = qMax(ComfyUiStyle::Spacing::primaryButtonHeight - 2, ComfyUiStyle::Spacing::comboHeight);
    const int w = pad + iconSide + pad + textW + pad + arrowW + pad;
    return QSize(w, h);
}

void ComfyQueueButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QStyle *style = this->style();
    const QRect rect = this->rect();
    QStyle::PrimitiveElement element = QStyle::PE_Widget;
    if (opt.state & QStyle::State_MouseOver)
        element = QStyle::PE_PanelButtonCommand;
    style->drawPrimitive(element, &opt, &painter, this);

    const int pad = ComfyUiStyle::Spacing::iconPadding;
    const int iconSide = qMax(12, static_cast<int>(rect.height() * 0.72));
    const QRect arrowColumn = ComfySpinPaint::buttonColumnRect(rect);

    const QPixmap pixmap = icon().pixmap(iconSide, iconSide);
    const QRect iconRect(pad, (rect.height() - iconSide) / 2, iconSide, iconSide);
    if (!pixmap.isNull())
        painter.drawPixmap(iconRect, pixmap);

    const QString label = text().isEmpty() ? QStringLiteral("0") : text();
    const QRect countRect(iconRect.right() + pad,
                          rect.top(),
                          qMax(1, arrowColumn.left() - iconRect.right() - pad * 2),
                          rect.height());
    painter.setPen(QColor(ComfyUiStyle::colors().primaryText));
    painter.drawText(countRect, Qt::AlignLeft | Qt::AlignVCenter, label);

    const QColor arrowColor(ComfyUiStyle::colors().secondaryText);
    ComfySpinPaint::paintTriangle(painter, arrowColumn.center(), false, arrowColor);
}
