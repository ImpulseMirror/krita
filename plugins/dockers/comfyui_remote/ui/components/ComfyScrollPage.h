/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

class QScrollArea;
class QVBoxLayout;
class QWidget;

/// Scrollable settings page with optional heading.
class ComfyScrollPage
{
public:
    static ComfyScrollPage create(QWidget *dialogParent, const QString &heading);

    QWidget *page() const { return m_page; }
    QVBoxLayout *outerLayout() const { return m_outerLayout; }
    QScrollArea *scrollArea() const { return m_scroll; }
    QWidget *body() const { return m_body; }
    QVBoxLayout *bodyLayout() const { return m_bodyLayout; }
    QWidget *inner() const { return m_body; }
    QVBoxLayout *innerLayout() const { return m_bodyLayout; }

private:
    QWidget *m_page = nullptr;
    QVBoxLayout *m_outerLayout = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
};
