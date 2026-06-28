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

void buildStylesTabWidgets(StylesWorkspace &ws)
{
    ComfyUIRemoteDock *dock = ws.dock;
    ComfyUIRemoteDock::Private *d = ws.d;
    QDialog *dlg = ws.dialog;
    QStackedWidget *stack = ws.stack;

        // Styles tab (index 1) — §4.5 Style Presets (mirrors dock; LoRA list + samplers.json presets partial / stub)
        QWidget *stylesPage = new QWidget(dlg);
        QVBoxLayout *stylesOuter = new QVBoxLayout(stylesPage);
        QScrollArea *stylesScroll = new QScrollArea(stylesPage);
        stylesScroll->setWidgetResizable(true);
        stylesScroll->setFrameShape(QFrame::NoFrame);
        QWidget *stylesInner = new QWidget();
        QVBoxLayout *stylesLayout = new QVBoxLayout(stylesInner);
        QLabel *stylesHeading = new QLabel(ComfyTr::tr("Style Presets"), stylesInner);
        QFont stylesHeadingFont = stylesHeading->font();
        stylesHeadingFont.setBold(true);
        stylesHeading->setFont(stylesHeadingFont);
        stylesLayout->addWidget(stylesHeading);

        QFrame *styleToolbarFrame = new QFrame(stylesInner);
        styleToolbarFrame->setFrameStyle(QFrame::StyledPanel);
        styleToolbarFrame->setLineWidth(1);
        QVBoxLayout *styleToolbarLayout = new QVBoxLayout(styleToolbarFrame);
        QHBoxLayout *presetBtnRow = new QHBoxLayout();
        ws.stylesPresetMirror = new QComboBox(styleToolbarFrame);
        ws.btnStylesAddPreset = new QToolButton(styleToolbarFrame);
        ws.btnStylesAddPreset->setIcon(ComfyTheme::icon(QStringLiteral("control-add")));
        ws.btnStylesAddPreset->setToolTip(ComfyTr::tr("Create a new style"));
        ws.btnStylesAddPreset->setAutoRaise(true);
        ws.btnStylesDuplicate = new QToolButton(styleToolbarFrame);
        ws.btnStylesDuplicate->setIcon(ComfyTheme::icon(QStringLiteral("edit")));
        ws.btnStylesDuplicate->setToolTip(ComfyTr::tr("Duplicate the current style"));
        ws.btnStylesDuplicate->setAutoRaise(true);
        ws.btnStylesDeletePreset = new QToolButton(styleToolbarFrame);
        ws.btnStylesDeletePreset->setIcon(ComfyTheme::icon(QStringLiteral("discard")));
        ws.btnStylesDeletePreset->setToolTip(ComfyTr::tr("Delete the current style"));
        ws.btnStylesDeletePreset->setAutoRaise(true);
        ws.btnStylesRefresh = new QToolButton(styleToolbarFrame);
        ws.btnStylesRefresh->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
        ws.btnStylesRefresh->setToolTip(ComfyTr::tr("Look for new style files"));
        ws.btnStylesRefresh->setAutoRaise(true);
        QObject::connect(ws.btnStylesDeletePreset, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotDeletePreset);
        presetBtnRow->addWidget(ws.stylesPresetMirror, 1);
        presetBtnRow->addWidget(ws.btnStylesAddPreset);
        presetBtnRow->addWidget(ws.btnStylesDuplicate);
        presetBtnRow->addWidget(ws.btnStylesDeletePreset);
        presetBtnRow->addWidget(ws.btnStylesRefresh);
        styleToolbarLayout->addLayout(presetBtnRow);

        ws.lblBuiltinMessage = new QLabel(ComfyTr::tr("Built-in styles cannot be modified."), styleToolbarFrame);
        ws.lblBuiltinMessage->setStyleSheet(QStringLiteral("font-style: italic;"));
        ws.lblBuiltinMessage->hide();
        ws.lblBuiltinCopyLink = new QLabel(
            QStringLiteral("<a href=\"copy\">%1</a>").arg(ComfyTr::tr("Click to edit a copy")), styleToolbarFrame);
        ws.lblBuiltinCopyLink->setTextFormat(Qt::RichText);
        ws.lblBuiltinCopyLink->setTextInteractionFlags(Qt::TextBrowserInteraction);
        ws.lblBuiltinCopyLink->setOpenExternalLinks(false);
        ws.lblBuiltinCopyLink->hide();
        QHBoxLayout *builtinLayout = new QHBoxLayout();
        builtinLayout->setContentsMargins(6, 1, 1, 1);
        builtinLayout->addWidget(ws.lblBuiltinMessage);
        builtinLayout->addWidget(ws.lblBuiltinCopyLink);
        builtinLayout->addStretch();
        ws.checkShowBuiltinStyles = new QCheckBox(ComfyTr::tr("Show pre-installed styles"), styleToolbarFrame);
        ws.checkShowBuiltinStyles->setChecked(
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true));
        builtinLayout->addWidget(ws.checkShowBuiltinStyles);
        styleToolbarLayout->addLayout(builtinLayout);
        stylesLayout->addWidget(styleToolbarFrame);

        auto addStylesBoldHeader = [stylesInner](const QString &title) -> QLabel * {
            QLabel *titleLabel = new QLabel(title, stylesInner);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            return titleLabel;
        };
        auto addStylesSettingRow = [stylesInner](const QString &title, const QString &description, QWidget *control) -> QWidget * {
            QWidget *row = new QWidget(stylesInner);
            QHBoxLayout *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            QVBoxLayout *textCol = new QVBoxLayout();
            textCol->setContentsMargins(0, 0, 0, 0);
            textCol->setSpacing(2);
            QLabel *titleLabel = new QLabel(title, row);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            textCol->addWidget(titleLabel);
            if (!description.isEmpty()) {
                QLabel *descLabel = new QLabel(description, row);
                descLabel->setWordWrap(true);
                textCol->addWidget(descLabel);
            }
            rowLayout->addLayout(textCol, 5);
            if (control)
                rowLayout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
            else
                rowLayout->addStretch(1);
            return row;
        };
        // Python LineEditSetting: header block then full-width line edit below.
        auto addStylesLineEditBlock = [stylesInner, addStylesBoldHeader](const QString &title, const QString &description,
                                                                         QLineEdit *edit) -> QWidget * {
            QWidget *block = new QWidget(stylesInner);
            QVBoxLayout *blockLayout = new QVBoxLayout(block);
            blockLayout->setContentsMargins(0, 4, 0, 4);
            blockLayout->setSpacing(4);
            blockLayout->addWidget(addStylesBoldHeader(title));
            if (!description.isEmpty()) {
                QLabel *descLabel = new QLabel(description, block);
                descLabel->setWordWrap(true);
                blockLayout->addWidget(descLabel);
            }
            edit->setMinimumWidth(0);
            edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            blockLayout->addWidget(edit);
            return block;
        };

        ws.editStyleName = new QLineEdit(stylesInner);
        stylesLayout->addWidget(addStylesSettingRow(ComfyTr::tr("Name"), QString(), ws.editStyleName));

        QHBoxLayout *ckptRow = new QHBoxLayout();
        ws.stylesCkptMirror = new QComboBox(stylesInner);
        ws.stylesCkptMirror->setEditable(true);
        ws.stylesCkptMirror->setMinimumWidth(230);
        ws.stylesCkptMirror->setPlaceholderText(ComfyTr::tr("The Diffusion model checkpoint file"));
        ws.btnStylesCkptRefresh = new QToolButton(stylesInner);
        ws.btnStylesCkptRefresh->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
        ws.btnStylesCkptRefresh->setToolTip(ComfyTr::tr("Look for new checkpoint files"));
        ws.btnStylesCkptRefresh->setAutoRaise(true);
        QObject::connect(ws.btnStylesCkptRefresh, &QToolButton::clicked, dock, &ComfyUIRemoteDock::slotRefreshCheckpoints);
        ckptRow->addWidget(ws.stylesCkptMirror, 1);
        ckptRow->addWidget(ws.btnStylesCkptRefresh);
        QWidget *ckptControl = new QWidget(stylesInner);
        ckptControl->setLayout(ckptRow);
        stylesLayout->addWidget(addStylesSettingRow(
            ComfyTr::tr("Model Checkpoint"),
            ComfyTr::tr("The Diffusion model checkpoint file"),
            ckptControl));
        ws.stylesCkptWarning = new QLabel(stylesInner);
        ws.stylesCkptWarning->setWordWrap(true);
        ws.stylesCkptWarning->setStyleSheet(QStringLiteral("color: #b8860b; font-style: italic;"));
        ws.stylesCkptWarning->setAlignment(Qt::AlignRight);
        ws.stylesCkptWarning->hide();
        stylesLayout->addWidget(ws.stylesCkptWarning);

        auto wireDisclosure = [](QToolButton *tb, QWidget *body, bool startOpen) {
            tb->setCheckable(true);
            tb->setChecked(startOpen);
            tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            tb->setArrowType(startOpen ? Qt::DownArrow : Qt::RightArrow);
            tb->setStyleSheet(QStringLiteral("QToolButton { border: none; font-weight: bold; text-align: left; }"));
            body->setVisible(startOpen);
            QObject::connect(tb, &QToolButton::toggled, body, &QWidget::setVisible);
            QObject::connect(tb, &QToolButton::toggled, tb, [tb](bool on) { tb->setArrowType(on ? Qt::DownArrow : Qt::RightArrow); });
        };
        ws.toggleAdvCkpt = new QToolButton(stylesInner);
        ws.toggleAdvCkpt->setText(ComfyTr::tr("Checkpoint configuration (advanced)"));
        ws.advCkptBody = new QWidget(stylesInner);
        QVBoxLayout *advCkptLay = new QVBoxLayout(ws.advCkptBody);
        advCkptLay->setContentsMargins(0, 0, 0, 0);
        advCkptLay->setSpacing(0);
        auto addAdvCkptSettingRow = [&ws, advCkptLay](const QString &title, const QString &description) -> QHBoxLayout * {
            QWidget *row = new QWidget(ws.advCkptBody);
            QHBoxLayout *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(16, 4, 0, 4);
            QVBoxLayout *textCol = new QVBoxLayout();
            textCol->setContentsMargins(0, 0, 0, 0);
            textCol->setSpacing(2);
            QLabel *titleLabel = new QLabel(title, row);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            textCol->addWidget(titleLabel);
            if (!description.isEmpty()) {
                QLabel *descLabel = new QLabel(description, row);
                descLabel->setWordWrap(true);
                textCol->addWidget(descLabel);
            }
            rowLayout->addLayout(textCol, 5);
            rowLayout->addStretch(1);
            advCkptLay->addWidget(row);
            return rowLayout;
        };
        ws.comboStyleArchitecture = new QComboBox(ws.advCkptBody);
        ws.comboStyleArchitecture->setMinimumWidth(230);
        ws.comboStyleArchitecture->setToolTip(
            ComfyTr::tr("Architecture for the checkpoint. Automatic resolves from the model at generate time."));
        ws.comboStyleVae = new QComboBox(ws.advCkptBody);
        ws.comboStyleVae->setMinimumWidth(230);
        ws.comboStyleVae->setToolTip(ComfyTr::tr("VAE used for encode/decode. Connect to the server and refresh to list installed VAE files."));
        ws.spinStyleClipSkip = new QSpinBox(ws.advCkptBody);
        ws.spinStyleClipSkip->setRange(0, 12);
        ws.spinStyleClipSkip->setMinimumWidth(100);
        ws.spinStyleClipSkip->setSpecialValueText(ComfyTr::tr("Default"));
        ws.spinStyleClipSkip->setToolTip(ComfyTr::tr("CLIP skip layers (SD 1.5 / SDXL / Illustrious). 0 uses the checkpoint default."));
        ws.checkStyleClipSkipOverride = new QCheckBox(ComfyTr::tr("Override"), ws.advCkptBody);
        ws.spinStylePreferredResolution = new QSpinBox(ws.advCkptBody);
        ws.spinStylePreferredResolution->setRange(0, 2048);
        ws.spinStylePreferredResolution->setSingleStep(8);
        ws.spinStylePreferredResolution->setMinimumWidth(100);
        ws.spinStylePreferredResolution->setSpecialValueText(ComfyTr::tr("Default"));
        ws.spinStylePreferredResolution->setToolTip(ComfyTr::tr("When enabled, sets generate width/height to dock square size when the style is applied."));
        ws.checkStylePreferredResolution = new QCheckBox(ComfyTr::tr("Override"), ws.advCkptBody);
        ws.switchStyleZsnr = new ComfySwitchWidget(ws.advCkptBody);
        ws.switchStyleZsnr->setToolTip(ComfyTr::tr("v-prediction zsnr (saved to style JSON; workflow nodes deferred)."));
        ws.labelStyleZsnrState = new QLabel(ComfyTr::tr("Off"), ws.advCkptBody);
        ws.switchStyleSag = new ComfySwitchWidget(ws.advCkptBody);
        ws.switchStyleSag->setToolTip(ComfyTr::tr("Self-attention guidance (saved to style JSON; workflow nodes deferred)."));
        ws.labelStyleSagState = new QLabel(ComfyTr::tr("Off"), ws.advCkptBody);
        {
            QHBoxLayout *archRow = addAdvCkptSettingRow(
                ComfyTr::tr("Diffusion Architecture"),
                ComfyTr::tr("The base model ecosystem which the selected checkpoint belongs to."));
            archRow->addWidget(ws.comboStyleArchitecture);
        }
        {
            QHBoxLayout *vaeRow = addAdvCkptSettingRow(
                ComfyTr::tr("VAE"),
                ComfyTr::tr("Model to encode and decode images. Commonly affects saturation and sharpness."));
            vaeRow->addWidget(ws.comboStyleVae);
        }
        {
            QHBoxLayout *clipRow = addAdvCkptSettingRow(
                ComfyTr::tr("Clip Skip"),
                ComfyTr::tr("Clip layers to omit at the end. Some checkpoints prefer a different value than the default."));
            clipRow->addWidget(ws.checkStyleClipSkipOverride);
            clipRow->addWidget(ws.spinStyleClipSkip);
        }
        {
            QHBoxLayout *resRow = addAdvCkptSettingRow(
                ComfyTr::tr("Preferred Resolution"),
                ComfyTr::tr("Image resolution the checkpoint was trained on"));
            resRow->addWidget(ws.checkStylePreferredResolution);
            resRow->addWidget(ws.spinStylePreferredResolution);
        }
        {
            QHBoxLayout *zsnrRow = addAdvCkptSettingRow(
                ComfyTr::tr("V-Prediction / Zero Terminal SNR"),
                ComfyTr::tr("Enable dock if the checkpoint is a v-prediction model which requires zero terminal SNR noise schedule"));
            zsnrRow->addWidget(ws.labelStyleZsnrState);
            zsnrRow->addWidget(ws.switchStyleZsnr);
        }
        {
            QHBoxLayout *sagRow = addAdvCkptSettingRow(
                ComfyTr::tr("Enable SAG / Self-Attention Guidance"),
                ComfyTr::tr("Pay more attention to difficult parts of the image. Can improve fine details."));
            sagRow->addWidget(ws.labelStyleSagState);
            sagRow->addWidget(ws.switchStyleSag);
        }
        QObject::connect(ws.switchStyleZsnr, &QAbstractButton::toggled, ws.labelStyleZsnrState, [&ws](bool on) {
            ws.labelStyleZsnrState->setText(on ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        });
        QObject::connect(ws.switchStyleSag, &QAbstractButton::toggled, ws.labelStyleSagState, [&ws](bool on) {
            ws.labelStyleSagState->setText(on ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
        });
        wireDisclosure(ws.toggleAdvCkpt, ws.advCkptBody, false);
        stylesLayout->addWidget(ws.toggleAdvCkpt);
        stylesLayout->addWidget(ws.advCkptBody);

        ws.loraListWidget = new ComfyStyleLoraListWidget(stylesInner);
        d->stylesTabLoraListWidget = ws.loraListWidget;
        stylesLayout->addWidget(ws.loraListWidget);

        ws.editStylesPositive = new QLineEdit(stylesInner);
        stylesLayout->addWidget(addStylesLineEditBlock(
            ComfyTr::tr("Style Prompt"),
            ComfyTr::tr("Text which is appended to all prompts. The {prompt} placeholder can be used to wrap prompts."),
            ws.editStylesPositive));

        ws.editStylesNegative = new QLineEdit(stylesInner);
        stylesLayout->addWidget(addStylesLineEditBlock(
            ComfyTr::tr("Negative Prompt"),
            ComfyTr::tr("Textual description of things to avoid in generated images."),
            ws.editStylesNegative));

        ws.comboLinkedEditStyle = new QComboBox(stylesInner);
        ws.comboLinkedEditStyle->setMinimumWidth(230);
        ws.linkedEditStyleRow = addStylesSettingRow(
            ComfyTr::tr("Linked Edit Style"),
            ComfyTr::tr("Select an alternative style for instruction-based editing"),
            ws.comboLinkedEditStyle);
        stylesLayout->addWidget(ws.linkedEditStyleRow);

        QLabel *samplerSectionDesc = new QLabel(
            ComfyTr::tr("Configure sampler type, steps and CFG to tweak the quality of generated images."), stylesInner);
        samplerSectionDesc->setWordWrap(true);
        stylesLayout->addWidget(addStylesBoldHeader(ComfyTr::tr("Sampler Settings")));
        stylesLayout->addWidget(samplerSectionDesc);

        ws.qualitySamplerWidget = new ComfyStyleSamplerWidget(ComfyStyleSamplerWidget::Kind::Quality, stylesInner);
        ws.liveSamplerWidget = new ComfyStyleSamplerWidget(ComfyStyleSamplerWidget::Kind::Live, stylesInner);
        stylesLayout->addWidget(ws.qualitySamplerWidget);
        stylesLayout->addWidget(ws.liveSamplerWidget);
        stylesLayout->addStretch();
        stylesScroll->setWidget(stylesInner);
        stylesOuter->addWidget(stylesScroll);
        ws.stack->addWidget(stylesPage);

}

} // namespace ComfySettingsDialogBuilderStylesInternal
