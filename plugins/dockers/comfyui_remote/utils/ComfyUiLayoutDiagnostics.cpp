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
#include <QLoggingCategory>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QSizePolicy>
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
        if (QWidget *widget = item->widget())
            itemH = widgetLayoutHeight(widget, contentWidth);
        else if (QLayout *sub = item->layout())
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
    } else {
        add(d->generate.regionPromptWidget);
        add(d->inpaint.strengthRowWidget);
        add(d->generate.generateActionRowWidget);
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

int measureCompactGenerateScrollHeight(void *dockPrivate, QScrollArea *scroll)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !scroll)
        return 0;

    const int contentWidth = scroll->viewport() && scroll->viewport()->width() > 0 ? scroll->viewport()->width()
                                                                                   : scroll->width();

    if (d->generate.genGroupBox) {
        d->generate.genGroupBox->updateGeometry();
        d->generate.genGroupBox->adjustSize();
    }
    if (d->generate.genContentContainer) {
        d->generate.genContentContainer->updateGeometry();
        d->generate.genContentContainer->adjustSize();
    }

    const int genGroupH = measureGenGroupHeight(d, contentWidth);
    const int genContentH = essentialGenContentHeight(d, contentWidth);
    const int frame = scroll->frameWidth() * 2;
    const int viewportPad = 2;
    const int ws = d->comboWorkspace ? d->comboWorkspace->currentIndex() : 0;
    int measured = genGroupH + frame + viewportPad;
    if (ws == 1 && d->generate.genGroupBox) {
        d->generate.genGroupBox->adjustSize();
        const int groupHint = d->generate.genGroupBox->sizeHint().height();
        if (groupHint > 0)
            measured = groupHint + frame + viewportPad;
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG measureScroll contentW=") << contentWidth
        << QStringLiteral("genContentH=") << genContentH << QStringLiteral("genGroupH=") << genGroupH
        << QStringLiteral("frame=") << frame << QStringLiteral("scrollH=") << measured
        << QStringLiteral("regionH=") << widgetLayoutHeight(d->generate.regionPromptWidget, contentWidth)
        << QStringLiteral("strengthH=") << widgetLayoutHeight(d->inpaint.strengthRowWidget, contentWidth)
        << QStringLiteral("generateRowH=") << widgetLayoutHeight(d->generate.generateActionRowWidget, contentWidth);

    return measured;
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
    if (d->progressBar && contentPage && d->progressBar->parentWidget() != contentPage) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG GAP progressBar parent is NOT contentPage");
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
