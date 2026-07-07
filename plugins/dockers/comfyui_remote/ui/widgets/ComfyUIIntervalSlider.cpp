/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIIntervalSlider.h"
#include "ComfySliderPaint.h"
#include "ComfyUiStyle.h"

#include <QMouseEvent>
#include <QPainter>
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
    setFixedHeight(ComfyUiStyle::Spacing::sliderWidgetHeight);
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
    return ComfySliderPaint::horizontalTrackRect(rect());
}

int ComfyUIIntervalSlider::valueFromX(int x) const
{
    return ComfySliderPaint::valueFromX(x, m_min, m_max, trackRect());
}

int ComfyUIIntervalSlider::xFromValue(int value) const
{
    return ComfySliderPaint::xFromValue(value, m_min, m_max, trackRect());
}

QRect ComfyUIIntervalSlider::handleRectForValue(int value) const
{
    const int cx = xFromValue(value);
    const int cy = height() / 2;
    return ComfySliderPaint::squareHandleRect(cx, cy, ComfyUiStyle::Spacing::sliderHandle);
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
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect tr = trackRect();
    const int lowX = xFromValue(m_low);
    const int highX = xFromValue(m_high);
    const ComfyUiStyle::Colors style = ComfyUiStyle::colors();

    ComfySliderPaint::paintHorizontalTrack(p,
                                           tr,
                                           QColor(style.sliderTrack),
                                           QColor(style.highlight),
                                           std::max(lowX, highX));

    const QRect lowHandle = handleRectForValue(m_low);
    const QRect highHandle = handleRectForValue(m_high);
    const QColor handleFill = QColor(style.secondaryPanel);
    const QColor handleBorder = QColor(m_activeHandle >= 0 ? style.highlight : style.border);
    ComfySliderPaint::paintSquareHandle(p, lowHandle, handleFill, handleBorder, m_activeHandle == LowHandle);
    ComfySliderPaint::paintSquareHandle(p, highHandle, handleFill, handleBorder, m_activeHandle == HighHandle);
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
    return QSize(180, ComfyUiStyle::Spacing::sliderWidgetHeight);
}

QSize ComfyUIIntervalSlider::minimumSizeHint() const
{
    return QSize(90, ComfyUiStyle::Spacing::sliderWidgetHeight);
}
