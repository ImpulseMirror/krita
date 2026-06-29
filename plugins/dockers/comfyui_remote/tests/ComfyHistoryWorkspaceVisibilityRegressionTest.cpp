/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: history thumbnails only on Generate workspace (upstream GenerationWidget).
 */

#include <simpletest.h>
#include <QTest>

#include <QApplication>
#include <QJsonObject>

#include "ComfyUIUtils.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"

class ComfyHistoryWorkspaceVisibilityRegressionTest : public QObject
{
    Q_OBJECT

private:
    static void exposeWorkspaceContent(ComfyUIRemoteDock &dock)
    {
        if (auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate())) {
            d->isConnected = true;
            if (d->mainStack && d->mainStack->count() > 1)
                d->mainStack->setCurrentIndex(1);
        }
    }

    static void switchWorkspace(ComfyUIRemoteDock &dock, int workspaceIndex)
    {
        auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
        QVERIFY(d->comboWorkspace);
        QVERIFY(workspaceIndex >= 0 && workspaceIndex < d->comboWorkspace->count());
        d->comboWorkspace->setCurrentIndex(workspaceIndex);
        QApplication::processEvents();
        QApplication::sendPostedEvents();
    }

    QJsonObject m_savedSettings;

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void testHistoryVisibleOnlyOnGenerateWorkspace_data();
    void testHistoryVisibleOnlyOnGenerateWorkspace();
    void testHistoryReturnsAfterGenerateRoundTrip();
};

void ComfyHistoryWorkspaceVisibilityRegressionTest::initTestCase()
{
    QJsonObject st = ComfyUIUtils::loadSettingsJson();
    m_savedSettings = st;
}

void ComfyHistoryWorkspaceVisibilityRegressionTest::cleanupTestCase()
{
    ComfyUIUtils::saveSettingsJson(m_savedSettings);
}

void ComfyHistoryWorkspaceVisibilityRegressionTest::testHistoryVisibleOnlyOnGenerateWorkspace_data()
{
    QTest::addColumn<int>("workspace");
    QTest::addColumn<bool>("historyVisible");

    QTest::newRow("generate") << 0 << true;
    QTest::newRow("upscale") << 1 << false;
    QTest::newRow("live") << 2 << false;
    QTest::newRow("animation") << 3 << false;
    QTest::newRow("graph") << 4 << false;
}

void ComfyHistoryWorkspaceVisibilityRegressionTest::testHistoryVisibleOnlyOnGenerateWorkspace()
{
    QFETCH(int, workspace);
    QFETCH(bool, historyVisible);

    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    exposeWorkspaceContent(dock);
    switchWorkspace(dock, workspace);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.historyGroup);
    QCOMPARE(access.historyGroup->isVisible(), historyVisible);
    QCOMPARE(access.historyGroup->height() > 0, historyVisible);
}

void ComfyHistoryWorkspaceVisibilityRegressionTest::testHistoryReturnsAfterGenerateRoundTrip()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    exposeWorkspaceContent(dock);

    switchWorkspace(dock, 1);
    QVERIFY(!dock.layoutTestAccess().historyGroup->isVisible());
    switchWorkspace(dock, 2);
    QVERIFY(!dock.layoutTestAccess().historyGroup->isVisible());
    switchWorkspace(dock, 0);
    QVERIFY(dock.layoutTestAccess().historyGroup->isVisible());
    QVERIFY2(dock.layoutTestAccess().historyGroup->height() >= 96,
             qPrintable(QStringLiteral("history should reclaim space on Generate: h=%1")
                            .arg(dock.layoutTestAccess().historyGroup->height())));
}

QTEST_MAIN(ComfyHistoryWorkspaceVisibilityRegressionTest)
#include "ComfyHistoryWorkspaceVisibilityRegressionTest.moc"
