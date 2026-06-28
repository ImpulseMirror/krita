/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyHistoryListWidget.h"

#include "ComfyHistoryInternal.h"
#include "ComfyLocalization.h"
#include "ComfyTheme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTimer>

namespace {

class HistoryThumbnailDelegate : public QStyledItemDelegate
{
public:
    explicit HistoryThumbnailDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(ComfyHistoryInternal::HistoryItemIsHeaderRole).toInt() == 1) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QSize deco = opt.decorationSize.isValid() ? opt.decorationSize : opt.rect.size();

        if (!icon.isNull()) {
            const QList<QSize> availSizes = icon.availableSizes();
            const QSize pixSize = availSizes.isEmpty() ? deco : availSizes.first();
            const QPixmap pix = icon.pixmap(pixSize, QIcon::Normal, QIcon::Off);
            if (!pix.isNull()) {
                // Top-align under header row; horizontal center in list width.
                QRect iconRect(QPoint(0, 0), pix.size());
                iconRect.moveTop(opt.rect.top());
                iconRect.moveLeft(opt.rect.left() + (opt.rect.width() - pix.width()) / 2);
                // Preserve masked alpha (circle/feathered selection); opaque fill would square the thumb.
                painter->drawPixmap(iconRect.topLeft(), pix);
            } else {
                painter->fillRect(opt.rect, opt.palette.base());
                QRect iconRect(QPoint(0, 0), deco);
                iconRect.moveTop(opt.rect.top());
                iconRect.moveLeft(opt.rect.left() + (opt.rect.width() - deco.width()) / 2);
                icon.paint(painter, iconRect, Qt::AlignCenter, QIcon::Normal, QIcon::On);
            }
        } else {
            painter->fillRect(opt.rect, opt.palette.base());
        }

        if (!opt.text.isEmpty()) {
            QRect textRect = opt.rect;
            textRect.setLeft(opt.rect.left() + 4);
            painter->setPen(opt.palette.text().color());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, opt.text);
        }
        painter->restore();
    }
};

QString overlayButtonStyleSheet()
{
    const QString grey = ComfyTheme::isDarkTheme() ? QStringLiteral("#888")
                                                   : QStringLiteral("#606060");
    const QString bg = ComfyTheme::isDarkTheme() ? QStringLiteral("rgba(64, 64, 64, 170)")
                                                 : QStringLiteral("rgba(240, 240, 240, 160)");
    const QString bgHover = ComfyTheme::isDarkTheme() ? QStringLiteral("rgba(72, 72, 72, 210)")
                                                      : QStringLiteral("rgba(240, 240, 240, 200)");
    return QStringLiteral(
               "QPushButton { border: 1px solid %1; background: %2; padding: 2px; }"
               "QPushButton:hover { background: %3; }")
        .arg(grey, bg, bgHover);
}

} // namespace

ComfyHistoryListWidget::ComfyHistoryListWidget(QWidget *parent)
    : QListWidget(parent)
{
    setViewportMargins(0, 0, 0, 0);
    setItemDelegate(new HistoryThumbnailDelegate(this));

    m_applyButton = new QPushButton(ComfyTheme::icon(QStringLiteral("apply")),
                                    ComfyTr::tr("Apply"),
                                    viewport());
    m_applyButton->setStyleSheet(overlayButtonStyleSheet());
    m_applyButton->setVisible(false);
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        QListWidgetItem *item = currentItem();
        if (!item && !selectedItems().isEmpty())
            item = selectedItems().first();
        if (item)
            Q_EMIT applyRequested(item);
    });

    m_contextButton = new QPushButton(ComfyTheme::icon(QStringLiteral("context")), QString(), viewport());
    m_contextButton->setStyleSheet(overlayButtonStyleSheet());
    m_contextButton->setVisible(false);
    connect(m_contextButton, &QPushButton::clicked, this, &ComfyHistoryListWidget::contextMenuRequested);

    const QFontMetrics fm(m_applyButton->fontMetrics());
    const int overlayH = fm.height() + 8;
    m_applyButton->setFixedHeight(overlayH);
    m_contextButton->setFixedHeight(overlayH);
    m_contextButton->setFixedWidth(overlayH);

    connect(this, &QListWidget::itemSelectionChanged, this, &ComfyHistoryListWidget::updateOverlayButtons);
    if (QScrollBar *sb = verticalScrollBar()) {
        connect(sb, &QScrollBar::valueChanged, this, &ComfyHistoryListWidget::updateOverlayButtons);
    }
}

void ComfyHistoryListWidget::updateOverlayButtons()
{
    QList<QListWidgetItem *> selected;
    for (QListWidgetItem *item : selectedItems()) {
        if (item && item->data(Qt::UserRole + 2).toInt() != 1)
            selected.append(item);
    }
    if (selected.isEmpty()) {
        m_applyButton->setVisible(false);
        m_contextButton->setVisible(false);
        return;
    }

    const QRect rect = visualItemRect(selected.first());
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
        m_applyButton->setVisible(false);
        m_contextButton->setVisible(false);
        return;
    }

    const bool contextVisible = rect.width() >= static_cast<int>(0.6 * iconSize().width());
    const QPoint applyPos(rect.left() + 3, rect.bottom() - m_applyButton->height() - 2);
    const int contextW = contextVisible ? m_contextButton->width() : 0;
    const QPoint contextPos(rect.right() - contextW - 2, applyPos.y());
    const int applyW = qMax(20, contextPos.x() - rect.left() - 5);

    m_applyButton->setVisible(true);
    m_applyButton->move(applyPos);
    m_applyButton->resize(applyW, m_applyButton->height());
    const bool applyTextFits = m_applyButton->fontMetrics().horizontalAdvance(ComfyTr::tr("Apply"))
                               < static_cast<int>(0.35 * rect.width());
    m_applyButton->setText(applyTextFits ? ComfyTr::tr("Apply") : QString());

    m_contextButton->setVisible(contextVisible);
    if (contextVisible) {
        m_contextButton->move(contextPos);
        m_contextButton->raise();
    }
    m_applyButton->raise();
}

void ComfyHistoryListWidget::resizeEvent(QResizeEvent *event)
{
    QListWidget::resizeEvent(event);
    ComfyHistoryInternal::syncHistoryListItemWidths(this);
    updateOverlayButtons();
}

bool ComfyHistoryListWidget::event(QEvent *event)
{
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &ComfyHistoryListWidget::updateOverlayButtons);
    return QListWidget::event(event);
}

void ComfyHistoryListWidget::mousePressEvent(QMouseEvent *event)
{
    // FAITHFUL_PORT: generation.HistoryWidget — re-click selected thumb clears selection (hides preview).
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        QListWidgetItem *pressed = itemAt(event->pos());
        if (pressed && pressed->data(Qt::UserRole + 2).toInt() == 1) {
            event->accept();
            return;
        }
        if (pressed && pressed->isSelected()) {
            clearSelection();
            setCurrentItem(nullptr);
            event->accept();
            return;
        }
    }
    QListWidget::mousePressEvent(event);
}
