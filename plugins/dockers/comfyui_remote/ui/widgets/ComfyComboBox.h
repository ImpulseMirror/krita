/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QComboBox>

/// Style-guide combo with custom paint — reliable on Android (QSS down-arrow fails there).
class ComfyComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit ComfyComboBox(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showPopup() override;
    void hidePopup() override;
    void changeEvent(QEvent *event) override;

private:
    void layoutLineEdit();
    void stylePopupView();
    bool arrowColumnContains(const QPoint &pos) const;

    bool m_hover = false;
    bool m_popupOpen = false;
};
