/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyStyleCollection.h"
#include "ComfyFileLibrary.h"
#include "ComfyResources.h"
#include "ComfyTheme.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyStyleLoraListWidget.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyPromptLayoutMetrics.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfySwitchWidget.h"

#include <QComboBox>
#include <QLoggingCategory>
#include <QScrollArea>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <KSharedConfig>

#include "ComfyUIRemoteDockShellInternal.h"

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

using namespace ComfyDockShellInternal;

namespace {

void collapseCompactLayoutRow(QWidget *widget)
{
    if (!widget)
        return;
    widget->hide();
    widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    widget->setMinimumSize(0, 0);
    widget->setMaximumSize(0, 0);
    widget->setFixedSize(0, 0);
}

void restoreCompactLayoutRow(QWidget *widget)
{
    if (!widget)
        return;
    widget->show();
    const bool promptRow = widget->objectName() == QLatin1String("RegionPromptWidget");
    const bool progressRow = widget->objectName() == QLatin1String("ComfyGenerateProgressBar");
    const bool genGroupRow = widget->objectName() == QLatin1String("ComfyGenerateGroupBox");
    if (promptRow) {
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        widget->setMinimumSize(0, 0);
        widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    } else if (genGroupRow) {
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        widget->setMinimumSize(0, 0);
        widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    } else if (progressRow) {
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        widget->setMinimumSize(0, 0);
        widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        widget->setFixedHeight(2);
    } else {
        widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        widget->setMinimumSize(0, 0);
        widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    }
    widget->updateGeometry();
}

void reapplyRegionPromptCompactLayout(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->generate.regionPromptWidget || !d->comboWorkspace)
        return;
    const int ws = d->comboWorkspace->currentIndex();
    if (ws != 0 && ws != 2)
        return;
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const int lines = qBound(1, s.value(QStringLiteral("prompt_line_count")).toInt(3), 10);
    const bool showNeg = s.value(QStringLiteral("show_negative_prompt")).toBool(false);
    const bool liveWs = (ws == 2);
    const int posLines = ComfyPromptLayoutMetrics::positiveLinesForGenerateWorkspace(showNeg, lines);
    d->generate.regionPromptWidget->applyCompactLayout(posLines, showNeg, true, liveWs);
}

int compactGenerateContentWidth(QScrollArea *scroll, QWidget *contentPage)
{
    if (scroll && scroll->viewport() && scroll->viewport()->width() > 0)
        return scroll->viewport()->width();
    if (scroll && scroll->width() > 0)
        return scroll->width();
    if (contentPage && contentPage->width() > 0)
        return contentPage->width();
    return 0;
}

} // namespace

namespace {

void clearCompactFixedHeight(QWidget *widget)
{
    if (!widget)
        return;
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    widget->setMinimumHeight(0);
    widget->setMaximumHeight(QWIDGETSIZE_MAX);
}

void restoreHistoryPanelLayout(QWidget *widget)
{
    if (!widget)
        return;
    widget->show();
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    widget->setMinimumWidth(0);
    widget->setMaximumWidth(QWIDGETSIZE_MAX);
    widget->setMinimumHeight(96);
    widget->setMaximumHeight(QWIDGETSIZE_MAX);
    widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    widget->updateGeometry();
}

void collapseTopRowWidget(QWidget *widget)
{
    if (!widget)
        return;
    widget->hide();
    widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    widget->setMinimumSize(0, 0);
    widget->setMaximumSize(0, 0);
    widget->setFixedSize(0, 0);
}

void restoreTopRowWidget(QWidget *widget, QSizePolicy::Policy horizontalPolicy)
{
    if (!widget)
        return;
    widget->setSizePolicy(horizontalPolicy, QSizePolicy::Fixed);
    widget->setMinimumSize(0, 0);
    widget->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    widget->show();
    widget->updateGeometry();
}

void restoreLivePreviewPanelLayout(ComfyUIRemoteDock::Private *d, QWidget *contentPage)
{
    ComfyUiLayoutDiagnostics::restoreLivePreviewPanelLayout(d, contentPage);
}

void syncWorkspaceTopRowLayout(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->workspaceTopRowLayout)
        return;
    const int ws = d->comboWorkspace ? d->comboWorkspace->currentIndex() : 0;
    const bool isGenerate = (ws == 0);
    const bool isUpscale = (ws == 1);
    const bool isLive = (ws == 2);
    const bool isAnimation = (ws == 3);
    const bool isGraph = (ws == 4);
    const bool styleComboVisible = isGenerate || isLive || isAnimation || isGraph;

    if (d->comboWorkspace)
        restoreTopRowWidget(d->comboWorkspace, QSizePolicy::Fixed);
    if (d->generate.comboPreset) {
        if (styleComboVisible)
            restoreTopRowWidget(d->generate.comboPreset, QSizePolicy::Expanding);
        else
            collapseTopRowWidget(d->generate.comboPreset);
    }
    if (d->upscale.comboUpscaleModel) {
        if (isUpscale)
            restoreTopRowWidget(d->upscale.comboUpscaleModel, QSizePolicy::Expanding);
        else
            collapseTopRowWidget(d->upscale.comboUpscaleModel);
    }
    for (QToolButton *btn :
         {d->live.btnLivePlay, d->live.btnLiveRecord, d->live.btnLiveApply, d->live.btnLiveApplyLayer}) {
        if (!btn)
            continue;
        if (isLive)
            restoreTopRowWidget(btn, QSizePolicy::Fixed);
        else
            collapseTopRowWidget(btn);
    }
    d->workspaceTopRowLayout->activate();
}

void applyHistoryPanelWorkspaceVisibility(ComfyUIRemoteDock::Private *d, QWidget *contentPage)
{
    if (!d || !d->history.histGroupBox)
        return;
    const int ws = d->comboWorkspace ? d->comboWorkspace->currentIndex() : 0;
    const bool generateWs = (ws == 0);
    if (generateWs) {
        restoreHistoryPanelLayout(d->history.histGroupBox);
        if (contentPage && contentPage->layout()) {
            if (auto *box = qobject_cast<QVBoxLayout *>(contentPage->layout())) {
                const int histIx = box->indexOf(d->history.histGroupBox);
                if (histIx >= 0)
                    box->setStretch(histIx, 1);
            }
        }
    } else {
        d->history.histGroupBox->hide();
        d->history.histGroupBox->setMinimumHeight(0);
        collapseCompactLayoutRow(d->history.histGroupBox);
        if (contentPage && contentPage->layout()) {
            if (auto *box = qobject_cast<QVBoxLayout *>(contentPage->layout())) {
                const int histIx = box->indexOf(d->history.histGroupBox);
                if (histIx >= 0)
                    box->setStretch(histIx, 0);
            }
        }
    }
}

} // namespace

void ComfyUIRemoteDock::syncHistoryPanelWorkspaceVisibility()
{
    QWidget *contentPage = nullptr;
    if (m_d->history.histGroupBox)
        contentPage = m_d->history.histGroupBox->parentWidget();
    if (!contentPage && m_d->progressBar)
        contentPage = m_d->progressBar->parentWidget();
    applyHistoryPanelWorkspaceVisibility(m_d.data(), contentPage);
}

void ComfyUIRemoteDock::syncCompactGenerateLayoutRows(bool compactGenerate)
{
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : 0;
    const bool upscaleWs = (ws == 1);
    const bool liveWs = (ws == 2);
    const bool generateWs = (ws == 0);
    const bool compactMode = upscaleWs || compactGenerate || liveWs;

    if (compactMode)
        syncWorkspaceTopRowLayout(m_d.data());

    auto syncLayout = [compactMode, upscaleWs, liveWs, compactGenerate](QLayout *layout, const auto &isEssential) {
        if (!layout)
            return;
        for (int i = 0; i < layout->count(); ++i) {
            QLayoutItem *item = layout->itemAt(i);
            if (!item)
                continue;
            QWidget *widget = item->widget();
            if (!widget)
                continue;
            if (upscaleWs || compactGenerate || liveWs) {
                if (isEssential(widget))
                    restoreCompactLayoutRow(widget);
                else
                    collapseCompactLayoutRow(widget);
            } else {
                restoreCompactLayoutRow(widget);
            }
        }
    };

    if (m_d->generate.genContentContainer) {
        syncLayout(m_d->generate.genContentContainer->layout(), [this, upscaleWs, liveWs, generateWs](QWidget *w) {
            if (upscaleWs) {
                return w == m_d->upscale.upscaleFactorRow || w == m_d->upscale.upscaleRefineBlock
                       || w == m_d->upscale.upscaleActionRowWidget || w == m_d->progressBar;
            }
            if (liveWs) {
                return w == m_d->live.liveParamsRowWidget || w == m_d->live.livePromptRowWidget;
            }
            if (generateWs) {
                return w == m_d->generate.regionPromptWidget || w == m_d->inpaint.strengthRowWidget
                       || w == m_d->generate.generateActionRowWidget || w == m_d->progressBar;
            }
            return false;
        });
        if (upscaleWs) {
            if (m_d->upscale.upscaleFactorRow)
                m_d->upscale.upscaleFactorRow->show();
            if (m_d->upscale.upscaleRefineBlock)
                m_d->upscale.upscaleRefineBlock->show();
            if (m_d->upscale.upscaleActionRowWidget)
                m_d->upscale.upscaleActionRowWidget->show();
            if (m_d->progressBar)
                m_d->progressBar->show();
        } else if (liveWs) {
            if (m_d->live.liveParamsRowWidget)
                m_d->live.liveParamsRowWidget->show();
            if (m_d->live.livePromptRowWidget)
                m_d->live.livePromptRowWidget->show();
            if (m_d->generate.regionPromptWidget && m_d->live.livePromptHostWidget
                && m_d->generate.regionPromptWidget->parentWidget() == m_d->live.livePromptHostWidget) {
                m_d->generate.regionPromptWidget->show();
            }
        } else if (generateWs) {
            if (m_d->generate.regionPromptWidget)
                m_d->generate.regionPromptWidget->show();
            if (m_d->inpaint.strengthRowWidget)
                m_d->inpaint.strengthRowWidget->show();
            if (m_d->generate.generateActionRowWidget)
                m_d->generate.generateActionRowWidget->show();
            if (m_d->progressBar)
                m_d->progressBar->show();
        }
        m_d->generate.genContentContainer->updateGeometry();
    }

    if (m_d->generate.genContentContainer && m_d->generate.regionPromptWidget) {
        if (auto *genLay = qobject_cast<QVBoxLayout *>(m_d->generate.genContentContainer->layout())) {
            const int rpIx = genLay->indexOf(m_d->generate.regionPromptWidget);
            if (generateWs) {
                if (rpIx < 0) {
                    restoreCompactLayoutRow(m_d->generate.regionPromptWidget);
                    genLay->insertWidget(0, m_d->generate.regionPromptWidget);
                }
            } else if (upscaleWs && rpIx >= 0) {
                genLay->removeWidget(m_d->generate.regionPromptWidget);
                collapseCompactLayoutRow(m_d->generate.regionPromptWidget);
            }
        }
    }

    if (compactMode && m_d->generate.genGroupBox) {
        if (QLayout *genLay = m_d->generate.genGroupBox->layout()) {
            syncLayout(genLay, [this](QWidget *w) {
                return w == m_d->generate.genContentContainer;
            });
        }
        if (m_d->graphPlaceholderWidget)
            collapseCompactLayoutRow(m_d->graphPlaceholderWidget);
    }

    if (generateWs || liveWs)
        reapplyRegionPromptCompactLayout(m_d.data());

    QWidget *contentPage = nullptr;
    if (m_d->history.histGroupBox)
        contentPage = m_d->history.histGroupBox->parentWidget();
    if (!contentPage && m_d->progressBar)
        contentPage = m_d->progressBar->parentWidget();
    if (contentPage) {
        applyHistoryPanelWorkspaceVisibility(m_d.data(), contentPage);
        if (compactMode) {
            clearCompactFixedHeight(m_d->generate.genContentContainer);
            clearCompactFixedHeight(m_d->generate.genGroupBox);
            if (m_d->live.livePreviewGroupBox && !liveWs) {
                if (contentPage->layout())
                    contentPage->layout()->removeWidget(m_d->live.livePreviewGroupBox);
                m_d->live.livePreviewGroupBox->setParent(m_d->generate.genGroupBox);
                m_d->live.livePreviewGroupBox->hide();
                collapseCompactLayoutRow(m_d->live.livePreviewGroupBox);
            } else if (m_d->live.livePreviewGroupBox && liveWs) {
                restoreLivePreviewPanelLayout(m_d.data(), contentPage);
            }
            if (m_d->labelStatus) {
                if (contentPage->layout())
                    contentPage->layout()->removeWidget(m_d->labelStatus);
                m_d->labelStatus->setParent(contentPage);
                m_d->labelStatus->hide();
                m_d->labelStatus->setFixedHeight(0);
            }
            const int contentWidth = compactGenerateContentWidth(nullptr, contentPage);
            const int chromeH =
                ComfyUiLayoutDiagnostics::measureEssentialGenerateChromeHeight(m_d.data(), contentWidth);
            const int contentH =
                ComfyUiLayoutDiagnostics::measureEssentialGenerateContentHeight(m_d.data(), contentWidth);
            if (liveWs) {
                qCWarning(KIS_COMFYUI_REMOTE).noquote()
                    << QStringLiteral("COMFY_UI_DIAG liveLayout sync reason=syncCompactGenerateLayoutRows marker=")
                    << ComfyUiLayoutDiagnostics::kBuildMarker << QStringLiteral("chromeH=") << chromeH
                    << QStringLiteral("contentH=") << contentH
                    << QStringLiteral("paramsH=")
                    << (m_d->live.liveParamsRowWidget ? m_d->live.liveParamsRowWidget->height() : -1)
                    << QStringLiteral("promptRowH=")
                    << (m_d->live.livePromptRowWidget ? m_d->live.livePromptRowWidget->height() : -1)
                    << QStringLiteral("regionPromptH=")
                    << (m_d->generate.regionPromptWidget ? m_d->generate.regionPromptWidget->height() : -1)
                    << QStringLiteral("previewParent=")
                    << (m_d->live.livePreviewGroupBox && m_d->live.livePreviewGroupBox->parentWidget()
                            ? m_d->live.livePreviewGroupBox->parentWidget()->metaObject()->className()
                            : QStringLiteral("null"))
                    << QStringLiteral("previewH=")
                    << (m_d->live.livePreviewGroupBox ? m_d->live.livePreviewGroupBox->height() : -1);
            }
            if (contentH > 0 && m_d->generate.genContentContainer) {
                if (liveWs) {
                    m_d->generate.genContentContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                    m_d->generate.genContentContainer->setMinimumHeight(contentH);
                    m_d->generate.genContentContainer->setMaximumHeight(QWIDGETSIZE_MAX);
                } else {
                    m_d->generate.genContentContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
                    m_d->generate.genContentContainer->setMinimumHeight(0);
                    m_d->generate.genContentContainer->setMaximumHeight(contentH);
                }
            }
            if (chromeH > 0 && m_d->generate.genGroupBox) {
                if (liveWs) {
                    m_d->generate.genGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                    m_d->generate.genGroupBox->setMinimumHeight(chromeH);
                    m_d->generate.genGroupBox->setMaximumHeight(QWIDGETSIZE_MAX);
                } else {
                    m_d->generate.genGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
                    m_d->generate.genGroupBox->setMaximumHeight(chromeH);
                    m_d->generate.genGroupBox->setMinimumHeight(0);
                }
            }
            if (QLayout *pageLay = contentPage->layout()) {
                if (auto *box = qobject_cast<QVBoxLayout *>(pageLay)) {
                    if (m_d->generate.genGroupBox) {
                        const int genIx = box->indexOf(m_d->generate.genGroupBox);
                        if (genIx >= 0)
                            box->setStretch(genIx, liveWs ? 1 : 0);
                    }
                    if (liveWs && m_d->live.livePreviewGroupBox) {
                        const int previewIx = box->indexOf(m_d->live.livePreviewGroupBox);
                        if (previewIx >= 0)
                            box->removeWidget(m_d->live.livePreviewGroupBox);
                    }
                }
            }
        }
        for (QObject *child : contentPage->children()) {
            auto *scroll = qobject_cast<QScrollArea *>(child);
            if (!scroll || !scroll->widget())
                continue;
            syncLayout(scroll->widget()->layout(), [this](QWidget *w) {
                return m_d->generate.genGroupBox && w == m_d->generate.genGroupBox;
            });
            scroll->widget()->updateGeometry();
            scroll->widget()->adjustSize();
            if (upscaleWs || liveWs) {
                scroll->setWidgetResizable(true);
                scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
                scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                const int scrollH = ComfyUiLayoutDiagnostics::measureCompactGenerateScrollHeight(m_d.data(), scroll);
                scroll->setMinimumHeight(scrollH);
                scroll->setMaximumHeight(scrollH);
                scroll->setFixedHeight(scrollH);
            } else {
                scroll->setWidgetResizable(true);
                scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
                scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                scroll->setMinimumHeight(0);
                scroll->setMaximumHeight(QWIDGETSIZE_MAX);
                scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            }
            scroll->updateGeometry();
        }
        contentPage->updateGeometry();
        if (QLayout *lay = contentPage->layout()) {
            lay->activate();
            if (auto *box = qobject_cast<QVBoxLayout *>(lay))
                box->setAlignment(Qt::AlignTop);
        }
        ComfyUiLayoutDiagnostics::logGenerateHistoryLayout(m_d.data(), "syncCompactGenerateLayoutRows");
        QWidget *dockerRoot = widget();
        ComfyUiLayoutDiagnostics::logWorkspaceChromeLayout(m_d.data(), dockerRoot, "syncCompactGenerateLayoutRows");
        if (liveWs)
            ComfyUiLayoutDiagnostics::logLiveWorkspaceLayout(m_d.data(), dockerRoot, "syncCompactGenerateLayoutRows");
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG syncCompactGenerateLayoutRows compact=") << compactGenerate
        << QStringLiteral("genContentHint=")
        << (m_d->generate.genContentContainer ? m_d->generate.genContentContainer->sizeHint() : QSize())
        << QStringLiteral("progressH=") << (m_d->progressBar ? m_d->progressBar->height() : -1);
}

void ComfyUIRemoteDock::applyInterfaceAppearanceSettings()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const bool liveWs = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
    const bool showNeg = s.value(QStringLiteral("show_negative_prompt")).toBool(false);
    if (m_d->generate.negativePromptBlock)
        m_d->generate.negativePromptBlock->setVisible(false);
    if (m_d->generate.rootPromptColumnWidget)
        m_d->generate.rootPromptColumnWidget->setVisible(false);
    const bool onGenerate = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0;
    const bool onUpscale = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 1;
    if (m_d->generate.regionPromptWidget) {
        m_d->generate.regionPromptWidget->setPromptHeaderMode(2);
        m_d->generate.regionPromptWidget->setShowNegativePrompt(showNeg);
        if (!(onGenerate || liveWs))
            collapseCompactLayoutRow(m_d->generate.regionPromptWidget);
    }
    if (m_d->generate.regionControlLayersGroupBox)
        m_d->generate.regionControlLayersGroupBox->setVisible(false);
    if (m_d->history.historyButtonsRowWidget)
        m_d->history.historyButtonsRowWidget->setVisible(false);
    if (m_d->labelStatus && onGenerate)
        m_d->labelStatus->setVisible(false);
    // FAITHFUL_PORT: forcibly hide every advanced row that upstream krita-ai-diffusion
    // does not expose on the main docker. We deliberately ignore any stale value
    // already in settings.json (older builds defaulted `show_steps` to true and
    // saved it on first Settings → OK, which leaked through to the compact view).
    // If/when a "Power user view" toggle ships, swap these back to settings reads.
    if (m_d->generate.stepsParametersWidget) m_d->generate.stepsParametersWidget->setVisible(false);
    if (m_d->generate.seedRowWidget) m_d->generate.seedRowWidget->setVisible(false);
    if (m_d->generate.sizeRowWidget) m_d->generate.sizeRowWidget->setVisible(false);
    if (m_d->inpaint.btnInpaint) m_d->inpaint.btnInpaint->setVisible(false);
    if (m_d->inpaint.comboInpaintMode) m_d->inpaint.comboInpaintMode->setVisible(false);
    if (m_d->inpaint.comboFillMode) m_d->inpaint.comboFillMode->setVisible(false);
    if (m_d->inpaint.comboInpaintContext) m_d->inpaint.comboInpaintContext->setVisible(false);
    if (m_d->inpaint.checkInpaintUseModel) m_d->inpaint.checkInpaintUseModel->setVisible(false);
    if (m_d->inpaint.checkInpaintUsePromptFocus) m_d->inpaint.checkInpaintUsePromptFocus->setVisible(false);
    if (m_d->generate.checkRegionOnly) m_d->generate.checkRegionOnly->setVisible(false);
    if (m_d->generate.checkEditMode) m_d->generate.checkEditMode->setVisible(false);
    if (m_d->generate.btnRefreshSamplers) m_d->generate.btnRefreshSamplers->setVisible(false);
    if (m_d->generate.btnRefreshCheckpoints) m_d->generate.btnRefreshCheckpoints->setVisible(false);
    // FAITHFUL_PORT: "Control preprocessor preview" groupbox is not part of the
    // upstream docker; advanced control-layer preview lives under Settings.
    if (m_d->generate.controlPreviewGroupBox) m_d->generate.controlPreviewGroupBox->setVisible(false);
    // FAITHFUL_PORT: the "Header:" combo above the region prompt widget is debug
    // chrome (Full/Icon/None) — upstream doesn't expose it. Hide along with the
    // verbose region heading label so the regions block reads cleanly.
    if (m_d->generate.regionHeaderCombo) m_d->generate.regionHeaderCombo->setVisible(false);
    if (m_d->generate.regionHeaderLabel) m_d->generate.regionHeaderLabel->setVisible(false);
    if (m_d->generate.regionButtonsRowWidget) m_d->generate.regionButtonsRowWidget->setVisible(false);
    if (m_d->inpaint.labelPrompt) m_d->inpaint.labelPrompt->setVisible(false);
    // FAITHFUL_PORT: the root "Control layers" groupbox (with "Add control layer"
    // button) is an advanced control-net workflow; upstream surfaces it via the
    // strength row's "Add control layer" icon, not a full groupbox on the home.
    if (m_d->generate.controlLayersGroupBox) m_d->generate.controlLayersGroupBox->setVisible(false);
    // FAITHFUL_PORT: hide Seamless/Focus row wrappers so their orphan labels
    // disappear alongside the inpaint switches in compact view.
    if (m_d->inpaint.seamlessRowWidget) m_d->inpaint.seamlessRowWidget->setVisible(false);
    if (m_d->inpaint.focusRowWidget) m_d->inpaint.focusRowWidget->setVisible(false);
    // animFramesRowWidget visibility is owned by the workspace lambda — only the
    // Animation workspace shows it, every other workspace hides it.
    syncHistoryPanelWorkspaceVisibility();
    syncCompactGenerateLayoutRows(onGenerate || onUpscale || liveWs);
    resetProgressBarToIdle();
    dumpUiLayoutDiagnostics("applyInterfaceAppearanceSettings");
}
void ComfyUIRemoteDock::updateNegativePromptAlertVisibility()
{
    if (!m_d->generate.comboPreset)
        return;
    const int index = m_d->generate.comboPreset->currentIndex();
    const int firstCustom = firstCustomPresetIndex();
    bool showAlert = false;
    if (index == 0) {
        showAlert = true;  // "None" — no style selected, may not use negative prompt
    } else if (index < firstCustom) {
        const bool showBuiltin =
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
        const QList<const ComfyStyleEntry *> styles = ComfyStyleCollection::instance().filtered(showBuiltin);
        const int styleIdx = index - 1;
        if (styleIdx >= 0 && styleIdx < styles.size())
            showAlert = !styles.at(styleIdx)->usesNegativePrompt();
    } else if (index >= firstCustom) {
        QString name = m_d->generate.comboPreset->itemText(index);
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
        showAlert = !cfg.readEntry("UsesNegativePrompt", true);
    }
    if (m_d->generate.labelNegativePromptAlert)
        m_d->generate.labelNegativePromptAlert->setVisible(false);
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->setNegativePromptWarningVisible(showAlert);
}
int ComfyUIRemoteDock::legacyKConfigPresetCount() const
{
    return KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote")).readEntry(QStringLiteral("PresetNames"), QStringList()).size();
}
bool ComfyUIRemoteDock::isCurrentJsonStyleBuiltin() const
{
    const ComfyStyleEntry *st = currentJsonStyleEntry();
    return !st || st->isBuiltin;
}
bool ComfyUIRemoteDock::saveStyleEntry(const ComfyStyleEntry &entry, bool rebuildCombo, bool applyToDock)
{
    if (entry.styleId.isEmpty())
        return false;
    if (ComfyStyleCollection::instance().saveEntryToUserStyles(entry).isEmpty())
        return false;
    if (rebuildCombo)
        rebuildPresetComboItems();
    else
        updateStyleComboItemLabel(entry.styleId);
    if (applyToDock) {
        if (const ComfyStyleEntry *saved = ComfyStyleCollection::instance().findByStyleId(entry.styleId))
            applyComfyStyleEntry(*saved);
    }
    return true;
}
void ComfyUIRemoteDock::updateStyleComboItemLabel(const QString &styleId)
{
    if (!m_d->generate.comboPreset || styleId.isEmpty())
        return;
    for (int i = 1; i < m_d->generate.comboPreset->count(); ++i) {
        if (m_d->generate.comboPreset->itemData(i).toString() == styleId) {
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                m_d->generate.comboPreset->setItemText(i, ComfyStyleCollection::comboPresetName(*st));
            break;
        }
    }
}
bool ComfyUIRemoteDock::createJsonStyle(const QString &checkpoint, const QString &copyFromStyleId)
{
    const QString styleId = ComfyStyleCollection::instance().createStyle(checkpoint, copyFromStyleId);
    if (styleId.isEmpty())
        return false;
    rebuildPresetComboItems();
    applyStyleIdToPresetCombo(m_d->generate.comboPreset, styleId);
    if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
        applyComfyStyleEntry(*st);
    persistDocumentDefaultsToSettings();
    return true;
}
bool ComfyUIRemoteDock::duplicateJsonStyle()
{
    if (!m_d->generate.comboPreset)
        return false;
    const QString from = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
    if (from.isEmpty() || from == QLatin1String("none"))
        return false;
    QString ckpt;
    if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(from)) {
        if (!st->checkpoints.isEmpty())
            ckpt = st->checkpoints.first();
    }
    if (ckpt.isEmpty() && m_d->generate.comboCheckpoint)
        ckpt = m_d->generate.comboCheckpoint->currentText().trimmed();
    return createJsonStyle(ckpt, from);
}
int ComfyUIRemoteDock::firstCustomPresetIndex() const
{
    const bool showBuiltin = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
    return 1 + ComfyStyleCollection::instance().filtered(showBuiltin).size();
}
void ComfyUIRemoteDock::applyComfyStyleEntry(const ComfyStyleEntry &style)
{
    if (!m_d->generate.editPrompt || !m_d->generate.editNegative)
        return;
    if (!style.stylePrompt.contains(QStringLiteral("{prompt}")))
        m_d->generate.editPrompt->setPlainText(style.stylePrompt);
    m_d->generate.editNegative->setPlainText(style.negativePrompt);
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->refreshRootPromptFromDock();

    if (style.preferredResolution > 0) {
        const int side = style.preferredResolution;
        m_d->generate.spinWidth->setValue(side);
        m_d->generate.spinHeight->setValue(side);
    }

    if (m_d->generate.comboCheckpoint && !style.checkpoints.isEmpty()) {
        QStringList available;
        for (int i = 0; i < m_d->generate.comboCheckpoint->count(); ++i)
            available.append(m_d->generate.comboCheckpoint->itemText(i));
        QString ckpt = ComfyFileLibrary::preferredCheckpoint(style.checkpoints, available);
        if (ckpt == QLatin1String("not-found"))
            ckpt = style.checkpoints.first();
        int ix = m_d->generate.comboCheckpoint->findText(ckpt);
        if (ix >= 0)
            m_d->generate.comboCheckpoint->setCurrentIndex(ix);
        else
            m_d->generate.comboCheckpoint->setCurrentText(ckpt);
    }

    const QJsonObject samplerRoot = ComfyUIUtils::builtinSamplerPresetsRoot();
    QString sam, sch;
    int steps = style.samplerSteps;
    int minSteps = 1;
    double cfg = style.cfgScale;
    if (!style.samplerPresetName.isEmpty()
        && ComfyUIUtils::samplerPresetLookup(samplerRoot, style.samplerPresetName, &sam, &sch, &steps, &minSteps, &cfg)) {
        m_d->generate.spinSteps->setValue(qMax(steps, minSteps));
        m_d->generate.spinCfg->setValue(cfg);
        if (m_d->generate.comboSampler) {
            const int si = m_d->generate.comboSampler->findText(sam);
            if (si >= 0)
                m_d->generate.comboSampler->setCurrentIndex(si);
            else
                m_d->generate.comboSampler->setCurrentText(sam);
        }
        m_d->generateRt.ksamplerScheduler = sch;
    } else {
        m_d->generate.spinSteps->setValue(style.samplerSteps);
        m_d->generate.spinCfg->setValue(style.cfgScale);
    }

    if (m_d->generate.layerCountRow) {
        const QString arch = style.architecture.toLower();
        const bool qwenLayered = arch.contains(QLatin1String("qwen")) && arch.contains(QLatin1String("layered"));
        m_d->generate.layerCountRow->setVisible(qwenLayered);
    }

    m_d->generateRt.generateStyleVae = style.vae;
    m_d->generateRt.generateStyleClipSkip = style.clipSkip;
    const QString ckpt = style.checkpoints.isEmpty() ? QString() : style.checkpoints.first();
    m_d->generateRt.generateStyleArch =
        ComfyResources::archFromKey(style.architecture);
    if (m_d->generateRt.generateStyleArch == ComfyResources::Arch::Unknown && !ckpt.isEmpty())
        m_d->generateRt.generateStyleArch = ComfyResources::archFromCheckpointName(ckpt);
}
void ComfyUIRemoteDock::rebuildPresetComboItems()
{
    if (!m_d->generate.comboPreset) return;
    ComfyUIUtils::ensureBundledPluginDataInstalled();
    ComfyStyleCollection::instance().reload();
    const QString prevStyleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
    m_d->generate.comboPreset->blockSignals(true);
    m_d->generate.comboPreset->clear();
    m_d->generate.comboPreset->addItem(ComfyTr::tr("None"));
    m_d->generate.comboPreset->setItemData(0, QStringLiteral("none"));
    m_d->generate.comboPreset->setItemIcon(0, ComfyTheme::icon(QStringLiteral("file-json")));
    const bool showBuiltin = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
    const QList<const ComfyStyleEntry *> styles = ComfyStyleCollection::instance().filtered(showBuiltin);
    for (const ComfyStyleEntry *s : styles) {
        m_d->generate.comboPreset->addItem(ComfyStyleCollection::comboPresetName(*s));
        const int idx = m_d->generate.comboPreset->count() - 1;
        m_d->generate.comboPreset->setItemData(idx, s->styleId);
        const QString ckpt = s->checkpoints.isEmpty() ? QString() : s->checkpoints.first();
        m_d->generate.comboPreset->setItemIcon(idx, ComfyTheme::checkpointIconForArchitectureKey(s->architecture, ckpt));
    }
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    const QStringList customNames = cfg.readEntry("PresetNames", QStringList());
    for (const QString &name : customNames) {
        if (name.isEmpty())
            continue;
        m_d->generate.comboPreset->addItem(name);
        const int idx = m_d->generate.comboPreset->count() - 1;
        m_d->generate.comboPreset->setItemData(idx, QString(QStringLiteral("custom:") + name));
    }
    // Determine which style id to restore. On initial build the combo was empty
    // and prevStyleId comes back as "none"; consult document_defaults.style in
    // settings.json so the user's last selection survives Krita restarts.
    QString idToApply = prevStyleId;
    if (idToApply.isEmpty() || idToApply == QLatin1String("none")) {
        const QJsonObject dd =
            ComfyUIUtils::documentDefaultsFromSettingsRoot(ComfyUIUtils::loadSettingsJson());
        const QString saved = dd.value(QStringLiteral("style")).toString().trimmed();
        if (!saved.isEmpty() && saved != QLatin1String("none"))
            idToApply = saved;
    }
    int restoreIdx = 0;
    if (!idToApply.isEmpty() && idToApply != QLatin1String("none"))
        applyStyleIdToPresetCombo(m_d->generate.comboPreset, idToApply);
    restoreIdx = m_d->generate.comboPreset->currentIndex();
    if (restoreIdx < 0)
        restoreIdx = 0;
    m_d->generate.comboPreset->setCurrentIndex(restoreIdx);
    m_d->generate.comboPreset->blockSignals(false);
    ComfyTheme::applyToolbarComboStyle(m_d->generate.comboPreset);
    if (m_d->generate.btnDeletePreset)
        m_d->generate.btnDeletePreset->setEnabled(m_d->generate.comboPreset->currentIndex() >= firstCustomPresetIndex());
    updateNegativePromptAlertVisibility();
    syncUpscaleRefinementModelFromPresetCombo();
    persistDocumentDefaultsToSettings();  // §13.194
}
void ComfyUIRemoteDock::syncUpscaleRefinementModelFromPresetCombo()
{
    if (!m_d->upscale.comboUpscaleRefinementModel || !m_d->generate.comboPreset)
        return;
    const QString prevText = m_d->upscale.comboUpscaleRefinementModel->currentText();
    m_d->upscale.comboUpscaleRefinementModel->blockSignals(true);
    m_d->upscale.comboUpscaleRefinementModel->clear();
    for (int i = 0; i < m_d->generate.comboPreset->count(); ++i)
        m_d->upscale.comboUpscaleRefinementModel->addItem(m_d->generate.comboPreset->itemText(i));
    KConfigGroup ucfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const int restore = ucfg.readEntry(QStringLiteral("UpscaleRefinementModelIndex"), -1);
    if (restore >= 0 && restore < m_d->upscale.comboUpscaleRefinementModel->count())
        m_d->upscale.comboUpscaleRefinementModel->setCurrentIndex(restore);
    else if (!prevText.isEmpty()) {
        const int fi = m_d->upscale.comboUpscaleRefinementModel->findText(prevText);
        if (fi >= 0)
            m_d->upscale.comboUpscaleRefinementModel->setCurrentIndex(fi);
        else
            m_d->upscale.comboUpscaleRefinementModel->setCurrentIndex(0);
    } else {
        m_d->upscale.comboUpscaleRefinementModel->setCurrentIndex(0);
    }
    m_d->upscale.comboUpscaleRefinementModel->blockSignals(false);
}
bool ComfyUIRemoteDock::renameCustomPreset(const QString &oldName, const QString &newName)
{
    const QString o = oldName.trimmed();
    const QString n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n || !m_d->generate.comboPreset)
        return false;

    KSharedConfig::Ptr cfgPtr = KSharedConfig::openConfig();
    KConfigGroup mainGrp(cfgPtr, QStringLiteral("ComfyUIRemote"));
    QStringList names = mainGrp.readEntry(QStringLiteral("PresetNames"), QStringList());
    const int nameIdx = names.indexOf(o);
    if (nameIdx < 0 || names.contains(n))
        return false;

    KConfigGroup fromGrp(cfgPtr, QStringLiteral("ComfyUIRemote_Preset_") + o);
    if (!fromGrp.exists())
        return false;
    KConfigGroup toGrp(cfgPtr, QStringLiteral("ComfyUIRemote_Preset_") + n);
    if (toGrp.exists())
        return false;

    const QMap<QString, QString> em = fromGrp.entryMap();
    for (auto it = em.constBegin(); it != em.constEnd(); ++it)
        toGrp.writeEntry(it.key(), it.value());

    fromGrp.deleteGroup();
    names[nameIdx] = n;
    mainGrp.writeEntry(QStringLiteral("PresetNames"), names);
    cfgPtr->sync();

    rebuildPresetComboItems();
    const int ni = m_d->generate.comboPreset->findText(n);
    if (ni >= 0)
        m_d->generate.comboPreset->setCurrentIndex(ni);
    return true;
}
void ComfyUIRemoteDock::applyQualitySamplerPresetFromSettings()
{
    QString key;
    if (const ComfyStyleEntry *st = currentJsonStyleEntry())
        key = st->samplerPresetName;
    if (key.isEmpty())
        key = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("quality_sampler_preset")).toString().trimmed();
    applyQualitySamplerPresetKey(key);
}
void ComfyUIRemoteDock::applyQualitySamplerPresetKey(const QString &presetName)
{
    const QString key = presetName.trimmed();
    if (key.isEmpty()) {
        m_d->generateRt.ksamplerScheduler = QStringLiteral("normal");
        return;
    }
    QString sampler, scheduler;
    int steps = 20;
    int minSteps = 1;
    double cfg = 8.0;
    const QJsonObject root = ComfyUIUtils::builtinSamplerPresetsRoot();
    if (!ComfyUIUtils::samplerPresetLookup(root, key, &sampler, &scheduler, &steps, &minSteps, &cfg)) {
        m_d->generateRt.ksamplerScheduler = QStringLiteral("normal");
        return;
    }
    if (m_d->generate.comboSampler)
        m_d->generate.comboSampler->setCurrentText(sampler);
    if (m_d->generate.spinSteps)
        m_d->generate.spinSteps->setValue(qMax(steps, minSteps));
    if (m_d->generate.spinCfg)
        m_d->generate.spinCfg->setValue(cfg);
    m_d->generateRt.ksamplerScheduler = scheduler;
}
void ComfyUIRemoteDock::refreshStylesTabLoraWarning()
{
    ComfyStyleLoraListWidget *list = m_d->stylesTabLoraListWidget.data();
    if (!list)
        return;
    list->setServerLoraFilenames(m_d->comfyServerLoraFilenames);
}
void ComfyUIRemoteDock::applyStylesTabLoraListFilter()
{
    ComfyStyleLoraListWidget *list = m_d->stylesTabLoraListWidget.data();
    if (!list)
        return;
    list->refreshFilters();
}

const ComfyStyleEntry *ComfyUIRemoteDock::currentJsonStyleEntry() const
{
    if (!m_d->generate.comboPreset)
        return nullptr;
    const int idx = m_d->generate.comboPreset->currentIndex();
    if (idx <= 0 || idx >= firstCustomPresetIndex())
        return nullptr;
    return ComfyStyleCollection::instance().findByStyleId(encodeStyleIdFromPresetCombo(m_d->generate.comboPreset));
}

QJsonArray ComfyUIRemoteDock::currentStyleLoras() const
{
    if (!m_d->generate.comboPreset || m_d->generate.comboPreset->currentIndex() <= 0)
        return {};
    const QString styleId = encodeStyleIdFromPresetCombo(m_d->generate.comboPreset);
    if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
        return st->loras;
    return {};
}

QJsonArray ComfyUIRemoteDock::currentStyleLorasForLive() const
{
    const QJsonArray styleLoras = currentStyleLoras();
    const ComfyStyleEntry *style = currentJsonStyleEntry();
    const QString preset =
        ComfyUIUtils::liveSamplerPresetName(style, ComfyUIUtils::loadSettingsJson());
    if (preset.isEmpty())
        return styleLoras;
    const QString ckpt = checkpointForGenerate();
    const QString styleArch = style ? style->architecture : QString();
    const ComfyResources::Arch arch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
    const ComfyUIUtils::SamplerPresetLoraResult resolved =
        ComfyUIUtils::resolveSamplerPresetLora(
            preset,
            arch,
            ComfyUIUtils::mergedServerLoraFilenames(m_d->comfyServerLoraFilenames));
    if (!resolved.ok || resolved.loraFilename.isEmpty())
        return styleLoras;
    return ComfyUIUtils::appendSamplerPresetLora(styleLoras, resolved.loraFilename);
}
