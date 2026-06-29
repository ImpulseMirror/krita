/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPromptResizeHandle.h"
#include "ComfyLocalization.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>

#include <klocalizedstring.h>

ComfyPromptResizeHandle::ComfyPromptResizeHandle(QPlainTextEdit *editor,
                                                 PersistLinesFn persistLines,
                                                 int minHeightPx,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
    , m_persistLines(std::move(persistLines))
    , m_minHeightPx(minHeightPx)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setFixedHeight(12);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::SizeVerCursor);
    setToolTip(ComfyTr::tr("Drag vertically to resize the text area."));
    if (m_editor) {
        m_editor->installEventFilter(this);
        reposition();
        raise();
    }
}

void ComfyPromptResizeHandle::reposition()
{
    if (!m_editor)
        return;
    const int h = height();
    setGeometry(0, qMax(0, m_editor->height() - h), m_editor->width(), h);
    raise();
}

bool ComfyPromptResizeHandle::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::LayoutRequest)) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void ComfyPromptResizeHandle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = palette().color(QPalette::Mid);
    p.setBrush(c);
    p.setPen(Qt::NoPen);

    const int dotR = 1;
    const int spacing = 4;
    const int rows[2] = {3, 4};
    const int baseY = height() - 3;
    for (int row = 0; row < 2; ++row) {
        const int count = rows[row];
        const int rowWidth = (count - 1) * spacing;
        const int x0 = (width() - rowWidth) / 2;
        const int y = baseY - row * spacing;
        for (int i = 0; i < count; ++i)
            p.drawEllipse(QPoint(x0 + i * spacing, y), dotR, dotR);
    }
}

void ComfyPromptResizeHandle::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_editor) {
        m_dragging = true;
        m_pressGlobalY = e->globalPos().y();
        m_startHeight = m_editor->height();
        grabMouse();
    }
}

void ComfyPromptResizeHandle::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging || !m_editor)
        return;
    const int dy = e->globalPos().y() - m_pressGlobalY;
    const int nh = qBound(m_minHeightPx, m_startHeight + dy, 800);
    m_editor->setFixedHeight(nh);
    reposition();
    Q_EMIT heightChanged();
}

void ComfyPromptResizeHandle::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        releaseMouse();
        if (m_editor && m_persistLines) {
            const QFontMetrics fm(m_editor->document() ? m_editor->document()->defaultFont() : m_editor->font());
            const int h = m_editor->height();
            const int ls = qMax(1, fm.lineSpacing());
            const int lines = qBound(1, static_cast<int>(qRound((h - 10) / double(ls))), 10);
            m_persistLines(lines);
        }
        reposition();
        Q_EMIT heightChanged();
    }
}
