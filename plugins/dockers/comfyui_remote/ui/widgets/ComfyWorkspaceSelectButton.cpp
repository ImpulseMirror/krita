/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyWorkspaceSelectButton.h"

#include "ComfyLocalization.h"
#include "ComfyTheme.h"

#include <QAction>
#include <QMenu>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

namespace {

struct WorkspaceEntry {
    const char *iconStem;
    const char *label;
};

const WorkspaceEntry kWorkspaces[] = {
    {"workspace-generation", "Generate"},
    {"workspace-upscaling", "Upscale"},
    {"workspace-live", "Live"},
    {"workspace-animation", "Animation"},
    {"workspace-custom", "Graph"},
};

} // namespace

ComfyWorkspaceSelectButton::ComfyWorkspaceSelectButton(QWidget *parent)
    : QToolButton(parent)
{
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setPopupMode(QToolButton::InstantPopup);
    setAutoRaise(true);
    setToolTip(ComfyTr::tr(
        "Switch between workspaces: image generation, upscaling, live preview and animation."));
    setMinimumWidth(static_cast<int>(sizeHint().width() * 1.6));

    auto *menu = new QMenu(this);
    for (int i = 0; i < count(); ++i) {
        QAction *action = menu->addAction(
            ComfyTheme::icon(QString::fromLatin1(kWorkspaces[i].iconStem)),
            ComfyTr::tr(kWorkspaces[i].label));
        action->setIconVisibleInMenu(true);
        connect(action, &QAction::triggered, this, [this, i]() { setWorkspaceIndex(i, true); });
    }
    setMenu(menu);
    setWorkspaceIndex(0, false);
}

void ComfyWorkspaceSelectButton::setCurrentIndex(int index)
{
    setWorkspaceIndex(qBound(0, index, count() - 1), false);
}

void ComfyWorkspaceSelectButton::setWorkspaceIndex(int index, bool notify)
{
    index = qBound(0, index, count() - 1);
    const int prev = m_index;
    m_index = index;
    setIcon(ComfyTheme::icon(QString::fromLatin1(kWorkspaces[index].iconStem)));
    update();
    if (notify && prev != index)
        Q_EMIT currentIndexChanged(index);
}

void ComfyWorkspaceSelectButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter painter(this);
    QStyle *style = this->style();
    const Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter | Qt::AlignAbsolute;
    const QRect rect = this->rect();
    const QPixmap pixmap = icon().pixmap(static_cast<int>(rect.height() * 0.75));
    QStyle::PrimitiveElement element = QStyle::PE_Widget;
    if (opt.state & QStyle::State_MouseOver)
        element = QStyle::PE_PanelButtonCommand;
    style->drawPrimitive(element, &opt, &painter, this);
    style->drawItemPixmap(&painter, rect.adjusted(4, 0, 0, 0), static_cast<int>(align), pixmap);
    painter.translate(static_cast<int>(0.5 * rect.width() - 10), 0);
    style->drawPrimitive(QStyle::PE_IndicatorArrowDown, &opt, &painter, this);
}
