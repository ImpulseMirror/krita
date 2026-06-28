/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyDockUiBuilder.h"
#include "ComfyUIRemoteDockShellInternal.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyWorkspaceSelectButton.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyHistoryListWidget.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPixmap>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KSharedConfig>
#include <KConfigGroup>

#include <kis_annotation.h>
#include <kis_types.h>

using ComfyDockShellInternal::ComfyPromptPlainTextEdit;
using ComfyDockShellInternal::LiveSpinnerWidget;
using ComfyDockShellInternal::StrengthSpinBox;
using ComfyDockShellInternal::setComboCurrentItemData;



namespace ComfyDockUiBuilder {

void buildRegionsPanel(const Context &ctx, QVBoxLayout *scrollLayout)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    d->generate.regionsGroupBox = new QGroupBox(ComfyTr::tr("Regions"));
    QVBoxLayout *regLayout = new QVBoxLayout(d->generate.regionsGroupBox);
    // §13.90: PromptHeader — full (title + description), icon (icon only), none (no header)
    QHBoxLayout *regionHeaderRow = new QHBoxLayout();
    regionHeaderRow->addWidget(new QLabel(ComfyTr::tr("Header:")));
    d->generate.regionHeaderCombo = new QComboBox();
    d->generate.regionHeaderCombo->addItem(ComfyTr::tr("Full"), 0);
    d->generate.regionHeaderCombo->addItem(ComfyTr::tr("Icon"), 1);
    d->generate.regionHeaderCombo->addItem(ComfyTr::tr("None"), 2);
    d->generate.regionHeaderCombo->setCurrentIndex(qBound(0, d->promptHeaderMode, 2));
    d->generate.regionHeaderCombo->setToolTip(
        ComfyTr::tr("Region prompt header style: Full text, Icon only, or None (compact)."));
    QObject::connect(d->generate.regionHeaderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), dock,
                     [dock, d](int idx) {
                         d->promptHeaderMode = qBound(0, idx, 2);
                         KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("PromptHeader", d->promptHeaderMode);
                         dock->applyPromptHeader();
                     });
    regionHeaderRow->addWidget(d->generate.regionHeaderCombo);
    regionHeaderRow->addStretch();
    regLayout->addLayout(regionHeaderRow);
    d->generate.regionHeaderLabel = new QLabel(ComfyTr::tr("Different prompt per area (layer or selection):"));
    regLayout->addWidget(d->generate.regionHeaderLabel);
    d->generate.regionPromptWidget = new ComfyRegionPromptWidget(d->generate.regionsGroupBox);
    d->generate.regionPromptWidget->setPromptHeaderMode(d->promptHeaderMode);
    d->generate.regionPromptWidget->setRootPromptEditors(d->generate.editPrompt, d->generate.editNegative);
    d->generate.regionPromptWidget->setShowNegativePrompt(d->generate.negativePromptBlock
                                                          && d->generate.negativePromptBlock->isVisible());
    {
        QJsonObject st = ComfyUIUtils::loadSettingsJson();
        d->generate.regionPromptWidget->setPromptTranslationCode(st.value(QStringLiteral("prompt_translation")).toString());
    }
    d->generate.regionPromptWidget->bind(&comfyActiveRegionEntries(d), &d->activeRegionIndex);
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::activeIndexChanged, dock, [dock]() {
        dock->refreshRegionControlLayersList();
        dock->updateGenerateOptions();
    });
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::regionEdited, dock, [dock]() {
        dock->saveRegionsToConfig();
        dock->refreshRegionsList();
    });
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::removeRegionRequested, dock,
                     &ComfyUIRemoteDock::slotRemoveRegion);
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::requestAddRegion, dock,
                     &ComfyUIRemoteDock::slotAddRegion);
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::layoutHeightsChanged, dock, [dock, d]() {
        if (d->comboWorkspace && d->comboWorkspace->currentIndex() == 0)
            dock->syncCompactGenerateLayoutRows(true);
    });
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::translatePromptRequested, dock,
                     [dock, d](bool negative) {
                         if (!d->nam || !d->editServerUrl)
                             return;
                         const QString url = d->editServerUrl->text().trimmed();
                         if (url.isEmpty())
                             return;
                         QJsonObject st = ComfyUIUtils::loadSettingsJson();
                         if (!st.value(QStringLiteral("translation_enabled")).toBool(false))
                             return;
                         const QString lang = st.value(QStringLiteral("prompt_translation")).toString();
                         if (lang.isEmpty() || lang == QLatin1String("disabled"))
                             return;
                         QString source;
                         if (d->activeRegionIndex == ComfyRegionLink::kRootRegionIndex) {
                             dock->commitPromptEditorsFromUi();
                             source = negative && d->generate.editNegative ? d->generate.editNegative->toPlainText()
                                                                           : (d->generate.editPrompt
                                                                                  ? d->generate.editPrompt->toPlainText()
                                                                                  : QString());
                         } else if (d->activeRegionIndex >= 0) {
                             QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(d);
                             if (d->activeRegionIndex < regs.size())
                                 source = regs.at(d->activeRegionIndex).prompt;
                         }
                         if (source.trimmed().isEmpty())
                             return;
                         ComfyUIUtils::requestEtnPromptTranslation(
                             d->nam, url, lang, source, dock, [dock, d, negative](bool ok, const QString &translated) {
                                 if (!ok)
                                     return;
                                 if (d->activeRegionIndex == ComfyRegionLink::kRootRegionIndex) {
                                     if (negative && d->generate.editNegative)
                                         d->generate.editNegative->setPlainText(translated);
                                     else if (d->generate.editPrompt)
                                         d->generate.editPrompt->setPlainText(translated);
                                 } else if (d->activeRegionIndex >= 0) {
                                     QList<ComfyUIRemoteDock::Private::RegionEntry> &regs = comfyActiveRegionEntries(d);
                                     if (d->activeRegionIndex < regs.size())
                                         regs[d->activeRegionIndex].prompt = translated;
                                     dock->saveRegionsToConfig();
                                 }
                                 if (d->generate.regionPromptWidget)
                                     d->generate.regionPromptWidget->refresh();
                             });
                     });
    QObject::connect(d->generate.regionPromptWidget, &ComfyRegionPromptWidget::editingModeChanged, dock,
                     [d](int idx) {
                         Q_UNUSED(idx);
                         if (d->generate.rootPromptColumnWidget)
                             d->generate.rootPromptColumnWidget->setVisible(false);
                         if (d->generate.negativePromptBlock)
                             d->generate.negativePromptBlock->setVisible(false);
                     });
    regLayout->addWidget(d->generate.regionPromptWidget);
    d->generate.regionButtonsRowWidget = new QWidget(d->generate.regionsGroupBox);
    QHBoxLayout *regionBtns = new QHBoxLayout(d->generate.regionButtonsRowWidget);
    regionBtns->setContentsMargins(0, 0, 0, 0);
    d->generate.btnAddRegion = new QPushButton(ComfyTr::tr("Add"));
    d->generate.btnRemoveRegion = new QPushButton(ComfyTr::tr("Remove"));
    d->generate.btnMoveRegionUp = new QPushButton(ComfyTr::tr("Up"));
    d->generate.btnMoveRegionDown = new QPushButton(ComfyTr::tr("Down"));
    d->generate.btnGenerateRegions = new QPushButton(ComfyTr::tr("Generate regions"));
    d->generate.btnGenerateRegions->setToolTip(
        ComfyTr::tr("Same as Generate: builds one job with regional prompts and masks (Python process_regions path)."));
    QObject::connect(d->generate.btnAddRegion, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotAddRegion);
    QObject::connect(d->generate.btnRemoveRegion, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotRemoveRegion);
    QObject::connect(d->generate.btnMoveRegionUp, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotMoveRegionUp);
    QObject::connect(d->generate.btnMoveRegionDown, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotMoveRegionDown);
    QObject::connect(d->generate.btnGenerateRegions, &QPushButton::clicked, dock, &ComfyUIRemoteDock::slotGenerateRegions);
    regionBtns->addWidget(d->generate.btnAddRegion);
    regionBtns->addWidget(d->generate.btnRemoveRegion);
    regionBtns->addWidget(d->generate.btnMoveRegionUp);
    regionBtns->addWidget(d->generate.btnMoveRegionDown);
    regionBtns->addWidget(d->generate.btnGenerateRegions);
    regLayout->addWidget(d->generate.regionButtonsRowWidget);
    dock->setupRegionControlLayersUi(d->generate.regionsGroupBox, nullptr);
    // FAITHFUL_PORT: control-layer list is reached via strength-row icons, not
    // embedded in the compact region prompt stack (avoids overlap with empty-state text).
    scrollLayout->addWidget(d->generate.regionsGroupBox);
}

} // namespace ComfyDockUiBuilder
