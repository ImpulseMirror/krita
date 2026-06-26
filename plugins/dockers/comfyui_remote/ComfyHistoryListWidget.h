/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_HISTORY_LIST_WIDGET_H_
#define COMFY_HISTORY_LIST_WIDGET_H_

#include <QListWidget>

class QPushButton;

#include <QMouseEvent>

/// Python HistoryWidget — thumbnails with Apply + context overlay on selection.
class ComfyHistoryListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ComfyHistoryListWidget(QWidget *parent = nullptr);

Q_SIGNALS:
    void applyRequested(QListWidgetItem *item);
    void contextMenuRequested();

public Q_SLOTS:
    void updateOverlayButtons();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_contextButton = nullptr;
};

#endif
