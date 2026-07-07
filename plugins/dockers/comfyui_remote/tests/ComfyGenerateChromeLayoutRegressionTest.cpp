/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: compact Generate chrome height and history flush under progress bar.
 */

#include <simpletest.h>
#include <QTest>

#include <QApplication>
#include <QJsonObject>
#include <QLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "ComfyUIUtils.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfyPromptLayoutMetrics.h"

class ComfyGenerateChromeLayoutRegressionTest : public QObject
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

    static void settleLayout(ComfyUIRemoteDock &dock)
    {
        exposeWorkspaceContent(dock);
        if (QWidget *page = dock.layoutTestAccess().contentPage) {
            page->adjustSize();
            if (QLayout *lay = page->layout())
                lay->activate();
        }
        QApplication::processEvents();
        QApplication::sendPostedEvents();
    }

    static void settleGenerateLayout(ComfyUIRemoteDock &dock)
    {
        if (auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate())) {
            if (d->comboWorkspace)
                d->comboWorkspace->setCurrentIndex(0);
        }
        settleLayout(dock);
    }

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void testHistoryStartsBelowGenerateChrome();
    void testHistoryGroupHasVisibleHeight();
    void testGenerateChromeHeightMatchesEssential();
    void testProgressBarIdleIsFull();
    void testProgressBarFixedHeightIsThin();
    void testHistoryPanelVisibleOnGenerateWorkspace();
    void testUpscaleTopInsetMatchesGenerate();
    void testContentPageVBoxAlignsTop();
    void testUpscaleGenGroupNotCenteredInTallDocker();
    void testUpscaleHistoryHiddenGenFlushedToPageTop();
    void testUpscaleRegionPromptDetachedFromGenContent();
    void testLiveParamsRowHasVisibleHeight();
    void testLiveRegionPromptHostedAndVisible();
    void testLivePreviewOnContentPage();
    void testLivePromptSurvivesUpscaleRoundTrip();
    void testLivePositiveLineCountMatchesGenerate();
    void testGenerateNegativePromptFitsInGenContent();
    void testAllCompactWorkspacesShareTopInset_data();
    void testAllCompactWorkspacesShareTopInset();

private:
    QJsonObject m_savedSettings;
};

void ComfyGenerateChromeLayoutRegressionTest::initTestCase()
{
    m_savedSettings = ComfyUIUtils::loadSettingsJson();
    QJsonObject st = m_savedSettings;
    st.insert(QStringLiteral("show_negative_prompt"), true);
    st.insert(QStringLiteral("prompt_line_count"), 3);
    ComfyUIUtils::saveSettingsJson(st);
}

void ComfyGenerateChromeLayoutRegressionTest::cleanupTestCase()
{
    ComfyUIUtils::saveSettingsJson(m_savedSettings);
}

void ComfyGenerateChromeLayoutRegressionTest::testHistoryStartsBelowGenerateChrome()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.contentPage);
    QVERIFY(access.generateChrome);
    QVERIFY(access.historyGroup);
    QVERIFY(access.progressBar);
    QVERIFY2(!access.generateScroll,
             "generate chrome should not be wrapped in a scroll area on contentPage");

    const QWidget *page = access.contentPage;
    const int chromeBottom = access.generateChrome->mapTo(page, QPoint(0, access.generateChrome->height())).y();
    const int histTop = access.historyGroup->mapTo(page, QPoint(0, 0)).y();
    const int gap = histTop - chromeBottom;
    QVERIFY2(gap >= 0 && gap <= 5,
             qPrintable(QStringLiteral("history should sit flush under generate chrome: gap=%1 chromeBottom=%2 histTop=%3")
                            .arg(gap)
                            .arg(chromeBottom)
                            .arg(histTop)));
}

void ComfyGenerateChromeLayoutRegressionTest::testHistoryGroupHasVisibleHeight()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.historyGroup);
    QVERIFY2(access.historyGroup->height() >= 96,
             qPrintable(QStringLiteral("history group needs vertical space: h=%1")
                            .arg(access.historyGroup->height())));
}

void ComfyGenerateChromeLayoutRegressionTest::testGenerateChromeHeightMatchesEssential()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.generateChrome);
    QVERIFY(access.contentPage);
    const int contentWidth = access.contentPage->width();
    const int essentialH =
        ComfyUiLayoutDiagnostics::measureEssentialGenerateChromeHeight(dock.testDockPrivate(), contentWidth);
    QVERIFY(essentialH > 0);
    QVERIFY2(access.generateChrome->height() <= essentialH + 6,
             qPrintable(QStringLiteral("chrome taller than essential: chrome=%1 essential=%2")
                            .arg(access.generateChrome->height())
                            .arg(essentialH)));
}

void ComfyGenerateChromeLayoutRegressionTest::testProgressBarIdleIsFull()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.progressBar);
    QCOMPARE(access.progressBar->value(), access.progressBar->maximum());
}

void ComfyGenerateChromeLayoutRegressionTest::testProgressBarFixedHeightIsThin()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.progressBar);
    QVERIFY2(access.progressBar->height() <= 4,
             qPrintable(QStringLiteral("progress bar too tall: h=%1").arg(access.progressBar->height())));
}

void ComfyGenerateChromeLayoutRegressionTest::testHistoryPanelVisibleOnGenerateWorkspace()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.historyGroup);
    QVERIFY(access.historyGroup->isVisible());
    QVERIFY2(access.historyGroup->height() >= 96,
             qPrintable(QStringLiteral("history panel needs space on Generate: h=%1")
                            .arg(access.historyGroup->height())));
}

void ComfyGenerateChromeLayoutRegressionTest::testUpscaleTopInsetMatchesGenerate()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(0);
    QApplication::processEvents();
    settleLayout(dock);
    const int generateInset = ComfyUiLayoutDiagnostics::measureWorkspaceTopChromeInset(dock.testDockPrivate());
    const int generateGenTop = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    const int generateDockerTop =
        ComfyUiLayoutDiagnostics::measurePrimaryChromeTopOnDocker(dock.testDockPrivate(), &dock);

    d->comboWorkspace->setCurrentIndex(1);
    QApplication::processEvents();
    settleLayout(dock);
    const int upscaleInset = ComfyUiLayoutDiagnostics::measureWorkspaceTopChromeInset(dock.testDockPrivate());
    const int upscaleGenTop = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    const int upscaleDockerTop =
        ComfyUiLayoutDiagnostics::measurePrimaryChromeTopOnDocker(dock.testDockPrivate(), &dock);
    const int regionH = ComfyUiLayoutDiagnostics::measureRegionPromptHeightOnUpscale(dock.testDockPrivate());

    QVERIFY(generateInset >= 0);
    QVERIFY(upscaleInset >= 0);
    QVERIFY2(qAbs(generateInset - upscaleInset) <= 4,
             qPrintable(QStringLiteral("upscale top inset should match generate: gen=%1 upscale=%2")
                            .arg(generateInset)
                            .arg(upscaleInset)));
    QVERIFY2(generateGenTop >= 0 && upscaleGenTop >= 0,
             qPrintable(QStringLiteral("genGroup top on contentPage invalid: gen=%1 upscale=%2")
                            .arg(generateGenTop)
                            .arg(upscaleGenTop)));
    QVERIFY2(qAbs(generateGenTop - upscaleGenTop) <= 4,
             qPrintable(QStringLiteral("genGroup Y on contentPage drift: gen=%1 upscale=%2")
                            .arg(generateGenTop)
                            .arg(upscaleGenTop)));
    QVERIFY2(generateDockerTop >= 0 && upscaleDockerTop >= 0,
             qPrintable(QStringLiteral("primary chrome docker Y invalid: gen=%1 upscale=%2")
                            .arg(generateDockerTop)
                            .arg(upscaleDockerTop)));
    QVERIFY2(qAbs(generateDockerTop - upscaleDockerTop) <= 8,
             qPrintable(QStringLiteral("primary chrome docker Y drift: gen=%1 upscale=%2")
                            .arg(generateDockerTop)
                            .arg(upscaleDockerTop)));
    QVERIFY2(regionH <= 4,
             qPrintable(QStringLiteral("regionPrompt should collapse on upscale: h=%1").arg(regionH)));
    if (d->generate.genContentContainer) {
        if (auto *genLay = qobject_cast<QVBoxLayout *>(d->generate.genContentContainer->layout())) {
            QVERIFY2(genLay->indexOf(static_cast<QWidget *>(d->generate.regionPromptWidget)) < 0,
                     qPrintable(QStringLiteral("regionPrompt must be removed from genContent layout on upscale")));
        }
    }
}

void ComfyGenerateChromeLayoutRegressionTest::testContentPageVBoxAlignsTop()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.contentPage);
    auto *box = qobject_cast<QVBoxLayout *>(access.contentPage->layout());
    QVERIFY2(box != nullptr, "contentPage must use QVBoxLayout");
    QVERIFY2((box->alignment() & Qt::AlignTop) != 0,
             qPrintable(QStringLiteral("contentPage layout must align children to top: alignment=%1")
                            .arg(static_cast<int>(box->alignment()))));
}

void ComfyGenerateChromeLayoutRegressionTest::testUpscaleGenGroupNotCenteredInTallDocker()
{
    // Regression: when history is hidden on Upscale, a lone genGroup was vertically
    // centered in the tall contentPage (~216px dead space above the model dropdown).
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 943);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(1);
    QApplication::processEvents();
    settleLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.contentPage);
    QVERIFY(access.generateChrome);

    const int genTop = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    const int pageH = access.contentPage->height();
    const int chromeH = access.generateChrome->height();
    QVERIFY(genTop >= 0);
    QVERIFY2(pageH > chromeH + 100,
             qPrintable(QStringLiteral("tall docker required to catch centering: pageH=%1 chromeH=%2")
                            .arg(pageH)
                            .arg(chromeH)));
    QVERIFY2(genTop <= 4,
             qPrintable(QStringLiteral("upscale genGroup must sit flush with contentPage top: genTop=%1 pageH=%2 chromeH=%3")
                            .arg(genTop)
                            .arg(pageH)
                            .arg(chromeH)));
    const int centeredTop = (pageH - chromeH) / 2;
    QVERIFY2(genTop < centeredTop / 2,
             qPrintable(QStringLiteral("genGroup looks vertically centered: genTop=%1 centeredTop=%2")
                            .arg(genTop)
                            .arg(centeredTop)));
}

void ComfyGenerateChromeLayoutRegressionTest::testUpscaleHistoryHiddenGenFlushedToPageTop()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(1);
    QApplication::processEvents();
    settleLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    QVERIFY(access.historyGroup);
    QVERIFY2(!access.historyGroup->isVisible(),
             "history panel must be hidden on Upscale workspace");
    QVERIFY2(access.historyGroup->height() <= 4,
             qPrintable(QStringLiteral("collapsed history must not reserve height on Upscale: h=%1")
                            .arg(access.historyGroup->height())));

    const int genTop = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    QVERIFY2(genTop <= 4,
             qPrintable(QStringLiteral("genGroup must stay at contentPage top when history hidden: genTop=%1")
                            .arg(genTop)));

    d->comboWorkspace->setCurrentIndex(0);
    QApplication::processEvents();
    settleLayout(dock);
    QVERIFY(access.historyGroup->isVisible());
    const int genTopAfter = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    QVERIFY2(genTopAfter <= 4,
             qPrintable(QStringLiteral("genGroup must return to contentPage top on Generate: genTop=%1")
                            .arg(genTopAfter)));
}

void ComfyGenerateChromeLayoutRegressionTest::testUpscaleRegionPromptDetachedFromGenContent()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    QVERIFY(d->generate.genContentContainer);
    QVERIFY(d->generate.regionPromptWidget);

    auto *genLay = qobject_cast<QVBoxLayout *>(d->generate.genContentContainer->layout());
    QVERIFY(genLay);
    QVERIFY2(genLay->indexOf(static_cast<QWidget *>(d->generate.regionPromptWidget)) >= 0,
             "regionPrompt should be in genContent layout on Generate");

    d->comboWorkspace->setCurrentIndex(1);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY2(genLay->indexOf(static_cast<QWidget *>(d->generate.regionPromptWidget)) < 0,
             "regionPrompt must be removed from genContent layout on Upscale");
    QVERIFY2(ComfyUiLayoutDiagnostics::measureRegionPromptHeightOnUpscale(dock.testDockPrivate()) <= 4,
             "regionPrompt must not reserve height on Upscale");
    QVERIFY2(!d->generate.regionPromptWidget->isVisible(),
             "regionPrompt must be hidden on Upscale");
}

void ComfyGenerateChromeLayoutRegressionTest::testLiveParamsRowHasVisibleHeight()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(2);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY(d->live.liveParamsRowWidget);
    QVERIFY(d->live.liveParamsRowWidget->isVisible());
    QVERIFY(d->generate.spinStrength);
    QVERIFY2(d->live.liveParamsRowWidget->height() >= d->generate.spinStrength->height(),
             qPrintable(QStringLiteral("live params row clipped strength: rowH=%1 spinH=%2")
                            .arg(d->live.liveParamsRowWidget->height())
                            .arg(d->generate.spinStrength->height())));
    QVERIFY2(d->live.liveParamsRowWidget->height() >= 24,
             qPrintable(QStringLiteral("live params row too short: h=%1")
                            .arg(d->live.liveParamsRowWidget->height())));
}

void ComfyGenerateChromeLayoutRegressionTest::testLiveRegionPromptHostedAndVisible()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(2);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY(d->generate.regionPromptWidget);
    QVERIFY(d->live.livePromptHostWidget);
    QVERIFY(d->live.livePromptRowWidget);
    QVERIFY2(d->generate.regionPromptWidget->parentWidget() == d->live.livePromptHostWidget,
             "regionPrompt must live inside livePromptHostWidget on Live");
    QVERIFY(d->generate.regionPromptWidget->isVisible());
    QVERIFY(d->live.livePromptRowWidget->isVisible());
    QVERIFY2(d->generate.regionPromptWidget->height() >= 48,
             qPrintable(QStringLiteral("live prompt field too short: h=%1")
                            .arg(d->generate.regionPromptWidget->height())));
    if (d->generate.genContentContainer) {
        QVERIFY2(d->generate.genContentContainer->height()
                         >= d->live.liveParamsRowWidget->height() + d->generate.regionPromptWidget->height() - 4,
                 qPrintable(QStringLiteral("genContent max height clips live prompt: genContentH=%1 paramsH=%2 promptH=%3")
                                .arg(d->generate.genContentContainer->height())
                                .arg(d->live.liveParamsRowWidget->height())
                                .arg(d->generate.regionPromptWidget->height())));
    }
}

void ComfyGenerateChromeLayoutRegressionTest::testLivePreviewOnContentPage()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    const ComfyUIRemoteDock::LayoutTestAccess access = dock.layoutTestAccess();
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(2);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY(d->live.livePreviewGroupBox);
    QVERIFY(access.contentPage);
    QVERIFY2(d->live.livePreviewGroupBox->parentWidget() == d->generate.genContentContainer,
             "live preview must sit inside gen content, directly below prompt");
    QVERIFY(d->live.livePreviewGroupBox->isVisible());
    QVERIFY(d->live.livePreviewArea);
    QVERIFY(d->live.livePreviewRowWidget);
    QVERIFY(d->live.liveSpinner);
    QVERIFY2(d->live.livePreviewArea->parentWidget() == d->live.livePreviewRowWidget,
             "live preview image must sit left of the progress indicator");
    QVERIFY2(d->live.liveSpinner->parentWidget() == d->live.livePreviewRowWidget,
             "live progress indicator must sit to the right of preview");
    QVERIFY2(d->live.livePreviewArea->minimumHeight() >= 128,
             qPrintable(QStringLiteral("live preview area min height too small: %1")
                            .arg(d->live.livePreviewArea->minimumHeight())));

    d->comboWorkspace->setCurrentIndex(0);
    QApplication::processEvents();
    settleLayout(dock);
    QVERIFY2(!d->live.livePreviewGroupBox->isVisible(),
             "live preview must hide when leaving Live workspace");
}

void ComfyGenerateChromeLayoutRegressionTest::testLivePromptSurvivesUpscaleRoundTrip()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(1);
    QApplication::processEvents();
    settleLayout(dock);
    d->comboWorkspace->setCurrentIndex(2);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY(d->generate.regionPromptWidget);
    QVERIFY(d->live.livePromptRowWidget);
    QVERIFY2(d->generate.regionPromptWidget->parentWidget() == d->live.livePromptHostWidget,
             "prompt must be hosted on Live after Upscale→Live");
    QVERIFY2(d->live.livePromptRowWidget->height() >= 48,
             qPrintable(QStringLiteral("prompt row collapsed after Upscale→Live: h=%1")
                            .arg(d->live.livePromptRowWidget->height())));
    QVERIFY2(d->generate.regionPromptWidget->height() >= 48,
             qPrintable(QStringLiteral("regionPrompt collapsed after Upscale→Live: h=%1")
                            .arg(d->generate.regionPromptWidget->height())));
    QVERIFY2(d->live.livePromptRowWidget->sizePolicy().horizontalPolicy() != QSizePolicy::Ignored,
             "live prompt row must not keep Ignored size policy after workspace collapse");
}

void ComfyGenerateChromeLayoutRegressionTest::testLivePositiveLineCountMatchesGenerate()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(0);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY(d->generate.regionPromptWidget);
    QPlainTextEdit *genPos = d->generate.regionPromptWidget->positivePromptEditor();
    QVERIFY(genPos);
    const int generatePosH = genPos->height();

    d->comboWorkspace->setCurrentIndex(2);
    QApplication::processEvents();
    settleLayout(dock);

    QPlainTextEdit *livePos = d->generate.regionPromptWidget->positivePromptEditor();
    QVERIFY(livePos);
    QVERIFY2(qAbs(livePos->height() - generatePosH) <= 2,
             qPrintable(QStringLiteral("Live positive prompt height should match Generate: live=%1 gen=%2")
                            .arg(livePos->height())
                            .arg(generatePosH)));

    const QFontMetrics fm(genPos->fontMetrics());
    const int expectedH = ComfyPromptLayoutMetrics::heightForLines(fm, 3);
    QVERIFY2(qAbs(livePos->height() - expectedH) <= 2,
             qPrintable(QStringLiteral("Live positive prompt should be 3 lines: h=%1 expected=%2")
                            .arg(livePos->height())
                            .arg(expectedH)));
}

void ComfyGenerateChromeLayoutRegressionTest::testGenerateNegativePromptFitsInGenContent()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    QVERIFY(d->generate.regionPromptWidget);
    QVERIFY(d->generate.genContentContainer);

    auto *rp = d->generate.regionPromptWidget;
    QPlainTextEdit *pos = rp->positivePromptEditor();
    QPlainTextEdit *neg = rp->negativePromptEditor();
    QVERIFY(pos);
    QVERIFY(neg);
    QVERIFY(neg->isVisible());

    const QFontMetrics fm(pos->fontMetrics());
    const int expectedPosH = ComfyPromptLayoutMetrics::heightForLines(fm, 3);
    const int expectedNegH = ComfyPromptLayoutMetrics::heightForLines(fm, ComfyPromptLayoutMetrics::kNegativeLineCount);
    QVERIFY2(qAbs(pos->height() - expectedPosH) <= 2,
             qPrintable(QStringLiteral("Generate positive should be 3 lines: h=%1 expected=%2")
                            .arg(pos->height())
                            .arg(expectedPosH)));
    QVERIFY2(qAbs(neg->height() - expectedNegH) <= 2,
             qPrintable(QStringLiteral("Generate negative should be 1 line: h=%1 expected=%2")
                            .arg(neg->height())
                            .arg(expectedNegH)));
    QVERIFY2(rp->height() >= pos->height() + neg->height() - 4,
             qPrintable(QStringLiteral("regionPrompt too short for pos+neg: rp=%1 pos=%2 neg=%3")
                            .arg(rp->height())
                            .arg(pos->height())
                            .arg(neg->height())));

    const int genContentMax = d->generate.genContentContainer->maximumHeight();
    QVERIFY2(genContentMax >= rp->height() - 4,
             qPrintable(QStringLiteral("genContent max clips prompt: max=%1 prompt=%2")
                            .arg(genContentMax)
                            .arg(rp->height())));

    d->comboWorkspace->setCurrentIndex(2);
    QApplication::processEvents();
    settleLayout(dock);
    d->comboWorkspace->setCurrentIndex(0);
    QApplication::processEvents();
    settleLayout(dock);

    QVERIFY2(neg->isVisible(), "negative prompt hidden after Live→Generate round trip");
    QVERIFY2(rp->height() >= pos->height() + neg->height() - 4,
             qPrintable(QStringLiteral("regionPrompt clipped after Live→Generate: rp=%1 pos=%2 neg=%3")
                            .arg(rp->height())
                            .arg(pos->height())
                            .arg(neg->height())));
    QVERIFY2(d->generate.genContentContainer->maximumHeight() >= rp->height() - 4,
             qPrintable(QStringLiteral("genContent max clips prompt after Live→Generate: max=%1 prompt=%2")
                            .arg(d->generate.genContentContainer->maximumHeight())
                            .arg(rp->height())));
}

void ComfyGenerateChromeLayoutRegressionTest::testAllCompactWorkspacesShareTopInset_data()
{
    QTest::addColumn<int>("workspace");
    QTest::newRow("generate") << 0;
    QTest::newRow("upscale") << 1;
    QTest::newRow("live") << 2;
}

void ComfyGenerateChromeLayoutRegressionTest::testAllCompactWorkspacesShareTopInset()
{
    QFETCH(int, workspace);

    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    settleGenerateLayout(dock);

    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate());
    d->comboWorkspace->setCurrentIndex(0);
    QApplication::processEvents();
    settleLayout(dock);
    const int referenceInset = ComfyUiLayoutDiagnostics::measureWorkspaceTopChromeInset(dock.testDockPrivate());
    const int referenceGenTop = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    const int referenceDockerTop =
        ComfyUiLayoutDiagnostics::measurePrimaryChromeTopOnDocker(dock.testDockPrivate(), &dock);

    d->comboWorkspace->setCurrentIndex(workspace);
    QApplication::processEvents();
    settleLayout(dock);
    const int inset = ComfyUiLayoutDiagnostics::measureWorkspaceTopChromeInset(dock.testDockPrivate());
    const int genTop = ComfyUiLayoutDiagnostics::measureGenGroupTopOnContentPage(dock.testDockPrivate());
    const int dockerTop = ComfyUiLayoutDiagnostics::measurePrimaryChromeTopOnDocker(dock.testDockPrivate(), &dock);

    QVERIFY(referenceInset >= 0);
    QVERIFY(inset >= 0);
    QVERIFY2(qAbs(referenceInset - inset) <= 4,
             qPrintable(QStringLiteral("workspace %1 top inset drift: ref=%2 got=%3")
                            .arg(workspace)
                            .arg(referenceInset)
                            .arg(inset)));
    QVERIFY2(referenceGenTop >= 0 && genTop >= 0,
             qPrintable(QStringLiteral("workspace %1 genGroup top invalid: ref=%2 got=%3")
                            .arg(workspace)
                            .arg(referenceGenTop)
                            .arg(genTop)));
    QVERIFY2(qAbs(referenceGenTop - genTop) <= 4,
             qPrintable(QStringLiteral("workspace %1 genGroup top drift: ref=%2 got=%3")
                            .arg(workspace)
                            .arg(referenceGenTop)
                            .arg(genTop)));
    QVERIFY2(referenceDockerTop >= 0 && dockerTop >= 0,
             qPrintable(QStringLiteral("workspace %1 docker chrome top invalid: ref=%2 got=%3")
                            .arg(workspace)
                            .arg(referenceDockerTop)
                            .arg(dockerTop)));
    const int dockerTolerance = (workspace == 2) ? 16 : 8;
    QVERIFY2(qAbs(referenceDockerTop - dockerTop) <= dockerTolerance,
             qPrintable(QStringLiteral("workspace %1 docker chrome top drift: ref=%2 got=%3")
                            .arg(workspace)
                            .arg(referenceDockerTop)
                            .arg(dockerTop)));
}

QTEST_MAIN(ComfyGenerateChromeLayoutRegressionTest)
#include "ComfyGenerateChromeLayoutRegressionTest.moc"
