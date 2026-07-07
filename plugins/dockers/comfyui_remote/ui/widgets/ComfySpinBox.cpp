/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySpinBox.h"
#include "ComfySpinPaint.h"
#include "ComfyUiStyle.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QTimerEvent>

ComfySpinBox::ComfySpinBox(QWidget *parent)
    : QSpinBox(parent)
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

    connect(this, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { update(); });
}

QString ComfySpinBox::displayText() const
{
    return prefix() + QString::number(value()) + suffix();
}

ComfySpinBox::HitZone ComfySpinBox::hitZoneAt(const QPoint &pos) const
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

void ComfySpinBox::stepInZone(HitZone zone)
{
    if (!isEnabled())
        return;
    if (zone == HitZone::Up)
        stepUp();
    else if (zone == HitZone::Down)
        stepDown();
}

void ComfySpinBox::startRepeat(HitZone zone)
{
    stopRepeat();
    if (zone == HitZone::None)
        return;
    m_repeatZone = zone;
    m_repeatTimerId = startTimer(400);
}

void ComfySpinBox::stopRepeat()
{
    if (m_repeatTimerId != 0) {
        killTimer(m_repeatTimerId);
        m_repeatTimerId = 0;
    }
    m_repeatZone = HitZone::None;
}

void ComfySpinBox::refreshHover(const QPoint &pos)
{
    const HitZone zone = hitZoneAt(pos);
    if (m_hover == zone)
        return;
    m_hover = zone;
    update();
}

void ComfySpinBox::paintEvent(QPaintEvent *event)
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

void ComfySpinBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled()) {
        QSpinBox::mousePressEvent(event);
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

void ComfySpinBox::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = HitZone::None;
        stopRepeat();
        update();
        event->accept();
        return;
    }
    QSpinBox::mouseReleaseEvent(event);
}

void ComfySpinBox::mouseMoveEvent(QMouseEvent *event)
{
    refreshHover(event->pos());
    QSpinBox::mouseMoveEvent(event);
}

void ComfySpinBox::leaveEvent(QEvent *event)
{
    m_hover = HitZone::None;
    update();
    QSpinBox::leaveEvent(event);
}

void ComfySpinBox::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_repeatTimerId) {
        stepInZone(m_repeatZone);
        killTimer(m_repeatTimerId);
        m_repeatTimerId = startTimer(60);
        return;
    }
    QSpinBox::timerEvent(event);
}

void ComfySpinBox::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange)
        update();
    QSpinBox::changeEvent(event);
}

QSize ComfySpinBox::sizeHint() const
{
    const QFontMetrics fm(font());
    const int textW = fm.horizontalAdvance(displayText());
    const int w = ComfyUiStyle::Spacing::nestedPanel + textW + ComfyUiStyle::Spacing::spinButtonWidth
                  + ComfyUiStyle::Spacing::nestedPanel;
    return QSize(w, ComfyUiStyle::Spacing::comboHeight);
}

QSize ComfySpinBox::minimumSizeHint() const
{
    return sizeHint();
}
