/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilderInternal.h"

#include <QLineEdit>
#include <QMouseEvent>

namespace ComfySettingsDialogBuilderInternal {

bool g_nsfwFilterWarningShownThisSession = false;

ComfyBuiltinStyleEditFilter::ComfyBuiltinStyleEditFilter(QLineEdit *edit,
                                                         std::function<void()> onDuplicated,
                                                         QObject *parent)
    : QObject(parent)
    , m_edit(edit)
    , m_onDuplicated(std::move(onDuplicated))
{
    if (m_edit)
        m_edit->installEventFilter(this);
}

bool ComfyBuiltinStyleEditFilter::eventFilter(QObject *obj, QEvent *event)
{
    if (!m_edit || obj != m_edit || !m_edit->isReadOnly())
        return QObject::eventFilter(obj, event);
    if (event->type() != QEvent::MouseButtonPress)
        return QObject::eventFilter(obj, event);
    const auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton)
        return QObject::eventFilter(obj, event);
    if (m_onDuplicated)
        m_onDuplicated();
    return QObject::eventFilter(obj, event);
}

} // namespace ComfySettingsDialogBuilderInternal
