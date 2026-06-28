/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilderStylesInternal.h"
#include "ComfySettingsDialogBuilderInternal.h"
#include "ComfySettingsDialogBuilder.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"
#include "ComfyStyleCollection.h"
#include "ComfyTheme.h"
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
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include <QUrl>
#include <QDesktopServices>
#include <QVBoxLayout>

#include <KSharedConfig>
#include <KConfigGroup>

#include <functional>

namespace ComfySettingsDialogBuilderStylesInternal {

ComfySettingsDialogBuilder::StylesTabResult wireStylesTabSync(StylesWorkspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    QDialog *dlg = ws.dialog;

    QComboBox *stylesPresetMirror = ws.stylesPresetMirror;
    QToolButton *btnStylesAddPreset = ws.btnStylesAddPreset;
    QToolButton *btnStylesDuplicate = ws.btnStylesDuplicate;
    QToolButton *btnStylesDeletePreset = ws.btnStylesDeletePreset;
    QToolButton *btnStylesRefresh = ws.btnStylesRefresh;
    QLabel *lblBuiltinMessage = ws.lblBuiltinMessage;
    QLabel *lblBuiltinCopyLink = ws.lblBuiltinCopyLink;
    QCheckBox *checkShowBuiltinStyles = ws.checkShowBuiltinStyles;
    QLineEdit *editStyleName = ws.editStyleName;
    QComboBox *stylesCkptMirror = ws.stylesCkptMirror;
    QToolButton *btnStylesCkptRefresh = ws.btnStylesCkptRefresh;
    QLabel *stylesCkptWarning = ws.stylesCkptWarning;
    QToolButton *toggleAdvCkpt = ws.toggleAdvCkpt;
    QWidget *advCkptBody = ws.advCkptBody;
    QComboBox *comboStyleArchitecture = ws.comboStyleArchitecture;
    QComboBox *comboStyleVae = ws.comboStyleVae;
    QSpinBox *spinStyleClipSkip = ws.spinStyleClipSkip;
    QCheckBox *checkStyleClipSkipOverride = ws.checkStyleClipSkipOverride;
    QSpinBox *spinStylePreferredResolution = ws.spinStylePreferredResolution;
    QCheckBox *checkStylePreferredResolution = ws.checkStylePreferredResolution;
    ComfySwitchWidget *switchStyleZsnr = ws.switchStyleZsnr;
    ComfySwitchWidget *switchStyleSag = ws.switchStyleSag;
    QLabel *labelStyleZsnrState = ws.labelStyleZsnrState;
    QLabel *labelStyleSagState = ws.labelStyleSagState;
    ComfyStyleLoraListWidget *loraListWidget = ws.loraListWidget;
    QLineEdit *editStylesPositive = ws.editStylesPositive;
    QLineEdit *editStylesNegative = ws.editStylesNegative;
    QComboBox *comboLinkedEditStyle = ws.comboLinkedEditStyle;
    QWidget *linkedEditStyleRow = ws.linkedEditStyleRow;
    ComfyStyleSamplerWidget *qualitySamplerWidget = ws.qualitySamplerWidget;
    ComfyStyleSamplerWidget *liveSamplerWidget = ws.liveSamplerWidget;

    ComfySettingsDialogBuilder::StylesTabResult result;
        d->stylesTabPersistingLoras = false;
        auto persistStyleLoras = [dock, d, loraListWidget]() {
            if (d->stylesTabPersistingLoras || !d->generate.comboPreset)
                return;
            const ComfyStyleEntry *st = dock->currentJsonStyleEntry();
            if (!st || st->isBuiltin)
                return;
            ComfyStyleEntry e = *st;
            e.loras = loraListWidget->value();
            d->stylesTabPersistingLoras = true;
            dock->saveStyleEntry(e);
            d->stylesTabPersistingLoras = false;
        };
        auto reloadStyleLorasFromPreset = [dock, d, loraListWidget]() {
            if (!loraListWidget)
                return;
            d->stylesTabPersistingLoras = true;
            const QString styleId = dock->encodeStyleIdFromPresetCombo(d->generate.comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                loraListWidget->setValue(st->loras);
            else
                loraListWidget->setValue(QJsonArray());
            loraListWidget->setServerLoraFilenames(d->comfyServerLoraFilenames);
            loraListWidget->refreshFilters();
            d->stylesTabPersistingLoras = false;
        };
        reloadStyleLorasFromPreset();
        QObject::connect(loraListWidget, &ComfyStyleLoraListWidget::valueChanged, dlg, persistStyleLoras);
        QObject::connect(loraListWidget, &ComfyStyleLoraListWidget::refreshRequested, dock, &ComfyUIRemoteDock::slotRefreshCheckpoints);

        // FAITHFUL_PORT/CRASH FIX: these were stack-locals, captured by reference by
        // syncStylesFromDock() / editStyles{Positive,Negative}::textChanged /
        // editStyleName::editingFinished. The Settings dialog is non-modal and
        // outlives slotConfigureHelp(), so once dock function returned the
        // captured references dangled and clicking the Styles nav tab on
        // Android crashed in QListWidget::_q_emitCurrentItemChanged (stack
        // canary check failed on epilogue). Bind to d-pointer members instead.
        d->stylesTabPresetNameBaselineMember.clear();
        d->stylesTabSyncing = false;
        QString &stylesTabPresetNameBaseline = d->stylesTabPresetNameBaselineMember;
        bool &syncingStylesTab = d->stylesTabSyncing;
        auto repopulateLinkedEditStyleCombo = [dock, d, comboLinkedEditStyle, linkedEditStyleRow, stylesCkptMirror]() {
            const QString currentStyleId = dock->encodeStyleIdFromPresetCombo(d->generate.comboPreset);
            QString ckpt = stylesCkptMirror->currentText().trimmed();
            QString styleArch = QStringLiteral("auto");
            if (const ComfyStyleEntry *cur = ComfyStyleCollection::instance().findByStyleId(currentStyleId)) {
                styleArch = cur->architecture;
                if (ckpt.isEmpty() && !cur->checkpoints.isEmpty())
                    ckpt = cur->checkpoints.first();
            }
            const ComfyResources::Arch curArch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
            const bool hideLinked = ComfyResources::supportsEditInstructions(curArch);
            linkedEditStyleRow->setVisible(!hideLinked);
            if (hideLinked)
                return;
            QString saved;
            if (const ComfyStyleEntry *cur = ComfyStyleCollection::instance().findByStyleId(currentStyleId))
                saved = cur->linkedEditStyle.trimmed();
            comboLinkedEditStyle->blockSignals(true);
            comboLinkedEditStyle->clear();
            comboLinkedEditStyle->addItem(ComfyTr::tr("None"), QString());
            const bool showBuiltin =
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
            for (const ComfyStyleEntry *s : ComfyStyleCollection::instance().filtered(showBuiltin)) {
                if (s->styleId == currentStyleId)
                    continue;
                const QString ckpt = s->checkpoints.isEmpty() ? QString() : s->checkpoints.first();
                const ComfyResources::Arch a = ComfyWorkflowEngine::resolveArch(ckpt, s->architecture);
                if (!ComfyResources::supportsEditInstructions(a))
                    continue;
                comboLinkedEditStyle->addItem(ComfyStyleCollection::comboDisplayName(*s), s->styleId);
            }
            int si = 0;
            if (!saved.isEmpty()) {
                const int fi = comboLinkedEditStyle->findData(saved);
                if (fi >= 0)
                    si = fi;
            }
            comboLinkedEditStyle->setCurrentIndex(si);
            comboLinkedEditStyle->blockSignals(false);
        };
        auto updateBuiltinStyleUi = [dock, d, lblBuiltinMessage, lblBuiltinCopyLink, editStyleName, stylesCkptMirror,
                                     loraListWidget, editStylesPositive, editStylesNegative, comboLinkedEditStyle,
                                     qualitySamplerWidget, liveSamplerWidget, toggleAdvCkpt, advCkptBody,
                                     comboStyleArchitecture, comboStyleVae, spinStyleClipSkip, checkStyleClipSkipOverride,
                                     spinStylePreferredResolution, checkStylePreferredResolution, switchStyleZsnr,
                                     switchStyleSag, btnStylesDeletePreset]() {
            const ComfyStyleEntry *st = dock->currentJsonStyleEntry();
            const bool hasJson = st != nullptr;
            const bool builtin = hasJson && st->isBuiltin;
            const bool editable = hasJson && !builtin;
            lblBuiltinMessage->setVisible(builtin);
            lblBuiltinCopyLink->setVisible(builtin);
            editStyleName->setReadOnly(!editable);
            stylesCkptMirror->setEnabled(editable);
            loraListWidget->setEditingEnabled(editable);
            editStylesPositive->setReadOnly(!editable);
            editStylesNegative->setReadOnly(!editable);
            if (!hasJson) {
                editStylesPositive->clear();
                editStylesNegative->clear();
            }
            comboLinkedEditStyle->setEnabled(editable);
            qualitySamplerWidget->setEditingEnabled(editable);
            liveSamplerWidget->setEditingEnabled(editable);
            toggleAdvCkpt->setEnabled(editable);
            advCkptBody->setEnabled(editable);
            comboStyleArchitecture->setEnabled(editable);
            comboStyleVae->setEnabled(editable);
            checkStyleClipSkipOverride->setEnabled(editable);
            spinStyleClipSkip->setEnabled(editable && checkStyleClipSkipOverride->isChecked());
            checkStylePreferredResolution->setEnabled(editable);
            spinStylePreferredResolution->setEnabled(editable && checkStylePreferredResolution->isChecked());
            switchStyleZsnr->setEnabled(editable);
            switchStyleSag->setEnabled(editable);
            btnStylesDeletePreset->setEnabled(editable);
        };
        auto syncStyleNameField = [editStyleName, &stylesTabPresetNameBaseline, dock, d]() {
            if (!d->generate.comboPreset)
                return;
            editStyleName->blockSignals(true);
            if (const ComfyStyleEntry *st = dock->currentJsonStyleEntry()) {
                editStyleName->setText(st->name);
                stylesTabPresetNameBaseline = st->name;
            } else {
                editStyleName->clear();
                stylesTabPresetNameBaseline.clear();
            }
            editStyleName->blockSignals(false);
        };
        // FAITHFUL_PORT/CRASH FIX: dock guard flag was a stack-local captured by
        // reference into syncAdvCkptFromStyle() and persistStyleCheckpointOptions(),
        // both connected to long-lived signals on widgets owned by the persistent
        // Settings dialog. After slotConfigureHelp() returned, the reference
        // dangled — opening the Styles nav tab later invoked the lambdas which
        // then read/wrote freed stack memory, corrupting the link register and
        // crashing in <unknown> with `lr == pc` style SIGSEGV. Bind to a d-pointer
        // member so the storage outlives the dialog.
        d->stylesTabPersistingAdvanced = false;
        bool &persistingStyleAdvanced = d->stylesTabPersistingAdvanced;
        auto resolvedStyleArch = [dock, d, stylesCkptMirror]() -> ComfyResources::Arch {
            QString ckpt = stylesCkptMirror->currentText().trimmed();
            QString styleArch = QStringLiteral("auto");
            const QString styleId = dock->encodeStyleIdFromPresetCombo(d->generate.comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
                styleArch = st->architecture;
                if (ckpt.isEmpty() && !st->checkpoints.isEmpty())
                    ckpt = st->checkpoints.first();
            }
            if (ckpt.isEmpty() && d->generate.comboCheckpoint)
                ckpt = d->generate.comboCheckpoint->currentText().trimmed();
            return ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
        };
        auto repopulateStyleArchitectureCombo = [dock, d, comboStyleArchitecture, stylesCkptMirror](const QString &styleArchitectureKey) {
            QString ckpt = stylesCkptMirror->currentText().trimmed();
            const QString styleId = dock->encodeStyleIdFromPresetCombo(d->generate.comboPreset);
            if (ckpt.isEmpty()) {
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
                    if (!st->checkpoints.isEmpty())
                        ckpt = st->checkpoints.first();
                }
            }
            if (ckpt.isEmpty() && d->generate.comboCheckpoint)
                ckpt = d->generate.comboCheckpoint->currentText().trimmed();
            const ComfyResources::Arch resolved = ComfyWorkflowEngine::resolveArch(ckpt, styleArchitectureKey);
            const QVector<QString> keys = ComfyResources::validArchitectureKeysForResolvedArch(resolved);
            const QString wantKey =
                styleArchitectureKey.trimmed().isEmpty() ? QStringLiteral("auto") : styleArchitectureKey.trimmed().toLower();
            comboStyleArchitecture->blockSignals(true);
            comboStyleArchitecture->clear();
            for (const QString &key : keys)
                comboStyleArchitecture->addItem(ComfyResources::architectureKeyDisplayName(key), key);
            int pick = comboStyleArchitecture->findData(wantKey);
            if (pick < 0)
                pick = comboStyleArchitecture->findData(QStringLiteral("auto"));
            if (pick >= 0)
                comboStyleArchitecture->setCurrentIndex(pick);
            comboStyleArchitecture->blockSignals(false);
        };
        auto repopulateStyleVaeCombo = [dock, d, comboStyleVae]() {
            const QString prev = comboStyleVae->currentText();
            comboStyleVae->blockSignals(true);
            comboStyleVae->clear();
            comboStyleVae->addItem(QStringLiteral("Checkpoint Default"));
            for (const QString &v : ComfyUIUtils::vaeNamesFromObjectInfo(d->lastObjectInfoRoot))
                comboStyleVae->addItem(v);
            int fi = comboStyleVae->findText(prev);
            if (fi >= 0)
                comboStyleVae->setCurrentIndex(fi);
            else if (!prev.isEmpty()) {
                comboStyleVae->addItem(prev);
                comboStyleVae->setCurrentText(prev);
            }
            comboStyleVae->blockSignals(false);
        };
        auto updateStyleAdvancedArchUi = [resolvedStyleArch, spinStyleClipSkip, checkStyleClipSkipOverride, switchStyleZsnr,
                                          switchStyleSag]() {
            const ComfyResources::Arch arch = resolvedStyleArch();
            const bool clipOk = ComfyResources::supportsClipSkip(arch);
            checkStyleClipSkipOverride->setEnabled(clipOk);
            spinStyleClipSkip->setEnabled(clipOk && checkStyleClipSkipOverride->isChecked());
            const bool attnOk = ComfyResources::supportsAttentionGuidance(arch);
            switchStyleZsnr->setEnabled(attnOk);
            switchStyleSag->setEnabled(attnOk);
        };
        auto syncAdvCkptFromStyle = [dock, d, comboStyleArchitecture, stylesCkptMirror, comboStyleVae, spinStyleClipSkip,
                                     checkStyleClipSkipOverride, spinStylePreferredResolution, checkStylePreferredResolution,
                                     switchStyleZsnr, switchStyleSag, labelStyleZsnrState, labelStyleSagState,
                                     &persistingStyleAdvanced, updateStyleAdvancedArchUi, repopulateStyleVaeCombo,
                                     repopulateStyleArchitectureCombo]() {
            repopulateStyleVaeCombo();
            const QString styleId = dock->encodeStyleIdFromPresetCombo(d->generate.comboPreset);
            const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId);
            const bool hasStyle = st != nullptr;
            const QString styleArchKey = hasStyle ? st->architecture : QStringLiteral("auto");
            repopulateStyleArchitectureCombo(styleArchKey);
            persistingStyleAdvanced = true;
            if (hasStyle) {
                int vi = comboStyleVae->findText(st->vae);
                if (vi >= 0)
                    comboStyleVae->setCurrentIndex(vi);
                else if (!st->vae.isEmpty()) {
                    comboStyleVae->addItem(st->vae);
                    comboStyleVae->setCurrentText(st->vae);
                } else
                    comboStyleVae->setCurrentIndex(0);
                const bool clipOn = st->clipSkip > 0;
                checkStyleClipSkipOverride->setChecked(clipOn);
                spinStyleClipSkip->setValue(clipOn ? st->clipSkip : 0);
                const bool resOn = st->preferredResolution > 0;
                checkStylePreferredResolution->setChecked(resOn);
                spinStylePreferredResolution->setValue(resOn ? st->preferredResolution : 0);
                spinStylePreferredResolution->setEnabled(resOn);
                switchStyleZsnr->setChecked(st->vPredictionZsnr);
                labelStyleZsnrState->setText(st->vPredictionZsnr ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
                switchStyleSag->setChecked(st->selfAttentionGuidance);
                labelStyleSagState->setText(st->selfAttentionGuidance ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            } else {
                comboStyleVae->setCurrentIndex(0);
                checkStyleClipSkipOverride->setChecked(false);
                spinStyleClipSkip->setValue(0);
                checkStylePreferredResolution->setChecked(false);
                spinStylePreferredResolution->setValue(0);
                spinStylePreferredResolution->setEnabled(false);
                switchStyleZsnr->setChecked(false);
                labelStyleZsnrState->setText(ComfyTr::tr("Off"));
                switchStyleSag->setChecked(false);
                labelStyleSagState->setText(ComfyTr::tr("Off"));
            }
            persistingStyleAdvanced = false;
            updateStyleAdvancedArchUi();
        };
        auto persistCurrentJsonStyle = [dock, d, editStyleName, stylesCkptMirror, editStylesPositive, editStylesNegative,
                                        qualitySamplerWidget, liveSamplerWidget, loraListWidget,
                                        comboStyleArchitecture, comboStyleVae, spinStyleClipSkip, checkStyleClipSkipOverride,
                                        spinStylePreferredResolution, checkStylePreferredResolution, switchStyleZsnr,
                                        switchStyleSag, comboLinkedEditStyle]() -> bool {
            const ComfyStyleEntry *st = dock->currentJsonStyleEntry();
            if (!st || st->isBuiltin)
                return false;
            ComfyStyleEntry e = *st;
            e.name = editStyleName->text().trimmed().isEmpty() ? e.name : editStyleName->text().trimmed();
            const QString ckpt = stylesCkptMirror->currentText().trimmed();
            if (!ckpt.isEmpty())
                e.checkpoints = QStringList{ckpt};
            e.stylePrompt = editStylesPositive->text();
            e.negativePrompt = editStylesNegative->text();
            e.loras = loraListWidget->value();
            e.linkedEditStyle = comboLinkedEditStyle->currentData().toString();
            const QString archKey = comboStyleArchitecture->currentData().toString().trimmed();
            if (!archKey.isEmpty())
                e.architecture = archKey;
            e.vae = comboStyleVae->currentText();
            e.clipSkip = checkStyleClipSkipOverride->isChecked() ? spinStyleClipSkip->value() : 0;
            e.preferredResolution =
                checkStylePreferredResolution->isChecked() ? spinStylePreferredResolution->value() : 0;
            e.vPredictionZsnr = switchStyleZsnr->isChecked();
            e.selfAttentionGuidance = switchStyleSag->isChecked();
            qualitySamplerWidget->writeToStyle(&e);
            liveSamplerWidget->writeToStyle(&e);
            return dock->saveStyleEntry(e, false, false);
        };
        auto persistStyleCheckpointOptions = [persistCurrentJsonStyle, &persistingStyleAdvanced]() {
            if (persistingStyleAdvanced)
                return;
            persistCurrentJsonStyle();
        };
        auto readJsonStyleIntoTab = [dock, d, stylesCkptMirror, editStylesPositive, editStylesNegative, qualitySamplerWidget,
                                     liveSamplerWidget, &syncingStylesTab]() {
            syncingStylesTab = true;
            if (const ComfyStyleEntry *st = dock->currentJsonStyleEntry()) {
                stylesCkptMirror->blockSignals(true);
                if (!st->checkpoints.isEmpty()) {
                    const QString ck = st->checkpoints.first();
                    int fi = stylesCkptMirror->findText(ck);
                    if (fi >= 0)
                        stylesCkptMirror->setCurrentIndex(fi);
                    else
                        stylesCkptMirror->setEditText(ck);
                }
                stylesCkptMirror->blockSignals(false);
                editStylesPositive->blockSignals(true);
                editStylesNegative->blockSignals(true);
                QString stylePrompt = st->stylePrompt;
                QString negativePrompt = st->negativePrompt;
                // User styles created before defaults were wired may have empty prompts on disk.
                if (!st->isBuiltin && stylePrompt.trimmed().isEmpty() && negativePrompt.trimmed().isEmpty()
                    && st->name == ComfyTr::tr("New Style")) {
                    stylePrompt = comfyDefaultStylePrompt();
                    negativePrompt = comfyDefaultNegativeStylePrompt();
                }
                editStylesPositive->setText(stylePrompt);
                editStylesNegative->setText(negativePrompt);
                editStylesPositive->blockSignals(false);
                editStylesNegative->blockSignals(false);
                qualitySamplerWidget->readFromStyle(*st);
                liveSamplerWidget->readFromStyle(*st);
            }
            syncingStylesTab = false;
        };
        auto updateStylesCkptWarning = [stylesCkptWarning, stylesCkptMirror, dock, d]() {
            const ComfyStyleEntry *st = dock->currentJsonStyleEntry();
            if (!st) {
                stylesCkptWarning->hide();
                return;
            }
            ComfyStyleEntry probe = *st;
            const QString ck = stylesCkptMirror->currentText().trimmed();
            if (!ck.isEmpty())
                probe.checkpoints = QStringList{ck};
            QStringList serverCkpts;
            if (d->generate.comboCheckpoint) {
                for (int i = 0; i < d->generate.comboCheckpoint->count(); ++i)
                    serverCkpts.append(d->generate.comboCheckpoint->itemText(i));
            }
            const QStringList warn =
                ComfyUIUtils::styleCheckpointWarnings(probe, serverCkpts, d->lastObjectInfoRoot);
            if (warn.isEmpty()) {
                stylesCkptWarning->hide();
            } else {
                stylesCkptWarning->setText(warn.join(QLatin1Char('\n')));
                stylesCkptWarning->show();
            }
        };
        auto syncStylesFromDock = [dock, d, stylesPresetMirror, stylesCkptMirror, updateStylesCkptWarning,
                                    repopulateLinkedEditStyleCombo, syncStyleNameField, reloadStyleLorasFromPreset,
                                    syncAdvCkptFromStyle, readJsonStyleIntoTab, updateBuiltinStyleUi, &syncingStylesTab]() {
            if (!d->generate.comboPreset)
                return;
            ComfyUIUtils::reloadSamplerPresetsCache();
            syncingStylesTab = true;
            stylesPresetMirror->blockSignals(true);
            stylesPresetMirror->clear();
            for (int i = 0; i < d->generate.comboPreset->count(); ++i)
                stylesPresetMirror->addItem(d->generate.comboPreset->itemText(i), i);
            const int cur = d->generate.comboPreset->currentIndex();
            const int mirrorIdx = stylesPresetMirror->findData(cur);
            stylesPresetMirror->setCurrentIndex(mirrorIdx >= 0 ? mirrorIdx : 0);
            stylesPresetMirror->blockSignals(false);
            if (d->generate.comboCheckpoint) {
                stylesCkptMirror->blockSignals(true);
                stylesCkptMirror->clear();
                for (int i = 0; i < d->generate.comboCheckpoint->count(); ++i)
                    stylesCkptMirror->addItem(d->generate.comboCheckpoint->itemText(i));
                stylesCkptMirror->setCurrentIndex(d->generate.comboCheckpoint->currentIndex());
                if (!d->generate.comboCheckpoint->currentText().isEmpty()
                    && stylesCkptMirror->findText(d->generate.comboCheckpoint->currentText()) < 0)
                    stylesCkptMirror->setEditText(d->generate.comboCheckpoint->currentText());
                stylesCkptMirror->blockSignals(false);
            }
            syncingStylesTab = false;
            readJsonStyleIntoTab();
            syncStyleNameField();
            reloadStyleLorasFromPreset();
            syncAdvCkptFromStyle();
            repopulateLinkedEditStyleCombo();
            updateStylesCkptWarning();
            updateBuiltinStyleUi();
        };
        QObject::connect(btnStylesAddPreset, &QToolButton::clicked, dock, [dock, d, stylesCkptMirror, syncStylesFromDock]() {
            dock->createJsonStyle(stylesCkptMirror->currentText().trimmed());
            syncStylesFromDock();
        });
        QObject::connect(btnStylesDuplicate, &QToolButton::clicked, dock, [dock, d, syncStylesFromDock]() {
            dock->duplicateJsonStyle();
            syncStylesFromDock();
        });
        QObject::connect(lblBuiltinCopyLink, &QLabel::linkActivated, dock, [dock, d, syncStylesFromDock](const QString &) {
            dock->duplicateJsonStyle();
            syncStylesFromDock();
        });
        QObject::connect(btnStylesRefresh, &QToolButton::clicked, dock, [dock, d, syncStylesFromDock]() {
            ComfyStyleCollection::instance().reload();
            dock->rebuildPresetComboItems();
            dock->slotRefreshCheckpoints();
            syncStylesFromDock();
        });
        // Also pick up out-of-dialog mutations to the preset list (refresh, delete,
        // etc.) so the mirror never goes stale. modelReset fires from clear()+addItem
        // bursts inside dock->rebuildPresetComboItems(); rowsInserted/Removed cover the
        // incremental add/delete paths used by slotSaveAsPreset / slotDeletePreset.
        if (d->generate.comboPreset) {
            QObject::connect(d->generate.comboPreset->model(), &QAbstractItemModel::rowsInserted, dlg,
                    [syncStylesFromDock](const QModelIndex &, int, int) { syncStylesFromDock(); });
            QObject::connect(d->generate.comboPreset->model(), &QAbstractItemModel::rowsRemoved, dlg,
                    [syncStylesFromDock](const QModelIndex &, int, int) { syncStylesFromDock(); });
            QObject::connect(d->generate.comboPreset->model(), &QAbstractItemModel::modelReset, dlg,
                    [syncStylesFromDock]() { syncStylesFromDock(); });
        }
        QObject::connect(checkStyleClipSkipOverride, &QCheckBox::toggled, advCkptBody,
                [spinStyleClipSkip, checkStyleClipSkipOverride, resolvedStyleArch, updateStyleAdvancedArchUi,
                 persistStyleCheckpointOptions](bool on) {
                    spinStyleClipSkip->setEnabled(on);
                    if (on && spinStyleClipSkip->value() == 0) {
                        const ComfyResources::Arch a = resolvedStyleArch();
                        spinStyleClipSkip->setValue(a == ComfyResources::Arch::Sd15 ? 1 : 2);
                    } else if (!on)
                        spinStyleClipSkip->setValue(0);
                    updateStyleAdvancedArchUi();
                    persistStyleCheckpointOptions();
                });
        QObject::connect(checkStylePreferredResolution, &QCheckBox::toggled, advCkptBody,
                [spinStylePreferredResolution, resolvedStyleArch, persistStyleCheckpointOptions](bool on) {
                    spinStylePreferredResolution->setEnabled(on);
                    if (on && spinStylePreferredResolution->value() == 0) {
                        const ComfyResources::Arch a = resolvedStyleArch();
                        spinStylePreferredResolution->setValue(a == ComfyResources::Arch::Sd15 ? 640 : 1024);
                    } else if (!on && spinStylePreferredResolution->value() > 0) {
                        spinStylePreferredResolution->setValue(0);
                    }
                    persistStyleCheckpointOptions();
                });
        QObject::connect(comboStyleArchitecture, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [persistStyleCheckpointOptions, updateStyleAdvancedArchUi](int) {
                    persistStyleCheckpointOptions();
                    updateStyleAdvancedArchUi();
                });
        QObject::connect(comboStyleVae, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        QObject::connect(spinStyleClipSkip, QOverload<int>::of(&QSpinBox::valueChanged), dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        QObject::connect(spinStylePreferredResolution, QOverload<int>::of(&QSpinBox::valueChanged), dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        QObject::connect(switchStyleZsnr, &QAbstractButton::toggled, dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        QObject::connect(switchStyleSag, &QAbstractButton::toggled, dlg,
                [persistStyleCheckpointOptions]() { persistStyleCheckpointOptions(); });
        QObject::connect(qualitySamplerWidget, &ComfyStyleSamplerWidget::valueChanged, dlg,
                [persistCurrentJsonStyle]() { persistCurrentJsonStyle(); });
        QObject::connect(liveSamplerWidget, &ComfyStyleSamplerWidget::valueChanged, dlg,
                [persistCurrentJsonStyle]() { persistCurrentJsonStyle(); });
        QObject::connect(stylesPresetMirror, QOverload<int>::of(&QComboBox::currentIndexChanged), dock,
                [dock, d, stylesPresetMirror, syncStylesFromDock, &syncingStylesTab](int) {
                    if (syncingStylesTab)
                        return;
                    const int dataIdx = stylesPresetMirror->currentData().toInt();
                    if (d->generate.comboPreset && dataIdx >= 0 && dataIdx < d->generate.comboPreset->count())
                        d->generate.comboPreset->setCurrentIndex(dataIdx);
                    syncStylesFromDock();
                });
        QObject::connect(stylesCkptMirror, &QComboBox::currentTextChanged, dock,
                [dock, d, updateStylesCkptWarning, updateStyleAdvancedArchUi, persistStyleCheckpointOptions,
                 repopulateStyleArchitectureCombo, stylesCkptMirror](const QString &t) {
                    if (d->generate.comboCheckpoint) {
                        int fi = d->generate.comboCheckpoint->findText(t);
                        if (fi >= 0)
                            d->generate.comboCheckpoint->setCurrentIndex(fi);
                        else
                            d->generate.comboCheckpoint->setCurrentText(t);
                    }
                    const QString styleId = dock->encodeStyleIdFromPresetCombo(d->generate.comboPreset);
                    QString styleArch = QStringLiteral("auto");
                    if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                        styleArch = st->architecture;
                    repopulateStyleArchitectureCombo(styleArch);
                    updateStylesCkptWarning();
                    updateStyleAdvancedArchUi();
                    persistStyleCheckpointOptions();
                });
        QObject::connect(editStylesPositive, &QLineEdit::editingFinished, dock, [persistCurrentJsonStyle]() {
            persistCurrentJsonStyle();
        });
        QObject::connect(editStylesNegative, &QLineEdit::editingFinished, dock, [persistCurrentJsonStyle]() {
            persistCurrentJsonStyle();
        });
        const auto duplicateBuiltinForStyleEdit = [dock, d, syncStylesFromDock]() {
            const ComfyStyleEntry *st = dock->currentJsonStyleEntry();
            if (!st || !st->isBuiltin)
                return;
            dock->duplicateJsonStyle();
            syncStylesFromDock();
        };
        new ComfySettingsDialogBuilderInternal::ComfyBuiltinStyleEditFilter(editStylesPositive, duplicateBuiltinForStyleEdit, dlg);
        new ComfySettingsDialogBuilderInternal::ComfyBuiltinStyleEditFilter(editStylesNegative, duplicateBuiltinForStyleEdit, dlg);
        QObject::connect(checkShowBuiltinStyles, &QCheckBox::toggled, dlg, [dock, d, syncStylesFromDock](bool on) {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("show_builtin_styles"), on);
            ComfyUIUtils::saveSettingsJson(s);
            dock->rebuildPresetComboItems();
            syncStylesFromDock();
        });
        QObject::connect(comboLinkedEditStyle, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [persistCurrentJsonStyle]() { persistCurrentJsonStyle(); });
        QObject::connect(editStyleName, &QLineEdit::editingFinished, dock,
                [dock, d, editStyleName, &stylesTabPresetNameBaseline, persistCurrentJsonStyle, syncStylesFromDock]() {
                    const ComfyStyleEntry *st = dock->currentJsonStyleEntry();
                    if (!st || st->isBuiltin)
                        return;
                    const QString oldName = stylesTabPresetNameBaseline.trimmed();
                    const QString newName = editStyleName->text().trimmed();
                    if (newName.isEmpty() || oldName == newName)
                        return;
                    if (!ComfyStyleCollection::instance().renameStyle(st->styleId, newName)) {
                        editStyleName->blockSignals(true);
                        editStyleName->setText(oldName);
                        editStyleName->blockSignals(false);
                        return;
                    }
                    stylesTabPresetNameBaseline = newName;
                    dock->rebuildPresetComboItems();
                    dock->applyStyleIdToPresetCombo(d->generate.comboPreset, st->styleId);
                    syncStylesFromDock();
                });

    result.syncFromDock = syncStylesFromDock;
    return result;
}

} // namespace ComfySettingsDialogBuilderStylesInternal
