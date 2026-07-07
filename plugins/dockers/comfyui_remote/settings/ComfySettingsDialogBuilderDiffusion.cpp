/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySettingsDialogBuilderInternal.h"
#include "ComfySwitchWidget.h"
#include "ComfySettingsDialogBuilder.h"
#include "ComfyFormUi.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QAbstractSlider>
#include <QStackedWidget>

namespace ComfySettingsDialogBuilder {

void buildDiffusionTab(const Context &ctx, QStackedWidget *stack)
{
    ComfyUIRemoteDock *dock = ctx.dock;
    Q_UNUSED(dock);
    QDialog *dlg = ctx.dialog;

    ComfyFormUi::ScrollTab tab = ComfyFormUi::createScrollTab(dlg, ComfyTr::tr("Diffusion Settings"));

    QJsonObject diffSettings = ComfyUIUtils::loadSettingsJson();
    int selFeather = qBound(0, diffSettings.value(QStringLiteral("selection_feather")).toInt(10), 25);
    int selBlend = qBound(0, diffSettings.value(QStringLiteral("selection_blend")).toInt(25), 100);
    int selPadding = qBound(0, diffSettings.value(QStringLiteral("selection_padding")).toInt(6), 25);
    const bool colorMatch = diffSettings.value(QStringLiteral("color_match")).toBool(true);
    const double nsfwVal = qBound(0.0, diffSettings.value(QStringLiteral("nsfw_filter")).toDouble(0.0), 1.0);
    if (nsfwVal > 0.0)
        ComfySettingsDialogBuilderInternal::g_nsfwFilterWarningShownThisSession = true;

    auto featherRow = ComfyFormUi::addSliderRow(
        tab.inner,
        ComfyTr::tr("Selection Feather"),
        ComfyTr::tr("The border is expanded and blurred by a fraction of selection size"),
        0,
        25,
        QStringLiteral("%1 %").arg(selFeather));
    QAbstractSlider *sliderSelectionFeather = featherRow.qtSlider();
    QLabel *labelFeatherVal = featherRow.valueLabel();
    sliderSelectionFeather->setValue(selFeather);
    tab.innerLayout->addWidget(featherRow.row);

    auto blendRow = ComfyFormUi::addSliderRow(
        tab.inner,
        ComfyTr::tr("Selection Blend"),
        ComfyTr::tr("Transition area for alpha blending the result image"),
        0,
        100,
        QString::number(selBlend) + ComfyTr::tr(" px"));
    QAbstractSlider *sliderSelectionBlend = blendRow.qtSlider();
    QLabel *labelBlendVal = blendRow.valueLabel();
    sliderSelectionBlend->setValue(selBlend);
    tab.innerLayout->addWidget(blendRow.row);

    auto paddingRow = ComfyFormUi::addSliderRow(
        tab.inner,
        ComfyTr::tr("Selection Padding"),
        ComfyTr::tr("Minimum additional padding around the selection area"),
        0,
        25,
        QStringLiteral("%1 %").arg(selPadding));
    QAbstractSlider *sliderSelectionPadding = paddingRow.qtSlider();
    QLabel *labelPaddingVal = paddingRow.valueLabel();
    sliderSelectionPadding->setValue(selPadding);
    tab.innerLayout->addWidget(paddingRow.row);

    auto colorMatchRow = ComfyFormUi::addSwitchRow(
        tab.inner,
        ComfyTr::tr("Color Match"),
        ComfyTr::tr("Match peripheral colors and brightness with existing content. Requires a selection."),
        ComfyTr::tr("On"),
        ComfyTr::tr("Off"));
    colorMatchRow.setChecked(colorMatch);
    tab.innerLayout->addWidget(colorMatchRow.row);
    ComfySwitchWidget *switchColorMatch = colorMatchRow.switchWidget;

    QComboBox *comboNsfwFilter = nullptr;
    tab.innerLayout->addWidget(ComfyFormUi::addComboRow(
        tab.inner,
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
    QObject::connect(sliderSelectionFeather, &QAbstractSlider::valueChanged, dlg, [labelFeatherVal, saveDiffusionSettings](int v) {
        labelFeatherVal->setText(QStringLiteral("%1 %").arg(v));
        saveDiffusionSettings();
    });
    QObject::connect(sliderSelectionBlend, &QAbstractSlider::valueChanged, dlg, [labelBlendVal, saveDiffusionSettings](int v) {
        labelBlendVal->setText(QString::number(v) + ComfyTr::tr(" px"));
        saveDiffusionSettings();
    });
    QObject::connect(sliderSelectionPadding, &QAbstractSlider::valueChanged, dlg, [labelPaddingVal, saveDiffusionSettings](int v) {
        labelPaddingVal->setText(QStringLiteral("%1 %").arg(v));
        saveDiffusionSettings();
    });
    QObject::connect(switchColorMatch, &QAbstractButton::toggled, dlg, [saveDiffusionSettings](bool) {
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

    tab.innerLayout->addStretch();
    stack->addWidget(tab.page);
}

} // namespace ComfySettingsDialogBuilder
