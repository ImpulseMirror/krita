/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySwitchWidget.h"
#include "ComfyUiStyle.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>

ComfySwitchWidget::ComfySwitchWidget(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(ComfyUiStyle::Spacing::toggleWidth, ComfyUiStyle::Spacing::toggleHeight);
    const int trackH = ComfyUiStyle::Spacing::toggleHeight - 2 * ComfyUiStyle::Spacing::labelControl;
    m_trackRadius = trackH / 2;
    m_thumbRadius = ComfyUiStyle::Spacing::toggleKnob / 2;
    m_margin = ComfyUiStyle::Spacing::labelControl;
    m_baseOffset = m_margin + m_thumbRadius;
    m_offset = m_baseOffset;
    connect(this, &QAbstractButton::toggled, this, [this](bool) { syncOffsetToChecked(); });
}

QSize ComfySwitchWidget::sizeHint() const
{
    return QSize(ComfyUiStyle::Spacing::toggleWidth, ComfyUiStyle::Spacing::toggleHeight);
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
    const int endOn = width() - m_margin - m_thumbRadius;
    m_offset = isChecked() ? endOn : m_baseOffset;
    update();
}

void ComfySwitchWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const ComfyUiStyle::Colors style = ComfyUiStyle::colors();
    QColor trackBrush;
    QColor thumbBrush;
    qreal trackOpacity = 1.0;
    qreal thumbOpacity = 1.0;
    if (isEnabled()) {
        trackBrush = isChecked() ? QColor(style.highlight) : QColor(style.toggleOff);
        thumbBrush = isChecked() ? QColor(Qt::white) : QColor(Qt::black);
    } else {
        trackOpacity = 0.8;
        trackBrush = QColor(style.disabledBg);
        thumbBrush = QColor(style.disabledText);
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    p.setBrush(trackBrush);
    p.setOpacity(trackOpacity);
    p.drawRoundedRect(m_margin, m_margin, width() - 2 * m_margin, height() - 2 * m_margin, m_trackRadius, m_trackRadius);

    p.setBrush(thumbBrush);
    p.setOpacity(thumbOpacity);
    const int knobY = height() / 2;
    p.drawEllipse(m_offset - m_thumbRadius, knobY - m_thumbRadius, 2 * m_thumbRadius, 2 * m_thumbRadius);
}

void ComfySwitchWidget::mouseReleaseEvent(QMouseEvent *event)
{
    const int start = m_offset;
    QAbstractButton::mouseReleaseEvent(event);
    if (event && event->button() == Qt::LeftButton) {
        const int end = isChecked() ? width() - m_margin - m_thumbRadius : m_baseOffset;
        auto *anim = new QPropertyAnimation(this, "offset", this);
        anim->setDuration(120);
        anim->setStartValue(start);
        anim->setEndValue(end);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}
