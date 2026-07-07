/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilder.h"
#include "ComfyFormUi.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"
#include "ComfyStyleCollection.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfySwitchWidget.h"
#include "ComfyStyleLoraListWidget.h"
#include "ComfyStyleSamplerWidget.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QAbstractSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QDesktopServices>
#include <QVBoxLayout>

#include <KSharedConfig>
#include <KConfigGroup>

#include <functional>

namespace ComfySettingsDialogBuilder {

PerformanceTabResult buildPerformanceTab(const Context &ctx, QStackedWidget *stack)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    QDialog *dlg = ctx.dialog;
    PerformanceTabResult result;
        ComfyFormUi::ScrollTab perfTab =
            ComfyFormUi::createScrollTab(dlg, ComfyTr::tr("Performance Settings"));
        QWidget *perfInner = perfTab.inner;
        QVBoxLayout *perfLayout = perfTab.innerLayout;

        QJsonObject perfSettings = ComfyUIUtils::loadSettingsJson();
        QSpinBox *spinActiveHistoryMb = nullptr;
        QSpinBox *spinStoredHistoryMb = nullptr;
        perfLayout->addWidget(ComfyFormUi::addHistorySizeBlock(
            perfInner,
            ComfyTr::tr("Active History Size"),
            ComfyTr::tr("Main memory (RAM) used for the history of generated images."),
            &spinActiveHistoryMb,
            &d->labelHistoryUsageMb,
            5,
            20000,
            100));
        {
            int amb = perfSettings.value(QStringLiteral("history_size")).toInt(0);
            if (amb <= 0)
                amb = perfSettings.value(QStringLiteral("history_active_mb")).toInt(0);
            if (amb <= 0)
                amb = perfSettings.value(QStringLiteral("history_storage")).toInt(1000);
            spinActiveHistoryMb->setValue(qBound(5, amb, 20000));
            spinActiveHistoryMb->setToolTip(ComfyTr::tr("Oldest history entries are removed when over dock limit."));
        }
        perfLayout->addWidget(ComfyFormUi::addHistorySizeBlock(
            perfInner,
            ComfyTr::tr("Stored History Size"),
            ComfyTr::tr("Memory used to store generated images in .kra files on disk."),
            &spinStoredHistoryMb,
            &d->labelStoredHistoryMb,
            5,
            2000,
            5));
        {
            int stored = perfSettings.value(QStringLiteral("history_storage")).toInt(0);
            if (stored <= 0)
                stored = perfSettings.value(QStringLiteral("history_document_storage_mb")).toInt(20);
            spinStoredHistoryMb->setValue(qBound(5, stored, 2000));
            spinStoredHistoryMb->setToolTip(ComfyTr::tr("Reserved for document-embedded history quota."));
        }

        QComboBox *comboPerfPreset = nullptr;
        perfLayout->addWidget(ComfyFormUi::addComboRow(
            perfInner,
            ComfyTr::tr("Performance Preset"),
            ComfyTr::tr("Configures performance settings to match available hardware."),
            &comboPerfPreset));
        d->labelPerfDevice = new QLabel(perfInner);
        d->labelPerfDevice->setWordWrap(true);
        ComfyUiStyle::styleHint(d->labelPerfDevice);
        d->labelPerfDevice->setText(d->comfyDeviceSummary.isEmpty()
                                          ? ComfyTr::tr("Device: (connect to server)")
                                          : d->comfyDeviceSummary);
        perfLayout->addWidget(d->labelPerfDevice);
        comboPerfPreset->addItem(ComfyTr::tr("Automatic"), QStringLiteral("auto"));
        comboPerfPreset->addItem(ComfyTr::tr("CPU"), QStringLiteral("cpu"));
        comboPerfPreset->addItem(ComfyTr::tr("GPU low (up to 6GB)"), QStringLiteral("low"));
        comboPerfPreset->addItem(ComfyTr::tr("GPU medium (6GB to 12GB)"), QStringLiteral("medium"));
        comboPerfPreset->addItem(ComfyTr::tr("GPU high (more than 12GB)"), QStringLiteral("high"));
        comboPerfPreset->addItem(ComfyTr::tr("Cloud"), QStringLiteral("cloud"));
        comboPerfPreset->addItem(ComfyTr::tr("Custom"), QStringLiteral("custom"));
        {
            QString pp = perfSettings.value(QStringLiteral("performance_preset")).toString();
            if (pp.isEmpty())
                pp = QStringLiteral("auto");
            const int ppi = comboPerfPreset->findData(pp);
            comboPerfPreset->setCurrentIndex(ppi >= 0 ? ppi : 0);
        }
        comboPerfPreset->setToolTip(ComfyTr::tr("Configures performance settings to match available hardware."));

        QWidget *customPerfWidget = new QWidget(perfInner);
        auto *customPerfLayout = new QVBoxLayout(customPerfWidget);
        customPerfLayout->setContentsMargins(8, 0, 0, 4);
        customPerfLayout->setSpacing(0);

        QAbstractSlider *sliderPerfBatch = nullptr;
        QLabel *labelPerfBatchVal = nullptr;
        {
            const ComfyFormUi::SliderSetting batchRow = ComfyFormUi::addSliderRow(
                customPerfWidget,
                ComfyTr::tr("Maximum Batch Size"),
                ComfyTr::tr("Increase efficiency by generating multiple images at once."),
                1,
                16,
                QStringLiteral("1"));
            sliderPerfBatch = batchRow.qtSlider();
            labelPerfBatchVal = batchRow.valueLabel();
            customPerfLayout->addWidget(batchRow.row);
        }
        sliderPerfBatch->setToolTip(ComfyTr::tr("Increase efficiency by generating multiple images at once."));

        QAbstractSlider *sliderPerfRes = nullptr;
        QLabel *labelPerfResVal = nullptr;
        {
            const ComfyFormUi::SliderSetting resRow = ComfyFormUi::addSliderRow(
                customPerfWidget,
                ComfyTr::tr("Resolution Multiplier"),
                ComfyTr::tr("Scaling factor for generation. Values below 1.0 improve performance for high resolution canvas."),
                3,
                15,
                QStringLiteral("1.0×"));
            sliderPerfRes = resRow.qtSlider();
            labelPerfResVal = resRow.valueLabel();
            customPerfLayout->addWidget(resRow.row);
        }
        sliderPerfRes->setToolTip(
            ComfyTr::tr("Scaling factor for generation. Values below 1.0 improve performance for high resolution canvas."));

        QSpinBox *spinMaxMp = nullptr;
        customPerfLayout->addWidget(ComfyFormUi::addSpinRow(
            customPerfWidget,
            ComfyTr::tr("Maximum Pixel Count"),
            ComfyTr::tr("Maximum resolution to generate images at, in megapixels (FullHD ~ 2MP, 4k ~ 8MP)."),
            &spinMaxMp,
            1,
            99,
            ComfyTr::tr(" MP")));
        {
            int maxMp = perfSettings.value(QStringLiteral("max_pixel_count")).toInt(0);
            if (maxMp <= 0)
                maxMp = perfSettings.value(QStringLiteral("max_pixel_count_mp")).toInt(8);
            spinMaxMp->setValue(qBound(1, maxMp, 99));
        }

        ComfySwitchWidget *switchTiledVae = nullptr;
        QLabel *labelTiledVaeState = nullptr;
        {
            const ComfyFormUi::SwitchSetting tiledRow = ComfyFormUi::addSwitchRow(
                customPerfWidget,
                ComfyTr::tr("Tiled VAE"),
                ComfyTr::tr("Conserve memory by processing output images in smaller tiles."),
                ComfyTr::tr("Always"),
                ComfyTr::tr("Automatic"));
            switchTiledVae = tiledRow.switchWidget;
            labelTiledVaeState = tiledRow.stateLabel;
            customPerfLayout->addWidget(tiledRow.row);
        }
        {
            bool tiledAlways = perfSettings.value(QStringLiteral("tiled_vae")).toBool(false);
            if (!perfSettings.contains(QStringLiteral("tiled_vae"))) {
                QString tvm = perfSettings.value(QStringLiteral("tiled_vae_mode")).toString();
                if (tvm.isEmpty())
                    tvm = perfSettings.value(QStringLiteral("tiled_vae_always")).toBool(false)
                        ? QStringLiteral("always")
                        : QStringLiteral("automatic");
                tiledAlways = (tvm == QLatin1String("always"));
            }
            switchTiledVae->setChecked(tiledAlways);
            labelTiledVaeState->setText(tiledAlways ? ComfyTr::tr("Always") : ComfyTr::tr("Automatic"));
        }

        perfLayout->addWidget(customPerfWidget);

        ComfySwitchWidget *switchDynCache = nullptr;
        QLabel *labelDynCacheState = nullptr;
        {
            const ComfyFormUi::SwitchSetting dynRow = ComfyFormUi::addSwitchRow(
                perfInner,
                ComfyTr::tr("Dynamic Caching"),
                ComfyTr::tr("Re-use outputs of previous steps (First Block Cache) to speed up generation."),
                ComfyTr::tr("On"),
                ComfyTr::tr("Off"));
            switchDynCache = dynRow.switchWidget;
            labelDynCacheState = dynRow.stateLabel;
            perfLayout->addWidget(dynRow.row);
        }
        switchDynCache->setChecked(perfSettings.value(QStringLiteral("dynamic_caching")).toBool(false));
        labelDynCacheState->setText(switchDynCache->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        switchDynCache->setToolTip(
            ComfyTr::tr("Re-use outputs of previous steps (First Block Cache) to speed up generation.\n\n"
                        "When enabled, the dock turns on common enable toggles on workflow nodes whose class names look like "
                        "First Block / Block / FB / Tea cache nodes (if those nodes are already in the graph)."));

        ComfySwitchWidget *switchMultiThread = nullptr;
        QLabel *labelMultiThreadState = nullptr;
        {
            const ComfyFormUi::SwitchSetting threadRow = ComfyFormUi::addSwitchRow(
                perfInner,
                ComfyTr::tr("Multi-Threading"),
                ComfyTr::tr("Perform certain plugin operations in background threads."),
                ComfyTr::tr("On"),
                ComfyTr::tr("Off"));
            switchMultiThread = threadRow.switchWidget;
            labelMultiThreadState = threadRow.stateLabel;
            perfLayout->addWidget(threadRow.row);
        }
        switchMultiThread->setChecked(perfSettings.value(QStringLiteral("multi_threading")).toBool(true));
        labelMultiThreadState->setText(switchMultiThread->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        switchMultiThread->setToolTip(
            ComfyTr::tr("Perform certain plugin operations in background threads.\n\n"
                        "When enabled: Import Workflow reads and strips comments off the UI thread, and large \"Dump workflow\" "
                        "writes to last_comfy_prompt.json are written on a worker thread."));

        auto syncPerfSlidersFromDock = [dock, d, sliderPerfBatch, labelPerfBatchVal, sliderPerfRes, labelPerfResVal, spinMaxMp,
                                        switchTiledVae, labelTiledVaeState, switchDynCache, labelDynCacheState,
                                        switchMultiThread, labelMultiThreadState]() {
            if (d->generate.spinBatchCount) {
                sliderPerfBatch->blockSignals(true);
                sliderPerfBatch->setValue(qBound(1, d->generate.spinBatchCount->value(), 16));
                sliderPerfBatch->blockSignals(false);
                labelPerfBatchVal->setText(QString::number(sliderPerfBatch->value()));
            }
            if (d->generate.sliderResolutionMultiplier) {
                sliderPerfRes->blockSignals(true);
                sliderPerfRes->setValue(d->generate.sliderResolutionMultiplier->value());
                sliderPerfRes->blockSignals(false);
                labelPerfResVal->setText(QString::number(d->generate.resolutionMultiplier, 'f', 1) + QLatin1String("×"));
            }
            const QJsonObject s = ComfyUIUtils::loadSettingsJson();
            spinMaxMp->blockSignals(true);
            int maxMp = s.value(QStringLiteral("max_pixel_count")).toInt(0);
            if (maxMp <= 0)
                maxMp = s.value(QStringLiteral("max_pixel_count_mp")).toInt(8);
            spinMaxMp->setValue(qBound(1, maxMp, 99));
            spinMaxMp->blockSignals(false);
            bool tiledAlways = s.value(QStringLiteral("tiled_vae")).toBool(false);
            if (!s.contains(QStringLiteral("tiled_vae"))) {
                QString tvm = s.value(QStringLiteral("tiled_vae_mode")).toString();
                if (tvm.isEmpty())
                    tvm = s.value(QStringLiteral("tiled_vae_always")).toBool(false) ? QStringLiteral("always")
                                                                                  : QStringLiteral("automatic");
                tiledAlways = (tvm == QLatin1String("always"));
            }
            switchTiledVae->blockSignals(true);
            switchTiledVae->setChecked(tiledAlways);
            switchTiledVae->blockSignals(false);
            labelTiledVaeState->setText(tiledAlways ? ComfyTr::tr("Always") : ComfyTr::tr("Automatic"));
            switchDynCache->blockSignals(true);
            switchDynCache->setChecked(s.value(QStringLiteral("dynamic_caching")).toBool(false));
            switchDynCache->blockSignals(false);
            labelDynCacheState->setText(switchDynCache->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            switchMultiThread->blockSignals(true);
            switchMultiThread->setChecked(s.value(QStringLiteral("multi_threading")).toBool(true));
            switchMultiThread->blockSignals(false);
            labelMultiThreadState->setText(switchMultiThread->isChecked() ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            if (d->labelPerfDevice)
                d->labelPerfDevice->setText(d->comfyDeviceSummary.isEmpty()
                                                  ? ComfyTr::tr("Device: (connect to server)")
                                                  : d->comfyDeviceSummary);
        };
        syncPerfSlidersFromDock();
        auto updateCustomPerfEnabled = [comboPerfPreset, customPerfWidget]() {
            customPerfWidget->setEnabled(comboPerfPreset->currentData().toString() == QLatin1String("custom"));
        };
        updateCustomPerfEnabled();
        auto savePerfSettings = [dock, d, comboPerfPreset, spinActiveHistoryMb, spinStoredHistoryMb, spinMaxMp, switchTiledVae,
                                 switchDynCache, switchMultiThread]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("performance_preset"), comboPerfPreset->currentData().toString());
            const int activeMb = spinActiveHistoryMb->value();
            const int storedMb = spinStoredHistoryMb->value();
            s.insert(QStringLiteral("history_size"), activeMb);
            s.insert(QStringLiteral("history_active_mb"), activeMb);
            s.insert(QStringLiteral("history_storage"), storedMb);
            s.insert(QStringLiteral("history_document_storage_mb"), storedMb);
            const int maxMp = spinMaxMp->value();
            s.insert(QStringLiteral("max_pixel_count"), maxMp);
            s.insert(QStringLiteral("max_pixel_count_mp"), maxMp);
            const bool tiledAlways = switchTiledVae->isChecked();
            s.insert(QStringLiteral("tiled_vae"), tiledAlways);
            const QString tiledMode = tiledAlways ? QStringLiteral("always") : QStringLiteral("automatic");
            s.insert(QStringLiteral("tiled_vae_mode"), tiledMode);
            s.insert(QStringLiteral("tiled_vae_always"), tiledAlways);
            s.insert(QStringLiteral("dynamic_caching"), switchDynCache->isChecked());
            s.insert(QStringLiteral("multi_threading"), switchMultiThread->isChecked());
            s.insert(QStringLiteral("batch_size"), d->generate.spinBatchCount ? d->generate.spinBatchCount->value() : 1);
            s.insert(QStringLiteral("resolution_multiplier"), d->generate.resolutionMultiplier <= 0.0 ? 1.0 : d->generate.resolutionMultiplier);
            ComfyUIUtils::saveSettingsJson(s);
            dock->refreshQueueResolutionRowVisibility();
            dock->updateUpscaleTargetSize();
        };
        QObject::connect(comboPerfPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [savePerfSettings, updateCustomPerfEnabled](int) {
                    updateCustomPerfEnabled();
                    savePerfSettings();
                });
        QObject::connect(comboPerfPreset, QOverload<int>::of(&QComboBox::activated), dock,
                [dock, d, comboPerfPreset, sliderPerfBatch, labelPerfBatchVal, sliderPerfRes, labelPerfResVal, spinMaxMp,
                 switchTiledVae, labelTiledVaeState, savePerfSettings, syncPerfSlidersFromDock](int) {
                    const QString key = comboPerfPreset->currentData().toString();
                    if (key == QLatin1String("custom")) {
                        savePerfSettings();
                        return;
                    }
                    if (key == QLatin1String("auto")) {
                        savePerfSettings();
                        dock->syncPerformanceFromAutoPreset();
                        syncPerfSlidersFromDock();
                        return;
                    }
                    int batch = 4;
                    int maxMp = 8;
                    bool tiledAlways = false;
                    if (key == QLatin1String("cpu")) {
                        batch = 1;
                        maxMp = 2;
                    } else if (key == QLatin1String("low")) {
                        batch = 2;
                        maxMp = 2;
                        tiledAlways = true;
                    } else if (key == QLatin1String("medium")) {
                        batch = 4;
                        maxMp = 6;
                    } else if (key == QLatin1String("high")) {
                        batch = 6;
                        maxMp = 8;
                    } else if (key == QLatin1String("cloud")) {
                        batch = 8;
                        maxMp = 6;
                    } else {
                        savePerfSettings();
                        return;
                    }
                    if (d->generate.spinBatchCount)
                        d->generate.spinBatchCount->setValue(batch);
                    d->generate.resolutionMultiplier = 1.0;
                    if (d->generate.sliderResolutionMultiplier) {
                        d->generate.sliderResolutionMultiplier->blockSignals(true);
                        d->generate.sliderResolutionMultiplier->setValue(10);
                        d->generate.sliderResolutionMultiplier->blockSignals(false);
                    }
                    if (d->generate.labelResolutionMultiplier)
                        d->generate.labelResolutionMultiplier->setText(QStringLiteral("1.0×"));
                    dock->schedulePersistDocumentDefaults();
                    sliderPerfBatch->blockSignals(true);
                    sliderPerfBatch->setValue(qBound(1, batch, 16));
                    sliderPerfBatch->blockSignals(false);
                    labelPerfBatchVal->setText(QString::number(sliderPerfBatch->value()));
                    sliderPerfRes->blockSignals(true);
                    sliderPerfRes->setValue(10);
                    sliderPerfRes->blockSignals(false);
                    labelPerfResVal->setText(QStringLiteral("1.0×"));
                    spinMaxMp->blockSignals(true);
                    spinMaxMp->setValue(maxMp);
                    spinMaxMp->blockSignals(false);
                    switchTiledVae->blockSignals(true);
                    switchTiledVae->setChecked(tiledAlways);
                    switchTiledVae->blockSignals(false);
                    labelTiledVaeState->setText(tiledAlways ? ComfyTr::tr("Always") : ComfyTr::tr("Automatic"));
                    savePerfSettings();
                });
        QObject::connect(spinActiveHistoryMb, QOverload<int>::of(&QSpinBox::valueChanged), dock, [dock, d, savePerfSettings](int) {
            savePerfSettings();
            dock->pruneHistoryToStorageLimit();
            dock->updateHistoryUsageLabel();
        });
        QObject::connect(spinStoredHistoryMb, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        QObject::connect(spinMaxMp, QOverload<int>::of(&QSpinBox::valueChanged), dlg, savePerfSettings);
        QObject::connect(switchTiledVae, &QAbstractButton::toggled, dlg, savePerfSettings);
        QObject::connect(switchDynCache, &QAbstractButton::toggled, dlg, savePerfSettings);
        QObject::connect(switchMultiThread, &QAbstractButton::toggled, dlg, savePerfSettings);
        QObject::connect(sliderPerfBatch, &QAbstractSlider::valueChanged, dock, [dock, d, labelPerfBatchVal, savePerfSettings](int v) {
            labelPerfBatchVal->setText(QString::number(v));
            if (d->generate.spinBatchCount)
                d->generate.spinBatchCount->setValue(v);
            savePerfSettings();
        });
        QObject::connect(sliderPerfRes, &QAbstractSlider::valueChanged, dock, [dock, d, labelPerfResVal, savePerfSettings](int v) {
            const double mul = qMax(0.3, v / 10.0);
            labelPerfResVal->setText(QString::number(mul, 'f', 1) + QLatin1String("×"));
            d->generate.resolutionMultiplier = mul;
            if (d->generate.sliderResolutionMultiplier) {
                d->generate.sliderResolutionMultiplier->blockSignals(true);
                d->generate.sliderResolutionMultiplier->setValue(v);
                d->generate.sliderResolutionMultiplier->blockSignals(false);
            }
            if (d->generate.labelResolutionMultiplier)
                d->generate.labelResolutionMultiplier->setText(labelPerfResVal->text());
            KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("ResolutionMultiplier", mul);
            savePerfSettings();
        });

        perfLayout->addStretch();
        stack->addWidget(perfTab.page);

    result.syncSlidersFromDock = syncPerfSlidersFromDock;
    return result;
}

} // namespace ComfySettingsDialogBuilder
