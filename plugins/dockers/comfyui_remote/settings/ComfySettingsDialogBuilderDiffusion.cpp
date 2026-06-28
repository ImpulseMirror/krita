/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

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

namespace ComfySettingsDialogBuilder {

void buildDiffusionTab(const Context &ctx, QStackedWidget *stack)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    ComfyUIRemoteDock::Private *d = ctx.d;
    QDialog *dlg = ctx.dialog;
        // Diffusion tab (index 2) — Python DiffusionSettings (settings.py L669–697)
        QWidget *diffusionPage = new QWidget(dlg);
        QVBoxLayout *diffusionOuter = new QVBoxLayout(diffusionPage);
        diffusionOuter->setContentsMargins(0, 0, 0, 0);
        QScrollArea *diffusionScroll = new QScrollArea(diffusionPage);
        diffusionScroll->setWidgetResizable(true);
        diffusionScroll->setFrameShape(QFrame::NoFrame);
        diffusionScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        QWidget *diffusionInner = new QWidget();
        QVBoxLayout *diffusionLayout = new QVBoxLayout(diffusionInner);
        QLabel *diffHeading = new QLabel(ComfyTr::tr("Diffusion Settings"), diffusionInner);
        QFont diffFont = diffHeading->font();
        diffFont.setBold(true);
        diffFont.setPointSize(diffFont.pointSize() + 2);
        diffHeading->setFont(diffFont);
        diffusionLayout->addWidget(diffHeading);
        diffusionLayout->addSpacing(6);

        auto makeDiffusionLabelColumn = [](QWidget *parent, const QString &title, const QString &description) -> QWidget * {
            auto *col = new QWidget(parent);
            auto *colLayout = new QVBoxLayout(col);
            colLayout->setContentsMargins(0, 0, 0, 0);
            colLayout->setSpacing(2);
            auto *titleLabel = new QLabel(title, col);
            QFont titleFont = titleLabel->font();
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            colLayout->addWidget(titleLabel);
            if (!description.isEmpty()) {
                auto *descLabel = new QLabel(description, col);
                descLabel->setWordWrap(true);
                colLayout->addWidget(descLabel);
            }
            col->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            return col;
        };
        auto addDiffusionSliderRow = [makeDiffusionLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                        QSlider **outSlider, QLabel **outValueLabel, int min, int max,
                                        const QString &valueFormat) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeDiffusionLabelColumn(row, title, description), 1);
            auto *sliderBox = new QWidget(row);
            auto *sliderLayout = new QHBoxLayout(sliderBox);
            sliderLayout->setContentsMargins(0, 0, 0, 0);
            auto *slider = new QSlider(Qt::Horizontal, sliderBox);
            slider->setMinimumWidth(200);
            slider->setMaximumWidth(300);
            slider->setRange(min, max);
            auto *valueLabel = new QLabel(sliderBox);
            const QFontMetrics fm(valueLabel->font());
            valueLabel->setMinimumWidth(fm.horizontalAdvance(QStringLiteral("555 px")));
            valueLabel->setText(valueFormat);
            sliderLayout->addWidget(slider);
            sliderLayout->addWidget(valueLabel);
            rowLayout->addWidget(sliderBox, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSlider = slider;
            *outValueLabel = valueLabel;
            return row;
        };
        auto addDiffusionSwitchRow = [makeDiffusionLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                        ComfySwitchWidget **outSwitch, QLabel **outStateLabel) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeDiffusionLabelColumn(row, title, description), 1);
            auto *stateLabel = new QLabel(ComfyTr::tr("Off"), row);
            auto *sw = new ComfySwitchWidget(row);
            rowLayout->addWidget(stateLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            rowLayout->addWidget(sw, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outSwitch = sw;
            *outStateLabel = stateLabel;
            return row;
        };
        auto addDiffusionComboRow = [makeDiffusionLabelColumn](QWidget *parent, const QString &title, const QString &description,
                                       QComboBox **outCombo) -> QWidget * {
            auto *row = new QWidget(parent);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 4, 0, 4);
            rowLayout->addWidget(makeDiffusionLabelColumn(row, title, description), 1);
            auto *combo = new QComboBox(row);
            combo->setMinimumWidth(230);
            rowLayout->addWidget(combo, 0, Qt::AlignRight | Qt::AlignVCenter);
            *outCombo = combo;
            return row;
        };

        QJsonObject diffSettings = ComfyUIUtils::loadSettingsJson();
        int selFeather = qBound(0, diffSettings.value(QStringLiteral("selection_feather")).toInt(10), 25);
        int selBlend = qBound(0, diffSettings.value(QStringLiteral("selection_blend")).toInt(25), 100);
        int selPadding = qBound(0, diffSettings.value(QStringLiteral("selection_padding")).toInt(6), 25);
        const bool colorMatch = diffSettings.value(QStringLiteral("color_match")).toBool(true);
        const double nsfwVal = qBound(0.0, diffSettings.value(QStringLiteral("nsfw_filter")).toDouble(0.0), 1.0);
        if (nsfwVal > 0.0)
            ComfySettingsDialogBuilderInternal::g_nsfwFilterWarningShownThisSession = true;

        QSlider *sliderSelectionFeather = nullptr;
        QLabel *labelFeatherVal = nullptr;
        diffusionLayout->addWidget(addDiffusionSliderRow(
            diffusionInner,
            ComfyTr::tr("Selection Feather"),
            ComfyTr::tr("The border is expanded and blurred by a fraction of selection size"),
            &sliderSelectionFeather,
            &labelFeatherVal,
            0,
            25,
            QStringLiteral("%1 %").arg(selFeather)));
        sliderSelectionFeather->setValue(selFeather);

        QSlider *sliderSelectionBlend = nullptr;
        QLabel *labelBlendVal = nullptr;
        diffusionLayout->addWidget(addDiffusionSliderRow(
            diffusionInner,
            ComfyTr::tr("Selection Blend"),
            ComfyTr::tr("Transition area for alpha blending the result image"),
            &sliderSelectionBlend,
            &labelBlendVal,
            0,
            100,
            QString::number(selBlend) + ComfyTr::tr(" px")));
        sliderSelectionBlend->setValue(selBlend);

        QSlider *sliderSelectionPadding = nullptr;
        QLabel *labelPaddingVal = nullptr;
        diffusionLayout->addWidget(addDiffusionSliderRow(
            diffusionInner,
            ComfyTr::tr("Selection Padding"),
            ComfyTr::tr("Minimum additional padding around the selection area"),
            &sliderSelectionPadding,
            &labelPaddingVal,
            0,
            25,
            QStringLiteral("%1 %").arg(selPadding)));
        sliderSelectionPadding->setValue(selPadding);

        ComfySwitchWidget *switchColorMatch = nullptr;
        QLabel *labelColorMatchState = nullptr;
        diffusionLayout->addWidget(addDiffusionSwitchRow(
            diffusionInner,
            ComfyTr::tr("Color Match"),
            ComfyTr::tr("Match peripheral colors and brightness with existing content. Requires a selection."),
            &switchColorMatch,
            &labelColorMatchState));
        switchColorMatch->setChecked(colorMatch);
        labelColorMatchState->setText(colorMatch ? ComfyTr::tr("On") : ComfyTr::tr("Off"));

        QComboBox *comboNsfwFilter = nullptr;
        diffusionLayout->addWidget(addDiffusionComboRow(
            diffusionInner,
            ComfyTr::tr("NSFW Filter"),
            ComfyTr::tr("Attempt to filter out images with explicit content"),
            &comboNsfwFilter));
        comboNsfwFilter->addItem(ComfyTr::tr("Disabled"), 0.0);
        comboNsfwFilter->addItem(ComfyTr::tr("Basic"), 0.65);
        comboNsfwFilter->addItem(ComfyTr::tr("Strict"), 0.8);
        const int nsfwIdx = (nsfwVal <= 0.0) ? 0 : (nsfwVal < 0.7) ? 1 : 2;
        comboNsfwFilter->setCurrentIndex(nsfwIdx);

        auto saveDiffusionSettings = [sliderSelectionFeather, sliderSelectionBlend, sliderSelectionPadding,
                                      switchColorMatch, comboNsfwFilter]() {
            QJsonObject s = ComfyUIUtils::loadSettingsJson();
            s.insert(QStringLiteral("selection_feather"), sliderSelectionFeather->value());
            s.insert(QStringLiteral("selection_blend"), sliderSelectionBlend->value());
            s.insert(QStringLiteral("selection_padding"), sliderSelectionPadding->value());
            s.insert(QStringLiteral("color_match"), switchColorMatch->isChecked());
            s.insert(QStringLiteral("nsfw_filter"), comboNsfwFilter->currentData().toDouble());
            ComfyUIUtils::saveSettingsJson(s);
        };
        QObject::connect(sliderSelectionFeather, &QSlider::valueChanged, dlg, [labelFeatherVal, saveDiffusionSettings](int v) {
            labelFeatherVal->setText(QStringLiteral("%1 %").arg(v));
            saveDiffusionSettings();
        });
        QObject::connect(sliderSelectionBlend, &QSlider::valueChanged, dlg, [labelBlendVal, saveDiffusionSettings](int v) {
            labelBlendVal->setText(QString::number(v) + ComfyTr::tr(" px"));
            saveDiffusionSettings();
        });
        QObject::connect(sliderSelectionPadding, &QSlider::valueChanged, dlg, [labelPaddingVal, saveDiffusionSettings](int v) {
            labelPaddingVal->setText(QStringLiteral("%1 %").arg(v));
            saveDiffusionSettings();
        });
        QObject::connect(switchColorMatch, &QAbstractButton::toggled, dlg, [labelColorMatchState, saveDiffusionSettings](bool on) {
            labelColorMatchState->setText(on ? ComfyTr::tr("On") : ComfyTr::tr("Off"));
            saveDiffusionSettings();
        });
        QObject::connect(comboNsfwFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), dlg,
                [dlg, saveDiffusionSettings](int idx) {
                    if (idx > 0 && !ComfySettingsDialogBuilderInternal::g_nsfwFilterWarningShownThisSession) {
                        ComfySettingsDialogBuilderInternal::g_nsfwFilterWarningShownThisSession = true;
                        QMessageBox::warning(
                            dlg,
                            ComfyTr::tr("NSFW Filter Warning"),
                            ComfyTr::tr("The NSFW filter is a basic tool to exclude explicit content from generated images. It is NOT a guarantee and may not catch all inappropriate content. Please use responsibly and always review the generated images."));
                    }
                    saveDiffusionSettings();
                });

        diffusionLayout->addStretch();
        diffusionScroll->setWidget(diffusionInner);
        diffusionOuter->addWidget(diffusionScroll);
        stack->addWidget(diffusionPage);

}


} // namespace ComfySettingsDialogBuilder
