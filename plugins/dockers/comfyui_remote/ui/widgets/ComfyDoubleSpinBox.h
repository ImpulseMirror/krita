/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDoubleSpinBox>

/// Style-guide double spin box with custom paint — reliable on Android (QSS arrows fail there).
class ComfyDoubleSpinBox : public QDoubleSpinBox
{
    Q_OBJECT

public:
    explicit ComfyDoubleSpinBox(QWidget *parent = nullptr);

    QString displayText() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void changeEvent(QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    enum class HitZone {
        None,
        Up,
        Down
    };

    HitZone hitZoneAt(const QPoint &pos) const;
    void stepInZone(HitZone zone);
    void startRepeat(HitZone zone);
    void stopRepeat();
    void refreshHover(const QPoint &pos);

    HitZone m_hover = HitZone::None;
    HitZone m_pressed = HitZone::None;
    HitZone m_repeatZone = HitZone::None;
    int m_repeatTimerId = 0;
};
