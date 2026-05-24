/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlLayer.h"
#include "ComfyResources.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QUuid>

#include <KisViewManager.h>
#include <kis_layer.h>

#include <klocalizedstring.h>

void ComfyUIRemoteDock::setupRootControlLayersUi(QWidget *parent, QVBoxLayout *layout)
{
    m_d->controlLayersGroupBox = new QGroupBox(i18n("Control layers"), parent);
    QVBoxLayout *clLay = new QVBoxLayout(m_d->controlLayersGroupBox);
    m_d->listRootControlLayers = new QListWidget(m_d->controlLayersGroupBox);
    m_d->listRootControlLayers->setToolTip(
        i18n("Control layers applied to the root prompt on Generate (ControlNet; IP-Adapter in a later step)."));
    clLay->addWidget(m_d->listRootControlLayers);
    QHBoxLayout *btnRow = new QHBoxLayout();
    m_d->btnAddControlLayer = new QPushButton(i18n("Add control layer"), m_d->controlLayersGroupBox);
    m_d->btnRemoveControlLayer = new QPushButton(i18n("Remove"), m_d->controlLayersGroupBox);
    m_d->btnRemoveControlLayer->setEnabled(false);
    btnRow->addWidget(m_d->btnAddControlLayer);
    btnRow->addWidget(m_d->btnRemoveControlLayer);
    btnRow->addStretch();
    clLay->addLayout(btnRow);
    connect(m_d->btnAddControlLayer, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotAddControlLayer);
    connect(m_d->btnRemoveControlLayer, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRemoveControlLayer);
    connect(m_d->listRootControlLayers, &QListWidget::currentRowChanged, this,
            &ComfyUIRemoteDock::slotControlLayerSelectionChanged);
    layout->addWidget(m_d->controlLayersGroupBox);
}

void ComfyUIRemoteDock::refreshRootControlLayersList()
{
    if (!m_d->listRootControlLayers)
        return;
    m_d->listRootControlLayers->clear();
    for (const ComfyControlLayerEntry &e : m_d->rootControlLayers) {
        const QString layer = e.layerName.isEmpty() ? i18n("(no layer)") : e.layerName;
        m_d->listRootControlLayers->addItem(
            QStringLiteral("%1 — %2").arg(ComfyControlLayer::modeLabel(e.mode), layer));
    }
}

void ComfyUIRemoteDock::slotAddControlLayer()
{
    QString layerName;
    if (m_d->viewManager) {
        if (KisLayerSP al = m_d->viewManager->activeLayer())
            layerName = al->name();
    }
    const QString archKey =
        ComfyResources::archToKey(ComfyResources::archFromCheckpointName(m_d->comboCheckpoint->currentText().trimmed()));
    ComfyControlLayerEntry e = ComfyControlLayer::makeDefaultForLayer(layerName, archKey);
    e.layerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (m_d->controlPreviewRangeSlider) {
        const QPair<int, int> iv = m_d->controlPreviewRangeSlider->interval();
        e.start = iv.first / 100.0;
        e.end = iv.second / 100.0;
    }
    m_d->rootControlLayers.append(e);
    refreshRootControlLayersList();
    m_d->listRootControlLayers->setCurrentRow(m_d->rootControlLayers.size() - 1);
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::slotRemoveControlLayer()
{
    const int row = m_d->listRootControlLayers ? m_d->listRootControlLayers->currentRow() : -1;
    if (row < 0 || row >= m_d->rootControlLayers.size())
        return;
    m_d->rootControlLayers.removeAt(row);
    refreshRootControlLayersList();
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::slotControlLayerSelectionChanged()
{
    if (m_d->btnRemoveControlLayer)
        m_d->btnRemoveControlLayer->setEnabled(m_d->listRootControlLayers
                                               && m_d->listRootControlLayers->currentRow() >= 0);
}
