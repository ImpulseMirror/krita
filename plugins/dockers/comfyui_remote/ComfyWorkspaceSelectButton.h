/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_WORKSPACE_SELECT_BUTTON_H_
#define COMFY_WORKSPACE_SELECT_BUTTON_H_

#include <QToolButton>

/// Python WorkspaceSelectWidget — icon-only button, popup menu with labels + icons.
class ComfyWorkspaceSelectButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ComfyWorkspaceSelectButton(QWidget *parent = nullptr);

    int currentIndex() const { return m_index; }
    int count() const { return 5; }
    void setCurrentIndex(int index);

Q_SIGNALS:
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setWorkspaceIndex(int index, bool notify);

    int m_index = 0;
};

#endif
