/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyGrid.h"
#include "ComfyUiStyle.h"

#include <QHBoxLayout>
#include <QSizePolicy>

ComfyGridCol::ComfyGridCol(int span, QWidget *parent)
    : QWidget(parent)
    , m_span(qBound(1, span, ComfyGridRow::kColumns))
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(ComfyUiStyle::Spacing::rowGap);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ComfyGridCol::setSpan(int span)
{
    m_span = qBound(1, span, ComfyGridRow::kColumns);
}

void ComfyGridCol::addWidget(QWidget *widget, int stretch, Qt::Alignment alignment)
{
    if (!widget || !m_layout)
        return;
    m_layout->addWidget(widget, stretch, alignment);
}

ComfyGridRow::ComfyGridRow(QWidget *parent)
    : QWidget(parent)
{
    m_rowLayout = new QHBoxLayout(this);
    ComfyUiStyle::applyTightRowLayout(m_rowLayout);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QSize ComfyGridRow::sizeHint() const
{
    if (m_rowLayout)
        return m_rowLayout->sizeHint();
    return QWidget::sizeHint();
}

QSize ComfyGridRow::minimumSizeHint() const
{
    if (m_rowLayout)
        return m_rowLayout->minimumSize();
    return QWidget::minimumSizeHint();
}

int ComfyGridRow::clampSpan(int span) const
{
    return qBound(1, span, kColumns);
}

ComfyGridCol *ComfyGridRow::addCol(int span)
{
    const int s = clampSpan(span);
    auto *col = new ComfyGridCol(s, this);
    m_rowLayout->addWidget(col, s);
    return col;
}

ComfyGridCol *ComfyGridRow::addWidget(QWidget *widget, int span, Qt::Alignment alignment)
{
    ComfyGridCol *col = addCol(span);
    col->addWidget(widget, 0, alignment);
    return col;
}

ComfyGridCol *ComfyGridRow::addWidget(QWidget *widget, int span, int contentStretch, Qt::Alignment alignment)
{
    ComfyGridCol *col = addCol(span);
    col->addWidget(widget, contentStretch, alignment);
    return col;
}

void ComfyGridRow::clearRow()
{
    if (!m_rowLayout)
        return;
    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->setParent(this);
        delete item;
    }
}
