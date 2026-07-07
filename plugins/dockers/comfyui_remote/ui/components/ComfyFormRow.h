/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyGrid.h"

class QLabel;

/// Settings-style row: bold title + optional description (col-5) + control (col-7).
class ComfyFormRow : public ComfyGridRow
{
    Q_OBJECT
public:
    explicit ComfyFormRow(const QString &title,
                          const QString &description,
                          QWidget *parent = nullptr,
                          int leftIndent = 0);

    void setControl(QWidget *control, int stretch = 0);
    void addTrailingWidget(QWidget *widget, int span = 2, Qt::Alignment align = Qt::AlignRight | Qt::AlignVCenter);

    QLabel *titleLabel() const { return m_titleLabel; }
    QLabel *descriptionLabel() const { return m_descriptionLabel; }

    static QWidget *makeLabelColumn(QWidget *parent, const QString &title, const QString &description);
    static QLabel *makeBoldHeader(QWidget *parent, const QString &title);

private:
    QLabel *m_titleLabel = nullptr;
    QLabel *m_descriptionLabel = nullptr;
};
