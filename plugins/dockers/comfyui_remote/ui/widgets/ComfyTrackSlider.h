/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QAbstractSlider>

/// Style-guide horizontal slider (6px track, 14px handle). Custom paint — reliable on Android.
class ComfyTrackSlider : public QAbstractSlider
{
    Q_OBJECT

public:
    explicit ComfyTrackSlider(Qt::Orientation orientation = Qt::Horizontal, QWidget *parent = nullptr);

    void setTickInterval(int interval);
    int tickInterval() const { return m_tickInterval; }

    void setTicksVisible(bool visible);
    bool ticksVisible() const { return m_ticksVisible; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QRect trackRect() const;
    QRect handleRect() const;
    void setValueFromX(int x);
    void logMetricsOnce(const char *reason);

    int m_tickInterval = 0;
    bool m_ticksVisible = false;
    bool m_dragging = false;
    bool m_hovered = false;
    mutable bool m_loggedMetrics = false;
};
