/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyControlLayerListWidget.h"
#include "ComfyLocalization.h"

#include "ComfyControlLayerRowWidget.h"

#include <QPushButton>
#include <QVBoxLayout>

#include <KisViewManager.h>
#include <klocalizedstring.h>

ComfyControlLayerListWidget::ComfyControlLayerListWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout = new QVBoxLayout();
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    lay->addLayout(m_rowsLayout);
    m_btnAdd = new QPushButton(ComfyTr::tr("Add control layer"), this);
    connect(m_btnAdd, &QPushButton::clicked, this, &ComfyControlLayerListWidget::addLayerRequested);
    lay->addWidget(m_btnAdd);
}

void ComfyControlLayerListWidget::setViewManager(KisViewManager *viewManager)
{
    m_viewManager = viewManager;
    refreshLayerCombos();
}

void ComfyControlLayerListWidget::setArchKeyProvider(std::function<QString()> provider)
{
    m_archKeyProvider = std::move(provider);
    for (ComfyControlLayerRowWidget *row : m_rows)
        row->updateVisibility();
}

void ComfyControlLayerListWidget::setLayers(QList<ComfyControlLayerEntry> *layers)
{
    m_layers = layers;
    rebuildRows();
}

void ComfyControlLayerListWidget::refresh()
{
    rebuildRows();
}

void ComfyControlLayerListWidget::setGenerateEnabled(bool enabled)
{
    for (ComfyControlLayerRowWidget *row : m_rows)
        row->setGenerateEnabled(enabled);
}

void ComfyControlLayerListWidget::rebuildRows()
{
    for (ComfyControlLayerRowWidget *row : m_rows)
        row->deleteLater();
    m_rows.clear();
    if (!m_layers || !m_rowsLayout)
        return;
    for (int i = 0; i < m_layers->size(); ++i) {
        auto *row = new ComfyControlLayerRowWidget(&(*m_layers)[i], i, m_viewManager, m_archKeyProvider, this);
        m_rows.append(row);
        m_rowsLayout->addWidget(row);
        connect(row, &ComfyControlLayerRowWidget::entryEdited, this, &ComfyControlLayerListWidget::entryEdited);
        connect(row, &ComfyControlLayerRowWidget::generateRequested, this,
                &ComfyControlLayerListWidget::generateRequested);
        connect(row, &ComfyControlLayerRowWidget::addPoseCharacterRequested, this,
                &ComfyControlLayerListWidget::addPoseCharacterRequested);
        connect(row, &ComfyControlLayerRowWidget::removeRequested, this, &ComfyControlLayerListWidget::removeRequested);
    }
}

void ComfyControlLayerListWidget::refreshLayerCombos()
{
    for (ComfyControlLayerRowWidget *row : m_rows)
        row->refreshLayerCombo();
}
