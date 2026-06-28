/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_SWITCH_WIDGET_H_
#define COMFY_SWITCH_WIDGET_H_

#include <QAbstractButton>

/// Python ai_diffusion/ui/switch.py SwitchWidget — animated toggle track + thumb.
class ComfySwitchWidget : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(int offset READ offset WRITE setOffset)

public:
    explicit ComfySwitchWidget(QWidget *parent = nullptr);

    QSize sizeHint() const override;

    int offset() const { return m_offset; }
    void setOffset(int value);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void syncOffsetToChecked();

    int m_thumbRadius = 0;
    int m_trackRadius = 0;
    int m_margin = 0;
    int m_baseOffset = 0;
    int m_offset = 0;
};

#endif
