/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUiLayoutDiagnostics.h"

#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyLocalization.h"
#include "ComfySliderPaint.h"
#include "ComfyTrackSlider.h"
#include "ComfyUiStyle.h"

#include <QAbstractSlider>
#include <QBoxLayout>
#include <QHBoxLayout>
#include <QBoxLayout>
#include <QImage>
#include <QLayout>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyUiLayoutDiagnostics {

namespace {

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
        if (d->inpaint.strengthRowWidget) {
            int strengthH = 0;
            if (auto *row = qobject_cast<QHBoxLayout *>(d->inpaint.strengthRowWidget->layout()))
                strengthH = boxLayoutVisibleHeight(row, contentWidth);
            if (strengthH <= 0)
                strengthH = widgetLayoutHeight(d->inpaint.strengthRowWidget, contentWidth);
            if (strengthH <= 0)
                strengthH = ComfyUiStyle::Spacing::rowHeight;
            h += strengthH;
            ++visibleItems;
        }
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

void removeWidgetFromParentLayout(QWidget *widget)
{
    if (!widget || !widget->parentWidget())
        return;
    if (auto *lay = qobject_cast<QBoxLayout *>(widget->parentWidget()->layout()))
        lay->removeWidget(widget);
}

void ensureGenerateStrengthRowLayout(void *dockPrivate)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !d->inpaint.strengthRowWidget)
        return;
    auto *strengthLay = qobject_cast<QHBoxLayout *>(d->inpaint.strengthRowWidget->layout());
    if (!strengthLay)
        return;

    removeWidgetFromParentLayout(d->inpaint.strengthSliderWidget);
    removeWidgetFromParentLayout(d->generate.spinStrength);
    removeWidgetFromParentLayout(d->generate.btnAddControlIcon);
    removeWidgetFromParentLayout(d->generate.btnAddRegionIcon);

    if (d->inpaint.strengthSliderWidget) {
        d->inpaint.strengthSliderWidget->setVisible(true);
        strengthLay->insertWidget(0, d->inpaint.strengthSliderWidget, 1);
    }
    if (d->generate.spinStrength) {
        d->generate.spinStrength->setPrefix(ComfyTr::tr("Strength") + QStringLiteral(": "));
        d->generate.spinStrength->setVisible(true);
        strengthLay->insertWidget(1, d->generate.spinStrength);
    }
    if (d->generate.layerCountRow && strengthLay->indexOf(d->generate.layerCountRow) < 0)
        strengthLay->addWidget(d->generate.layerCountRow);
    if (d->generate.btnAddControlIcon) {
        d->generate.btnAddControlIcon->setVisible(true);
        strengthLay->addWidget(d->generate.btnAddControlIcon);
    }
    if (d->generate.btnAddRegionIcon) {
        d->generate.btnAddRegionIcon->setVisible(true);
        strengthLay->addWidget(d->generate.btnAddRegionIcon);
    }

    d->inpaint.strengthRowWidget->setVisible(true);
    d->inpaint.strengthRowWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const int rowH =
        qMax(ComfyUiStyle::Spacing::rowHeight, d->inpaint.strengthRowWidget->sizeHint().height());
    d->inpaint.strengthRowWidget->setMinimumHeight(rowH);
    d->inpaint.strengthRowWidget->updateGeometry();
}

void logSliderMetrics(const char *reason, QWidget *widget)
{
    if (!widget)
        return;
    widget->ensurePolished();
    const QSizePolicy sp = widget->sizePolicy();
    QString parentChain;
    for (QWidget *p = widget->parentWidget(); p; p = p->parentWidget()) {
        if (!parentChain.isEmpty())
            parentChain += QStringLiteral(" <- ");
        parentChain += QString::fromUtf8(p->metaObject()->className());
        if (p->objectName().size())
            parentChain += QLatin1Char('#') + p->objectName();
        parentChain += QStringLiteral(" h=%1").arg(p->height());
    }
  qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG SLIDER") << reason
        << QStringLiteral("class=") << widget->metaObject()->className()
        << QStringLiteral("geom=") << rectStr(widget->geometry())
        << QStringLiteral("sizeHint=") << widget->sizeHint()
        << QStringLiteral("minSizeHint=") << widget->minimumSizeHint()
        << QStringLiteral("policy=") << static_cast<int>(sp.horizontalPolicy()) << static_cast<int>(sp.verticalPolicy())
        << QStringLiteral("minH=") << widget->minimumHeight() << QStringLiteral("maxH=") << widget->maximumHeight()
        << QStringLiteral("fixed=") << (widget->minimumHeight() == widget->maximumHeight() && widget->maximumHeight() > 0)
        << QStringLiteral("styleSheetBytes=") << widget->styleSheet().size()
        << QStringLiteral("parents=") << parentChain;
    if (auto *track = qobject_cast<ComfyTrackSlider *>(widget)) {
        const QRect tr = ComfySliderPaint::horizontalTrackRect(widget->rect());
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG SLIDER track") << reason
            << QStringLiteral("track=") << rectStr(tr)
            << QStringLiteral("value=") << track->value() << QStringLiteral("range=") << track->minimum()
            << track->maximum();
    } else if (auto *legacy = qobject_cast<QSlider *>(widget)) {
        qCWarning(KIS_COMFYUI_REMOTE).noquote()
            << QStringLiteral("COMFY_UI_DIAG SLIDER LEGACY_QSLIDER") << reason
            << QStringLiteral("value=") << legacy->value();
    }
}

void logStrengthRowMetrics(void *dockPrivate)
{
    auto *d = static_cast<ComfyUIRemoteDock::Private *>(dockPrivate);
    if (!d || !d->inpaint.strengthRowWidget)
        return;
    logSliderMetrics("strengthRow", d->inpaint.strengthRowWidget);
    if (d->inpaint.strengthSliderWidget)
        logSliderMetrics("strengthSliderWidget", d->inpaint.strengthSliderWidget);
    if (d->inpaint.sliderStrength)
        logSliderMetrics("sliderStrength", d->inpaint.sliderStrength);
    if (d->generate.spinStrength)
        logSliderMetrics("spinStrength", d->generate.spinStrength);
}

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

} // namespace ComfyUiLayoutDiagnostics
