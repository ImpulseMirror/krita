/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: IconMode + setUniformItemSizes(false) + bad sizeHint width on history rows.
 */

#include <simpletest.h>
#include <QTest>

#include <QApplication>
#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>

#include "ComfyHistoryInternal.h"
#include "ComfyHistoryListWidget.h"

namespace {

void configureHistoryListLikeDock(ComfyHistoryListWidget *list)
{
    list->setViewMode(QListWidget::IconMode);
    list->setIconSize(QSize(96, 96));
    list->setFlow(QListView::LeftToRight);
    list->setResizeMode(QListView::Adjust);
    list->setSpacing(0);
    list->setUniformItemSizes(false);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

QPixmap makeSampleThumb(int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.fillRect(4, 2, w - 8, h - 4, QColor(180, 120, 90));
    return pix;
}

} // namespace

class ComfyHistoryListLayoutRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHistoryThumbnailItemVisibleWithPixmapWidthHint();
    void testHistoryThumbnailItemNotFullViewportWidth();
    void testHistoryThumbnailItemHiddenWithOversizedWidthHint();
    void testSyncHistoryListItemWidthsRepairsOversizedThumbHints();
    void testAdjacentHistoryThumbsHaveHorizontalGap();
};

void ComfyHistoryListLayoutRegressionTest::testHistoryThumbnailItemVisibleWithPixmapWidthHint()
{
    ComfyHistoryListWidget list;
    configureHistoryListLikeDock(&list);
    list.resize(280, 420);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    QListWidgetItem *header = new QListWidgetItem(QStringLiteral("04:33 - best quality"));
    header->setFlags(Qt::NoItemFlags);
    header->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 1);
    header->setSizeHint(ComfyHistoryInternal::historyHeaderItemSizeHint(&list, 20));

    const QPixmap thumbPix = makeSampleThumb(72, 36);
    QListWidgetItem *thumb = new QListWidgetItem();
    thumb->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 0);
    thumb->setIcon(QIcon(thumbPix));
    thumb->setSizeHint(ComfyHistoryInternal::historyThumbnailItemSizeHint(&list, thumbPix.size()));

    list.addItem(header);
    list.addItem(thumb);
    QApplication::processEvents();

    const QRect headerRect = list.visualItemRect(header);
    const QRect thumbRect = list.visualItemRect(thumb);
    QVERIFY2(headerRect.isValid(), "header should have layout rect");
    QVERIFY2(thumbRect.isValid() && thumbRect.height() >= 16,
             qPrintable(QStringLiteral("thumb row height=%1").arg(thumbRect.height())));
    QVERIFY2(thumbRect.left() >= 0,
             qPrintable(QStringLiteral("thumb off-screen left: x=%1").arg(thumbRect.left())));
    QVERIFY2(thumbRect.right() <= list.viewport()->width() + 1,
             qPrintable(QStringLiteral("thumb off-screen right: right=%1 viewport=%2")
                            .arg(thumbRect.right())
                            .arg(list.viewport()->width())));
    QVERIFY2(thumbRect.top() >= headerRect.bottom() - 2,
             qPrintable(QStringLiteral("thumb should follow header: headerBottom=%1 thumbTop=%2")
                            .arg(headerRect.bottom())
                            .arg(thumbRect.top())));
    QCOMPARE(thumb->sizeHint().width(),
             thumbPix.width() + ComfyHistoryInternal::historyThumbnailHorizontalCellPadding());
}

void ComfyHistoryListLayoutRegressionTest::testHistoryThumbnailItemNotFullViewportWidth()
{
    ComfyHistoryListWidget list;
    configureHistoryListLikeDock(&list);
    list.resize(320, 420);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    const int viewportW = list.viewport()->width();
    QVERIFY2(viewportW >= 200,
             qPrintable(QStringLiteral("test needs wide viewport, got %1").arg(viewportW)));

    const QPixmap thumbPix = makeSampleThumb(80, 48);
  QListWidgetItem *thumb = new QListWidgetItem();
    thumb->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 0);
    thumb->setIcon(QIcon(thumbPix));
    // Regression: viewport-width hint made Apply overlay span full docker width.
    thumb->setSizeHint(QSize(viewportW, thumbPix.height() + 2));
    list.addItem(thumb);
    QApplication::processEvents();

    const QRect fullWidthRect = list.visualItemRect(thumb);
    QVERIFY2(fullWidthRect.width() >= viewportW - 2,
             qPrintable(QStringLiteral("precondition: viewport-width hint should be wide: w=%1")
                            .arg(fullWidthRect.width())));

    thumb->setSizeHint(ComfyHistoryInternal::historyThumbnailItemSizeHint(&list, thumbPix.size()));
    list.doItemsLayout();
    QApplication::processEvents();

    const QRect compactRect = list.visualItemRect(thumb);
    const int pad = ComfyHistoryInternal::historyThumbnailHorizontalCellPadding();
    QVERIFY2(compactRect.width() <= thumbPix.width() + pad + 4,
             qPrintable(QStringLiteral("thumb row should match pixmap width, not docker: w=%1 pixmap=%2 viewport=%3")
                            .arg(compactRect.width())
                            .arg(thumbPix.width())
                            .arg(viewportW)));
    QVERIFY2(compactRect.width() < viewportW / 2,
             qPrintable(QStringLiteral("thumb row must not span docker width: w=%1 viewport=%2")
                            .arg(compactRect.width())
                            .arg(viewportW)));
}

void ComfyHistoryListLayoutRegressionTest::testHistoryThumbnailItemHiddenWithOversizedWidthHint()
{
    ComfyHistoryListWidget list;
    configureHistoryListLikeDock(&list);
    list.resize(280, 420);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    QListWidgetItem *header = new QListWidgetItem(QStringLiteral("header"));
    header->setFlags(Qt::NoItemFlags);
    header->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 1);
    header->setSizeHint(QSize(9999, 20));

    const QPixmap thumbPix = makeSampleThumb(72, 36);
    QListWidgetItem *thumb = new QListWidgetItem();
    thumb->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 0);
    thumb->setIcon(QIcon(thumbPix));
    thumb->setSizeHint(QSize(9999, thumbPix.height() + 2));

    list.addItem(header);
    list.addItem(thumb);
    QApplication::processEvents();

    const QRect thumbRect = list.visualItemRect(thumb);
    QVERIFY2(thumbRect.right() > list.viewport()->width(),
             qPrintable(QStringLiteral("oversized width hint should push thumb past viewport: right=%1 viewport=%2")
                            .arg(thumbRect.right())
                            .arg(list.viewport()->width())));
}

void ComfyHistoryListLayoutRegressionTest::testSyncHistoryListItemWidthsRepairsOversizedThumbHints()
{
    ComfyHistoryListWidget list;
    configureHistoryListLikeDock(&list);
    list.resize(280, 420);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    const QPixmap thumbPix = makeSampleThumb(72, 36);
    QListWidgetItem *thumb = new QListWidgetItem();
    thumb->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 0);
    thumb->setIcon(QIcon(thumbPix));
    thumb->setSizeHint(QSize(9999, thumbPix.height() + 2));
    list.addItem(thumb);
    QApplication::processEvents();

    ComfyHistoryInternal::syncHistoryListItemWidths(&list);
    QApplication::processEvents();

    const QRect thumbRect = list.visualItemRect(thumb);
    QVERIFY2(thumbRect.isValid() && thumbRect.height() >= 16,
             qPrintable(QStringLiteral("repaired thumb height=%1").arg(thumbRect.height())));
    QVERIFY2(thumbRect.right() <= list.viewport()->width() + 1,
             qPrintable(QStringLiteral("repaired thumb right=%1 viewport=%2")
                            .arg(thumbRect.right())
                            .arg(list.viewport()->width())));
    QCOMPARE(thumb->sizeHint().width(),
             thumbPix.width() + ComfyHistoryInternal::historyThumbnailHorizontalCellPadding());
    QVERIFY(thumbRect.width() < list.viewport()->width() / 2);
}

void ComfyHistoryListLayoutRegressionTest::testAdjacentHistoryThumbsHaveHorizontalGap()
{
    ComfyHistoryListWidget list;
    configureHistoryListLikeDock(&list);
    list.resize(400, 200);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));

    const QPixmap thumbPix = makeSampleThumb(72, 48);
    QListWidgetItem *a = new QListWidgetItem();
    a->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 0);
    a->setIcon(QIcon(thumbPix));
    a->setSizeHint(ComfyHistoryInternal::historyThumbnailItemSizeHint(&list, thumbPix.size()));

    QListWidgetItem *b = new QListWidgetItem();
    b->setData(ComfyHistoryInternal::HistoryItemIsHeaderRole, 0);
    b->setIcon(QIcon(thumbPix));
    b->setSizeHint(ComfyHistoryInternal::historyThumbnailItemSizeHint(&list, thumbPix.size()));

    list.addItem(a);
    list.addItem(b);
    QApplication::processEvents();

    const QRect rectA = list.visualItemRect(a);
    const QRect rectB = list.visualItemRect(b);
    QVERIFY2(rectA.isValid() && rectB.isValid(), "both thumbs need layout rects");
    QVERIFY2(rectA.top() == rectB.top(),
             qPrintable(QStringLiteral("thumbs should share a row: topA=%1 topB=%2").arg(rectA.top()).arg(rectB.top())));

    const int pad = ComfyHistoryInternal::historyThumbnailHorizontalCellPadding();
    QVERIFY2(rectA.width() >= thumbPix.width() + pad,
             qPrintable(QStringLiteral("thumb cell should include horizontal padding: w=%1 pixmap=%2 pad=%3")
                            .arg(rectA.width())
                            .arg(thumbPix.width())
                            .arg(pad)));
    const int aPixRight = rectA.left() + (rectA.width() + thumbPix.width()) / 2;
    const int bPixLeft = rectB.left() + (rectB.width() - thumbPix.width()) / 2;
    const int gap = bPixLeft - aPixRight;
    QVERIFY2(gap >= pad - 1,
             qPrintable(QStringLiteral("expected horizontal gap between thumb images, got gap=%1").arg(gap)));
}

QTEST_MAIN(ComfyHistoryListLayoutRegressionTest)
#include "ComfyHistoryListLayoutRegressionTest.moc"
