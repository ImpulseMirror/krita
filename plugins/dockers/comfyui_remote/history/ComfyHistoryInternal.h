/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_HISTORY_INTERNAL_H_
#define COMFY_HISTORY_INTERNAL_H_

#include "ComfyUIRemoteDockPrivate.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <kis_types.h>

class KisViewManager;

namespace ComfyHistoryInternal {

enum HistoryListItemRole {
    HistoryItemJobIdRole = Qt::UserRole,
    HistoryItemImageIndexRole = Qt::UserRole + 1,
    HistoryItemIsHeaderRole = Qt::UserRole + 2,
};

QJsonObject historyEntryToJsonObject(const ComfyUIRemoteDock::Private::HistoryEntry &e, const QString &histFmt);
void remapImageInUseAfterRemoval(QMap<int, bool> &m, int removedIndex);
QString safeDocIdFragment(KisImageSP img);
bool historyJsonToEntry(const QJsonObject &ho, const QByteArray &blob, KisImageSP img,
                        ComfyUIRemoteDock::Private::HistoryEntry *out);
QString historyEntryDisplayName(const ComfyUIRemoteDock::Private::HistoryEntry &e, int maxLen);
QString historyEntryShortLabel(const ComfyUIRemoteDock::Private::HistoryEntry &e);
bool historyEntryIsHeaderItem(const QListWidgetItem *item);
bool historyParamsEqualIgnoreSeed(const ComfyUIRemoteDock::Private::HistoryEntry &a,
                                  const ComfyUIRemoteDock::Private::HistoryEntry &b);
QString historyEntryHeaderLabel(const ComfyUIRemoteDock::Private::HistoryEntry &e);
QString previewLayerNameForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &e);
QString generatedLayerNameForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &e);
QPoint historyMaskedPreviewOffset(const ComfyUIRemoteDock::Private::HistoryEntry &e, const QSize &imageSize);
bool loadImageFileIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QString &path,
                                 const QPoint &offset = QPoint());
bool loadQImageIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QImage &qimg,
                              const QPoint &offset = QPoint());
QImage cachedHistoryPreviewImage(const QString &path, QHash<QString, QImage> *cache);
void trimHistoryPreviewImageCache(QHash<QString, QImage> *cache, int maxEntries = 48);
void nudgePreviewLayerProjection(KisLayerSP layer);
KisNodeSP topDirectRootChild(KisNodeSP root);
void raiseLayerToRootTop(KisViewManager *viewManager, KisImageSP image, KisLayerSP layer,
                         bool waitForCompletion = true);
void configurePreviewLayerState(KisNodeSP node, bool visible, bool locked);
bool updatePreviewPaintLayerFromImage(KisViewManager *viewManager, KisImageSP image, KisPaintLayer *pl,
                                      const QImage &qimg, const QString &layerName,
                                      const QPoint &offset = QPoint());
bool updatePreviewPaintLayerFromFile(KisViewManager *viewManager, KisImageSP image, KisPaintLayer *pl,
                                     const QString &path, const QString &layerName,
                                     const QPoint &offset = QPoint(),
                                     QHash<QString, QImage> *cache = nullptr);
bool addPreviewPaintLayerFromFile(KisViewManager *viewManager, KisImageSP image, const QString &path,
                                  const QString &layerName, KisLayerSP *outLayer,
                                  const QPoint &offset = QPoint());
KisLayerSP findPreviewLayerByUuidString(KisImageSP image, const QString &layerId);
bool isDirectChildOf(KisNodeSP parent, KisNodeSP child);
void moveLayerInParent(KisViewManager *viewManager, KisImageSP image, KisNodeSP layer, KisNodeSP parent,
                       KisNodeSP above, bool waitForCompletion);
void collectPreviewLayers(KisNodeSP node, QList<KisLayerSP> *out);
bool layerStillInDocument(KisImageSP image, KisLayerSP layer);
KisNodeSP firstImportAnchorLayer(KisImageSP image, const QList<KisLayerSP> &excluding = {});
void ensureActiveLayerValidForImport(KisViewManager *viewManager, KisImageSP image);
void removePreviewLayersFromImage(KisImageSP image, KisViewManager *viewManager, const QString &trackedLayerId);
void placeImportedLayerForBehavior(KisViewManager *viewManager, KisImageSP image, KisLayerSP imported,
                                   KisLayerSP activeBefore, const QString &behavior);
bool commitPreviewLayerForApply(KisViewManager *viewManager, KisImageSP image, KisLayerSP previewLayer,
                                const QString &committedLayerName, const QString &applyBehavior);
QString applyBehaviorFromSettings(const ComfyUIRemoteDock::Private *d);
QString historyPathForListItem(const QListWidgetItem *item,
                               const QList<ComfyUIRemoteDock::Private::HistoryEntry> &entries,
                               int *outEntryIndex, int *outImageIndex);
QString historyPathForIdentity(const QString &jobId, int imageIndex,
                               const QList<ComfyUIRemoteDock::Private::HistoryEntry> &entries,
                               int *outEntryIndex, int *outImageIndex);
QListWidgetItem *findHistoryListItem(QListWidget *list, const QString &jobId, int imageIndex);

} // namespace ComfyHistoryInternal

#endif
