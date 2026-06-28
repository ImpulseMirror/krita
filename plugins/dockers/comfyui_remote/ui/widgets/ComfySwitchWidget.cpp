/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySwitchWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>

ComfySwitchWidget::ComfySwitchWidget(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_thumbRadius = fontMetrics().height() / 2;
    m_trackRadius = m_thumbRadius + 2;
    m_margin = qMax(0, m_thumbRadius - m_trackRadius);
    m_baseOffset = qMax(m_thumbRadius, m_trackRadius);
    m_offset = m_baseOffset;
    connect(this, &QAbstractButton::toggled, this, [this](bool) { syncOffsetToChecked(); });
}

QSize ComfySwitchWidget::sizeHint() const
{
    return QSize(4 * m_trackRadius + 2 * m_margin, 2 * m_trackRadius + 2 * m_margin);
}

void ComfySwitchWidget::setOffset(int value)
{
    if (m_offset == value)
        return;
    m_offset = value;
    update();
}

void ComfySwitchWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractButton::resizeEvent(event);
    syncOffsetToChecked();
}

void ComfySwitchWidget::syncOffsetToChecked()
{
    m_offset = isChecked() ? width() - m_baseOffset : m_baseOffset;
    update();
}

void ComfySwitchWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const QPalette pal = palette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    QColor trackBrush;
    QColor thumbBrush;
    qreal trackOpacity = 1.0;
    qreal thumbOpacity = 1.0;
    if (isEnabled()) {
        trackBrush = isChecked() ? pal.highlight().color() : pal.color(QPalette::Dark);
        thumbBrush = isChecked() ? pal.color(QPalette::Text) : pal.color(QPalette::Light);
    } else {
        trackOpacity = 0.8;
        trackBrush = pal.color(QPalette::Shadow);
        thumbBrush = pal.color(QPalette::Mid);
    }

    p.setBrush(trackBrush);
    p.setOpacity(trackOpacity);
    p.drawRoundedRect(m_margin, m_margin, width() - 2 * m_margin, height() - 2 * m_margin, m_trackRadius, m_trackRadius);

    p.setBrush(thumbBrush);
    p.setOpacity(thumbOpacity);
    p.drawEllipse(m_offset - m_thumbRadius, m_baseOffset - m_thumbRadius, 2 * m_thumbRadius, 2 * m_thumbRadius);
}

void ComfySwitchWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const int start = m_offset;
    QAbstractButton::mouseReleaseEvent(event);
    if (event && event->button() == Qt::LeftButton) {
        const int end = isChecked() ? width() - m_baseOffset : m_baseOffset;
        auto *anim = new QPropertyAnimation(this, "offset", this);
        anim->setDuration(120);
        anim->setStartValue(start);
        anim->setEndValue(end);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}
