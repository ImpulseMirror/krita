/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyComboBox.h"
#include "ComfyFormUi.h"
#include "ComfySpinBox.h"
#include "ComfyFormRow.h"
#include "ComfyGrid.h"
#include "ComfyLocalization.h"
#include "ComfyScrollPage.h"
#include "ComfySlider.h"
#include "ComfySwitchWidget.h"
#include "ComfyTextArea.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QAbstractSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ComfyFormUi {

QAbstractSlider *SliderSetting::qtSlider() const
{
    return slider ? slider->slider() : nullptr;
}

QLabel *SliderSetting::valueLabel() const
{
    return slider ? slider->valueLabel() : nullptr;
}

QAbstractSlider *DockerSlider::qtSlider() const
{
    return widget ? widget->slider() : nullptr;
}

QLabel *DockerSlider::valueLabel() const
{
    return widget ? widget->valueLabel() : nullptr;
}

QAbstractSlider *InlineSliderRow::qtSlider() const
{
    return slider ? slider->slider() : nullptr;
}

QLabel *InlineSliderRow::valueLabel() const
{
    return slider ? slider->valueLabel() : nullptr;
}

void SwitchSetting::setChecked(bool checked)
{
    if (switchWidget)
        switchWidget->setChecked(checked);
    if (stateLabel)
        stateLabel->setText(checked ? onLabel : offLabel);
}

bool SwitchSetting::isChecked() const
{
    return switchWidget && switchWidget->isChecked();
}

ScrollTab createScrollTab(QWidget *dialogParent, const QString &heading)
{
    const ComfyScrollPage page = ComfyScrollPage::create(dialogParent, heading);
    ScrollTab tab;
    tab.page = page.page();
    tab.outerLayout = page.outerLayout();
    tab.scroll = page.scrollArea();
    tab.inner = page.body();
    tab.innerLayout = page.bodyLayout();
    return tab;
}

QWidget *makeLabelColumn(QWidget *parent, const QString &title, const QString &description)
{
    return ComfyFormRow::makeLabelColumn(parent, title, description);
}

QLabel *makeBoldHeader(QWidget *parent, const QString &title)
{
    return ComfyFormRow::makeBoldHeader(parent, title);
}

QWidget *addSpinRow(QWidget *parent,
                    const QString &title,
                    const QString &description,
                    QSpinBox **outSpin,
                    int min,
                    int max,
                    const QString &suffix)
{
    auto *row = new ComfyFormRow(title, description, parent);
    auto *spin = new ComfySpinBox(row);
    spin->setMinimumWidth(100);
    spin->setRange(min, max);
    ComfyUiStyle::applySpinBox(spin);
    if (!suffix.isEmpty())
        spin->setSuffix(suffix);
    row->setControl(spin);
    if (outSpin)
        *outSpin = spin;
    return row;
}

QWidget *addComboRow(QWidget *parent, const QString &title, const QString &description, QComboBox **outCombo)
{
    auto *row = new ComfyFormRow(title, description, parent);
    auto *combo = new ComfyComboBox(row);
    combo->setMinimumWidth(230);
    ComfyUiStyle::applyComboBox(combo);
    row->setControl(combo);
    if (outCombo)
        *outCombo = combo;
    return row;
}

SliderSetting addSliderRow(QWidget *parent,
                           const QString &title,
                           const QString &description,
                           int min,
                           int max,
                           const QString &initialValueText)
{
    SliderSetting result;
    result.row = new ComfyFormRow(title, description, parent);
    result.slider = new ComfySlider(min, max, initialValueText, ComfySlider::Layout::Settings, result.row);
    result.row->setControl(result.slider);
    return result;
}

SwitchSetting addSwitchRow(QWidget *parent,
                           const QString &title,
                           const QString &description,
                           const QString &onLabel,
                           const QString &offLabel)
{
    SwitchSetting result;
    auto *formRow = new ComfyFormRow(title, description, parent);
    result.row = formRow;
    result.onLabel = onLabel;
    result.offLabel = offLabel;
    result.stateLabel = new QLabel(offLabel, formRow);
    result.switchWidget = new ComfySwitchWidget(formRow);
    auto *trailingCol = new QWidget(formRow);
    auto *trailingLay = new QHBoxLayout(trailingCol);
    trailingLay->setContentsMargins(0, 0, 0, 0);
    trailingLay->setSpacing(ComfyUiStyle::Spacing::iconTextGap);
    trailingLay->addWidget(result.stateLabel, 1);
    trailingLay->addWidget(result.switchWidget);
    formRow->setControl(trailingCol);
    QObject::connect(result.switchWidget, &QAbstractButton::toggled, formRow, [result](bool on) {
        if (result.stateLabel)
            result.stateLabel->setText(on ? result.onLabel : result.offLabel);
    });
    return result;
}

QWidget *addControlRow(QWidget *parent,
                       const QString &title,
                       const QString &description,
                       QWidget *control,
                       int leftIndent)
{
    auto *row = new ComfyFormRow(title, description, parent, leftIndent);
    row->setControl(control);
    return row;
}

QWidget *addLabeledRow(QWidget *parent, const QString &labelText, QWidget *control, int controlStretch)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(labelText, row));
    if (control)
        layout->addWidget(control, controlStretch);
    return row;
}

DockerSlider addLabeledSliderRow(QWidget *parent,
                                 const QString &labelText,
                                 int min,
                                 int max,
                                 const QString &initialValueText)
{
    DockerSlider result;
    result.row = addLabeledRow(parent, labelText, nullptr);
    result.widget = new ComfySlider(min, max, initialValueText, ComfySlider::Layout::Expanding, result.row);
    if (auto *layout = qobject_cast<QHBoxLayout *>(result.row->layout()))
        layout->addWidget(result.widget, 1);
    return result;
}

ComfySlider *makeExpandingSlider(int min,
                                 int max,
                                 const QString &initialValueText,
                                 QWidget *parent,
                                 bool showValueLabel)
{
    auto *slider = new ComfySlider(min, max, initialValueText, ComfySlider::Layout::Expanding, parent);
    slider->setValueLabelVisible(showValueLabel);
    return slider;
}

ComfySlider *makeSliderWithTrailing(int min, int max, QWidget *trailing, QWidget *parent)
{
    auto *slider = makeExpandingSlider(min, max, QString(), parent, false);
    if (trailing)
        slider->setTrailingWidget(trailing);
    return slider;
}

void bindSliderValueLabel(QAbstractSlider *slider, QLabel *label, const QString &valueSuffix)
{
    if (!slider || !label)
        return;
    const auto update = [slider, label, valueSuffix]() {
        label->setText(QString::number(slider->value()) + valueSuffix);
    };
    update();
    QObject::connect(slider, &QAbstractSlider::valueChanged, label, [update](int) { update(); });
}

QAbstractSlider *InlineSliderSpinRow::qtSlider() const
{
    return slider ? slider->slider() : nullptr;
}

InlineSliderRow addInlineSliderRow(QWidget *parent,
                                   const QString &title,
                                   int min,
                                   int max,
                                   int titleStretch,
                                   int sliderStretch,
                                   const QString &valueSuffix)
{
    InlineSliderRow result;
    result.row = new QWidget(parent);
    auto *layout = new QHBoxLayout(result.row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *titleLabel = new QLabel(title, result.row);
    layout->addWidget(titleLabel, titleStretch);
    result.slider = new ComfySlider(min, max, QString(), ComfySlider::Layout::Expanding, result.row);
    layout->addWidget(result.slider, sliderStretch);
    if (result.slider->valueLabel()) {
        result.slider->valueLabel()->setMinimumWidth(40);
        bindSliderValueLabel(result.slider->slider(), result.slider->valueLabel(), valueSuffix);
    }
    return result;
}

InlineSliderSpinRow addInlineSliderSpinRow(QWidget *parent,
                                           const QString &title,
                                           int min,
                                           int max,
                                           const QString &suffix)
{
    InlineSliderSpinRow result;
    result.row = new QWidget(parent);
    auto *layout = new QHBoxLayout(result.row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ComfyUiStyle::Spacing::rowGap);
    layout->setAlignment(Qt::AlignVCenter);
    layout->addWidget(new QLabel(title, result.row));
    result.slider = new ComfySlider(min, max, QString(), ComfySlider::Layout::Expanding, result.row);
    result.slider->setValueLabelVisible(false);
    layout->addWidget(result.slider, 1);
    result.spin = new ComfySpinBox(result.row);
    result.spin->setRange(min, max);
    if (!suffix.isEmpty())
        result.spin->setSuffix(suffix);
    {
        const QFontMetrics fm(result.spin->font());
        result.spin->setMinimumWidth(fm.horizontalAdvance(QString::number(max) + suffix)
                                     + ComfyUiStyle::Spacing::spinButtonWidth
                                     + ComfyUiStyle::Spacing::nestedPanel * 2);
    }
    layout->addWidget(result.spin);

    if (QAbstractSlider *track = result.qtSlider()) {
        QObject::connect(track, &QAbstractSlider::valueChanged, result.spin, [result](int v) {
            if (!result.spin || result.spin->value() == v)
                return;
            QSignalBlocker blocker(result.spin);
            result.spin->setValue(v);
        });
        QObject::connect(result.spin, QOverload<int>::of(&QSpinBox::valueChanged), track, [track](int v) {
            if (!track || track->value() == v)
                return;
            QSignalBlocker blocker(track);
            track->setValue(v);
        });
        result.spin->setValue(track->value());
    }
    return result;
}

SwitchSetting addInlineSwitchRow(QWidget *parent,
                                 const QString &title,
                                 const QString &onLabel,
                                 const QString &offLabel)
{
    SwitchSetting result;
    result.row = new QWidget(parent);
    result.onLabel = onLabel;
    result.offLabel = offLabel;
    auto *layout = new QHBoxLayout(result.row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(title, result.row));
    result.stateLabel = new QLabel(offLabel, result.row);
    result.stateLabel->setMinimumWidth(40);
    layout->addWidget(result.stateLabel, 1);
    result.switchWidget = new ComfySwitchWidget(result.row);
    layout->addWidget(result.switchWidget);
    QObject::connect(result.switchWidget, &QAbstractButton::toggled, result.row, [result](bool on) {
        if (result.stateLabel)
            result.stateLabel->setText(on ? result.onLabel : result.offLabel);
    });
    return result;
}

QWidget *addHistorySizeBlock(QWidget *parent,
                             const QString &title,
                             const QString &description,
                             QSpinBox **outSpin,
                             QLabel **outUsageLabel,
                             int min,
                             int max,
                             int step)
{
    auto *block = new QWidget(parent);
    auto *blockLayout = new QVBoxLayout(block);
    blockLayout->setContentsMargins(0, ComfyUiStyle::Spacing::unit, 0, ComfyUiStyle::Spacing::unit);
    blockLayout->setSpacing(ComfyUiStyle::Spacing::unit);
    blockLayout->addWidget(makeLabelColumn(block, title, description));
    auto *usageRow = new QWidget(block);
    auto *usageLayout = new QHBoxLayout(usageRow);
    usageLayout->setContentsMargins(0, 0, 0, 0);
    auto *spin = new ComfySpinBox(usageRow);
    spin->setRange(min, max);
    spin->setSingleStep(step);
    spin->setSuffix(ComfyTr::tr(" MB"));
    auto *usageLabel = new QLabel(ComfyTr::tr("Currently using %1 MB", QStringLiteral("0.0")), usageRow);
    usageLabel->setStyleSheet(ComfyUiStyle::warningLabelStyleSheet());
    usageLayout->addWidget(spin);
    usageLayout->addWidget(usageLabel, 1);
    blockLayout->addWidget(usageRow);
    if (outSpin)
        *outSpin = spin;
    if (outUsageLabel)
        *outUsageLabel = usageLabel;
    return block;
}

QWidget *addLineEditBlock(QWidget *parent, const QString &title, const QString &description, QLineEdit *edit)
{
    QWidget *block = new QWidget(parent);
    QVBoxLayout *blockLayout = new QVBoxLayout(block);
    blockLayout->setContentsMargins(0, ComfyUiStyle::Spacing::unit, 0, ComfyUiStyle::Spacing::unit);
    blockLayout->setSpacing(ComfyUiStyle::Spacing::unit);
    blockLayout->addWidget(makeBoldHeader(block, title));
    if (!description.isEmpty()) {
        QLabel *descLabel = new QLabel(description, block);
        descLabel->setWordWrap(true);
        blockLayout->addWidget(descLabel);
    }
    if (edit) {
        edit->setMinimumWidth(0);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        ComfyUiStyle::applyLineEdit(edit);
        blockLayout->addWidget(edit);
    }
    return block;
}

SeedControls addSeedControls(QWidget *parent)
{
    SeedControls controls;
    controls.row = new QWidget(parent);
    auto *layout = new QHBoxLayout(controls.row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ComfyUiStyle::Spacing::iconTextGap);

    controls.fixedSeedCheckBox = new ComfyCheckBox(ComfyTr::tr("Fixed seed"), controls.row);
    controls.seedSpinBox = new ComfySpinBox(controls.row);
    controls.seedSpinBox->setRange(0, 2147483647);
    ComfyUiStyle::applySpinBox(controls.seedSpinBox);
    controls.randomSeedButton = new QPushButton(controls.row);
    controls.randomSeedButton->setIcon(ComfyTheme::icon(QStringLiteral("random")));
    controls.randomSeedButton->setToolTip(ComfyTr::tr("Pick a new random seed."));
    controls.randomSeedButton->setAccessibleName(ComfyTr::tr("Random seed"));
    ComfyUiStyle::applyIconButton(controls.randomSeedButton);

    layout->addWidget(controls.fixedSeedCheckBox);
    layout->addWidget(controls.seedSpinBox, 1);
    layout->addWidget(controls.randomSeedButton);
    return controls;
}

TextAreaColumn addTextAreaColumn(QWidget *parent, QCompleter *completer)
{
    TextAreaColumn result;
    result.column = new ComfyTextInputContainer(parent);
    auto *layout = new QVBoxLayout(result.column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    result.editor = new ComfyTextArea(completer, result.column);
    layout->addWidget(result.editor);
    return result;
}

void attachTextAreaResizeHandle(TextAreaColumn &column,
                                int minHeightPx,
                                const std::function<void(int lines)> &onLineCountChanged)
{
    if (column.editor)
        column.editor->attachResizeHandle(minHeightPx, onLineCountChanged, column.column);
}

} // namespace ComfyFormUi
