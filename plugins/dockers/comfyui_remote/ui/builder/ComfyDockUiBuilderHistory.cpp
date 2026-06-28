/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilder.h"
#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyWorkspaceSelectButton.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyHistoryListWidget.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KSharedConfig>
#include <KConfigGroup>

#include <kis_annotation.h>
#include <kis_types.h>

using ComfyDockShellInternal::ComfyPromptPlainTextEdit;
using ComfyDockShellInternal::LiveSpinnerWidget;
using ComfyDockShellInternal::StrengthSpinBox;
using ComfyDockShellInternal::setComboCurrentItemData;



namespace ComfyDockUiBuilder {

void buildHistoryPanel(const Context &ctx, QVBoxLayout *scrollLayout)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    QGroupBox *histGroup = new QGroupBox();
    d->history.histGroupBox = histGroup;
    histGroup->setFlat(true);
    histGroup->setStyleSheet(QStringLiteral("QGroupBox{border:0;margin:0;padding:0;}"));
    QVBoxLayout *histLayout = new QVBoxLayout(histGroup);
    histLayout->setContentsMargins(0, 0, 0, 0);
    d->history.listHistory = new ComfyHistoryListWidget();
    d->history.listHistory->setMaximumHeight(140);
    d->history.listHistory->setViewMode(QListWidget::IconMode);
    d->history.listHistory->setIconSize(QSize(96, 96));  // §13.28a: thumbnail size 96×96 px
    d->history.listHistory->setFlow(QListView::LeftToRight);
    d->history.listHistory->setResizeMode(QListView::Adjust);
    d->history.listHistory->setSelectionMode(QAbstractItemView::SingleSelection);
    d->history.listHistory->setFrameShape(QFrame::NoFrame);
    d->history.listHistory->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // §13.28a: vertical scroll only
    d->history.listHistory->setStyleSheet(QStringLiteral("QListWidget { background-color: transparent; }"));
    d->history.listHistory->setSpacing(4);
    d->history.listHistory->setMovement(QListWidget::Static);
    d->history.listHistory->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(d->history.listHistory, &QListWidget::customContextMenuRequested, dock,
                     &ComfyUIRemoteDock::slotHistoryContextMenu);
    QObject::connect(d->history.listHistory, &QListWidget::itemSelectionChanged, dock,
                     &ComfyUIRemoteDock::slotHistoryItemSelected);
    QObject::connect(d->history.listHistory, &QListWidget::doubleClicked, dock, [dock, d](const QModelIndex &) {
        if (d->history.listHistory)
            dock->slotHistoryApplyForItem(d->history.listHistory->currentItem());
    });
    // FAITHFUL_PORT: selection drives preview (jobs.selection_changed → update_preview).
    // Re-click selected thumb deselects → hide preview. Apply commits via overlay or double-click.
    QObject::connect(d->history.listHistory, &ComfyHistoryListWidget::applyRequested, dock,
                     &ComfyUIRemoteDock::slotHistoryApplyForItem);
    QObject::connect(d->history.listHistory, &ComfyHistoryListWidget::contextMenuRequested, dock, [dock, d]() {
        if (!d->history.listHistory || !d->history.listHistory->currentItem())
            return;
        const QRect rect = d->history.listHistory->visualItemRect(d->history.listHistory->currentItem());
        dock->slotHistoryContextMenu(rect.bottomRight());
    });
    histLayout->addWidget(d->history.listHistory);
    d->history.historyButtonsRowWidget = new QWidget(histGroup);
    QHBoxLayout *historyBtns = new QHBoxLayout(d->history.historyButtonsRowWidget);
    historyBtns->setContentsMargins(0, 0, 0, 0);
    d->history.btnHistoryReRun = new QPushButton(ComfyTr::tr("Re-run"));
    d->history.btnHistoryApply = new QPushButton(ComfyTr::tr("Apply"));
    QObject::connect(d->history.btnHistoryReRun, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotHistoryReRun);
    QObject::connect(d->history.btnHistoryApply, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotHistoryApply);
    historyBtns->addWidget(d->history.btnHistoryReRun);
    historyBtns->addWidget(d->history.btnHistoryApply);
    histLayout->addWidget(d->history.historyButtonsRowWidget);
    d->history.btnHistoryReRun->setEnabled(false);
    d->history.btnHistoryApply->setEnabled(false);
    scrollLayout->addWidget(histGroup);
}


} // namespace ComfyDockUiBuilder
