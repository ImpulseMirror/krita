/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfySettingsDialogBuilder.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfySwitchWidget.h"
#include "ComfyStyleLoraListWidget.h"
#include "ComfyUiStyle.h"

#include <QCursor>
#include <QDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QAbstractItemView>
#include <QListWidgetItem>

#include <KSharedConfig>
#include <KConfigGroup>

void ComfyUIRemoteDock::slotConfigureHelp()
{
    // FAITHFUL_PORT: rebuild the dock-side preset combo BEFORE the Settings
    // dialog is constructed. The Styles tab's stylesPresetMirror is populated
    // from m_d->generate.comboPreset, so if the bundled ComfyStyleCollection hadn't been
    // reloaded yet (e.g. first time opening Settings on Android after a fresh
    // install) the mirror only saw the placeholder "None" entry and the
    // dropdown popup looked empty / non-functional.
    if (m_d->generate.comboPreset)
        rebuildPresetComboItems();
    if (!m_d->settingsDialog) {
        QDialog *dlg = new QDialog(this);
        dlg->setWindowTitle(ComfyTr::tr("Configure Image Diffusion"));
        dlg->setMinimumSize(960, 480);  // §13.197
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen) {
            QSize av = screen->availableSize();
            int minW = qMin(av.width(), dlg->fontMetrics().horizontalAdvance(QLatin1Char('M')) * 100);
            dlg->resize(minW, static_cast<int>(av.height() * 0.8));
        } else {
            dlg->resize(960, 480);
        }
        m_d->settingsDialog = dlg;
        // §13.78: When user dismisses Configure (e.g. after connecting), refresh dock so Welcome vs workspace updates
        connect(dlg, &QDialog::finished, this, [this](int) {
            m_d->stylesTabLoraListWidget = nullptr;
            updateWelcomeVisibility();
            refreshPromptTagCompleter();
            refreshQueueResolutionRowVisibility();
            updateUpscaleTargetSize();
        });

        QVBoxLayout *mainLayout = new QVBoxLayout(dlg);

        // Top-level horizontal layout: left navigation + right stacked content
        QHBoxLayout *contentLayout = new QHBoxLayout();
        mainLayout->addLayout(contentLayout);

        QListWidget *navList = new QListWidget(dlg);
        navList->setFixedWidth(ComfyUiStyle::Spacing::navWidth);
        navList->setSelectionMode(QAbstractItemView::SingleSelection);
        ComfyUiStyle::applySettingsNavList(navList);
        // FAITHFUL_PORT: Plugin tab dropped on Android — the auto-update path and
        // diagnostics buttons there expose host-only flows (URL handlers, intent
        // launchers) that crash or no-op on Android; the docker's About menu plus
        // the footer Ok button cover the remaining info.
        const QStringList navItems = { ComfyTr::tr("Connection"), ComfyTr::tr("Styles"), ComfyTr::tr("Diffusion"), ComfyTr::tr("Interface"), ComfyTr::tr("Performance") };
        for (const QString &label : navItems) {
            QListWidgetItem *item = new QListWidgetItem(label);
            item->setSizeHint(QSize(ComfyUiStyle::Spacing::navWidth - 8, ComfyUiStyle::Spacing::rowHeight));
            navList->addItem(item);
        }
        contentLayout->addWidget(navList);

        QStackedWidget *stack = new QStackedWidget(dlg);
        contentLayout->addWidget(stack, 1);

        ComfySettingsDialogBuilder::Context settingsCtx{this, m_d.data(), dlg};
        ComfySettingsDialogBuilder::buildConnectionTab(settingsCtx, stack);
        ComfySettingsDialogBuilder::StylesTabResult stylesTab =
            ComfySettingsDialogBuilder::buildStylesTab(settingsCtx, stack);

        ComfySettingsDialogBuilder::buildDiffusionTab(settingsCtx, stack);
        ComfySettingsDialogBuilder::buildInterfaceTab(settingsCtx, stack);
        ComfySettingsDialogBuilder::PerformanceTabResult perfTab =
            ComfySettingsDialogBuilder::buildPerformanceTab(settingsCtx, stack);

        // Plugin tab removed on Android (per FAITHFUL_PORT note above). The
        // d-pointer's pluginTabLatestVersionLabel / pluginTabDownloadInstallButton
        // QPointers stay null and syncPluginUpdateUi() guards on each access, so
        // background update polling code paths still compile and run as no-ops.

        // Canonical refresh control for slotRefreshCheckpoints (hidden); Styles tab uses its own refresh button.
        if (m_d->generate.btnRefreshCheckpoints) {
            m_d->generate.btnRefreshCheckpoints->setParent(dlg);
            m_d->generate.btnRefreshCheckpoints->hide();
        }

        // Footer (Restore Defaults, version text, Ok)
        QHBoxLayout *footerLayout = new QHBoxLayout();
        QPushButton *restoreButton = new QPushButton(ComfyTr::tr("Restore Defaults"), dlg);
        restoreButton->setStyleSheet(ComfyUiStyle::restoreDefaultsButtonStyleSheet());
        connect(restoreButton, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRestoreDefaults);
        footerLayout->addWidget(restoreButton);
        QLabel *footerVersion = new QLabel(ComfyTr::tr("Plugin version: %1", ComfyUIUtils::pluginVersion()), dlg);
        footerVersion->setAlignment(Qt::AlignCenter);
        footerVersion->setStyleSheet(ComfyUiStyle::footerVersionStyleSheet());
        footerLayout->addWidget(footerVersion, 1);
        QPushButton *okButton = new QPushButton(ComfyTr::tr("Ok"), dlg);
        ComfyUiStyle::applyPrimaryButton(okButton);
        okButton->setDefault(true);
        okButton->setAutoDefault(true);
        connect(okButton, &QPushButton::clicked, dlg, [dlg](bool) {
            KSharedConfig::openConfig()->sync();
            dlg->accept();
        });
        footerLayout->addWidget(okButton);
        mainLayout->addLayout(footerLayout);

        connect(navList, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);
        connect(navList, &QListWidget::currentRowChanged, this, [this, stylesTab, perfTab](int row) {
            if (row == 0)
                refreshConnectionTabUi();
            if (row == 1)
                if (stylesTab.syncFromDock) stylesTab.syncFromDock();
            if (row == 4) {
                updateHistoryUsageLabel();
                syncPerformanceFromAutoPreset();
                if (perfTab.syncSlidersFromDock)
                    perfTab.syncSlidersFromDock();
            }
        });
        updateHistoryUsageLabel();
        ComfyUiStyle::applyWidgetTree(dlg);
        navList->setCurrentRow(0);

        refreshConnectionTabUi();
        refreshCustomWorkflowParameterPanel();
    }

    if (m_d->settingsDialog) {
        if (m_d->checkConfirmDiscardImage)
            m_d->checkConfirmDiscardImage->setChecked(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ConfirmDiscardImage", true));
        // Only external ComfyUI URLs are supported in this build; normalize legacy modes.
        if (m_d->connectionStack) {
            KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
            if (cfg.readEntry("ServerMode", QStringLiteral("external")) != QLatin1String("external")) {
                cfg.writeEntry("ServerMode", QStringLiteral("external"));
                KSharedConfig::openConfig()->sync();
            }
            m_d->connectionStack->setCurrentIndex(0);
        }
        refreshCustomWorkflowParameterPanel();
        syncPluginUpdateUi();
        refreshConnectionTabUi();
        m_d->settingsDialog->show();
        m_d->settingsDialog->raise();
        m_d->settingsDialog->activateWindow();
    }
}

void ComfyUIRemoteDock::slotRestoreDefaults()
{
    KSharedConfig::Ptr cfgPtr = KSharedConfig::openConfig();
    KConfigGroup cfg(cfgPtr, "ComfyUIRemote");
    cfg.deleteGroup();
    cfgPtr->sync();

    // Reapply default values to UI widgets where applicable (§3.1: persist to settings.json)
    if (m_d->editServerUrl) {
        m_d->editServerUrl->setText(QStringLiteral("127.0.0.1:8188"));
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        settings.insert(QStringLiteral("server_url"), QStringLiteral("127.0.0.1:8188"));
        ComfyUIUtils::saveSettingsJson(settings);
    }
    if (m_d->generate.comboCheckpoint) {
        m_d->generate.comboCheckpoint->setCurrentIndex(0);
    }
    if (m_d->generate.comboPreset) {
        rebuildPresetComboItems();
    }
    if (m_d->generate.comboQuality) {
        m_d->generate.comboQuality->setCurrentIndex(1);
    }
    if (m_d->generate.comboSizePreset) {
        m_d->generate.comboSizePreset->setCurrentIndex(0);
    }
    if (m_d->generate.spinWidth) {
        m_d->generate.spinWidth->setValue(512);
    }
    if (m_d->generate.spinHeight) {
        m_d->generate.spinHeight->setValue(512);
    }
    if (m_d->generate.spinSteps) {
        m_d->generate.spinSteps->setValue(20);
    }
    if (m_d->generate.spinCfg) {
        m_d->generate.spinCfg->setValue(8.0);
    }
    if (m_d->generate.spinStrength) {
        m_d->generate.spinStrength->setValue(100);
    }
    if (m_d->generate.comboSampler) {
        m_d->generate.comboSampler->setCurrentIndex(0);
    }
    if (m_d->comboWorkspace) {
        m_d->comboWorkspace->setCurrentIndex(0);
    }
    if (m_d->generate.comboQueueMode) {
        m_d->generate.comboQueueMode->setCurrentIndex(0);
    }
    if (m_d->generate.spinBatchCount) {
        m_d->generate.spinBatchCount->setValue(1);
    }
    if (m_d->generate.sliderResolutionMultiplier) {
        m_d->generate.resolutionMultiplier = 1.0;
        m_d->generate.sliderResolutionMultiplier->setValue(10);
    }
    if (m_d->generate.labelResolutionMultiplier) {
        m_d->generate.labelResolutionMultiplier->setText(QStringLiteral("1.0×"));
    }

    if (m_d->generate.checkFixedSeed) {
        m_d->generate.checkFixedSeed->setChecked(false);
    }
    if (m_d->generate.spinSeed) {
        m_d->generate.spinSeed->setValue(0);
    }
    if (m_d->checkConfirmDiscardImage) {
        m_d->checkConfirmDiscardImage->setChecked(true);
    }
    // §5.4: Reset Region-only, Edit mode, Layer count
    if (m_d->generate.checkRegionOnly) {
        m_d->generate.checkRegionOnly->setChecked(false);
    }
    if (m_d->generate.checkEditMode) {
        m_d->generate.checkEditMode->setChecked(false);
    }
    if (m_d->generate.spinLayerCount) {
        m_d->generate.spinLayerCount->setValue(1);
    }
    // §5.6, §5.7: Full Animation / Single Frame — restore to Single Frame
    if (m_d->radioSingleFrame) m_d->radioSingleFrame->setChecked(true);
    if (m_d->radioFullAnimation) m_d->radioFullAnimation->setChecked(false);
    cfg.writeEntry("FullAnimation", false);
    cfg.writeEntry(QStringLiteral("InpaintUseModel"), true);
    cfg.writeEntry(QStringLiteral("InpaintUsePromptFocus"), false);
    m_d->inpaintRt.inpaintPersistUseModel = true;
    m_d->inpaintRt.inpaintPersistUsePromptFocus = false;
    if (m_d->inpaint.checkInpaintUseModel)
        m_d->inpaint.checkInpaintUseModel->setChecked(true);
    if (m_d->inpaint.checkInpaintUsePromptFocus)
        m_d->inpaint.checkInpaintUsePromptFocus->setChecked(false);
    updateAnimationButtonLabel();

    // Clear history and regions, then refresh lists and persist
    m_d->history.historyEntries.clear();
    m_d->history.pendingHistoryByPromptId.clear();
    refreshHistoryList();

    m_d->regionEntries.clear();
    m_d->editRegionEntries.clear();
    saveRegionsToConfig();
    refreshRegionsList();

    cfg.writeEntry(QStringLiteral("ServerMode"), QStringLiteral("external"));
    cfgPtr->sync();
    if (m_d->connectionStack) {
        m_d->connectionStack->setCurrentIndex(0);
    }
    syncPluginUpdateUi();
}
