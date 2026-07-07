/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyFormRow.h"
#include "ComfyScrollPage.h"

#include <functional>
#include <QString>

class QCheckBox;
class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QWidget;

class ComfyFormRow;
class ComfySlider;
class ComfySpinBox;
class ComfySwitchWidget;
class ComfyTextArea;
class ComfyTrackSlider;
class QAbstractSlider;

namespace ComfyFormUi {

struct SliderSetting {
    ComfyFormRow *row = nullptr;
    ComfySlider *slider = nullptr;

    QAbstractSlider *qtSlider() const;
    QLabel *valueLabel() const;
};

struct SwitchSetting {
    QWidget *row = nullptr;
    ComfySwitchWidget *switchWidget = nullptr;
    QLabel *stateLabel = nullptr;
    QString onLabel;
    QString offLabel;

    void setChecked(bool checked);
    bool isChecked() const;
};

struct TextAreaColumn {
    QWidget *column = nullptr;
    ComfyTextArea *editor = nullptr;
};

struct SeedControls {
    QWidget *row = nullptr;
    QCheckBox *fixedSeedCheckBox = nullptr;
    QSpinBox *seedSpinBox = nullptr;
    QPushButton *randomSeedButton = nullptr;
};

struct DockerSlider {
    QWidget *row = nullptr;
    ComfySlider *widget = nullptr;

    QAbstractSlider *qtSlider() const;
    QLabel *valueLabel() const;
};

struct InlineSliderSpinRow {
    QWidget *row = nullptr;
    ComfySlider *slider = nullptr;
    ComfySpinBox *spin = nullptr;

    QAbstractSlider *qtSlider() const;
};

struct InlineSliderRow {
    QWidget *row = nullptr;
    ComfySlider *slider = nullptr;

    QAbstractSlider *qtSlider() const;
    QLabel *valueLabel() const;
};

struct ScrollTab {
    QWidget *page = nullptr;
    QVBoxLayout *outerLayout = nullptr;
    QScrollArea *scroll = nullptr;
    QWidget *inner = nullptr;
    QVBoxLayout *innerLayout = nullptr;
};

ScrollTab createScrollTab(QWidget *dialogParent, const QString &heading);

QWidget *makeLabelColumn(QWidget *parent, const QString &title, const QString &description);
QLabel *makeBoldHeader(QWidget *parent, const QString &title);

QWidget *addSpinRow(QWidget *parent,
                    const QString &title,
                    const QString &description,
                    QSpinBox **outSpin,
                    int min,
                    int max,
                    const QString &suffix = QString());

QWidget *addComboRow(QWidget *parent, const QString &title, const QString &description, QComboBox **outCombo);

SliderSetting addSliderRow(QWidget *parent,
                           const QString &title,
                           const QString &description,
                           int min,
                           int max,
                           const QString &initialValueText);

SwitchSetting addSwitchRow(QWidget *parent,
                           const QString &title,
                           const QString &description,
                           const QString &onLabel,
                           const QString &offLabel);

QWidget *addControlRow(QWidget *parent,
                       const QString &title,
                       const QString &description,
                       QWidget *control,
                       int leftIndent = 0);

QWidget *addLabeledRow(QWidget *parent, const QString &labelText, QWidget *control, int controlStretch = 1);

DockerSlider addLabeledSliderRow(QWidget *parent,
                                 const QString &labelText,
                                 int min,
                                 int max,
                                 const QString &initialValueText);

ComfySlider *makeExpandingSlider(int min,
                                 int max,
                                 const QString &initialValueText = QString(),
                                 QWidget *parent = nullptr,
                                 bool showValueLabel = true);

ComfySlider *makeSliderWithTrailing(int min, int max, QWidget *trailing, QWidget *parent = nullptr);

InlineSliderRow addInlineSliderRow(QWidget *parent,
                                   const QString &title,
                                   int min,
                                   int max,
                                   int titleStretch = 1,
                                   int sliderStretch = 3,
                                   const QString &valueSuffix = QStringLiteral("%"));

InlineSliderSpinRow addInlineSliderSpinRow(QWidget *parent,
                                           const QString &title,
                                           int min,
                                           int max,
                                           const QString &suffix = QStringLiteral("%"));

SwitchSetting addInlineSwitchRow(QWidget *parent,
                                 const QString &title,
                                 const QString &onLabel,
                                 const QString &offLabel);

QWidget *addHistorySizeBlock(QWidget *parent,
                             const QString &title,
                             const QString &description,
                             QSpinBox **outSpin,
                             QLabel **outUsageLabel,
                             int min,
                             int max,
                             int step);

void bindSliderValueLabel(QAbstractSlider *slider, QLabel *label, const QString &valueSuffix = QStringLiteral("%"));

QWidget *addLineEditBlock(QWidget *parent, const QString &title, const QString &description, QLineEdit *edit);

TextAreaColumn addTextAreaColumn(QWidget *parent, QCompleter *completer);
void attachTextAreaResizeHandle(TextAreaColumn &column,
                              int minHeightPx,
                              const std::function<void(int lines)> &onLineCountChanged);

SeedControls addSeedControls(QWidget *parent);

} // namespace ComfyFormUi
