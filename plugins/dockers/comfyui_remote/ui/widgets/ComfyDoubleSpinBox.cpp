/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDoubleSpinBox.h"
#include "ComfySpinPaint.h"
#include "ComfyUiStyle.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QTimerEvent>

ComfyDoubleSpinBox::ComfyDoubleSpinBox(QWidget *parent)
    : QDoubleSpinBox(parent)
{
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    setFrame(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(ComfyUiStyle::Spacing::comboHeight);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    setStyleSheet(QString());

    if (QLineEdit *edit = lineEdit()) {
        edit->hide();
        edit->setEnabled(false);
    }

    connect(this, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { update(); });
}

QString ComfyDoubleSpinBox::displayText() const
{
    return prefix() + QString::number(value(), 'f', decimals()) + suffix();
}

ComfyDoubleSpinBox::HitZone ComfyDoubleSpinBox::hitZoneAt(const QPoint &pos) const
{
    const QRect column = ComfySpinPaint::buttonColumnRect(rect());
    if (!column.contains(pos))
        return HitZone::None;
    if (ComfySpinPaint::upButtonRect(column).contains(pos))
        return HitZone::Up;
    if (ComfySpinPaint::downButtonRect(column).contains(pos))
        return HitZone::Down;
    return HitZone::None;
}

void ComfyDoubleSpinBox::stepInZone(HitZone zone)
{
    if (!isEnabled())
        return;
    if (zone == HitZone::Up)
        stepUp();
    else if (zone == HitZone::Down)
        stepDown();
}

void ComfyDoubleSpinBox::startRepeat(HitZone zone)
{
    stopRepeat();
    if (zone == HitZone::None)
        return;
    m_repeatZone = zone;
    m_repeatTimerId = startTimer(400);
}

void ComfyDoubleSpinBox::stopRepeat()
{
    if (m_repeatTimerId != 0) {
        killTimer(m_repeatTimerId);
        m_repeatTimerId = 0;
    }
    m_repeatZone = HitZone::None;
}

void ComfyDoubleSpinBox::refreshHover(const QPoint &pos)
{
    const HitZone zone = hitZoneAt(pos);
    if (m_hover == zone)
        return;
    m_hover = zone;
    update();
}

void ComfyDoubleSpinBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    ComfySpinPaint::SpinPaintState state;
    state.enabled = isEnabled();
    state.focused = hasFocus();
    state.hoverUp = m_hover == HitZone::Up;
    state.hoverDown = m_hover == HitZone::Down;
    state.pressedUp = m_pressed == HitZone::Up;
    state.pressedDown = m_pressed == HitZone::Down;

    ComfySpinPaint::paintSpinBox(p, rect(), displayText(), font(), state);
}

void ComfyDoubleSpinBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled()) {
        QDoubleSpinBox::mousePressEvent(event);
        return;
    }
    const HitZone zone = hitZoneAt(event->pos());
    if (zone == HitZone::None) {
        setFocus();
        event->accept();
        return;
    }
    m_pressed = zone;
    stepInZone(zone);
    startRepeat(zone);
    update();
    event->accept();
}

void ComfyDoubleSpinBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = HitZone::None;
        stopRepeat();
        update();
        event->accept();
        return;
    }
    QDoubleSpinBox::mouseReleaseEvent(event);
}

void ComfyDoubleSpinBox::mouseMoveEvent(QMouseEvent *event)
{
    refreshHover(event->pos());
    QDoubleSpinBox::mouseMoveEvent(event);
}

void ComfyDoubleSpinBox::leaveEvent(QEvent *event)
{
    m_hover = HitZone::None;
    update();
    QDoubleSpinBox::leaveEvent(event);
}

void ComfyDoubleSpinBox::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_repeatTimerId) {
        stepInZone(m_repeatZone);
        killTimer(m_repeatTimerId);
        m_repeatTimerId = startTimer(60);
        return;
    }
    QDoubleSpinBox::timerEvent(event);
}

void ComfyDoubleSpinBox::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange)
        update();
    QDoubleSpinBox::changeEvent(event);
}

QSize ComfyDoubleSpinBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int textW = fm.horizontalAdvance(displayText());
    const int w = ComfyUiStyle::Spacing::nestedPanel + textW + ComfyUiStyle::Spacing::spinButtonWidth
                  + ComfyUiStyle::Spacing::nestedPanel;
    return QSize(w, ComfyUiStyle::Spacing::comboHeight);
}

QSize ComfyDoubleSpinBox::minimumSizeHint() const
{
    return QSize(ComfyUiStyle::Spacing::spinButtonWidth + ComfyUiStyle::Spacing::nestedPanel * 2,
                 ComfyUiStyle::Spacing::comboHeight);
}
