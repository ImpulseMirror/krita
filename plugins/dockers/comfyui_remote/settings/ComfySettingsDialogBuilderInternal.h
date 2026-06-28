/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QObject>

#include <functional>

class QLineEdit;

namespace ComfySettingsDialogBuilderInternal {

class ComfyBuiltinStyleEditFilter : public QObject
{
public:
    ComfyBuiltinStyleEditFilter(QLineEdit *edit, std::function<void()> onDuplicated, QObject *parent);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QLineEdit *m_edit = nullptr;
    std::function<void()> m_onDuplicated;
};

extern bool g_nsfwFilterWarningShownThisSession;

} // namespace ComfySettingsDialogBuilderInternal
