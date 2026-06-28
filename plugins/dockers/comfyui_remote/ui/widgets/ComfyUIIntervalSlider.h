/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFYUI_INTERVAL_SLIDER_H_
#define COMFYUI_INTERVAL_SLIDER_H_

#include <QWidget>

// §13.49: Horizontal dual-handle interval slider for control-layer timing range.
class ComfyUIIntervalSlider : public QWidget
{
    Q_OBJECT

public:
    enum HandleId {
        LowHandle = 0,
        HighHandle = 1,
    };
    Q_ENUM(HandleId)

    explicit ComfyUIIntervalSlider(QWidget *parent = nullptr);

    int minimum() const;
    int maximum() const;
    int lowValue() const;
    int highValue() const;

public Q_SLOTS:
    void setRange(int minValue, int maxValue);
    void setLowValue(int value);
    void setHighValue(int value);
    void setInterval(int low, int high);

Q_SIGNALS:
    void rangeChanged(int min, int max);
    void intervalChanged(int low, int high);
    void sliderPressed(int id);
    void sliderMoved(int id, int value);
    void sliderReleased(int id);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QRect trackRect() const;
    int valueFromX(int x) const;
    int xFromValue(int value) const;
    QRect handleRectForValue(int value) const;
    int pickHandle(int x) const;
    void applyDraggedHandle(int x);

private:
    int m_min = 0;
    int m_max = 100;
    int m_low = 25;
    int m_high = 75;
    int m_activeHandle = -1;
    int m_handleRadius = 7;
};

#endif // COMFYUI_INTERVAL_SLIDER_H_
