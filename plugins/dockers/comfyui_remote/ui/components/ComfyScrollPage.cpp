/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyScrollPage.h"
#include "ComfyFormRow.h"
#include "ComfyUiStyle.h"

#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

ComfyScrollPage ComfyScrollPage::create(QWidget *dialogParent, const QString &heading)
{
    ComfyScrollPage tab;
    tab.m_page = new QWidget(dialogParent);
    tab.m_outerLayout = new QVBoxLayout(tab.m_page);
    tab.m_outerLayout->setContentsMargins(0, 0, 0, 0);
    tab.m_scroll = new QScrollArea(tab.m_page);
    tab.m_scroll->setWidgetResizable(true);
    tab.m_scroll->setFrameShape(QFrame::NoFrame);
    tab.m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tab.m_body = new QWidget();
    tab.m_bodyLayout = new QVBoxLayout(tab.m_body);
    QLabel *headingLabel = new QLabel(heading, tab.m_body);
    ComfyUiStyle::styleSectionTitle(headingLabel);
    tab.m_bodyLayout->addWidget(headingLabel);
    tab.m_bodyLayout->addSpacing(ComfyUiStyle::Spacing::labelControl + ComfyUiStyle::Spacing::unit);
    tab.m_bodyLayout->setSpacing(ComfyUiStyle::Spacing::settingsSectionGap);
    tab.m_bodyLayout->setContentsMargins(ComfyUiStyle::Spacing::panel,
                                         ComfyUiStyle::Spacing::panel,
                                         ComfyUiStyle::Spacing::panel,
                                         ComfyUiStyle::Spacing::panel);
    ComfyUiStyle::applyScrollArea(tab.m_scroll);
    tab.m_scroll->setWidget(tab.m_body);
    tab.m_outerLayout->addWidget(tab.m_scroll);
    return tab;
}
