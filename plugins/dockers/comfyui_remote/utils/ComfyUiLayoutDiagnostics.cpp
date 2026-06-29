/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUiLayoutDiagnostics.h"

#include "ComfyUIUtils.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionPromptWidget.h"

#include <QBoxLayout>
#include <QImage>
#include <QJsonObject>
#include <QLayout>
#include <QListWidget>
#include <QLoggingCategory>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyUiLayoutDiagnostics {

namespace {

QString sizePolicyTag(const QSizePolicy &sp)
{
    auto v = [](QSizePolicy::Policy p) {
        switch (p) {
        case QSizePolicy::Fixed:
            return QStringLiteral("Fixed");
        case QSizePolicy::Minimum:
            return QStringLiteral("Min");
        case QSizePolicy::Maximum:
            return QStringLiteral("Max");
        case QSizePolicy::Preferred:
            return QStringLiteral("Pref");
        case QSizePolicy::Expanding:
            return QStringLiteral("Exp");
        case QSizePolicy::MinimumExpanding:
            return QStringLiteral("MinExp");
        case QSizePolicy::Ignored:
            return QStringLiteral("Ign");
        }
        return QStringLiteral("?");
    };
    return v(sp.horizontalPolicy()) + QLatin1Char('/') + v(sp.verticalPolicy());
}

QString rectStr(const QRect &r)
{
    if (r.isNull())
        return QStringLiteral("null");
    return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

int widgetLayoutHeight(QWidget *widget, int contentWidth)
{
    if (!widget || widget->maximumHeight() == 0)
        return 0;
    widget->ensurePolished();
    const bool progressRow = widget->objectName() == QLatin1String("ComfyGenerateProgressBar");
    if (progressRow && widget->sizePolicy().verticalPolicy() == QSizePolicy::Fixed && widget->height() > 0)
        return widget->height();
    if (widget->objectName() == QLatin1String("RegionPromptWidget") && widget->height() > 0)
        return widget->height();
    if (widget->sizePolicy().verticalPolicy() == QSizePolicy::Fixed && widget->height() > 0)
        return widget->height();
    int h = contentWidth > 0 ? widget->heightForWidth(contentWidth) : 0;
    if (h <= 0)
        h = widget->sizeHint().height();
    if (h <= 0)
        h = widget->minimumSizeHint().height();
    return h;
}

int boxLayoutVisibleHeight(QBoxLayout *box, int contentWidth)
{
    if (!box)
        return 0;
    int h = 0;
    int visibleItems = 0;
    for (int i = 0; i < box->count(); ++i) {
        QLayoutItem *item = box->itemAt(i);
        if (!item)
            continue;
        int itemH = 0;
        if (QWidget *widget = item->widget()) {
            if (!widget->isVisibleTo(box->parentWidget() ? box->parentWidget() : widget->parentWidget()))
                continue;
            itemH = widgetLayoutHeight(widget, contentWidth);
        } else if (QLayout *sub = item->layout())
            itemH = boxLayoutVisibleHeight(qobject_cast<QBoxLayout *>(sub), contentWidth);
        if (itemH <= 0)
            continue;
        h += itemH;
        ++visibleItems;
    }
    if (visibleItems > 1)
        h += box->spacing() * (visibleItems - 1);
    const QMargins margins = box->contentsMargins();
    return h + margins.top() + margins.bottom();
}

int essentialGenContentHeight(ComfyUIRemoteDock::Private *d, int contentWidth)
{
    if (!d || !d->generate.genContentContainer)
        return 0;
    auto *box = qobject_cast<QVBoxLayout *>(d->generate.genContentContainer->layout());
    if (!box)
        return 0;

    int h = 0;
    int visibleItems = 0;
    const auto add = [&](QWidget *widget) {
        const int wh = widgetLayoutHeight(widget, contentWidth);
        if (wh <= 0)
            return;
        h += wh;
        ++visibleItems;
    };
    const int ws = d->comboWorkspace ? d->comboWorkspace->currentIndex() : 0;
    if (ws == 1) {
        add(d->upscale.upscaleFactorRow);
        add(d->upscale.upscaleRefineBlock);
        add(d->upscale.upscaleActionRowWidget);
        add(d->progressBar);
    } else if (ws == 2) {
        int paramsH = 0;
        if (d->live.liveParamsRowWidget) {
            if (auto *row = qobject_cast<QHBoxLayout *>(d->live.liveParamsRowWidget->layout()))
                paramsH = boxLayoutVisibleHeight(row, contentWidth);
            if (paramsH <= 0)
                paramsH = widgetLayoutHeight(d->live.liveParamsRowWidget, contentWidth);
            if (paramsH > 0) {
                h += paramsH;
                ++visibleItems;
            }
        }
        int promptRowH = 0;
        if (d->generate.regionPromptWidget
            && d->live.livePromptHostWidget
            && d->generate.regionPromptWidget->parentWidget() == d->live.livePromptHostWidget) {
            promptRowH = widgetLayoutHeight(d->generate.regionPromptWidget, contentWidth);
        }
        if (promptRowH <= 0 && d->live.livePromptRowWidget) {
            if (auto *row = qobject_cast<QHBoxLayout *>(d->live.livePromptRowWidget->layout()))
                promptRowH = boxLayoutVisibleHeight(row, contentWidth);
            if (promptRowH <= 0)
                promptRowH = widgetLayoutHeight(d->live.livePromptRowWidget, contentWidth);
        }
        if (promptRowH > 0) {
            h += promptRowH;
            ++visibleItems;
        }
    } else {
        add(d->generate.regionPromptWidget);
        add(d->inpaint.strengthRowWidget);
        add(d->generate.generateActionRowWidget);
        add(d->progressBar);
    }

    for (int i = 0; i < box->count(); ++i) {
        QLayoutItem *item = box->itemAt(i);
        if (!item || item->widget() || item->layout())
            continue;
        h += item->spacerItem() ? item->spacerItem()->sizeHint().height() : 0;
    }

    if (visibleItems > 1)
        h += box->spacing() * (visibleItems - 1);
    const QMargins margins = box->contentsMargins();
    return h + margins.top() + margins.bottom();
}

int measureGenGroupHeight(ComfyUIRemoteDock::Private *d, int contentWidth)
{
    if (!d || !d->generate.genGroupBox)
        return 0;
    auto *box = qobject_cast<QVBoxLayout *>(d->generate.genGroupBox->layout());
    if (!box)
        return 0;

    int h = 0;
    int visibleItems = 0;
    for (int i = 0; i < box->count(); ++i) {
        QLayoutItem *item = box->itemAt(i);
        if (!item)
            continue;
        int itemH = 0;
        if (QWidget *widget = item->widget()) {
            if (!widget->isVisibleTo(d->generate.genGroupBox))
                continue;
            if (widget == d->generate.genContentContainer)
                itemH = essentialGenContentHeight(d, contentWidth);
            else
                itemH = widgetLayoutHeight(widget, contentWidth);
        } else if (QLayout *sub = item->layout()) {
            itemH = boxLayoutVisibleHeight(qobject_cast<QBoxLayout *>(sub), contentWidth);
        }
        if (itemH <= 0)
            continue;
        h += itemH;
        ++visibleItems;
    }
    if (visibleItems > 1)
        h += box->spacing() * (visibleItems - 1);
    const QMargins margins = box->contentsMargins();
    return h + margins.top() + margins.bottom();
}

} // namespace

int measureEssentialGenerateChromeHeight(void *dockPrivate, int contentWidth)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d)
        return 0;
    if (d->generate.genContentContainer) {
        d->generate.genContentContainer->updateGeometry();
        d->generate.genContentContainer->adjustSize();
    }
    if (d->generate.genGroupBox) {
        d->generate.genGroupBox->updateGeometry();
        d->generate.genGroupBox->adjustSize();
    }
    return measureGenGroupHeight(d, contentWidth);
}

int measureEssentialGenerateContentHeight(void *dockPrivate, int contentWidth)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d)
        return 0;
    return essentialGenContentHeight(d, contentWidth);
}

int measureWorkspaceTopChromeInset(void *dockPrivate)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !d->generate.genGroupBox || !d->comboWorkspace)
        return -1;
    QWidget *gen = d->generate.genGroupBox;
    if (!d->comboWorkspace->isVisibleTo(gen))
        return -1;
    return d->comboWorkspace->mapTo(gen, QPoint(0, 0)).y();
}

int measureGenGroupTopOnContentPage(void *dockPrivate)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !d->generate.genGroupBox)
        return -1;
    QWidget *contentPage = d->history.histGroupBox ? d->history.histGroupBox->parentWidget() : nullptr;
    if (!contentPage && d->progressBar)
        contentPage = d->progressBar->parentWidget();
    if (!contentPage)
        return -1;
    return d->generate.genGroupBox->mapTo(contentPage, QPoint(0, 0)).y();
}

int measurePrimaryChromeTopOnDocker(void *dockPrivate, QWidget *dockerRoot)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !dockerRoot)
        return -1;
    const int ws = d->comboWorkspace ? d->comboWorkspace->currentIndex() : 0;
    QWidget *primary = nullptr;
    if (ws == 1)
        primary = d->upscale.comboUpscaleModel;
    else if (ws == 0)
        primary = d->generate.comboPreset ? static_cast<QWidget *>(d->generate.comboPreset)
                                            : static_cast<QWidget *>(d->comboWorkspace);
    else
        primary = d->comboWorkspace;
    if (!primary || !primary->isVisible())
        primary = d->comboWorkspace;
    if (!primary)
        return -1;
    return primary->mapTo(dockerRoot, QPoint(0, 0)).y();
}

int measureRegionPromptHeightOnUpscale(void *dockPrivate)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !d->generate.regionPromptWidget)
        return -1;
    if (!d->comboWorkspace || d->comboWorkspace->currentIndex() != 1)
        return -1;
    return d->generate.regionPromptWidget->height();
}

int measureCompactGenerateScrollHeight(void *dockPrivate, QScrollArea *scroll)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !scroll)
        return 0;

    const int contentWidth = [&]() {
        if (scroll->viewport() && scroll->viewport()->width() > 0)
            return scroll->viewport()->width();
        if (scroll->width() > 0)
            return scroll->width();
        if (QWidget *page = scroll->parentWidget())
            return page->width();
        return 0;
    }();

    const int genGroupH = measureEssentialGenerateChromeHeight(d, contentWidth);
    const int frame = scroll->frameWidth() * 2;
    return genGroupH + frame;
}

void logGenerateHistoryLayout(void *dockPrivate, const char *reason)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d)
        return;

    QWidget *contentPage = nullptr;
    if (d->history.histGroupBox)
        contentPage = d->history.histGroupBox->parentWidget();
    if (!contentPage && d->progressBar)
        contentPage = d->progressBar->parentWidget();

    const QWidget *genGroup = d->generate.genGroupBox;
    const QWidget *histGroup = d->history.histGroupBox;
    QListWidget *list = d->history.listHistory;

    int gap = -1;
    if (contentPage && genGroup && histGroup) {
        const int chromeBottom = genGroup->mapTo(contentPage, QPoint(0, genGroup->height())).y();
        const int histTop = histGroup->mapTo(contentPage, QPoint(0, 0)).y();
        gap = histTop - chromeBottom;
        if (gap < 0 || gap > 8) {
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_UI_DIAG genHistLayout GAP contentPageH=")
                << contentPage->height() << QStringLiteral("genH=") << genGroup->height()
                << QStringLiteral("histH=") << histGroup->height() << QStringLiteral("gap=") << gap;
        }
    }

    QString firstItemRect = QStringLiteral("none");
    const int listCount = list ? list->count() : 0;
    if (list && listCount > 0 && list->item(0))
        firstItemRect = rectStr(list->visualItemRect(list->item(0)));

    QString layoutKids;
    if (contentPage && contentPage->layout()) {
        QLayout *lay = contentPage->layout();
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *item = lay->itemAt(i);
            if (!item)
                continue;
            QWidget *w = item->widget();
            int stretch = 0;
            if (auto *box = qobject_cast<QBoxLayout *>(lay))
                stretch = box->stretch(i);
            layoutKids += QStringLiteral(" [") + QString::number(i) + QLatin1Char(':');
            layoutKids += w ? w->metaObject()->className() : QStringLiteral("non-widget");
            layoutKids += QLatin1Char(' ');
            layoutKids += w ? rectStr(w->geometry()) : QStringLiteral("null");
            layoutKids += QStringLiteral(" stretch=") + QString::number(stretch);
            layoutKids += QStringLiteral(" vis=") + QString::number(w && w->isVisible());
            layoutKids += QLatin1Char(']');
        }
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG genHistLayout reason=") << reason << QStringLiteral("marker=") << kBuildMarker
        << QStringLiteral("contentPage=") << rectStr(contentPage ? contentPage->geometry() : QRect())
        << QStringLiteral("genGroup=") << rectStr(genGroup ? genGroup->geometry() : QRect())
        << QStringLiteral("genPolicy=") << (genGroup ? sizePolicyTag(genGroup->sizePolicy()) : QString())
        << QStringLiteral("genContent=")
        << rectStr(d->generate.genContentContainer ? d->generate.genContentContainer->geometry() : QRect())
        << QStringLiteral("histGroup=") << rectStr(histGroup ? histGroup->geometry() : QRect())
        << QStringLiteral("histPolicy=") << (histGroup ? sizePolicyTag(histGroup->sizePolicy()) : QString())
        << QStringLiteral("gap=") << gap << QStringLiteral("entries=") << d->history.historyEntries.size()
        << QStringLiteral("listCount=") << listCount << QStringLiteral("listGeom=")
        << rectStr(list ? list->geometry() : QRect())
        << QStringLiteral("listMinH=") << (list ? list->minimumHeight() : -1)
        << QStringLiteral("scrollMax=") << (list && list->verticalScrollBar() ? list->verticalScrollBar()->maximum() : -1)
        << QStringLiteral("firstItem=") << firstItemRect << QStringLiteral("kids=") << layoutKids;
}

void logWorkspaceChromeLayout(void *dockPrivate, QWidget *dockerRoot, const char *reason)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d)
        return;

    QWidget *contentPage = nullptr;
    if (d->history.histGroupBox)
        contentPage = d->history.histGroupBox->parentWidget();
    if (!contentPage && d->progressBar)
        contentPage = d->progressBar->parentWidget();

    const int ws = d->comboWorkspace ? d->comboWorkspace->currentIndex() : -1;
    const QWidget *genGroup = d->generate.genGroupBox;
    const int genTopPage = measureGenGroupTopOnContentPage(dockPrivate);
    const int chromeTopDocker = measurePrimaryChromeTopOnDocker(dockPrivate, dockerRoot);
    const int wsInset = measureWorkspaceTopChromeInset(dockPrivate);

    int regionInLayout = -1;
    int regionPromptH = -1;
    if (d->generate.regionPromptWidget && d->generate.genContentContainer) {
        if (auto *lay = d->generate.genContentContainer->layout())
            regionInLayout = lay->indexOf(d->generate.regionPromptWidget);
        regionPromptH = d->generate.regionPromptWidget->height();
    }

    QString contentPageKids;
    if (contentPage && contentPage->layout()) {
        QLayout *lay = contentPage->layout();
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *item = lay->itemAt(i);
            if (!item)
                continue;
            QWidget *w = item->widget();
            contentPageKids += QStringLiteral(" [") + QString::number(i) + QLatin1Char(':');
            contentPageKids += w ? w->metaObject()->className() : QStringLiteral("non-widget");
            contentPageKids += QLatin1Char(' ');
            contentPageKids += rectStr(w ? w->geometry() : QRect());
            contentPageKids += QStringLiteral(" vis=") + QString::number(w && w->isVisible());
            contentPageKids += QStringLiteral(" minH=") + QString::number(w ? w->minimumHeight() : -1);
            if (w && dockerRoot)
                contentPageKids += QStringLiteral(" dockerY=") + QString::number(w->mapTo(dockerRoot, QPoint(0, 0)).y());
            contentPageKids += QLatin1Char(']');
        }
    }

    QString topRowKids;
    if (d->workspaceTopRowLayout) {
        for (int i = 0; i < d->workspaceTopRowLayout->count(); ++i) {
            QLayoutItem *item = d->workspaceTopRowLayout->itemAt(i);
            if (!item)
                continue;
            QWidget *w = item->widget();
            topRowKids += QStringLiteral(" [") + QString::number(i) + QLatin1Char(':');
            topRowKids += w ? w->objectName() : QStringLiteral("non-widget");
            topRowKids += QLatin1Char(' ');
            topRowKids += rectStr(w ? w->geometry() : QRect());
            topRowKids += QStringLiteral(" vis=") + QString::number(w && w->isVisible());
            topRowKids += QStringLiteral(" hintH=") + QString::number(w ? w->sizeHint().height() : -1);
            if (w && dockerRoot)
                topRowKids += QStringLiteral(" dockerY=") + QString::number(w->mapTo(dockerRoot, QPoint(0, 0)).y());
            topRowKids += QLatin1Char(']');
        }
    }

    QString genContentKids;
    if (d->generate.genContentContainer && d->generate.genContentContainer->layout()) {
        QLayout *gl = d->generate.genContentContainer->layout();
        for (int i = 0; i < gl->count(); ++i) {
            QLayoutItem *item = gl->itemAt(i);
            if (!item)
                continue;
            QWidget *w = item->widget();
            if (!w)
                continue;
            genContentKids += QStringLiteral(" [") + QString::number(i) + QLatin1Char(':');
            genContentKids += w->objectName().isEmpty() ? w->metaObject()->className() : w->objectName();
            genContentKids += QLatin1Char(' ');
            genContentKids += rectStr(w->geometry());
            genContentKids += QStringLiteral(" vis=") + QString::number(w->isVisible());
            genContentKids += QStringLiteral(" h=") + QString::number(w->height());
            if (dockerRoot)
                genContentKids += QStringLiteral(" dockerY=") + QString::number(w->mapTo(dockerRoot, QPoint(0, 0)).y());
            genContentKids += QLatin1Char(']');
        }
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG wsChrome reason=") << reason << QStringLiteral("marker=") << kBuildMarker
        << QStringLiteral("ws=") << ws << QStringLiteral("docker=") << rectStr(dockerRoot ? dockerRoot->geometry() : QRect())
        << QStringLiteral("contentPage=") << rectStr(contentPage ? contentPage->geometry() : QRect())
        << QStringLiteral("contentMargins=") << (contentPage && contentPage->layout() ? contentPage->layout()->contentsMargins() : QMargins())
        << QStringLiteral("genGroup=") << rectStr(genGroup ? genGroup->geometry() : QRect())
        << QStringLiteral("genTopPage=") << genTopPage << QStringLiteral("genMaxH=") << (genGroup ? genGroup->maximumHeight() : -1)
        << QStringLiteral("genMinH=") << (genGroup ? genGroup->minimumHeight() : -1)
        << QStringLiteral("wsInset=") << wsInset << QStringLiteral("chromeTopDocker=") << chromeTopDocker
        << QStringLiteral("regionInLayout=") << regionInLayout << QStringLiteral("regionH=") << regionPromptH
        << QStringLiteral("regionVis=") << (d->generate.regionPromptWidget && d->generate.regionPromptWidget->isVisible())
        << QStringLiteral("contentPageKids=") << contentPageKids
        << QStringLiteral("topRow=") << topRowKids << QStringLiteral("genContent=") << genContentKids;
}

void logLiveWorkspaceLayout(void *dockPrivate, QWidget *dockerRoot, const char *reason)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d)
        return;

    QWidget *contentPage = nullptr;
    if (d->history.histGroupBox)
        contentPage = d->history.histGroupBox->parentWidget();
    if (!contentPage && d->progressBar)
        contentPage = d->progressBar->parentWidget();

    const bool promptInHost =
        d->generate.regionPromptWidget && d->live.livePromptHostWidget
        && d->generate.regionPromptWidget->parentWidget() == d->live.livePromptHostWidget;
    const int contentWidth = contentPage ? contentPage->width() : 0;
    const int measuredContentH = measureEssentialGenerateContentHeight(dockPrivate, contentWidth);
    const int genContentMaxH =
        d->generate.genContentContainer ? d->generate.genContentContainer->maximumHeight() : -1;

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG liveLayout reason=") << reason
        << QStringLiteral("marker=") << kBuildMarker << QStringLiteral("paramsRow=")
        << rectStr(d->live.liveParamsRowWidget ? d->live.liveParamsRowWidget->geometry() : QRect())
        << QStringLiteral("paramsVis=") << (d->live.liveParamsRowWidget && d->live.liveParamsRowWidget->isVisible())
        << QStringLiteral("promptRow=")
        << rectStr(d->live.livePromptRowWidget ? d->live.livePromptRowWidget->geometry() : QRect())
        << QStringLiteral("promptRowVis=")
        << (d->live.livePromptRowWidget && d->live.livePromptRowWidget->isVisible())
        << QStringLiteral("regionPromptInHost=") << promptInHost
        << QStringLiteral("regionPrompt=")
        << rectStr(d->generate.regionPromptWidget ? d->generate.regionPromptWidget->geometry() : QRect())
        << QStringLiteral("regionPromptH=") << (d->generate.regionPromptWidget ? d->generate.regionPromptWidget->height() : -1)
        << QStringLiteral("regionPromptVis=")
        << (d->generate.regionPromptWidget && d->generate.regionPromptWidget->isVisible())
        << QStringLiteral("preview=")
        << rectStr(d->live.livePreviewGroupBox ? d->live.livePreviewGroupBox->geometry() : QRect())
        << QStringLiteral("previewVis=")
        << (d->live.livePreviewGroupBox && d->live.livePreviewGroupBox->isVisible())
        << QStringLiteral("previewParent=")
        << (d->live.livePreviewGroupBox && d->live.livePreviewGroupBox->parentWidget()
                ? d->live.livePreviewGroupBox->parentWidget()->metaObject()->className()
                : QStringLiteral("null"))
        << QStringLiteral("previewArea=")
        << rectStr(d->live.livePreviewArea ? d->live.livePreviewArea->geometry() : QRect())
        << QStringLiteral("genContent=")
        << rectStr(d->generate.genContentContainer ? d->generate.genContentContainer->geometry() : QRect())
        << QStringLiteral("genContentMaxH=") << genContentMaxH << QStringLiteral("measuredContentH=") << measuredContentH;
    if (dockerRoot && d->live.liveParamsRowWidget && d->generate.spinStrength) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG liveLayout controls strengthDockerY=")
            << d->generate.spinStrength->mapTo(dockerRoot, QPoint(0, 0)).y()
            << QStringLiteral("seedDockerY=")
            << (d->generate.spinSeed ? d->generate.spinSeed->mapTo(dockerRoot, QPoint(0, 0)).y() : -1)
            << QStringLiteral("promptDockerY=")
            << (d->generate.regionPromptWidget
                    ? d->generate.regionPromptWidget->mapTo(dockerRoot, QPoint(0, 0)).y()
                    : -1)
            << QStringLiteral("previewDockerY=")
            << (d->live.livePreviewGroupBox
                    ? d->live.livePreviewGroupBox->mapTo(dockerRoot, QPoint(0, 0)).y()
                    : -1);
    }
}

void restoreLivePreviewPanelLayout(void *dockPrivate, QWidget *contentPage)
{
    Q_UNUSED(contentPage);
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !d->live.livePreviewGroupBox || !d->generate.genContentContainer)
        return;
    QWidget *preview = d->live.livePreviewGroupBox;
    if (QWidget *oldParent = preview->parentWidget()) {
        if (QLayout *oldLay = oldParent->layout())
            oldLay->removeWidget(preview);
    }
    preview->setParent(d->generate.genContentContainer);
    preview->show();
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preview->setMinimumSize(0, 0);
    preview->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    if (d->live.livePreviewArea) {
        d->live.livePreviewArea->setMinimumSize(128, 128);
        d->live.livePreviewArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        d->live.livePreviewArea->setAlignment(Qt::AlignCenter);
        d->live.livePreviewArea->show();
    }
    if (d->live.livePreviewRowWidget) {
        d->live.livePreviewRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        d->live.livePreviewRowWidget->show();
    }
    if (auto *box = qobject_cast<QVBoxLayout *>(d->generate.genContentContainer->layout())) {
        const int previewIx = box->indexOf(preview);
        const int promptIx = d->live.livePromptRowWidget ? box->indexOf(d->live.livePromptRowWidget) : -1;
        if (previewIx < 0) {
            const int insertAt = promptIx >= 0 ? promptIx + 1 : box->count();
            box->insertWidget(insertAt, preview, 1);
        } else if (promptIx >= 0 && previewIx <= promptIx) {
            box->removeWidget(preview);
            box->insertWidget(promptIx + 1, preview, 1);
        } else {
            box->setStretch(previewIx, 1);
        }
        for (int i = 0; i < box->count(); ++i) {
            QWidget *child = box->itemAt(i) ? box->itemAt(i)->widget() : nullptr;
            if (!child)
                continue;
            box->setStretch(i, child == preview ? 1 : 0);
        }
    }
    preview->updateGeometry();
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG liveLayout restorePreview parent=")
        << (preview->parentWidget() ? preview->parentWidget()->metaObject()->className() : QStringLiteral("null"))
        << QStringLiteral("geom=") << rectStr(preview->geometry()) << QStringLiteral("marker=") << kBuildMarker;
}

void logWidget(const char *tag, const QWidget *widget)
{
    if (!widget) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG") << tag << QStringLiteral("widget=null");
        return;
    }
    const QWidget *parent = widget->parentWidget();
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG") << tag << QStringLiteral("name=") << widget->objectName()
        << QStringLiteral("class=") << widget->metaObject()->className()
        << QStringLiteral("vis=") << widget->isVisible() << QStringLiteral("geom=") << rectStr(widget->geometry())
        << QStringLiteral("hint=") << widget->sizeHint() << QStringLiteral("min=") << widget->minimumSize()
        << QStringLiteral("max=") << widget->maximumSize() << QStringLiteral("policy=") << sizePolicyTag(widget->sizePolicy())
        << QStringLiteral("parent=") << (parent ? parent->objectName() : QStringLiteral("null"));
}

void logLayoutChildren(const char *tag, QLayout *layout)
{
    if (!layout) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG") << tag << QStringLiteral("layout=null");
        return;
    }
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG") << tag << QStringLiteral("layoutClass=") << layout->metaObject()->className()
        << QStringLiteral("count=") << layout->count() << QStringLiteral("spacing=") << layout->spacing()
        << QStringLiteral("margins=") << layout->contentsMargins();
    if (auto *box = qobject_cast<QBoxLayout *>(layout)) {
        for (int i = 0; i < box->count(); ++i) {
            QLayoutItem *item = box->itemAt(i);
            if (!item)
                continue;
            QWidget *w = item->widget();
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_UI_DIAG") << tag << QStringLiteral("child[") << i << QStringLiteral("] stretch=")
                << box->stretch(i) << QStringLiteral("widget=") << (w ? w->objectName() : QStringLiteral("<non-widget>"))
                << QStringLiteral("vis=") << (w ? w->isVisible() : false)
                << QStringLiteral("hint=") << (w ? w->sizeHint() : QSize());
        }
    }
}

static void dumpDockerLayout(ComfyUIRemoteDock::Private *d, QWidget *dockerRoot, const char *reason)
{
    if (!d)
        return;

    const QJsonObject settings = ComfyUIUtils::loadSettingsJson();
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG ===== dump reason=") << reason << QStringLiteral("marker=") << kBuildMarker
        << QStringLiteral("plugin=") << ComfyUIUtils::pluginVersion()
        << QStringLiteral("dockerGeom=") << rectStr(dockerRoot ? dockerRoot->geometry() : QRect())
        << QStringLiteral("ws=") << (d->comboWorkspace ? d->comboWorkspace->currentIndex() : -1)
        << QStringLiteral("prompt_lines=") << settings.value(QStringLiteral("prompt_line_count")).toInt(-1)
        << settings.value(QStringLiteral("prompt_line_count_live")).toInt(-1)
        << QStringLiteral("neg_lines=") << settings.value(QStringLiteral("negative_prompt_line_count")).toInt(-1)
        << QStringLiteral("show_neg=") << settings.value(QStringLiteral("show_negative_prompt")).toBool()
        << QStringLiteral("resize_handle=") << settings.value(QStringLiteral("prompt_resize_handle")).toBool();

    logWidget("docker.root", dockerRoot);

    QWidget *contentPage = nullptr;
    if (d->history.histGroupBox)
        contentPage = d->history.histGroupBox->parentWidget();
    if (!contentPage && d->progressBar)
        contentPage = d->progressBar->parentWidget();
    logWidget("contentPage", contentPage);
    if (contentPage)
        logLayoutChildren("contentPage.layout", contentPage->layout());

    QScrollArea *scroll = nullptr;
    if (contentPage) {
        for (QObject *child : contentPage->children()) {
            if (auto *sa = qobject_cast<QScrollArea *>(child)) {
                scroll = sa;
                break;
            }
        }
    }
    logWidget("scroll", scroll);
    if (scroll) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG scroll.adjustPolicy=") << scroll->sizeAdjustPolicy()
            << QStringLiteral("widgetResizable=") << scroll->widgetResizable();
        logWidget("scroll.widget", scroll->widget());
        if (scroll->widget())
            logLayoutChildren("scrollContent.layout", scroll->widget()->layout());
    }

    logWidget("histGroup", d->history.histGroupBox);
    logWidget("listHistory", d->history.listHistory);
    logWidget("progressBar", d->progressBar);
    logWidget("labelStatus", d->labelStatus);

    logWidget("genContentContainer", d->generate.genContentContainer);
    if (d->generate.genContentContainer)
        logLayoutChildren("genContent.layout", d->generate.genContentContainer->layout());
    if (d->generate.genContentContainer) {
        if (QLayout *gl = d->generate.genContentContainer->layout()) {
        if (gl->count() > 10) {
            int visibleHintH = 0;
            if (auto *box = qobject_cast<QBoxLayout *>(gl)) {
                for (int i = 0; i < box->count(); ++i) {
                    if (QWidget *cw = box->itemAt(i)->widget()) {
                        if (cw->isVisibleTo(d->generate.genContentContainer))
                            visibleHintH += cw->sizeHint().height();
                    }
                }
            }
            if (visibleHintH > 220) {
                qCWarning(KIS_COMFYUI_REMOTE).noquote()
                    << QStringLiteral("COMFY_UI_DIAG GAP genContent visible sizeHint h=") << visibleHintH
                    << QStringLiteral("childCount=") << gl->count();
            }
        }
        }
    }
    if (scroll && scroll->widget()) {
        const int contentHintH = scroll->widget()->sizeHint().height();
        const int scrollHintH = scroll->sizeHint().height();
        const int scrollGeomH = scroll->height();
        const int measuredH = measureCompactGenerateScrollHeight(d, scroll);
        if (scrollGeomH > 0 && measuredH > scrollGeomH + 2) {
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_UI_DIAG GAP scroll clipped measured=") << measuredH
                << QStringLiteral("geomH=") << scrollGeomH << QStringLiteral("contentHint=") << contentHintH;
        } else if (contentHintH > scrollHintH + 40) {
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_UI_DIAG GAP scrollContent sizeHint h=") << contentHintH
                << QStringLiteral("scroll sizeHint h=") << scrollHintH;
        }
    }

    logWidget("regionPromptWidget", d->generate.regionPromptWidget);
    logWidget("rootPromptColumn", d->generate.rootPromptColumnWidget);
    logWidget("negativePromptBlock", d->generate.negativePromptBlock);
    logWidget("strengthRow", d->inpaint.strengthRowWidget);
    logWidget("generateActionRow", d->generate.generateActionRowWidget);
    logWidget("controlLayersGroup", d->generate.controlLayersGroupBox);
    logWidget("regionsGroup", d->generate.regionsGroupBox);

    if (d->generate.editPrompt)
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG legacy.editPrompt h=") << d->generate.editPrompt->height()
            << QStringLiteral("vis=") << d->generate.editPrompt->isVisible()
            << QStringLiteral("parent=") << (d->generate.editPrompt->parentWidget()
                                                 ? d->generate.editPrompt->parentWidget()->objectName()
                                                 : QString());
    if (d->generate.editNegative)
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG legacy.editNegative h=") << d->generate.editNegative->height()
            << QStringLiteral("vis=") << d->generate.editNegative->isVisible();

    if (d->history.histGroupBox && d->history.histGroupBox->parentWidget() != contentPage) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG GAP history parent is NOT contentPage — history still in scroll?");
    }
    if (d->progressBar && d->generate.genContentContainer
        && d->progressBar->parentWidget() != d->generate.genContentContainer) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG GAP progressBar parent is NOT genContentContainer");
    }
    if (d->generate.rootPromptColumnWidget && d->generate.rootPromptColumnWidget->isVisible()) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG GAP duplicate root prompt column still visible");
    }
    if (d->generate.negativePromptBlock && d->generate.negativePromptBlock->isVisible()) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG GAP duplicate negative prompt block still visible");
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote() << QStringLiteral("COMFY_UI_DIAG ===== end dump");
}

QString maskShapeDescription(const QImage &maskGray)
{
    if (maskGray.isNull())
        return QStringLiteral("null");
    QImage m = maskGray.format() == QImage::Format_Grayscale8
                   ? maskGray
                   : maskGray.convertToFormat(QImage::Format_Grayscale8);
    const int w = m.width();
    const int h = m.height();
    if (w <= 0 || h <= 0)
        return QStringLiteral("empty");

    int strong = 0;
    int minX = w;
    int minY = h;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < h; ++y) {
        const uchar *line = m.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (line[x] > 16) {
                ++strong;
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }
    auto sample = [&](int x, int y) -> int {
        return static_cast<int>(m.constScanLine(qBound(0, y, h - 1))[qBound(0, x, w - 1)]);
    };
    const int cornerAvg = (sample(0, 0) + sample(w - 1, 0) + sample(0, h - 1) + sample(w - 1, h - 1)) / 4;
    const int center = sample(w / 2, h / 2);
    const int bboxW = maxX >= minX ? maxX - minX + 1 : 0;
    const int bboxH = maxY >= minY ? maxY - minY + 1 : 0;
    const double fillPct = 100.0 * strong / qMax(1, w * h);
    const double bboxFill = (bboxW > 0 && bboxH > 0) ? 100.0 * strong / (bboxW * bboxH) : 0.0;
    const bool likelyRound = bboxFill > 0.0 && bboxFill < 82.0 && cornerAvg < 32;
    return QStringLiteral("size=%1x%2 strong=%3% bbox=%4x%5 bboxFill=%6% corner=%7 center=%8 round=%9")
        .arg(w)
        .arg(h)
        .arg(fillPct, 0, 'f', 1)
        .arg(bboxW)
        .arg(bboxH)
        .arg(bboxFill, 0, 'f', 1)
        .arg(cornerAvg)
        .arg(center)
        .arg(likelyRound ? QStringLiteral("yes") : QStringLiteral("no"));
}

QString imageAlphaCornerStats(const QImage &img)
{
    if (img.isNull())
        return QStringLiteral("null");
    QImage argb = img.format() == QImage::Format_ARGB32 ? img : img.convertToFormat(QImage::Format_ARGB32);
    const int w = argb.width();
    const int h = argb.height();
    if (w <= 0 || h <= 0)
        return QStringLiteral("empty");
    auto alphaAt = [&](int x, int y) -> int {
        return qAlpha(argb.pixel(qBound(0, x, w - 1), qBound(0, y, h - 1)));
    };
    const int corner = (alphaAt(0, 0) + alphaAt(w - 1, 0) + alphaAt(0, h - 1) + alphaAt(w - 1, h - 1)) / 4;
    return QStringLiteral("cornerAlpha=%1 centerAlpha=%2")
        .arg(corner)
        .arg(alphaAt(w / 2, h / 2));
}

void logHistoryThumbnailStage(const char *stage,
                              const QString &resultPath,
                              const QRect &contextBounds,
                              const QRect &targetBounds,
                              bool hasMask,
                              const QSize &imageSize,
                              const QSize &maskSize,
                              const QString &detail)
{
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG THUMB") << stage << QStringLiteral("path=") << resultPath
        << QStringLiteral("hasMask=") << hasMask << QStringLiteral("context=") << rectStr(contextBounds)
        << QStringLiteral("target=") << rectStr(targetBounds) << QStringLiteral("img=") << imageSize
        << QStringLiteral("mask=") << maskSize << detail;
}

void dumpDockerLayoutForDock(void *dockPrivate, QWidget *dockerRoot, const char *reason)
{
    dumpDockerLayout(static_cast<ComfyUIRemoteDock::Private *>(dockPrivate), dockerRoot, reason);
}

} // namespace ComfyUiLayoutDiagnostics
