/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyTextArea.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfyUiStyle.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QFrame>
#include <QLineEdit>
#include <QTextCursor>
#include <QVBoxLayout>

ComfyTextInputContainer::ComfyTextInputContainer(QWidget *parent)
    : QWidget(parent)
{
}

QVariant ComfyTextInputContainer::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return ComfyTextArea::forwardContainerInputMethodQuery(this, query);
}

ComfyTextInputFrame::ComfyTextInputFrame(QWidget *parent)
    : QFrame(parent)
{
}

QVariant ComfyTextInputFrame::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return ComfyTextArea::forwardContainerInputMethodQuery(this, query);
}

QVariant ComfyTextArea::forwardContainerInputMethodQuery(const QWidget *container, Qt::InputMethodQuery query)
{
    if (!container)
        return QVariant();

    if (QWidget *fw = QApplication::focusWidget()) {
        for (QWidget *w = fw; w; w = w->parentWidget()) {
            if (auto *editor = qobject_cast<QPlainTextEdit *>(w)) {
                if (editor == container || container->isAncestorOf(editor))
                    return editor->inputMethodQuery(query);
            } else if (auto *line = qobject_cast<QLineEdit *>(w)) {
                if (line == container || container->isAncestorOf(line))
                    return line->inputMethodQuery(query);
            }
            if (w == container)
                break;
        }
    }
    return const_cast<QWidget *>(container)->inputMethodQuery(query);
}

void ComfyTextArea::setPlainTextPreserveCursor(QPlainTextEdit *editor, const QString &text)
{
    if (!editor || editor->toPlainText() == text)
        return;
    const QTextCursor before = editor->textCursor();
    const int pos = before.position();
    const int anchor = before.anchor();
    editor->setPlainText(text);
    QTextCursor after = editor->textCursor();
    const int len = text.length();
    after.setPosition(qBound(0, qMin(pos, anchor), len));
    after.setPosition(qBound(0, qMax(pos, anchor), len), QTextCursor::KeepAnchor);
    editor->setTextCursor(after);
}

ComfyTextArea::ComfyTextArea(QCompleter *completer, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_completer(completer)
{
    setMinimumHeight(ComfyUiStyle::Spacing::promptMinHeight);
    ComfyUiStyle::applyPromptTextArea(this, false);
}

bool ComfyTextArea::focusNextPrevChild(bool next)
{
    if (!m_completer.isNull()) {
        QAbstractItemView *pop = m_completer->popup();
        if (pop && pop->isVisible())
            return false;
    }
    return QPlainTextEdit::focusNextPrevChild(next);
}

void ComfyTextArea::attachResizeHandle(int minHeightPx, std::function<void(int lines)> onLineCountChanged, QWidget *layoutParent)
{
    if (m_resizeHandle || !layoutParent)
        return;
    m_resizeHandle = new ComfyPromptResizeHandle(this, std::move(onLineCountChanged), minHeightPx, layoutParent);
    if (QVBoxLayout *col = qobject_cast<QVBoxLayout *>(layoutParent->layout()))
        col->addWidget(m_resizeHandle);
}
