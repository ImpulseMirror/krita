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
#include "ComfyUiLayoutDiagnostics.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMenu>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QDateTime>
#include <QRegularExpression>

#include <klocalizedstring.h>

using namespace ComfyHistoryInternal;

void ComfyUIRemoteDock::refreshHistoryList(bool scrollToBottom)
{
    if (!m_d->history.listHistory)
        return;

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG refreshHistoryList entries=") << m_d->history.historyEntries.size()
        << QStringLiteral("listGeom=") << m_d->history.listHistory->geometry()
        << QStringLiteral("histGroupGeom=")
        << (m_d->history.histGroupBox ? m_d->history.histGroupBox->geometry() : QRect())
        << QStringLiteral("histParent=")
        << (m_d->history.histGroupBox && m_d->history.histGroupBox->parentWidget()
                ? m_d->history.histGroupBox->parentWidget()->objectName()
                : QString());

    QString keepJobId;
    int keepImageIndex = -1;
    if (m_d->history.listHistory->currentItem() && !historyEntryIsHeaderItem(m_d->history.listHistory->currentItem())) {
        keepJobId = m_d->history.listHistory->currentItem()->data(HistoryItemJobIdRole).toString();
        keepImageIndex = m_d->history.listHistory->currentItem()->data(HistoryItemImageIndexRole).toInt();
    } else if (!m_d->history.previewHistoryJobId.isEmpty()) {
        keepJobId = m_d->history.previewHistoryJobId;
        keepImageIndex = m_d->history.previewHistoryImageIndex;
    }

    m_d->history.listHistory->clear();
    const QSize iconSize = m_d->history.listHistory->iconSize();
    const int starSize = 24;
    const int starMargin = 4;
    QIcon starIcon = ComfyTheme::icon(QStringLiteral("star"));
    QPixmap starPix = starIcon.pixmap(starSize, starSize);
    int selectRow = -1;
    int row = 0;
    bool haveLastParams = false;
    Private::HistoryEntry lastParams;

    for (const Private::HistoryEntry &e : m_d->history.historyEntries) {
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (paths.isEmpty())
            continue;

        if (!haveLastParams || !historyParamsEqualIgnoreSeed(lastParams, e)) {
            lastParams = e;
            haveLastParams = true;
            QListWidgetItem *header = new QListWidgetItem(historyEntryHeaderLabel(e));
            header->setFlags(Qt::NoItemFlags);
            header->setData(HistoryItemJobIdRole, e.jobId);
            header->setData(HistoryItemImageIndexRole, -1);
            header->setData(HistoryItemIsHeaderRole, 1);
            header->setToolTip(e.prompt);
            const int headerH = m_d->history.listHistory->fontMetrics().lineSpacing() + 2;
            header->setSizeHint(historyHeaderItemSizeHint(m_d->history.listHistory, headerH));
            header->setTextAlignment(Qt::AlignLeft);
            m_d->history.listHistory->addItem(header);
            ++row;
        }

        const QString snippet = historyEntryShortLabel(e);
        for (int imageIndex = 0; imageIndex < paths.size(); ++imageIndex) {
            const QString path = paths.at(imageIndex);
            QString tip = QStringLiteral("%1 (%2×%3)\nSeed: %4")
                              .arg(snippet)
                              .arg(e.width)
                              .arg(e.height)
                              .arg(e.seed);
            if (paths.size() > 1)
                tip = ComfyTr::tr("Image %1 of %2", imageIndex + 1, paths.size()) + QStringLiteral("\n") + tip;
            QListWidgetItem *item = new QListWidgetItem();
            if (!path.isEmpty() && QFile::exists(path)) {
                QPixmap pix = historyThumbnailPixmap(e, path, iconSize, &m_d->history.historyPreviewImageCache);
                if (!pix.isNull()) {
                    QPixmap displayPix = pix;
                    if (e.imageInUse.value(imageIndex, false) && !starPix.isNull()) {
                        displayPix = QPixmap(pix.size());
                        displayPix.fill(Qt::transparent);
                        QPainter p(&displayPix);
                        p.drawPixmap(0, 0, pix);
                        p.drawPixmap(pix.width() - starSize - starMargin, starMargin, starPix);
                        p.end();
                    }
                    item->setIcon(QIcon(displayPix));
                    item->setSizeHint(historyThumbnailItemSizeHint(m_d->history.listHistory, displayPix.size()));
                }
            }
            if (item->icon().isNull()) {
                item->setText(paths.size() > 1
                                  ? snippet + QStringLiteral(" [%1/%2]").arg(imageIndex + 1).arg(paths.size())
                                  : snippet);
            }
            item->setToolTip(tip);
            item->setData(HistoryItemJobIdRole, e.jobId);
            item->setData(HistoryItemImageIndexRole, imageIndex);
            item->setData(HistoryItemIsHeaderRole, 0);
            m_d->history.listHistory->addItem(item);
            if (!keepJobId.isEmpty() && e.jobId == keepJobId && imageIndex == keepImageIndex)
                selectRow = row;
            ++row;
        }
    }

    if (selectRow >= 0)
        m_d->history.listHistory->setCurrentRow(selectRow);
    else if (scrollToBottom)
        m_d->history.listHistory->scrollToBottom();
    syncHistoryListItemWidths(m_d->history.listHistory);
    m_d->history.listHistory->updateOverlayButtons();
    ComfyUiLayoutDiagnostics::logGenerateHistoryLayout(m_d.data(), "refreshHistoryList");
    dumpUiLayoutDiagnostics("refreshHistoryList");
}

void ComfyUIRemoteDock::slotHistoryItemSelected()
{
    QListWidgetItem *current = m_d->history.listHistory ? m_d->history.listHistory->currentItem() : nullptr;
    const bool hasSelection = current && !historyEntryIsHeaderItem(current);
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotHistoryItemSelected currentRow=" << (m_d->history.listHistory ? m_d->history.listHistory->currentRow() : -1)
        << " count=" << (m_d->history.listHistory ? m_d->history.listHistory->count() : 0)
        << " hasSelection=" << hasSelection;
    m_d->history.btnHistoryReRun->setEnabled(hasSelection);
    m_d->history.btnHistoryApply->setEnabled(hasSelection);
    if (m_d->history.listHistory)
        m_d->history.listHistory->updateOverlayButtons();
    updateHistoryPreviewFromSelection();
}
