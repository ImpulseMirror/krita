/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyComboBox.h"
#include "ComfySettingsDialogBuilder.h"
#include "ComfyFormUi.h"
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

namespace ComfySettingsDialogBuilder {

void buildInterfaceTab(const Context &ctx, QStackedWidget *stack)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    QDialog *dlg = ctx.dialog;
        // Interface tab (index 3) — Python InterfaceSettings (settings.py L699–793)
        ComfyFormUi::ScrollTab ifaceTab =
            ComfyFormUi::createScrollTab(dlg, ComfyTr::tr("Interface Settings"));
        QWidget *ifaceInner = ifaceTab.inner;
        QVBoxLayout *interfaceLayout = ifaceTab.innerLayout;

        QJsonObject ifaceSettings = ComfyUIUtils::loadSettingsJson();

        QComboBox *comboLanguage = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addComboRow(
            ifaceInner,
            ComfyTr::tr("Language"),
            ComfyTr::tr("Interface language used by the plugin - requires restart!"),
            &comboLanguage));
        const QList<ComfyLanguageInfo> availableLangs = ComfyLocalization::instance().availableLanguages();
        for (const ComfyLanguageInfo &lang : availableLangs)
            comboLanguage->addItem(lang.name, lang.id);
        if (comboLanguage->count() == 0)
            comboLanguage->addItem(ComfyTr::tr("English"), QStringLiteral("en"));
        {
            QString curLang = ifaceSettings.value(QStringLiteral("interface_language")).toString();
            if (curLang.isEmpty())
                curLang = ifaceSettings.value(QStringLiteral("language")).toString();
            if (curLang.isEmpty())
                curLang = ComfyLocalization::instance().languageId();
            curLang = curLang.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
            const int li = comboLanguage->findData(curLang);
            comboLanguage->setCurrentIndex(li >= 0 ? li : 0);
        }

        QComboBox *comboPromptTranslation = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addComboRow(
            ifaceInner,
            ComfyTr::tr("Prompt Translation"),
            ComfyTr::tr("Translate text prompts from the selected language to English"),
            &comboPromptTranslation));
        d->settingsPromptTranslationCombo = comboPromptTranslation;

        QSpinBox *spinPromptLines = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addSpinRow(
            ifaceInner,
            ComfyTr::tr("Prompt Line Count"),
            ComfyTr::tr("Size of the text editor for image descriptions"),
            &spinPromptLines,
            1,
            10));
        spinPromptLines->setValue(ifaceSettings.value(QStringLiteral("prompt_line_count")).toInt(3));

        auto showNegativeRow = ComfyFormUi::addSwitchRow(
            ifaceInner,
            ComfyTr::tr("Negative Prompt"),
            ComfyTr::tr("Show text editor to describe things to avoid"),
            ComfyTr::tr("Show"),
            ComfyTr::tr("Hide"));
        showNegativeRow.setChecked(ifaceSettings.value(QStringLiteral("show_negative_prompt")).toBool(false));
        interfaceLayout->addWidget(showNegativeRow.row);
        ComfySwitchWidget *switchShowNegative = showNegativeRow.switchWidget;

        auto showStepsRow = ComfyFormUi::addSwitchRow(
            ifaceInner,
            ComfyTr::tr("Show Steps"),
            ComfyTr::tr("Display the number of steps to be evaluated in the weights box."),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"));
        showStepsRow.setChecked(ifaceSettings.value(QStringLiteral("show_steps")).toBool(false));
        interfaceLayout->addWidget(showStepsRow.row);
        ComfySwitchWidget *switchShowSteps = showStepsRow.switchWidget;

        QSpinBox *spinRecentStyles = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addSpinRow(
            ifaceInner,
            ComfyTr::tr("Recent Styles"),
            ComfyTr::tr("Number of most recently used styles to show at the top of the style list"),
            &spinRecentStyles,
            0,
            10));
        spinRecentStyles->setValue(ifaceSettings.value(QStringLiteral("recent_styles_count")).toInt(4));

        // Tag auto-completion — bundled tag CSV lists (Danbooru / e621 + NSFW variants)
        QLabel *tagStateLabel = nullptr;
        QCheckBox *chkTagDanbooru = nullptr;
        QCheckBox *chkTagDanbooruNsfw = nullptr;
        QCheckBox *chkTagE621 = nullptr;
        QCheckBox *chkTagE621Nsfw = nullptr;
        {
            QSet<QString> selectedTagStems;
            const QJsonArray tf = ifaceSettings.value(QStringLiteral("tag_files")).toArray();
            if (tf.isEmpty()) {
                selectedTagStems.insert(QStringLiteral("Danbooru"));
                selectedTagStems.insert(QStringLiteral("e621"));
            } else {
                for (const QJsonValue &v : tf)
                    selectedTagStems.insert(v.toString());
            }

            auto *tagRow = new QWidget(ifaceInner);
            auto *tagRowLayout = new QHBoxLayout(tagRow);
            tagRowLayout->setContentsMargins(0, 4, 0, 4);
            tagRowLayout->addWidget(ComfyFormUi::makeLabelColumn(
                tagRow,
                ComfyTr::tr("Tag Auto-Completion"),
                ComfyTr::tr("Enable text completion for tags from the selected files")),
                                   1);
            tagStateLabel = new QLabel(ComfyTr::tr("Disabled"), tagRow);
            tagRowLayout->addWidget(tagStateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            interfaceLayout->addWidget(tagRow);

            auto *tagListRow = new QWidget(ifaceInner);
            auto *tagListLayout = new QHBoxLayout(tagListRow);
            tagListLayout->setContentsMargins(16, 0, 0, 4);
            chkTagDanbooru = new ComfyCheckBox(ComfyTr::tr("Danbooru"), tagListRow);
            chkTagDanbooru->setProperty("tagStem", QStringLiteral("Danbooru"));
            chkTagDanbooruNsfw = new ComfyCheckBox(ComfyTr::tr("Danbooru NSFW"), tagListRow);
            chkTagDanbooruNsfw->setProperty("tagStem", QStringLiteral("Danbooru NSFW"));
            chkTagE621 = new ComfyCheckBox(ComfyTr::tr("e621"), tagListRow);
            chkTagE621->setProperty("tagStem", QStringLiteral("e621"));
            chkTagE621Nsfw = new ComfyCheckBox(ComfyTr::tr("e621 NSFW"), tagListRow);
            chkTagE621Nsfw->setProperty("tagStem", QStringLiteral("e621 NSFW"));
            chkTagDanbooru->setChecked(selectedTagStems.contains(QStringLiteral("Danbooru")));
            chkTagDanbooruNsfw->setChecked(selectedTagStems.contains(QStringLiteral("Danbooru NSFW")));
            chkTagE621->setChecked(selectedTagStems.contains(QStringLiteral("e621")));
            chkTagE621Nsfw->setChecked(selectedTagStems.contains(QStringLiteral("e621 NSFW")));
            tagListLayout->addWidget(chkTagDanbooru);
            tagListLayout->addWidget(chkTagDanbooruNsfw);
            tagListLayout->addWidget(chkTagE621);
            tagListLayout->addWidget(chkTagE621Nsfw);
            tagListLayout->addStretch();
            interfaceLayout->addWidget(tagListRow);

            const auto syncTagStateLabel = [tagStateLabel, chkTagDanbooru, chkTagDanbooruNsfw, chkTagE621, chkTagE621Nsfw]() {
                const bool any = chkTagDanbooru->isChecked() || chkTagDanbooruNsfw->isChecked()
                    || chkTagE621->isChecked() || chkTagE621Nsfw->isChecked();
                tagStateLabel->setText(any ? ComfyTr::tr("Enabled") : ComfyTr::tr("Disabled"));
            };
            syncTagStateLabel();

            QPushButton *btnLookNewTagFiles = new QPushButton(ComfyTr::tr("Look for new tag files"), ifaceInner);
            btnLookNewTagFiles->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
            QPushButton *btnOpenTagFolder = new QPushButton(
                ComfyTr::tr("Open folder where custom tag files can be placed"),
                ifaceInner);
            btnOpenTagFolder->setIcon(ComfyTheme::icon(QStringLiteral("root")));
            auto *tagActionRow = new QWidget(ifaceInner);
            auto *tagActionLayout = new QHBoxLayout(tagActionRow);
            tagActionLayout->setContentsMargins(16, 0, 0, 4);
            tagActionLayout->addWidget(btnLookNewTagFiles);
            tagActionLayout->addWidget(btnOpenTagFolder);
            tagActionLayout->addStretch();
            interfaceLayout->addWidget(tagActionRow);

            QObject::connect(btnLookNewTagFiles, &QPushButton::clicked, dock, [dock, d, syncTagStateLabel](bool) {
                dock->refreshPromptTagCompleter();
                syncTagStateLabel();
            });
            QObject::connect(btnOpenTagFolder, &QPushButton::clicked, dlg, [](bool) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(ComfyUIUtils::tagsStorageDir()));
            });
        }

        QComboBox *comboFinishedAction = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addComboRow(
            ifaceInner,
            ComfyTr::tr("Finished Generation"),
            ComfyTr::tr("Action to take when an image generation job finishes"),
            &comboFinishedAction));
        comboFinishedAction->addItem(ComfyTr::tr("Do Nothing"), QStringLiteral("none"));
        comboFinishedAction->addItem(ComfyTr::tr("Preview"), QStringLiteral("preview"));
        comboFinishedAction->addItem(ComfyTr::tr("Apply"), QStringLiteral("apply"));
        {
            QString finAction = ifaceSettings.value(QStringLiteral("generation_finished_action")).toString();
            if (finAction.isEmpty())
                finAction = QStringLiteral("preview");
            const int finIdx = comboFinishedAction->findData(finAction);
            comboFinishedAction->setCurrentIndex(finIdx >= 0 ? finIdx : 1);
        }

        QComboBox *comboApplyBehavior = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addComboRow(
            ifaceInner,
            ComfyTr::tr("Apply Behavior"),
            ComfyTr::tr("Choose how result images are applied to the canvas (generation workspaces)"),
            &comboApplyBehavior));
        comboApplyBehavior->addItem(ComfyTr::tr("Modify active layer"), QStringLiteral("replace"));
        comboApplyBehavior->addItem(ComfyTr::tr("New layer on top"), QStringLiteral("layer"));
        comboApplyBehavior->addItem(ComfyTr::tr("New layer above active"), QStringLiteral("layer_active"));
        {
            QString applyBeh = ifaceSettings.value(QStringLiteral("apply_behavior")).toString();
            if (applyBeh.isEmpty())
                applyBeh = QStringLiteral("layer");
            const int applyIdx = comboApplyBehavior->findData(applyBeh);
            comboApplyBehavior->setCurrentIndex(applyIdx >= 0 ? applyIdx : 1);
        }

        QComboBox *comboApplyRegionBehavior = new ComfyComboBox(ifaceInner);
        comboApplyRegionBehavior->setMinimumWidth(230);
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Do not update regions"), QStringLiteral("none"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Modify region layers"), QStringLiteral("replace"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Layer group"), QStringLiteral("layer_group"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Layer group + mask"), QStringLiteral("transparency_mask"));
        comboApplyRegionBehavior->addItem(ComfyTr::tr("Layer group (don't hide)"), QStringLiteral("no_hide"));
        {
            QString savedRegionBehavior = ifaceSettings.value(QStringLiteral("apply_region_behavior")).toString();
            if (savedRegionBehavior.isEmpty())
                savedRegionBehavior = QStringLiteral("layer_group");
            const int regionIdx = comboApplyRegionBehavior->findData(savedRegionBehavior);
            comboApplyRegionBehavior->setCurrentIndex(regionIdx >= 0 ? regionIdx : 2);
        }
        comboApplyRegionBehavior->setToolTip(
            ComfyTr::tr("When applying a result that was generated with regions, how to place the result per region."));
        {
            auto *regionRow = new QWidget(ifaceInner);
            auto *regionLayout = new QHBoxLayout(regionRow);
            regionLayout->setContentsMargins(0, 4, 0, 4);
            regionLayout->addStretch(1);
            regionLayout->addWidget(comboApplyRegionBehavior, 0, Qt::AlignRight);
            interfaceLayout->addWidget(regionRow);
        }

        QComboBox *comboApplyBehaviorLive = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addComboRow(
            ifaceInner,
            ComfyTr::tr("Apply Behavior (Live)"),
            ComfyTr::tr("Choose how result images are applied to the canvas in Live mode"),
            &comboApplyBehaviorLive));
        comboApplyBehaviorLive->addItem(ComfyTr::tr("Modify active layer"), QStringLiteral("replace"));
        comboApplyBehaviorLive->addItem(ComfyTr::tr("New layer on top"), QStringLiteral("layer"));
        comboApplyBehaviorLive->addItem(ComfyTr::tr("New layer above active"), QStringLiteral("layer_active"));
        {
            QString applyBehLive = ifaceSettings.value(QStringLiteral("apply_behavior_live")).toString();
            if (applyBehLive.isEmpty())
                applyBehLive = QStringLiteral("replace");
            const int applyLiveIdx = comboApplyBehaviorLive->findData(applyBehLive);
            comboApplyBehaviorLive->setCurrentIndex(applyLiveIdx >= 0 ? applyLiveIdx : 0);
        }

        QComboBox *comboApplyRegionBehaviorLive = new ComfyComboBox(ifaceInner);
        comboApplyRegionBehaviorLive->setMinimumWidth(230);
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Do not update regions"), QStringLiteral("none"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Modify region layers"), QStringLiteral("replace"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Layer group"), QStringLiteral("layer_group"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Layer group + mask"), QStringLiteral("transparency_mask"));
        comboApplyRegionBehaviorLive->addItem(ComfyTr::tr("Layer group (don't hide)"), QStringLiteral("no_hide"));
        {
            QString savedRegionBehaviorLive = ifaceSettings.value(QStringLiteral("apply_region_behavior_live")).toString();
            if (savedRegionBehaviorLive.isEmpty())
                savedRegionBehaviorLive = QStringLiteral("replace");
            const int regionLiveIdx = comboApplyRegionBehaviorLive->findData(savedRegionBehaviorLive);
            comboApplyRegionBehaviorLive->setCurrentIndex(regionLiveIdx >= 0 ? regionLiveIdx : 1);
        }
        comboApplyRegionBehaviorLive->setToolTip(
            ComfyTr::tr("Same as apply region behavior, used when the Live workspace is active."));
        {
            auto *regionLiveRow = new QWidget(ifaceInner);
            auto *regionLiveLayout = new QHBoxLayout(regionLiveRow);
            regionLiveLayout->setContentsMargins(0, 4, 0, 4);
            regionLiveLayout->addStretch(1);
            regionLiveLayout->addWidget(comboApplyRegionBehaviorLive, 0, Qt::AlignRight);
            interfaceLayout->addWidget(regionLiveRow);
        }

        auto newSeedRow = ComfyFormUi::addSwitchRow(
            ifaceInner,
            ComfyTr::tr("Live: New Seed after Apply"),
            ComfyTr::tr("Pick a new seed after copying the result to the canvas in Live mode"),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"));
        newSeedRow.setChecked(ifaceSettings.value(QStringLiteral("new_seed_after_apply")).toBool(false));
        interfaceLayout->addWidget(newSeedRow.row);
        ComfySwitchWidget *switchNewSeedAfterApply = newSeedRow.switchWidget;

        QComboBox *comboSaveFormat = nullptr;
        interfaceLayout->addWidget(ComfyFormUi::addComboRow(
            ifaceInner,
            ComfyTr::tr("Save Image Format"),
            ComfyTr::tr("File format for saved images from thumbnails."),
            &comboSaveFormat));
        comboSaveFormat->addItem(ComfyTr::tr("PNG (fast)"), QStringLiteral("png_small"));
        comboSaveFormat->addItem(ComfyTr::tr("PNG"), QStringLiteral("png"));
        comboSaveFormat->addItem(ComfyTr::tr("WebP"), QStringLiteral("webp"));
        comboSaveFormat->addItem(ComfyTr::tr("WebP (lossless)"), QStringLiteral("webp_lossless"));
        comboSaveFormat->addItem(ComfyTr::tr("JPEG"), QStringLiteral("jpeg"));
        {
            QString sf = ifaceSettings.value(QStringLiteral("save_image_format")).toString();
            if (sf.isEmpty())
                sf = QStringLiteral("png_small");
            int sfi = comboSaveFormat->findData(sf);
            if (sfi < 0 && sf == QLatin1String("png"))
                sfi = comboSaveFormat->findData(QStringLiteral("png"));
            comboSaveFormat->setCurrentIndex(sfi >= 0 ? sfi : 0);
        }

        auto saveMetaRow = ComfyFormUi::addSwitchRow(
            ifaceInner,
            ComfyTr::tr("Save Image Metadata"),
            ComfyTr::tr("When saving generated images from thumbnails, include metadata in the PNG"),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"));
        saveMetaRow.setChecked(ifaceSettings.value(QStringLiteral("save_image_metadata")).toBool(false));
        interfaceLayout->addWidget(saveMetaRow.row);
        ComfySwitchWidget *switchSaveMeta = saveMetaRow.switchWidget;
        QLabel *labelSaveMetaState = saveMetaRow.stateLabel;

        auto dumpWorkflowRow = ComfyFormUi::addSwitchRow(
            ifaceInner,
            ComfyTr::tr("Dump Workflow"),
            ComfyTr::tr("Write latest ComfyUI prompt to the log folder for test & debug"),
            ComfyTr::tr("On"),
            ComfyTr::tr("Off"));
        const bool dumpOn = ifaceSettings.value(QStringLiteral("debug_dump_workflow")).toBool(false)
            || ifaceSettings.value(QStringLiteral("dump_workflow")).toBool(false);
        dumpWorkflowRow.setChecked(dumpOn);
        interfaceLayout->addWidget(dumpWorkflowRow.row);
        ComfySwitchWidget *switchDumpWorkflow = dumpWorkflowRow.switchWidget;

        auto updateSaveFormatSideEffects = [switchSaveMeta, labelSaveMetaState, comboSaveFormat]() {
            const QString f = comboSaveFormat->currentData().toString();
            const bool pngFmt = (f == QLatin1String("png") || f == QLatin1String("png_small"));
            switchSaveMeta->setEnabled(pngFmt);
            if (!pngFmt) {
                switchSaveMeta->setChecked(false);
                labelSaveMetaState->setText(ComfyTr::tr("Off"));
            }
        };
        updateSaveFormatSideEffects();

        auto saveIfaceSettings = [dock, comboFinishedAction, comboApplyBehavior, comboApplyBehaviorLive, switchNewSeedAfterApply,
                                  comboApplyRegionBehavior, comboApplyRegionBehaviorLive, comboLanguage,
                                  comboPromptTranslation, spinPromptLines, switchShowNegative, switchShowSteps,
                                  spinRecentStyles, comboSaveFormat, switchSaveMeta, switchDumpWorkflow,
                                  chkTagDanbooru, chkTagDanbooruNsfw, chkTagE621, chkTagE621Nsfw, tagStateLabel]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            const QString langId = comboLanguage->currentData().toString();
            s.insert(QStringLiteral("interface_language"), langId);
            s.insert(QStringLiteral("language"), langId);
            QString pt = comboPromptTranslation->currentData().toString();
            if (pt.isEmpty())
                pt = QStringLiteral("disabled");
            s.insert(QStringLiteral("prompt_translation"), pt);
            s.insert(QStringLiteral("prompt_line_count"), spinPromptLines->value());
            s.insert(QStringLiteral("show_negative_prompt"), switchShowNegative->isChecked());
            s.insert(QStringLiteral("show_steps"), switchShowSteps->isChecked());
            s.insert(QStringLiteral("recent_styles_count"), spinRecentStyles->value());
            QJsonArray tagFiles;
            const auto appendIfChecked = [&tagFiles](QCheckBox *cb) {
                if (cb && cb->isChecked()) {
                    const QString stem = cb->property("tagStem").toString();
                    tagFiles.append(stem.isEmpty() ? cb->text() : stem);
                }
            };
            appendIfChecked(chkTagDanbooru);
            appendIfChecked(chkTagDanbooruNsfw);
            appendIfChecked(chkTagE621);
            appendIfChecked(chkTagE621Nsfw);
            s.insert(QStringLiteral("tag_files"), tagFiles);
            if (tagStateLabel) {
                tagStateLabel->setText(tagFiles.isEmpty() ? ComfyTr::tr("Disabled") : ComfyTr::tr("Enabled"));
            }
            s.insert(QStringLiteral("save_image_format"), comboSaveFormat->currentData().toString());
            s.insert(QStringLiteral("save_image_metadata"), switchSaveMeta->isChecked());
            s.insert(QStringLiteral("debug_dump_workflow"), switchDumpWorkflow->isChecked());
            s.remove(QStringLiteral("dump_workflow"));
            s.insert(QStringLiteral("generation_finished_action"), comboFinishedAction->currentData().toString());
            s.insert(QStringLiteral("apply_behavior"), comboApplyBehavior->currentData().toString());
            s.insert(QStringLiteral("apply_behavior_live"), comboApplyBehaviorLive->currentData().toString());
            s.insert(QStringLiteral("new_seed_after_apply"), switchNewSeedAfterApply->isChecked());
            s.insert(QStringLiteral("apply_region_behavior"), comboApplyRegionBehavior->currentData().toString());
            s.insert(QStringLiteral("apply_region_behavior_live"), comboApplyRegionBehaviorLive->currentData().toString());
            ComfyUIUtils::saveSettingsJson(s);
            dock->applyInterfaceAppearanceSettings();
            dock->refreshPromptTagCompleter();
            dock->persistDocumentDefaultsToSettings();
        };

        QObject::connect(comboLanguage, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [comboLanguage, saveIfaceSettings](int) {
                    const QString newId = comboLanguage->currentData().toString();
                    if (newId != ComfyLocalization::instance().languageId()) {
                        QMessageBox::information(
                            comboLanguage,
                            ComfyTr::tr("Language"),
                            ComfyTr::tr("Restart Krita to apply the new interface language."));
                    }
                    saveIfaceSettings();
                });
        QObject::connect(comboPromptTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        QObject::connect(spinPromptLines, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        QObject::connect(switchShowNegative, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        QObject::connect(switchShowSteps, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        QObject::connect(spinRecentStyles, QOverload<int>::of(&QSpinBox::valueChanged), dlg, saveIfaceSettings);
        QObject::connect(comboSaveFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, [saveIfaceSettings, updateSaveFormatSideEffects](int) {
            updateSaveFormatSideEffects();
            saveIfaceSettings();
        });
        QObject::connect(switchSaveMeta, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        QObject::connect(switchDumpWorkflow, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        QObject::connect(comboFinishedAction, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        QObject::connect(comboApplyBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        QObject::connect(comboApplyBehaviorLive, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        QObject::connect(switchNewSeedAfterApply, &QAbstractButton::toggled, dlg, saveIfaceSettings);
        QObject::connect(comboApplyRegionBehavior, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        QObject::connect(comboApplyRegionBehaviorLive, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg, saveIfaceSettings);
        QObject::connect(chkTagDanbooru, &QCheckBox::toggled, dlg, saveIfaceSettings);
        QObject::connect(chkTagDanbooruNsfw, &QCheckBox::toggled, dlg, saveIfaceSettings);
        QObject::connect(chkTagE621, &QCheckBox::toggled, dlg, saveIfaceSettings);
        QObject::connect(chkTagE621Nsfw, &QCheckBox::toggled, dlg, saveIfaceSettings);

        dock->refreshInterfacePromptTranslationCombo();
        {
            QString pt = ifaceSettings.value(QStringLiteral("prompt_translation")).toString().trimmed();
            if (pt.isEmpty() || pt == QLatin1String("disabled"))
                comboPromptTranslation->setCurrentIndex(0);
            else {
                const int pti = comboPromptTranslation->findData(pt);
                if (pti >= 0)
                    comboPromptTranslation->setCurrentIndex(pti);
            }
        }

        interfaceLayout->addStretch();
        stack->addWidget(ifaceTab.page);

}


} // namespace ComfySettingsDialogBuilder
