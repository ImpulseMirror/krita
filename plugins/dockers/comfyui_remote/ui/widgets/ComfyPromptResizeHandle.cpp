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
    setFixedHeight(8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::SizeVerCursor);
    setToolTip(ComfyTr::tr("Drag vertically to resize the text area."));
}

void ComfyPromptResizeHandle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QColor c = palette().color(QPalette::Mid);
    p.setPen(QPen(c, 1, Qt::DotLine));
    const int y = height() / 2;
    p.drawLine(6, y, qMax(6, width() - 6), y);
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
}

void ComfyPromptResizeHandle::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        releaseMouse();
        if (m_editor && m_persistLines) {
            const QFontMetrics fm(m_editor->font());
            const int h = m_editor->height();
            const int ls = qMax(1, fm.lineSpacing());
            const int lines = qBound(1, static_cast<int>(qRound((h - fm.height() / 2) / double(ls))), 10);
            m_persistLines(lines);
        }
    }
}
