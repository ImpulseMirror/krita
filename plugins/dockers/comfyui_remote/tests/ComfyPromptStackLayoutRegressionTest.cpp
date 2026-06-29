/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: flush positive/negative prompt stack height, width, line split.
 */

#include <simpletest.h>
#include <QTest>

#include <QApplication>
#include <QFontMetrics>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QVBoxLayout>

#include "ComfyPromptLayoutMetrics.h"
#include "ComfyPromptStackWidget.h"

class ComfyPromptStackLayoutRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHeightForLinesMatchesUpstreamFormula();
    void testGenerateSplitIsThreePlusOneLines();
    void testStackFrameHeightSumsEditors();
    void testStackExpandsHorizontally();
    void testPositiveEditorFixedHeightTracksLineCount();
    void testNegativeHiddenOmitsNegativeHeight();
};

void ComfyPromptStackLayoutRegressionTest::testHeightForLinesMatchesUpstreamFormula()
{
    QFont font;
    font.setPointSize(10);
    const QFontMetrics fm(font);
    const int twoLines = ComfyPromptLayoutMetrics::heightForLines(fm, 2);
    const int threeLines = ComfyPromptLayoutMetrics::heightForLines(fm, 3);
    QCOMPARE(twoLines, fm.lineSpacing() * 2 + ComfyPromptLayoutMetrics::kPromptHeightPadPx);
    QCOMPARE(threeLines, fm.lineSpacing() * 3 + ComfyPromptLayoutMetrics::kPromptHeightPadPx);
    QVERIFY(threeLines > twoLines);
}

void ComfyPromptStackLayoutRegressionTest::testGenerateSplitIsThreePlusOneLines()
{
    QCOMPARE(ComfyPromptLayoutMetrics::positiveLinesForGenerateWorkspace(true, 2),
             ComfyPromptLayoutMetrics::kGeneratePositiveLinesMinWithNegative);
    QCOMPARE(ComfyPromptLayoutMetrics::positiveLinesForGenerateWorkspace(true, 3), 3);
    QCOMPARE(ComfyPromptLayoutMetrics::positiveLinesForGenerateWorkspace(false, 2), 2);
}

void ComfyPromptStackLayoutRegressionTest::testStackFrameHeightSumsEditors()
{
    ComfyPromptStackWidget stack;
    stack.applyLayout(3, true, false);
    stack.resize(320, stack.layoutSizeHint().height());
    stack.show();
    QVERIFY(QTest::qWaitForWindowExposed(&stack));

    QPlainTextEdit *pos = stack.positiveEditor();
    QPlainTextEdit *neg = stack.negativeEditor();
    QVERIFY(pos && neg);
    QCOMPARE(pos->height(), neg->height() > 0 ? pos->minimumHeight() : pos->height());
    QCOMPARE(stack.height(), pos->height() + neg->height());
}

void ComfyPromptStackLayoutRegressionTest::testStackExpandsHorizontally()
{
    QWidget host;
    auto *lay = new QVBoxLayout(&host);
    lay->setContentsMargins(0, 0, 0, 0);
    ComfyPromptStackWidget stack(&host);
    lay->addWidget(&stack);
    host.resize(480, 200);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QApplication::processEvents();

    QVERIFY2(stack.width() >= host.width() - 24,
             qPrintable(QStringLiteral("stack should span host width: stack=%1 host=%2")
                            .arg(stack.width())
                            .arg(host.width())));
    QCOMPARE(stack.layoutSizeHint().width(), 0);
    QCOMPARE(stack.sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
}

void ComfyPromptStackLayoutRegressionTest::testPositiveEditorFixedHeightTracksLineCount()
{
    ComfyPromptStackWidget stack;
    stack.show();
    QVERIFY(QTest::qWaitForWindowExposed(&stack));
    const QFontMetrics fm(stack.positiveEditor()->font());

    stack.applyLayout(3, false, false);
    QCOMPARE(stack.positiveEditor()->height(), ComfyPromptLayoutMetrics::heightForLines(fm, 3));

    stack.applyLayout(2, false, false);
    QCOMPARE(stack.positiveEditor()->height(), ComfyPromptLayoutMetrics::heightForLines(fm, 2));
}

void ComfyPromptStackLayoutRegressionTest::testNegativeHiddenOmitsNegativeHeight()
{
    ComfyPromptStackWidget stack;
    stack.applyLayout(3, true, false);
    const int withNeg = stack.layoutSizeHint().height();
    stack.applyLayout(3, false, false);
    const int withoutNeg = stack.layoutSizeHint().height();
    QVERIFY(withNeg > withoutNeg);
}

QTEST_MAIN(ComfyPromptStackLayoutRegressionTest)
#include "ComfyPromptStackLayoutRegressionTest.moc"
