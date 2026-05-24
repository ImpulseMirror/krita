/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIIntervalSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QtGlobal>

#include <algorithm>

namespace
{
int clampInt(int value, int lo, int hi)
{
    return std::max(lo, std::min(value, hi));
}
}

ComfyUIIntervalSlider::ComfyUIIntervalSlider(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

int ComfyUIIntervalSlider::minimum() const
{
    return m_min;
}

int ComfyUIIntervalSlider::maximum() const
{
    return m_max;
}

int ComfyUIIntervalSlider::lowValue() const
{
    return m_low;
}

int ComfyUIIntervalSlider::highValue() const
{
    return m_high;
}

void ComfyUIIntervalSlider::setRange(int minValue, int maxValue)
{
    if (maxValue < minValue) {
        std::swap(minValue, maxValue);
    }
    if (m_min == minValue && m_max == maxValue) {
        return;
    }
    m_min = minValue;
    m_max = maxValue;
    m_low = clampInt(m_low, m_min, m_max);
    m_high = clampInt(m_high, m_min, m_max);
    if (m_low > m_high) {
        m_low = m_high;
    }
    Q_EMIT rangeChanged(m_min, m_max);
    Q_EMIT intervalChanged(m_low, m_high);
    update();
}

void ComfyUIIntervalSlider::setLowValue(int value)
{
    setInterval(value, m_high);
}

void ComfyUIIntervalSlider::setHighValue(int value)
{
    setInterval(m_low, value);
}

void ComfyUIIntervalSlider::setInterval(int low, int high)
{
    low = clampInt(low, m_min, m_max);
    high = clampInt(high, m_min, m_max);
    if (low > high) {
        std::swap(low, high);
    }
    if (m_low == low && m_high == high) {
        return;
    }
    m_low = low;
    m_high = high;
    Q_EMIT intervalChanged(m_low, m_high);
    update();
}

QRect ComfyUIIntervalSlider::trackRect() const
{
    const int margin = m_handleRadius + 2;
    const int h = 4;
    return QRect(margin, (height() - h) / 2, std::max(1, width() - margin * 2), h);
}

int ComfyUIIntervalSlider::valueFromX(int x) const
{
    const QRect tr = trackRect();
    if (tr.width() <= 1 || m_max <= m_min) {
        return m_min;
    }
    const double t = clampInt(x, tr.left(), tr.right()) - tr.left();
    const double ratio = t / static_cast<double>(tr.width());
    return clampInt(qRound(m_min + ratio * static_cast<double>(m_max - m_min)), m_min, m_max);
}

int ComfyUIIntervalSlider::xFromValue(int value) const
{
    const QRect tr = trackRect();
    if (m_max <= m_min || tr.width() <= 1) {
        return tr.left();
    }
    const double ratio = (value - m_min) / static_cast<double>(m_max - m_min);
    return tr.left() + qRound(ratio * tr.width());
}

QRect ComfyUIIntervalSlider::handleRectForValue(int value) const
{
    const int cx = xFromValue(value);
    const int cy = height() / 2;
    return QRect(cx - m_handleRadius, cy - m_handleRadius, m_handleRadius * 2, m_handleRadius * 2);
}

int ComfyUIIntervalSlider::pickHandle(int x) const
{
    const int lowX = xFromValue(m_low);
    const int highX = xFromValue(m_high);
    if (std::abs(x - lowX) <= std::abs(x - highX)) {
        return LowHandle;
    }
    return HighHandle;
}

void ComfyUIIntervalSlider::applyDraggedHandle(int x)
{
    const int value = valueFromX(x);
    if (m_activeHandle == LowHandle) {
        const int low = std::min(value, m_high);
        if (low != m_low) {
            m_low = low;
            Q_EMIT intervalChanged(m_low, m_high);
            Q_EMIT sliderMoved(LowHandle, m_low);
            update();
        }
    } else if (m_activeHandle == HighHandle) {
        const int high = std::max(value, m_low);
        if (high != m_high) {
            m_high = high;
            Q_EMIT intervalChanged(m_low, m_high);
            Q_EMIT sliderMoved(HighHandle, m_high);
            update();
        }
    }
}

void ComfyUIIntervalSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

    const QRect tr = trackRect();
    const int lowX = xFromValue(m_low);
    const int highX = xFromValue(m_high);
    const QColor trackBase = palette().color(QPalette::Mid);
    const QColor trackFill = palette().color(QPalette::Highlight);
    const QColor handleColor = palette().color(QPalette::ButtonText);

    p.setPen(Qt::NoPen);
    p.setBrush(trackBase);
    p.drawRoundedRect(tr, 2, 2);

    const QRect selected(QPoint(std::min(lowX, highX), tr.top()), QPoint(std::max(lowX, highX), tr.bottom()));
    p.setBrush(trackFill);
    p.drawRoundedRect(selected, 2, 2);

    p.setBrush(handleColor);
    p.drawEllipse(handleRectForValue(m_low));
    p.drawEllipse(handleRectForValue(m_high));
}

void ComfyUIIntervalSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Qt5: use pos()/x(); QMouseEvent::position() is Qt6-only.
    const int mx = event->pos().x();
    m_activeHandle = pickHandle(mx);
    Q_EMIT sliderPressed(m_activeHandle);
    applyDraggedHandle(mx);
    event->accept();
}

void ComfyUIIntervalSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_activeHandle < 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    applyDraggedHandle(event->pos().x());
    event->accept();
}

void ComfyUIIntervalSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_activeHandle >= 0 && event->button() == Qt::LeftButton) {
        Q_EMIT sliderReleased(m_activeHandle);
        m_activeHandle = -1;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

QSize ComfyUIIntervalSlider::sizeHint() const
{
    return QSize(180, 24);
}

QSize ComfyUIIntervalSlider::minimumSizeHint() const
{
    return QSize(90, 24);
}
