/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyFormRow.h"
#include "ComfyUiStyle.h"

#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

namespace {
constexpr int kLabelSpan = 5;
constexpr int kControlSpan = 7;
constexpr int kTrailingSpan = 2;
} // namespace

ComfyFormRow::ComfyFormRow(const QString &title, const QString &description, QWidget *parent, int leftIndent)
    : ComfyGridRow(parent)
{
    setContentsMargins(leftIndent * ComfyUiStyle::Spacing::unit,
                       ComfyUiStyle::Spacing::unit,
                       0,
                       ComfyUiStyle::Spacing::unit);
    QWidget *labelCol = makeLabelColumn(this, title, description);
    addWidget(labelCol, kLabelSpan);
}

void ComfyFormRow::setControl(QWidget *control, int stretch)
{
    if (!control)
        return;
    Q_UNUSED(stretch);
    addWidget(control, kControlSpan, 1, Qt::AlignRight | Qt::AlignVCenter);
}

void ComfyFormRow::addTrailingWidget(QWidget *widget, int span, Qt::Alignment align)
{
    if (!widget)
        return;
    addWidget(widget, span > 0 ? span : kTrailingSpan, 0, align);
}

QWidget *ComfyFormRow::makeLabelColumn(QWidget *parent, const QString &title, const QString &description)
{
    auto *col = new QWidget(parent);
    auto *colLayout = new QVBoxLayout(col);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(ComfyUiStyle::Spacing::labelControl);
    auto *titleLabel = new QLabel(title, col);
    ComfyUiStyle::styleFormRowTitle(titleLabel);
    colLayout->addWidget(titleLabel);
    if (auto *formRow = qobject_cast<ComfyFormRow *>(parent)) {
        formRow->m_titleLabel = titleLabel;
        if (!description.isEmpty()) {
            auto *descLabel = new QLabel(description, col);
            descLabel->setWordWrap(true);
            ComfyUiStyle::styleDescription(descLabel);
            colLayout->addWidget(descLabel);
            formRow->m_descriptionLabel = descLabel;
        }
    }
    col->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return col;
}

QLabel *ComfyFormRow::makeBoldHeader(QWidget *parent, const QString &title)
{
    QLabel *titleLabel = new QLabel(title, parent);
    ComfyUiStyle::styleSectionTitle(titleLabel);
    return titleLabel;
}
