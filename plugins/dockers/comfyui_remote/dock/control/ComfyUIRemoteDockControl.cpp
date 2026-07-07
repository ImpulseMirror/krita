/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlLayer.h"
#include "ComfyControlLayerListWidget.h"
#include "ComfyResources.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyUIPoseLayers.h"

#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QUuid>

#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_layer_utils.h>
#include <kis_shape_layer.h>

#include <KisDocument.h>
#include <klocalizedstring.h>

namespace {

QString currentArchKey(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->generate.comboCheckpoint)
        return QString();
    return ComfyResources::archToKey(
        ComfyResources::archFromCheckpointName(d->generate.comboCheckpoint->currentText().trimmed()));
}

} // namespace

void ComfyUIRemoteDock::wireControlLayerList(ComfyControlLayerListWidget *list,
                                             QList<ComfyControlLayerEntry> *layers,
                                             bool forRegion)
{
    Q_UNUSED(forRegion);
    if (!list)
        return;
    ComfyUIRemoteDock::Private *d = m_d.data();
    list->setViewManager(d->viewManager);
    list->setLayers(layers);
    list->setArchKeyProvider([d]() { return currentArchKey(d); });
    connect(list, &ComfyControlLayerListWidget::entryEdited, this, [this]() {
        scheduleDocumentUiJsonSave();
    });
    connect(list, &ComfyControlLayerListWidget::addLayerRequested, this, [this]() {
        if (comfyActiveRegionRow(m_d.data()) >= 0)
            slotAddRegionControlLayer();
        else
            slotAddControlLayer();
    });
    connect(list, &ComfyControlLayerListWidget::generateRequested, this, [this](int index) {
        beginControlLayerGenerateJob(comfyActiveRegionRow(m_d.data()) >= 0, index);
    });
    connect(list, &ComfyControlLayerListWidget::removeRequested, this, [this](int index) {
        if (comfyActiveRegionRow(m_d.data()) >= 0)
            slotRemoveRegionControlLayerAt(index);
        else
            slotRemoveControlLayerAt(index);
    });
    connect(list, &ComfyControlLayerListWidget::addPoseCharacterRequested, this, [this](int index) {
        slotAddPoseForControlLayer(comfyActiveRegionRow(m_d.data()) >= 0, index);
    });
    connect(list, &ComfyControlLayerListWidget::layoutChanged, this, [this]() {
        if (!m_d->shellLayoutReady)
            return;
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0) {
            QTimer::singleShot(0, this, [this]() { syncCompactGenerateLayoutRows(true); });
        }
    });
}

void ComfyUIRemoteDock::setupRootControlLayersUi(QWidget *parent, QVBoxLayout *layout)
{
    Q_UNUSED(layout);
    m_d->generate.rootControlLayerList = new ComfyControlLayerListWidget(parent);
    m_d->generate.rootControlLayerList->setObjectName(QStringLiteral("ComfyRootControlLayerList"));
    m_d->generate.rootControlLayerList->setCompactChrome(true);
    m_d->generate.rootControlLayerList->setToolTip(
        ComfyTr::tr("Control layers for the whole image on Generate (ControlNet and IP-Adapter)."));
    wireControlLayerList(m_d->generate.rootControlLayerList, &m_d->rootControlLayers, false);

    // Legacy groupbox kept for hidden advanced panel; compact chrome embeds rootControlLayerList in region prompt.
    m_d->generate.controlLayersGroupBox = new QGroupBox(ComfyTr::tr("Control layers"), parent);
    m_d->generate.controlLayersGroupBox->hide();
}

void ComfyUIRemoteDock::refreshInlineControlLayersList()
{
    if (!m_d->generate.rootControlLayerList)
        return;
    m_d->generate.rootControlLayerList->setLayoutNotificationsEnabled(m_d->shellLayoutReady);
    QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int row = comfyActiveRegionRow(m_d.data());
    if (row >= 0 && row < regs.size())
        m_d->generate.rootControlLayerList->setLayers(&regs[row].controlLayers);
    else
        m_d->generate.rootControlLayerList->setLayers(&m_d->rootControlLayers);
    m_d->generate.rootControlLayerList->refreshLayerCombos();
    refreshControlLayerGenerateButtons();
}

void ComfyUIRemoteDock::refreshRootControlLayersList()
{
    refreshInlineControlLayersList();
    if (m_d->generate.regionControlLayerList) {
        QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
        const int row = comfyActiveRegionRow(m_d.data());
        const bool hasRegion = row >= 0 && row < regs.size();
        if (hasRegion) {
            m_d->generate.regionControlLayerList->setLayers(&regs[row].controlLayers);
            m_d->generate.regionControlLayerList->refresh();
            m_d->generate.regionControlLayerList->refreshLayerCombos();
        }
    }
}

void ComfyUIRemoteDock::slotAddControlLayer()
{
    QString layerName;
    QString layerId;
    if (m_d->viewManager) {
        if (KisLayerSP al = m_d->viewManager->activeLayer()) {
            layerName = al->name();
            layerId = al->uuid().toString(QUuid::WithoutBraces);
        }
    }
    ComfyControlLayerEntry e = ComfyControlLayer::makeDefaultForLayer(layerName, currentArchKey(m_d.data()));
    e.layerId = layerId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : layerId;
    if (m_d->generate.controlPreviewRangeSlider) {
        e.start = m_d->generate.controlPreviewRangeSlider->lowValue() / 100.0;
        e.end = m_d->generate.controlPreviewRangeSlider->highValue() / 100.0;
    }
    m_d->rootControlLayers.append(e);
    refreshRootControlLayersList();
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::slotRemoveControlLayerAt(int row)
{
    if (row < 0 || row >= m_d->rootControlLayers.size())
        return;
    m_d->rootControlLayers.removeAt(row);
    refreshRootControlLayersList();
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::slotControlLayerSelectionChanged()
{
    refreshControlLayerGenerateButtons();
}

void ComfyUIRemoteDock::setupRegionControlLayersUi(QWidget *parent, QVBoxLayout *layout)
{
    Q_UNUSED(parent);
    Q_UNUSED(layout);
    m_d->generate.regionControlLayersGroupBox = new QGroupBox(ComfyTr::tr("Control layers"));
    auto *clLay = new QVBoxLayout(m_d->generate.regionControlLayersGroupBox);
    m_d->generate.labelRegionControlLayers = new QLabel(
        ComfyTr::tr("Select a region above to edit its control layers."), m_d->generate.regionControlLayersGroupBox);
    m_d->generate.labelRegionControlLayers->setWordWrap(true);
    clLay->addWidget(m_d->generate.labelRegionControlLayers);
    m_d->generate.regionControlLayerList = new ComfyControlLayerListWidget(m_d->generate.regionControlLayersGroupBox);
    m_d->generate.regionControlLayerList->setToolTip(
        ComfyTr::tr("Control layers for the selected region only (merged with root controls on Generate)."));
    clLay->addWidget(m_d->generate.regionControlLayerList);
    wireControlLayerList(m_d->generate.regionControlLayerList, nullptr, true);
}

void ComfyUIRemoteDock::refreshRegionControlLayersList()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int row = comfyActiveRegionRow(m_d.data());
    const bool hasRegion = row >= 0 && row < regs.size();
    if (m_d->generate.labelRegionControlLayers) {
        if (hasRegion)
            m_d->generate.labelRegionControlLayers->setText(
                ComfyTr::tr("Control layers for region \"%1\":", regs.at(row).name));
        else
            m_d->generate.labelRegionControlLayers->setText(
                ComfyTr::tr("Select a region above to edit its control layers."));
    }
    if (m_d->generate.regionControlLayerList) {
        m_d->generate.regionControlLayerList->setEnabled(hasRegion);
        if (hasRegion) {
            m_d->generate.regionControlLayerList->setLayers(&regs[row].controlLayers);
            m_d->generate.regionControlLayerList->refresh();
            m_d->generate.regionControlLayerList->refreshLayerCombos();
        } else {
            static QList<ComfyControlLayerEntry> empty;
            m_d->generate.regionControlLayerList->setLayers(&empty);
            m_d->generate.regionControlLayerList->refresh();
        }
    }
    refreshInlineControlLayersList();
}

void ComfyUIRemoteDock::slotAddRegionControlLayer()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int row = comfyActiveRegionRow(m_d.data());
    if (row < 0 || row >= regs.size())
        return;
    QString layerName;
    QString layerId;
    if (m_d->viewManager) {
        if (KisLayerSP al = m_d->viewManager->activeLayer()) {
            layerName = al->name();
            layerId = al->uuid().toString(QUuid::WithoutBraces);
        }
    }
    ComfyControlLayerEntry e = ComfyControlLayer::makeDefaultForLayer(layerName, currentArchKey(m_d.data()));
    e.layerId = layerId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : layerId;
    if (m_d->generate.controlPreviewRangeSlider) {
        e.start = m_d->generate.controlPreviewRangeSlider->lowValue() / 100.0;
        e.end = m_d->generate.controlPreviewRangeSlider->highValue() / 100.0;
    }
    regs[row].controlLayers.append(e);
    refreshRegionControlLayersList();
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::slotRemoveRegionControlLayerAt(int clRow)
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int regionRow = comfyActiveRegionRow(m_d.data());
    if (regionRow < 0 || regionRow >= regs.size() || clRow < 0 || clRow >= regs[regionRow].controlLayers.size())
        return;
    regs[regionRow].controlLayers.removeAt(clRow);
    refreshRegionControlLayersList();
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::slotRegionControlLayerSelectionChanged()
{
    refreshControlLayerGenerateButtons();
}

void ComfyUIRemoteDock::slotAddPoseForControlLayer(bool forRegion, int index)
{
    QList<ComfyControlLayerEntry> *layers = forRegion ? nullptr : &m_d->rootControlLayers;
    if (forRegion) {
        QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
        const int row = comfyActiveRegionRow(m_d.data());
        if (row >= 0 && row < regs.size())
            layers = &regs[row].controlLayers;
    }
    if (!layers || index < 0 || index >= layers->size())
        return;
    const ComfyControlLayerEntry &e = layers->at(index);
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    const QUuid uid = QUuid::fromString(e.layerId);
    KisNodeSP node = uid.isNull() ? KisNodeSP() : KisLayerUtils::findNodeByUuid(image->rootLayer(), uid);
    auto *sl = node ? qobject_cast<KisShapeLayer *>(node.data()) : nullptr;
    if (!sl) {
        setStatusMessage(ComfyTr::tr("Select a vector layer for this control row, then add a pose guide."), true);
        return;
    }
    KisDocument *doc = m_d->canvas && m_d->canvas->imageView() ? m_d->canvas->imageView()->document() : nullptr;
    if (!doc) {
        setStatusMessage(ComfyTr::tr("Could not access the document to edit vector shapes."), true);
        return;
    }
    const int people = m_d->generate.spinPoseGuidePeopleCount ? m_d->generate.spinPoseGuidePeopleCount->value() : 1;
    if (ComfyUIPoseLayers::instance().addPoseCharacter(image, sl, doc, people))
        setStatusMessage(ComfyTr::tr("Pose guide added. Pose SVG is refreshed every 500 ms while the layer exists."), false);
    else
        setStatusMessage(ComfyTr::tr("Could not add pose guide (check that the layer is a vector layer)."), true);
}
