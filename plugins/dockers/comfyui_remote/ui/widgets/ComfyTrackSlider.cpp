/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyTrackSlider.h"
#include "ComfySliderPaint.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfyUiStyle.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>

ComfyTrackSlider::ComfyTrackSlider(Qt::Orientation orientation, QWidget *parent)
    : QAbstractSlider(parent)
{
    setOrientation(orientation);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(ComfyUiStyle::Spacing::sliderWidgetHeight);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

void ComfyTrackSlider::setTickInterval(int interval)
{
    if (m_tickInterval == interval)
        return;
    m_tickInterval = interval;
    update();
}

void ComfyTrackSlider::setTicksVisible(bool visible)
{
    if (m_ticksVisible == visible)
        return;
    m_ticksVisible = visible;
    update();
}

QRect ComfyTrackSlider::trackRect() const
{
    return ComfySliderPaint::horizontalTrackRect(rect());
}

QRect ComfyTrackSlider::handleRect() const
{
    const QRect tr = trackRect();
    const int cx = ComfySliderPaint::xFromValue(value(), minimum(), maximum(), tr);
    const int cy = height() / 2;
    return ComfySliderPaint::squareHandleRect(cx, cy, ComfyUiStyle::Spacing::sliderHandle);
}

void ComfyTrackSlider::setValueFromX(int x)
{
    const QRect tr = trackRect();
    int v = ComfySliderPaint::valueFromX(x, minimum(), maximum(), tr);
    if (singleStep() > 1) {
        const int step = singleStep();
        v = qRound(static_cast<double>(v) / step) * step;
        v = qBound(minimum(), v, maximum());
    }
    setValue(v);
}

void ComfyTrackSlider::logMetricsOnce(const char *reason)
{
    if (m_loggedMetrics)
        return;
    m_loggedMetrics = true;
    ComfyUiLayoutDiagnostics::logSliderMetrics(reason, this);
}

void ComfyTrackSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const ComfyUiStyle::Colors style = ComfyUiStyle::colors();
    const QRect tr = trackRect();
    const QRect handle = handleRect();
    const int fillRight = handle.center().x();

    ComfySliderPaint::paintHorizontalTrack(p,
                                           tr,
                                           QColor(style.sliderTrack),
                                           QColor(style.highlight),
                                           fillRight);

    if (m_ticksVisible && m_tickInterval > 0)
        ComfySliderPaint::paintTickMarks(p, tr, minimum(), maximum(), m_tickInterval, QColor(style.border));

    const bool active = m_hovered || m_dragging || hasFocus();
    const QColor handleFill = active ? QColor(style.secondaryPanel).lighter(115)
                                     : QColor(style.secondaryPanel);
    const QColor handleBorder = active ? QColor(style.highlight) : QColor(style.border);
    ComfySliderPaint::paintSquareHandle(p, handle, handleFill, handleBorder, active);
}

void ComfyTrackSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractSlider::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    setValueFromX(event->pos().x());
    event->accept();
}

void ComfyTrackSlider::mouseMoveEvent(QMouseEvent *event)
{
    m_hovered = rect().contains(event->pos());
    if (m_dragging) {
        setValueFromX(event->pos().x());
        event->accept();
        return;
    }
    update();
    QAbstractSlider::mouseMoveEvent(event);
}

void ComfyTrackSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    QAbstractSlider::mouseReleaseEvent(event);
}

void ComfyTrackSlider::resizeEvent(QResizeEvent *event)
{
    QAbstractSlider::resizeEvent(event);
    if (width() > 0)
        logMetricsOnce("resize");
}

void ComfyTrackSlider::showEvent(QShowEvent *event)
{
    QAbstractSlider::showEvent(event);
    QTimer::singleShot(0, this, [this]() { logMetricsOnce("show"); });
}

QSize ComfyTrackSlider::sizeHint() const
{
    return QSize(180, ComfyUiStyle::Spacing::sliderWidgetHeight);
}

QSize ComfyTrackSlider::minimumSizeHint() const
{
    return QSize(60, ComfyUiStyle::Spacing::sliderWidgetHeight);
}
