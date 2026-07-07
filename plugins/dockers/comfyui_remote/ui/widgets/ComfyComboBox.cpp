/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyComboBox.h"
#include "ComfyComboPaint.h"
#include "ComfyUiStyle.h"

#include <QAbstractItemView>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QShowEvent>
#include <QSizePolicy>

ComfyComboBox::ComfyComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setFixedHeight(ComfyUiStyle::Spacing::comboHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet(QString());

    connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { update(); });
    connect(this, &QComboBox::editTextChanged, this, [this](const QString &) {
        if (isEditable())
            update();
    });
}

bool ComfyComboBox::arrowColumnContains(const QPoint &pos) const
{
    return ComfyComboPaint::arrowColumnRect(rect()).contains(pos);
}

void ComfyComboBox::layoutLineEdit()
{
    if (!lineEdit())
        return;
    const QRect column = ComfyComboPaint::arrowColumnRect(rect());
    const QRect textArea = ComfyComboPaint::textRect(rect(), column);
    lineEdit()->setGeometry(textArea);
    lineEdit()->setFrame(false);
    lineEdit()->setStyleSheet(QStringLiteral("background: transparent; border: none; padding: 0px; margin: 0px;"));
}

void ComfyComboBox::stylePopupView()
{
    if (!view())
        return;
    view()->setStyleSheet(ComfyUiStyle::comboBoxPopupStyleSheet());
}

void ComfyComboBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    ComfyComboPaint::ComboPaintState state;
    state.enabled = isEnabled();
    state.focused = hasFocus() || (lineEdit() && lineEdit()->hasFocus());
    state.hover = m_hover;
    state.open = m_popupOpen;

    const QString text = isEditable() ? QString() : currentText();
    const QIcon icon = currentIndex() >= 0 ? itemIcon(currentIndex()) : QIcon();
    ComfyComboPaint::paintComboBox(p, rect(), text, font(), icon, state);
}

void ComfyComboBox::resizeEvent(QResizeEvent *event)
{
    QComboBox::resizeEvent(event);
    layoutLineEdit();
}

void ComfyComboBox::showEvent(QShowEvent *event)
{
    QComboBox::showEvent(event);
    layoutLineEdit();
}

void ComfyComboBox::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isEnabled() && arrowColumnContains(event->pos())) {
        if (m_popupOpen)
            hidePopup();
        else
            showPopup();
        event->accept();
        return;
    }
    QComboBox::mousePressEvent(event);
}

void ComfyComboBox::mouseMoveEvent(QMouseEvent *event)
{
    const bool hover = arrowColumnContains(event->pos());
    if (m_hover != hover) {
        m_hover = hover;
        update();
    }
    QComboBox::mouseMoveEvent(event);
}

void ComfyComboBox::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QComboBox::leaveEvent(event);
}

void ComfyComboBox::showPopup()
{
    stylePopupView();
    m_popupOpen = true;
    update();
    QComboBox::showPopup();
}

void ComfyComboBox::hidePopup()
{
    m_popupOpen = false;
    update();
    QComboBox::hidePopup();
}

void ComfyComboBox::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange)
        update();
    QComboBox::changeEvent(event);
}
