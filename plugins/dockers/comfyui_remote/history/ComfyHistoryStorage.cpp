/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyHistoryInternal.h"

#include <QFile>
#include <QFileInfo>
#include <KSharedConfig>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

QString ComfyUIRemoteDock::pathForCurrentHistoryRow(int *outEntryIndex, int *outImageIndex) const
{
    int row = m_d->history.listHistory->currentRow();
    if (row < 0) return QString();
    QListWidgetItem *item = m_d->history.listHistory->item(row);
    if (!item || item->data(Qt::UserRole + 2).toInt() == 1)
        return QString();
    QString jobId = item->data(Qt::UserRole).toString();
    int imageIndex = item->data(Qt::UserRole + 1).toInt();
    for (int i = 0; i < m_d->history.historyEntries.size(); i++) {
        const Private::HistoryEntry &e = m_d->history.historyEntries.at(i);
        if (e.jobId != jobId) continue;
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (imageIndex >= 0 && imageIndex < paths.size()) {
            if (outEntryIndex) *outEntryIndex = i;
            if (outImageIndex) *outImageIndex = imageIndex;
            return paths.at(imageIndex);
        }
        return QString();
    }
    return QString();
}

qint64 ComfyUIRemoteDock::historyResultStorageBytes() const
{
    qint64 total = 0;
    for (const Private::HistoryEntry &e : m_d->history.historyEntries) {
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty()) {
                const QFileInfo info(p);
                if (info.exists())
                    total += info.size();
            }
        }
    }
    return total;
}

void ComfyUIRemoteDock::pruneHistoryToStorageLimit()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    // §4.8: Active history size (RAM cache) — history_size (Python), then legacy keys
    int limitMb = s.value(QStringLiteral("history_size")).toInt(0);
    if (limitMb <= 0)
        limitMb = s.value(QStringLiteral("history_active_mb")).toInt(0);
    if (limitMb <= 0)
        limitMb = s.value(QStringLiteral("history_storage")).toInt(20);
    limitMb = qBound(5, limitMb, 20000);
    const qint64 limitBytes = static_cast<qint64>(limitMb) * 1024 * 1024;
    while (m_d->history.historyEntries.size() > 0 && historyResultStorageBytes() > limitBytes) {
        Private::HistoryEntry e = m_d->history.historyEntries.takeFirst();
        evictDocumentEmbeddedSlotIfAny(e.documentSlot);
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p);
        }
    }
    refreshHistoryList();
    updateHistoryUsageLabel();
}
void ComfyUIRemoteDock::updateHistoryUsageLabel()
{
    if (m_d->labelHistoryUsageMb) {
        const qint64 bytes = historyResultStorageBytes();
        const double mb = bytes / (1024.0 * 1024.0);
        m_d->labelHistoryUsageMb->setText(ComfyTr::tr("Currently using %1 MB", QString::number(mb, 'f', 1)));
    }
    if (m_d->labelStoredHistoryMb) {
        KisImageSP img = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
        const qint64 docBytes = ComfyUIUtils::documentEmbeddedHistoryStorageBytes(img);
        const double docMb = docBytes / (1024.0 * 1024.0);
        m_d->labelStoredHistoryMb->setText(ComfyTr::tr("Currently using %1 MB", QString::number(docMb, 'f', 1)));
    }
}
