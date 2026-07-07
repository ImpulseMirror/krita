/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QCheckBox>

/// Style-guide checkbox with custom paint — reliable on Android (QSS indicator fails there).
class ComfyCheckBox : public QCheckBox
{
    Q_OBJECT

public:
    explicit ComfyCheckBox(QWidget *parent = nullptr);
    explicit ComfyCheckBox(const QString &text, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    QRect indicatorRect() const;
    QRect labelRect(const QRect &indicator) const;
};
