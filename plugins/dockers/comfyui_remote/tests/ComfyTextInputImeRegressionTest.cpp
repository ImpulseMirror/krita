/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression: Android IME queries ancestor widgets for ImCursorPosition. Containers
 * that return 0 while the editor cursor is at end cause backwards typing.
 */

#include <simpletest.h>
#include <QTest>

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

#include "ComfyRegionPromptWidget.h"
#include "ComfyTextArea.h"
#include "ComfyPromptStackWidget.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionLink.h"

namespace {

bool isComboInternalLineEdit(const QLineEdit *lineEdit)
{
    for (const QWidget *p = lineEdit; p; p = p->parentWidget()) {
        if (qobject_cast<const QComboBox *>(p))
            return true;
    }
    return false;
}

int cursorPositionFor(QWidget *input)
{
    if (auto *editor = qobject_cast<QPlainTextEdit *>(input))
        return editor->inputMethodQuery(Qt::ImCursorPosition).toInt();
    if (auto *line = qobject_cast<QLineEdit *>(input))
        return line->inputMethodQuery(Qt::ImCursorPosition).toInt();
    return -1;
}

void focusTextInputWithCursor(QWidget *input, int cursorPos)
{
    QVERIFY(input);
    if (auto *editor = qobject_cast<QPlainTextEdit *>(input)) {
        editor->setPlainText(QStringLiteral("abcdef"));
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(cursorPos);
        editor->setTextCursor(cursor);
        editor->setFocus(Qt::OtherFocusReason);
    } else if (auto *line = qobject_cast<QLineEdit *>(input)) {
        line->setText(QStringLiteral("abcdef"));
        line->setCursorPosition(cursorPos);
        line->setFocus(Qt::OtherFocusReason);
    } else {
        QFAIL("unsupported text input widget");
    }
    QApplication::processEvents();
}

void auditAncestorImeCursorForwarding(QWidget *input, QWidget *stopAt, int cursorPos)
{
    QCOMPARE(cursorPositionFor(input), cursorPos);
    for (QWidget *ancestor = input->parentWidget(); ancestor && ancestor != stopAt;
         ancestor = ancestor->parentWidget()) {
        const QVariant reported = ancestor->inputMethodQuery(Qt::ImCursorPosition);
        if (!reported.isValid())
            continue;
        QVERIFY2(reported.toInt() == cursorPos,
                 qPrintable(QStringLiteral("%1 (%2) reported ImCursorPosition=%3 but editor is at %4")
                                .arg(ancestor->objectName(), ancestor->metaObject()->className())
                                .arg(reported.toInt())
                                .arg(cursorPos)));
    }
}

QList<QWidget *> collectAuditableTextInputs(QWidget *root)
{
    QList<QWidget *> inputs;
    const auto plainEdits = root->findChildren<QPlainTextEdit *>();
    for (QPlainTextEdit *editor : plainEdits)
        inputs.append(editor);
    const auto lineEdits = root->findChildren<QLineEdit *>();
    for (QLineEdit *line : lineEdits) {
        if (!isComboInternalLineEdit(line))
            inputs.append(line);
    }
    return inputs;
}

void exposeGenerateWorkspace(ComfyUIRemoteDock &dock)
{
    if (auto *d = static_cast<ComfyUIRemoteDock::Private *>(dock.testDockPrivate())) {
        d->isConnected = true;
        if (d->mainStack && d->mainStack->count() > 1)
            d->mainStack->setCurrentIndex(1);
        if (d->comboWorkspace)
            d->comboWorkspace->setCurrentIndex(0);
    }
    QApplication::processEvents();
}

} // namespace

class ComfyTextInputImeRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testForwardContainerInputMethodQueryMatchesFocusedEditor();
    void testSetPlainTextPreserveCursorKeepsCaret();
    void testTextInputContainerForwardsCursor();
    void testPromptStackForwardsCursorForBothEditors();
    void testRegionPromptForwardsCursorInRegionMode();
    void testDockTextInputsForwardImeCursor_data();
    void testDockTextInputsForwardImeCursor();
};

void ComfyTextInputImeRegressionTest::testForwardContainerInputMethodQueryMatchesFocusedEditor()
{
    ComfyTextInputContainer host;
    auto *editor = new ComfyTextArea(nullptr, &host);
    new QVBoxLayout(&host);
    qobject_cast<QVBoxLayout *>(host.layout())->addWidget(editor);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    editor->setPlainText(QStringLiteral("hello"));
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    editor->setFocus(Qt::OtherFocusReason);
    QApplication::processEvents();

    const int endPos = editor->inputMethodQuery(Qt::ImCursorPosition).toInt();
    QCOMPARE(endPos, QStringLiteral("hello").size());
    QCOMPARE(ComfyTextArea::forwardContainerInputMethodQuery(&host, Qt::ImCursorPosition).toInt(), endPos);
    QCOMPARE(host.inputMethodQuery(Qt::ImCursorPosition).toInt(), endPos);
}

void ComfyTextInputImeRegressionTest::testSetPlainTextPreserveCursorKeepsCaret()
{
    ComfyTextArea editor;
    editor.setPlainText(QStringLiteral("ab"));
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);

    ComfyTextArea::setPlainTextPreserveCursor(&editor, QStringLiteral("xy"));
    QCOMPARE(editor.toPlainText(), QStringLiteral("xy"));
    QCOMPARE(editor.textCursor().position(), 1);
}

void ComfyTextInputImeRegressionTest::testTextInputContainerForwardsCursor()
{
    ComfyTextInputContainer host;
    auto *editor = new ComfyTextArea(nullptr, &host);
    auto *layout = new QVBoxLayout(&host);
    layout->addWidget(editor);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    focusTextInputWithCursor(editor, 4);
    auditAncestorImeCursorForwarding(editor, nullptr, 4);
}

void ComfyTextInputImeRegressionTest::testPromptStackForwardsCursorForBothEditors()
{
    ComfyPromptStackWidget stack;
    stack.applyLayout(3, true, false);
    stack.show();
    QVERIFY(QTest::qWaitForWindowExposed(&stack));

    QPlainTextEdit *positive = stack.positiveEditor();
    QPlainTextEdit *negative = stack.negativeEditor();
    QVERIFY(positive && negative);

    focusTextInputWithCursor(positive, 3);
    auditAncestorImeCursorForwarding(positive, nullptr, 3);

    focusTextInputWithCursor(negative, 5);
    auditAncestorImeCursorForwarding(negative, nullptr, 5);
}

void ComfyTextInputImeRegressionTest::testRegionPromptForwardsCursorInRegionMode()
{
    ComfyRegionPromptWidget regionPrompt;
    QList<ComfyUIRemoteDock::Private::RegionEntry> regions;
    ComfyUIRemoteDock::Private::RegionEntry region;
    region.name = QStringLiteral("sky");
    region.prompt = QStringLiteral("clouds");
    regions.append(region);
    int activeIndex = 0;
    regionPrompt.bind(&regions, &activeIndex);
    regionPrompt.setPromptHeaderMode(2);
    regionPrompt.setShowNegativePrompt(false);
    regionPrompt.resize(360, 180);
    regionPrompt.show();
    QVERIFY(QTest::qWaitForWindowExposed(&regionPrompt));

    QPlainTextEdit *editor = regionPrompt.positivePromptEditor();
    QVERIFY(editor);
    focusTextInputWithCursor(editor, 4);
    auditAncestorImeCursorForwarding(editor, nullptr, 4);
}

void ComfyTextInputImeRegressionTest::testDockTextInputsForwardImeCursor_data()
{
    QTest::addColumn<int>("cursorPos");
    QTest::newRow("start") << 0;
    QTest::newRow("middle") << 3;
    QTest::newRow("end") << 6;
}

void ComfyTextInputImeRegressionTest::testDockTextInputsForwardImeCursor()
{
    QFETCH(int, cursorPos);

    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.resize(420, 720);
    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    dock.setEnabled(true);
    exposeGenerateWorkspace(dock);

    const QList<QWidget *> inputs = collectAuditableTextInputs(&dock);
    QVERIFY2(!inputs.isEmpty(), "dock should expose prompt/line text inputs to audit");

    for (QWidget *input : inputs) {
        if (!input->isVisible() || !input->isEnabled())
            continue;
        focusTextInputWithCursor(input, cursorPos);
        auditAncestorImeCursorForwarding(input, &dock, cursorPos);
    }
}

QTEST_MAIN(ComfyTextInputImeRegressionTest)
#include "ComfyTextInputImeRegressionTest.moc"
