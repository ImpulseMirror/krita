/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QWidget>

class QHBoxLayout;

/// Bootstrap-style 12-column grid cell (`span` = col-* width).
class ComfyGridCol : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int span READ span WRITE setSpan)

public:
    explicit ComfyGridCol(int span, QWidget *parent = nullptr);

    int span() const { return m_span; }
    void setSpan(int span);

    QHBoxLayout *contentLayout() const { return m_layout; }
    void addWidget(QWidget *widget, int stretch = 0, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter);

private:
    int m_span = 1;
    QHBoxLayout *m_layout = nullptr;
};

/// Horizontal row of `ComfyGridCol` cells; stretch factors follow span (sums to ≤ 12).
class ComfyGridRow : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kColumns = 12;

    explicit ComfyGridRow(QWidget *parent = nullptr);

    ComfyGridCol *addCol(int span);
    ComfyGridCol *addWidget(QWidget *widget, int span, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter);
    ComfyGridCol *addWidget(QWidget *widget, int span, int contentStretch, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter);

    QHBoxLayout *rowLayout() const { return m_rowLayout; }

    /// Remove column widgets from the row (children reparented to this row).
    void clearRow();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    int clampSpan(int span) const;

    QHBoxLayout *m_rowLayout = nullptr;
};
