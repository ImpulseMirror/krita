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

#include <QFile>
#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

#include <kis_annotation.h>
#include <kis_image.h>
#include <KisDocument.h>

using namespace ComfyHistoryInternal;

void ComfyUIRemoteDock::removeDocumentHistoryBlobForSlot(KisImageSP img, int slot)
{
    if (!img || slot < 0)
        return;
    const QString withW = ComfyUIUtils::documentAnnotationKey(ComfyUIUtils::historyResultLogicalKey(slot));
    const QString noW = ComfyUIUtils::documentAnnotationKey(QStringLiteral("result%1").arg(slot));
    img->removeAnnotation(withW);
    img->removeAnnotation(noW);
}

void ComfyUIRemoteDock::evictDocumentEmbeddedSlotIfAny(int documentSlot)
{
    if (documentSlot < 0)
        return;
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    removeDocumentHistoryBlobForSlot(img, documentSlot);
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::scheduleDocumentUiJsonSave()
{
    if (!m_d->documentUiJsonSaveTimer) {
        m_d->documentUiJsonSaveTimer = new QTimer(this);
        m_d->documentUiJsonSaveTimer->setSingleShot(true);
        connect(m_d->documentUiJsonSaveTimer, &QTimer::timeout, this, [this]() {
            flushDocumentUiJsonNow();
        });
    }
    m_d->documentUiJsonSaveTimer->start(1000);
}

void ComfyUIRemoteDock::flushDocumentUiJsonNow()
{
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    ui.insert(QStringLiteral("version"), ComfyUIUtils::persistenceFormatVersion);
    // §13.189: mirror slices that we store as separate annotations into ui.json for Python-plugin / .kra parity.
    {
        KisAnnotationSP annInpaint = img->annotation(ComfyUIUtils::inpaintWorkspaceAnnotationKey());
        if (annInpaint && !annInpaint->annotation().isEmpty()) {
            const QJsonObject slice = QJsonDocument::fromJson(annInpaint->annotation()).object();
            if (!slice.isEmpty())
                ui.insert(QStringLiteral("inpaint"), slice);
        }
        KisAnnotationSP annLive = img->annotation(ComfyUIUtils::liveWorkspaceAnnotationKey());
        if (annLive && !annLive->annotation().isEmpty()) {
            const QJsonObject slice = QJsonDocument::fromJson(annLive->annotation()).object();
            if (!slice.isEmpty())
                ui.insert(QStringLiteral("live"), slice);
        }
        KisAnnotationSP annPreview = img->annotation(ComfyUIUtils::previewLayerAnnotationKey());
        if (annPreview && !annPreview->annotation().isEmpty()) {
            const QString id = QString::fromUtf8(annPreview->annotation()).trimmed();
            if (!id.isEmpty())
                ui.insert(QStringLiteral("preview_layer"), id);
        }
    }
    mergeDocumentModelIntoUiJson(&ui, img);
    const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const QString histFmt = sset.value(QStringLiteral("history_format")).toString(QStringLiteral("webp"));
    QJsonArray hist;
    for (const Private::HistoryEntry &e : m_d->history.historyEntries) {
        if (e.documentSlot < 0)
            continue;
        hist.append(historyEntryToJsonObject(e, histFmt));
    }
    ui.insert(QStringLiteral("history"), hist);
    ui.insert(QStringLiteral("animation"), animationWorkspaceToJson());
    const QString key = ComfyUIUtils::documentUiJsonAnnotationKey();
    img->removeAnnotation(key);
    const QByteArray bytes = QJsonDocument(ui).toJson(QJsonDocument::Compact);
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("ui.json")), bytes)));
}

void ComfyUIRemoteDock::persistTopHistoryEntryToDocument(bool skipForAnimationFrame)
{
    if (skipForAnimationFrame || m_d->history.historyEntries.isEmpty())
        return;
    Private::HistoryEntry &entry = m_d->history.historyEntries.last();
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    QStringList paths = entry.resultImagePaths;
    if (paths.isEmpty() && !entry.resultImagePath.isEmpty())
        paths << entry.resultImagePath;
    if (paths.isEmpty())
        return;
    for (const QString &p : paths) {
        if (!QFile::exists(p))
            return;
    }
    QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const int slot = ComfyUIUtils::maxHistorySlotFromDocument(img, ui) + 1;
    const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const ComfyUIUtils::HistoryImageEncodeResult enc = ComfyUIUtils::encodeHistoryImagesFromPaths(paths, sset);
    if (enc.data.isEmpty())
        return;
    const QString logical = ComfyUIUtils::historyResultLogicalKey(slot);
    const QString annKey = ComfyUIUtils::documentAnnotationKey(logical);
    img->removeAnnotation(annKey);
    img->removeAnnotation(ComfyUIUtils::documentAnnotationKey(QStringLiteral("result%1").arg(slot)));
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(annKey, ComfyUIUtils::documentAnnotationDescription(logical), enc.data)));
    entry.documentSlot = slot;
    entry.documentBlobEndOffsets = enc.offsets;
    scheduleDocumentUiJsonSave();
    pruneDocumentEmbeddedHistoryIfNeeded();
}

void ComfyUIRemoteDock::reEmbedHistoryEntryAtIndex(int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size())
        return;
    Private::HistoryEntry &entry = m_d->history.historyEntries[entryIndex];
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    QStringList paths = entry.resultImagePaths;
    if (paths.isEmpty() && !entry.resultImagePath.isEmpty())
        paths << entry.resultImagePath;
    if (paths.isEmpty())
        return;
    for (const QString &p : paths) {
        if (!QFile::exists(p))
            return;
    }
    const int oldSlot = entry.documentSlot;
    if (oldSlot >= 0)
        removeDocumentHistoryBlobForSlot(img, oldSlot);
    QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const int slot = ComfyUIUtils::maxHistorySlotFromDocument(img, ui) + 1;
    const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const ComfyUIUtils::HistoryImageEncodeResult enc = ComfyUIUtils::encodeHistoryImagesFromPaths(paths, sset);
    if (enc.data.isEmpty())
        return;
    const QString logical = ComfyUIUtils::historyResultLogicalKey(slot);
    const QString annKey = ComfyUIUtils::documentAnnotationKey(logical);
    img->removeAnnotation(annKey);
    img->removeAnnotation(ComfyUIUtils::documentAnnotationKey(QStringLiteral("result%1").arg(slot)));
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(annKey, ComfyUIUtils::documentAnnotationDescription(logical), enc.data)));
    entry.documentSlot = slot;
    entry.documentBlobEndOffsets = enc.offsets;
    scheduleDocumentUiJsonSave();
    pruneDocumentEmbeddedHistoryIfNeeded();
}

void ComfyUIRemoteDock::pruneDocumentEmbeddedHistoryIfNeeded()
{
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    const qint64 limit = ComfyUIUtils::documentEmbeddedHistoryLimitBytes(ComfyUIUtils::loadSettingsJson());
    int guard = 0;
    while (ComfyUIUtils::documentEmbeddedHistoryStorageBytes(img) > limit && guard++ < 512) {
        int minIdx = -1;
        int minSlot = INT_MAX;
        for (int i = 0; i < m_d->history.historyEntries.size(); ++i) {
            const int s = m_d->history.historyEntries.at(i).documentSlot;
            if (s >= 0 && s < minSlot) {
                minSlot = s;
                minIdx = i;
            }
        }
        if (minIdx < 0)
            break;
        Private::HistoryEntry old = m_d->history.historyEntries.takeAt(minIdx);
        removeDocumentHistoryBlobForSlot(img, old.documentSlot);
        QStringList paths = old.resultImagePaths;
        if (paths.isEmpty() && !old.resultImagePath.isEmpty())
            paths << old.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty() && QFile::exists(p))
                QFile::remove(p);
        }
    }
    scheduleDocumentUiJsonSave();
    refreshHistoryList();
    updateHistoryUsageLabel();
}

void ComfyUIRemoteDock::loadDocumentHistoryFromAnnotations()
{
    m_d->history.historyEntries.clear();
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const QJsonArray h = ui.value(QStringLiteral("history")).toArray();
    for (const QJsonValue &v : h) {
        const QJsonObject ho = v.toObject();
        const int slot = ho.value(QStringLiteral("slot")).toInt(-1);
        if (slot < 0)
            continue;
        const auto keys = ComfyUIUtils::documentAnnotationKeysWithFallback(QStringLiteral("result%1").arg(slot), QStringLiteral("webp"));
        KisAnnotationSP ann = img->annotation(keys.first);
        if (!ann)
            ann = img->annotation(keys.second);
        if (!ann || ann->annotation().isEmpty())
            continue;
        Private::HistoryEntry e;
        if (!historyJsonToEntry(ho, ann->annotation(), img, &e))
            continue;
        m_d->history.historyEntries.append(e);
    }
    refreshHistoryList();
    updateHistoryUsageLabel();
}
