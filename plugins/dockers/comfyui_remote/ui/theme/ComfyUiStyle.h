/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

class QBoxLayout;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QToolButton;
class QWidget;

/// Canonical UI style tokens and QWidget helpers (AI Image Generation style guide).
namespace ComfyUiStyle {

namespace Spacing {
constexpr int unit = 4;
constexpr int panel = 16;
constexpr int nestedPanel = 8;
constexpr int sectionGap = 12;
constexpr int rowGap = 5;
constexpr int labelControl = 4;
constexpr int rowHeight = 28;
constexpr int iconSmall = 16;
constexpr int iconLarge = 20;
constexpr int iconPadding = 4;
constexpr int iconTextGap = 6;
constexpr int toggleWidth = 40;
constexpr int toggleHeight = 20;
constexpr int toggleKnob = 16;
constexpr int checkboxSize = 16;
constexpr int buttonHeight = 28;
constexpr int buttonMinWidth = 96;
constexpr int navWidth = 120;
constexpr int comboHeight = 24;
constexpr int iconButtonSize = comboHeight;
constexpr int primaryButtonHeight = 25;
constexpr int comboRadius = 3;
constexpr int comboArrowWidth = 20;
constexpr int spinButtonWidth = 16;
constexpr int sliderTrack = 3;
constexpr int sliderHandle = 10; // ~3× track; taller than bar, fits spin row
constexpr int sliderWidgetHeight = 12;
constexpr int sliderMargin = 8;
constexpr int promptMinHeight = 80;
constexpr int thumbnailWidth = 72;
constexpr int galleryGutter = 16;
constexpr int settingsSectionGap = 24;
constexpr int progressBarHeight = 2;
} // namespace Spacing

enum class StatusTone {
    Neutral,
    Success,
    Warning,
    Error,
};

struct Colors {
    QString panelBg;
    QString secondaryPanel;
    QString inputBg;
    QString disabledBg;
    QString border;
    QString inputBorder;
    QString primaryText;
    QString secondaryText;
    QString placeholderText;
    QString disabledText;
    QString highlight;
    QString highlightHover;
    QString highlightSelection;
    QString yellow;
    QString red;
    QString green;
    QString negativeTint;
    QString toggleOff;
    QString sliderTrack;
    QString scrollTrack;
    QString scrollThumb;
};

Colors colors();

QString sliderStyleSheet();
QString comboBoxStyleSheet();
QString comboBoxPopupStyleSheet();
QString flatComboStyleSheet();
QString lineEditStyleSheet();
QString spinBoxStyleSheet();
QString checkboxStyleSheet();
QString radioButtonStyleSheet();
QString promptTextAreaStyleSheet(bool negative = false);
QString promptStackFrameStyleSheet(bool focused = false);
QString outlinedPanelStyleSheet(const QString &objectName);
QString primaryButtonStyleSheet();
QString secondaryButtonStyleSheet();
QString iconButtonStyleSheet();
QString settingsNavListStyleSheet();
QString scrollBarStyleSheet();
QString galleryListStyleSheet();
QString warningLabelStyleSheet();
QString footerVersionStyleSheet();
QString restoreDefaultsButtonStyleSheet();
QString overlayButtonStyleSheet();
QString progressBarStyleSheet(bool upload = false);
QString expanderButtonStyleSheet();
QString flatGroupBoxStyleSheet();
QString logoPlaceholderStyleSheet();
QString statusLabelStyleSheet(StatusTone tone);

void styleSectionTitle(QLabel *label);
void styleFormRowTitle(QLabel *label);
void styleDescription(QLabel *label);
void styleHint(QLabel *label);
void styleCaption(QLabel *label);
void styleWarning(QLabel *label);
void styleGalleryCaption(QLabel *label);
void styleStatusLabel(QLabel *label, StatusTone tone);
void resetLabelStyle(QLabel *label);

void applySlider(QSlider *slider);
void applyComboBox(QWidget *combo);
void applyFlatCombo(QWidget *combo);
void applyLineEdit(QLineEdit *edit);
void applySpinBox(QSpinBox *spin);
void applyDoubleSpinBox(QDoubleSpinBox *spin);
void applyCheckbox(QCheckBox *box);
void applyRadioButton(QRadioButton *button);
void applyPromptTextArea(QPlainTextEdit *edit, bool negative = false);
void applyPrimaryButton(QPushButton *button);
void applySecondaryButton(QPushButton *button);
void applyIconToolButton(QToolButton *button);
void applyIconButton(QPushButton *button);
void applyExpanderButton(QToolButton *button);
void applyProgressBar(QProgressBar *bar, bool upload = false);
void applySettingsNavList(QListWidget *list);
void applyScrollArea(QScrollArea *scroll);
void applyGalleryList(QWidget *list);
void applyDockerPanelLayout(QWidget *panel, int margins = Spacing::panel);
void applyTightRowLayout(QBoxLayout *layout, int spacing = Spacing::rowGap);
void applyWidgetTree(QWidget *root);

} // namespace ComfyUiStyle
