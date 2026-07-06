/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QPlainTextEdit>
#include <QPointer>
#include <QVariant>
#include <QWidget>

#include <functional>

class QCompleter;
class ComfyPromptResizeHandle;

/// QWidget shell that hosts prompt/line editors — forwards Android IME queries to focused child.
class ComfyTextInputContainer : public QWidget
{
    Q_OBJECT
public:
    explicit ComfyTextInputContainer(QWidget *parent = nullptr);

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
};

/// QFrame variant of ComfyTextInputContainer (region chrome, bordered stacks).
class ComfyTextInputFrame : public QFrame
{
    Q_OBJECT
public:
    explicit ComfyTextInputFrame(QWidget *parent = nullptr);

    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
};

/// Multi-line text primitive: tag-completer tab behavior + optional resize handle column.
class ComfyTextArea : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit ComfyTextArea(QCompleter *completer = nullptr, QWidget *parent = nullptr);

    void attachResizeHandle(int minHeightPx, std::function<void(int lines)> onLineCountChanged, QWidget *layoutParent = nullptr);
    ComfyPromptResizeHandle *resizeHandle() const { return m_resizeHandle; }

    /// Android IME queries ancestor widgets for cursor/composing state — forward to focused editor.
    static QVariant forwardContainerInputMethodQuery(const QWidget *container, Qt::InputMethodQuery query);
    static void setPlainTextPreserveCursor(QPlainTextEdit *editor, const QString &text);

protected:
    bool focusNextPrevChild(bool next) override;

private:
    QPointer<QCompleter> m_completer;
    ComfyPromptResizeHandle *m_resizeHandle = nullptr;
};
