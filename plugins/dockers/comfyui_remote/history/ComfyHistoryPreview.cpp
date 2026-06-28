/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyHistoryInternal.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlLayer.h"
#include "ComfyResources.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMenu>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QHash>
#include <QUuid>

#include <kis_image_manager.h>
#include <kis_node_manager.h>
#include <KisDocument.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_layer_utils.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_node.h>
#include <kis_painter.h>
#include <KisImageBarrierLock.h>
#include <KoCompositeOpRegistry.h>
#include <KoColorSpaceConstants.h>

using namespace ComfyHistoryInternal;

void ComfyUIRemoteDock::slotHistoryPreview()
{
    if (!m_d->history.listHistory)
        return;
    showHistoryPreviewForItem(m_d->history.listHistory->currentItem());
}

void ComfyUIRemoteDock::slotHistoryPreviewForItem(QListWidgetItem *item)
{
    showHistoryPreviewForItem(item);
}

void ComfyUIRemoteDock::clearHistoryListSelection()
{
    if (!m_d->history.listHistory)
        return;
    m_d->history.historyPreviewUpdateBlocked = true;
    m_d->history.listHistory->clearSelection();
    m_d->history.listHistory->setCurrentItem(nullptr);
    m_d->history.historyPreviewUpdateBlocked = false;
}

void ComfyUIRemoteDock::tryBindPreviewLayerFromDocument()
{
    if (m_d->previewLayerId.isEmpty() || !m_d->viewManager)
        return;
    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return;
    if (!findPreviewLayerByUuidString(image, m_d->previewLayerId)) {
        m_d->previewLayerId.clear();
        m_d->history.previewHistoryJobId.clear();
        m_d->history.previewHistoryImageIndex = -1;
        savePreviewLayerIdToDocument(QString());
    }
}

void ComfyUIRemoteDock::hideHistoryPreview(bool deleteLayer)
{
    if (deleteLayer) {
        clearHistoryPreviewState();
        return;
    }
    if (!m_d->viewManager)
        return;
    KisImageSP image = m_d->viewManager->image();
    if (!image || m_d->previewLayerId.isEmpty())
        return;
    KisLayerSP layer = findPreviewLayerByUuidString(image, m_d->previewLayerId);
    if (!layer) {
        m_d->previewLayerId.clear();
        m_d->history.previewHistoryJobId.clear();
        m_d->history.previewHistoryImageIndex = -1;
        savePreviewLayerIdToDocument(QString());
        return;
    }
    configurePreviewLayerState(layer, false, layer->userLocked());
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
}

void ComfyUIRemoteDock::rememberHistoryPreviewImage(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull())
        return;
    QImage img = image;
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);
    trimHistoryPreviewImageCache(&m_d->history.historyPreviewImageCache);
    m_d->history.historyPreviewImageCache.insert(path, img);
}

void ComfyUIRemoteDock::updateHistoryPreviewFromSelection()
{
    if (m_d->history.historyPreviewUpdateBlocked || !m_d->history.listHistory)
        return;
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2)
            return;
    }
    QListWidgetItem *item = m_d->history.listHistory->currentItem();
    if (!item || historyEntryIsHeaderItem(item)) {
        hideHistoryPreview(false);
        return;
    }
    showHistoryPreviewForItem(item);
}

void ComfyUIRemoteDock::showHistoryPreviewForItem(QListWidgetItem *item)
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "showHistoryPreviewForItem item="
        << (item ? item->data(Qt::UserRole).toString() : QStringLiteral("null"))
        << " currentRow=" << (m_d->history.listHistory ? m_d->history.listHistory->currentRow() : -1)
        << " count=" << (m_d->history.listHistory ? m_d->history.listHistory->count() : -1)
        << " workspace=" << (m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1)
        << " currentPreviewLayerId=" << m_d->previewLayerId;
    if (!item || historyEntryIsHeaderItem(item))
        return;
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2) {
            qCWarning(KIS_COMFYUI_REMOTE) << "showHistoryPreviewForItem: workspace gate, ws=" << ws << "; aborting";
            return;
        }
    }
    if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    if (!image) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    int entryIndex = -1;
    int imageIndex = -1;
    const QString path = historyPathForListItem(item, m_d->history.historyEntries, &entryIndex, &imageIndex);
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "showHistoryPreviewForItem resolved entryIndex=" << entryIndex
        << " imageIndex=" << imageIndex << " path=" << path
        << " fileExists=" << (path.isEmpty() ? false : QFile::exists(path));
    if (path.isEmpty() || !QFile::exists(path)) {
        setStatusMessage(ComfyTr::tr("No result image to preview."), true);
        return;
    }

    QString label;
    QString previewName = QStringLiteral("[Preview] result");
    QPoint previewOffset;
    if (entryIndex >= 0 && entryIndex < m_d->history.historyEntries.size()) {
        const Private::HistoryEntry &entry = m_d->history.historyEntries.at(entryIndex);
        label = historyEntryShortLabel(entry);
        previewName = previewLayerNameForEntry(entry);
        const QImage previewImg = cachedHistoryPreviewImage(path, &m_d->history.historyPreviewImageCache);
        previewOffset = historyMaskedPreviewOffset(entry, previewImg.size());
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "showHistoryPreviewForItem: placement imageSize=" << previewImg.size()
            << " offset=" << previewOffset << " contextBounds=" << entry.contextBounds
            << " targetBounds=" << entry.targetBounds << " hasMask=" << entry.hasMask;
    }
    if (label.isEmpty())
        label = QStringLiteral("result");

    KisLayerSP previewLayer = findPreviewLayerByUuidString(image, m_d->previewLayerId);
    KisLayerSP imported;
    if (previewLayer && previewLayer->name().startsWith(QLatin1String("[Preview]"))) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "showHistoryPreviewForItem: reusing preview layer name=" << previewLayer->name();
        if (auto *pl = qobject_cast<KisPaintLayer *>(previewLayer.data())) {
            const QImage previewImg = cachedHistoryPreviewImage(path, &m_d->history.historyPreviewImageCache);
            if (previewImg.isNull()
                || !updatePreviewPaintLayerFromImage(m_d->viewManager.data(), image, pl, previewImg, previewName,
                                                     previewOffset))
                previewLayer = KisLayerSP();
        } else {
            previewLayer = KisLayerSP();
        }
        imported = previewLayer;
    }
    if (!imported) {
        hideHistoryPreview(true);
        const QImage previewImg = cachedHistoryPreviewImage(path, &m_d->history.historyPreviewImageCache);
        if (previewImg.isNull()) {
            setStatusMessage(ComfyTr::tr("No result image to preview."), true);
            return;
        }
        KisPaintLayerSP pl(new KisPaintLayer(image, previewName, OPACITY_OPAQUE_U8));
        if (!loadQImageIntoPaintLayer(pl.data(), image, previewImg, previewOffset)) {
            setStatusMessage(ComfyTr::tr("Could not import preview image."), true);
            return;
        }
        configurePreviewLayerState(pl, true, true);
        KisNodeSP root = image->rootLayer();
        if (!root) {
            setStatusMessage(ComfyTr::tr("Could not import preview image."), true);
            return;
        }
        KisNodeSP above = topDirectRootChild(root);
        if (m_d->viewManager->nodeManager()) {
            KisNodeList nodes;
            nodes.append(pl);
            m_d->viewManager->nodeManager()->addNodesDirect(nodes, root, above);
            m_d->viewManager->nodeManager()->slotNonUiActivatedNode(pl);
        } else {
            image->addNode(pl, root, above);
        }
        imported = pl;
        nudgePreviewLayerProjection(imported);
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "showHistoryPreviewForItem: created preview layer name=" << imported->name();
    }

    const QString uidStr = imported->uuid().toString(QUuid::WithoutBraces);
    m_d->previewLayerId = uidStr;
    m_d->history.previewHistoryJobId = item->data(Qt::UserRole).toString();
    m_d->history.previewHistoryImageIndex = item->data(Qt::UserRole + 1).toInt();
    savePreviewLayerIdToDocument(uidStr);

    nudgePreviewLayerProjection(imported);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    if (m_d->history.listHistory)
        m_d->history.listHistory->updateOverlayButtons();
    setStatusMessage(ComfyTr::tr("Previewing \"%1\". Tap Apply to keep, or tap another thumbnail.", label));
}

void ComfyUIRemoteDock::updateLiveResultPreview(const QImage &composition, const QPoint &docOffset)
{
    if (composition.isNull() || !m_d->viewManager || !m_d->viewManager->image())
        return;
    KisImageSP image = m_d->viewManager->image();
    const QString previewName = QStringLiteral("[Preview] live");

    KisLayerSP previewLayer = findPreviewLayerByUuidString(image, m_d->previewLayerId);
    KisLayerSP imported;
    if (previewLayer && previewLayer->name().startsWith(QLatin1String("[Preview]"))) {
        if (auto *pl = qobject_cast<KisPaintLayer *>(previewLayer.data())) {
            if (updatePreviewPaintLayerFromImage(m_d->viewManager.data(), image, pl, composition, previewName, docOffset))
                imported = previewLayer;
        }
    }
    if (!imported) {
        KisPaintLayerSP pl(new KisPaintLayer(image, previewName, OPACITY_OPAQUE_U8));
        if (!loadQImageIntoPaintLayer(pl.data(), image, composition, docOffset))
            return;
        configurePreviewLayerState(pl, true, true);
        KisNodeSP root = image->rootLayer();
        if (!root)
            return;
        KisNodeSP above = topDirectRootChild(root);
        if (m_d->viewManager->nodeManager()) {
            KisNodeList nodes;
            nodes.append(pl);
            m_d->viewManager->nodeManager()->addNodesDirect(nodes, root, above);
        } else {
            image->addNode(pl, root, above);
        }
        imported = pl;
        nudgePreviewLayerProjection(imported);
    }

    m_d->previewLayerId = imported->uuid().toString(QUuid::WithoutBraces);
    savePreviewLayerIdToDocument(m_d->previewLayerId);
    nudgePreviewLayerProjection(imported);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
}

void ComfyUIRemoteDock::clearHistoryPreviewState()
{
    if (m_d->viewManager && m_d->viewManager->image()) {
        removePreviewLayersFromImage(m_d->viewManager->image(),
                                     m_d->viewManager.data(),
                                     m_d->previewLayerId);
    }
    m_d->previewLayerId.clear();
    m_d->history.previewHistoryJobId.clear();
    m_d->history.previewHistoryImageIndex = -1;
    savePreviewLayerIdToDocument(QString());
}
