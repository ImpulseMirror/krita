/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PROMPT_RESIZE_HANDLE_H_
#define COMFY_PROMPT_RESIZE_HANDLE_H_

#include <QWidget>
#include <functional>

class QPlainTextEdit;

/// FAITHFUL_PORT: dot grip overlaid on bottom of prompt editor; drag resizes height.
class ComfyPromptResizeHandle : public QWidget
{
    Q_OBJECT
public:
    using PersistLinesFn = std::function<void(int lines)>;
    explicit ComfyPromptResizeHandle(QPlainTextEdit *editor,
                                     PersistLinesFn persistLines,
                                     int minHeightPx = 40,
                                     QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void heightChanged();

private:
    void reposition();

    QPlainTextEdit *m_editor = nullptr;
    PersistLinesFn m_persistLines;
    int m_minHeightPx = 40;
    bool m_dragging = false;
    int m_pressGlobalY = 0;
    int m_startHeight = 0;
};

#endif
