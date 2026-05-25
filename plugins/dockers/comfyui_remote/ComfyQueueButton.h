/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_QUEUE_BUTTON_H_
#define COMFY_QUEUE_BUTTON_H_

#include <QToolButton>

/// Python QueueButton — icon + job count + dropdown arrow paint.
class ComfyQueueButton : public QToolButton
{
    Q_OBJECT
public:
    enum class DisplayState { Inactive, Waiting, Active, Upload };

    explicit ComfyQueueButton(QWidget *parent = nullptr);

    void setDisplayState(DisplayState state, int displayCount, const QString &toolTip);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    DisplayState m_state = DisplayState::Inactive;
    int m_displayCount = 0;
};

#endif
