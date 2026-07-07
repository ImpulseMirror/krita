/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUiStyle.h"
#include "ComfyTheme.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfyCheckBox.h"
#include "ComfyComboBox.h"
#include "ComfyQueueButton.h"
#include "ComfySpinBox.h"
#include "ComfyTrackSlider.h"
#include "ComfyWorkspaceSelectButton.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

#include <QAbstractSpinBox>
#include <QBoxLayout>
#include <QCheckBox>
#include <QSize>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace ComfyUiStyle {

namespace {

QString lighten(const QString &hex, int factor = 115)
{
    QColor c(hex);
    if (!c.isValid())
        return hex;
    return c.lighter(factor).name(QColor::HexRgb);
}

QString withAlpha(const QColor &c, int alpha255)
{
    QColor out = c;
    out.setAlpha(qBound(0, alpha255, 255));
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(out.red())
        .arg(out.green())
        .arg(out.blue())
        .arg(out.alpha());
}

} // namespace

Colors colors()
{
    const QPalette pal = QGuiApplication::palette();
    const ComfyTheme::Palette theme = ComfyTheme::palette();
    const bool dark = ComfyTheme::isDarkTheme();

    Colors c;
    c.panelBg = pal.color(QPalette::Window).name(QColor::HexRgb);
    c.secondaryPanel = pal.color(QPalette::Mid).name(QColor::HexRgb);
    if (dark) {
        c.inputBg = pal.color(QPalette::Base).darker(108).name(QColor::HexRgb);
        c.disabledBg = QStringLiteral("#2D2E33");
        c.border = theme.line;
        c.inputBorder = QStringLiteral("#41454B");
        c.toggleOff = QStringLiteral("#444A53");
        c.sliderTrack = QStringLiteral("#50555C");
        c.scrollTrack = QStringLiteral("#2D2E33");
        c.scrollThumb = QStringLiteral("#50555C");
    } else {
        c.inputBg = pal.color(QPalette::Base).name(QColor::HexRgb);
        c.disabledBg = pal.color(QPalette::Mid).lighter(105).name(QColor::HexRgb);
        c.border = theme.line;
        c.inputBorder = theme.lineBase;
        c.toggleOff = pal.color(QPalette::Mid).name(QColor::HexRgb);
        c.sliderTrack = theme.line;
        c.scrollTrack = pal.color(QPalette::Midlight).name(QColor::HexRgb);
        c.scrollThumb = theme.line;
    }
    c.primaryText = pal.color(QPalette::Text).name(QColor::HexRgb);
    c.secondaryText = pal.color(QPalette::PlaceholderText).lighter(dark ? 125 : 90).name(QColor::HexRgb);
    c.placeholderText = pal.color(QPalette::PlaceholderText).name(QColor::HexRgb);
    c.disabledText = pal.color(QPalette::Disabled, QPalette::Text).name(QColor::HexRgb);
    c.highlight = theme.highlight;
    c.highlightHover = lighten(theme.highlight, 112);
    c.highlightSelection = withAlpha(QColor(theme.highlight), dark ? 153 : 120);
    c.yellow = theme.yellow;
    c.red = theme.red;
    c.green = theme.green;
    c.negativeTint = QStringLiteral("rgba(255, 0, 0, 15)");
    return c;
}

QString sliderStyleSheet()
{
    const Colors c = colors();
    const int grooveMarginY = (Spacing::sliderHandle - Spacing::sliderTrack) / 2;
    return QStringLiteral(
               "QSlider:horizontal {"
               "  min-height: %10px;"
               "  max-height: %10px;"
               "  height: %10px;"
               "  background: transparent;"
               "}"
               "QSlider::groove:horizontal {"
               "  border: none;"
               "  height: %1px;"
               "  background: %2;"
               "  margin: %3px %11px;"
               "  border-radius: %4px;"
               "}"
               "QSlider::sub-page:horizontal {"
               "  background: %5;"
               "  border-radius: %4px;"
               "  height: %1px;"
               "}"
               "QSlider::add-page:horizontal {"
               "  background: %2;"
               "  border-radius: %4px;"
               "  height: %1px;"
               "}"
               "QSlider::handle:horizontal {"
               "  background: %6;"
               "  border: 1px solid %7;"
               "  width: %8px;"
               "  height: %8px;"
               "  margin: -%9px 0;"
               "  border-radius: 2px;"
               "}"
               "QSlider::handle:horizontal:hover {"
               "  background: %12;"
               "  border: 2px solid %5;"
               "}"
               "QSlider:focus { outline: none; }")
        .arg(Spacing::sliderTrack)
        .arg(c.sliderTrack)
        .arg(grooveMarginY)
        .arg(Spacing::sliderTrack / 2)
        .arg(c.highlight)
        .arg(c.secondaryPanel)
        .arg(c.border)
        .arg(Spacing::sliderHandle)
        .arg(grooveMarginY)
        .arg(Spacing::sliderWidgetHeight)
        .arg(Spacing::sliderMargin)
        .arg(c.highlightHover);
}

QString comboBoxPopupStyleSheet()
{
    const Colors c = colors();
    const int r = Spacing::comboRadius;
    return QStringLiteral(
               "QAbstractItemView {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: %3px;"
               "  selection-background-color: %4;"
               "  selection-color: %5;"
               "  padding: 2px;"
               "  outline: none;"
               "}"
               "QAbstractItemView::item {"
               "  min-height: %6px;"
               "  padding-left: %7px;"
               "}")
        .arg(c.secondaryPanel,
             c.border,
             QString::number(r),
             c.highlightSelection,
             c.primaryText,
             QString::number(Spacing::rowHeight),
             QString::number(Spacing::nestedPanel));
}

QString comboBoxStyleSheet()
{
    const Colors c = colors();
    const QColor panel(c.secondaryPanel);
    const QString gradTop = panel.lighter(108).name(QColor::HexRgb);
    const QString gradBottom = panel.darker(108).name(QColor::HexRgb);
    const QString grad = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
                             .arg(gradTop, gradBottom);
    const int h = Spacing::comboHeight;
    const int r = Spacing::comboRadius;
  return QStringLiteral(
               "QComboBox {"
               "  border: 1px solid %1;"
               "  border-radius: %2px;"
               "  background-color: %3;"
               "  background: %4;"
               "  color: %5;"
               "  padding: 1px %6px 1px 6px;"
               "  min-height: %7px;"
               "  max-height: %7px;"
               "}"
               "QComboBox:hover { border-color: %8; }"
               "QComboBox:focus, QComboBox:on { border: 1px solid %9; }"
               "QComboBox:disabled { color: %10; background: %11; border-color: %12; }"
               "QComboBox::drop-down {"
               "  subcontrol-origin: padding;"
               "  subcontrol-position: center right;"
               "  width: %6px;"
               "  border: none;"
               "  background: transparent;"
               "}"
               "QComboBox::down-arrow {"
               "  width: 0; height: 0;"
               "  border-left: 4px solid transparent;"
               "  border-right: 4px solid transparent;"
               "  border-top: 5px solid %13;"
               "  margin-right: 4px;"
               "}"
               "QComboBox::down-arrow:disabled { border-top-color: %10; }"
               "QComboBox QAbstractItemView {"
               "  background-color: %3;"
               "  border: 1px solid %1;"
               "  border-radius: %2px;"
               "  selection-background-color: %14;"
               "  selection-color: %5;"
               "  padding: 2px;"
               "  outline: none;"
               "}"
               "QComboBox QAbstractItemView::item {"
               "  min-height: %15px;"
               "  padding-left: %16px;"
               "}")
        .arg(c.border,
             QString::number(r),
             c.secondaryPanel,
             grad,
             c.primaryText,
             QString::number(Spacing::comboArrowWidth),
             QString::number(h),
             lighten(c.border, 112),
             c.highlight,
             c.disabledText,
             c.disabledBg,
             c.inputBorder,
             c.secondaryText,
             c.highlightSelection,
             QString::number(Spacing::rowHeight),
             QString::number(Spacing::nestedPanel));
}

QString lineEditStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QLineEdit {"
               "  border: 1px solid %1;"
               "  border-radius: 2px;"
               "  background-color: %2;"
               "  color: %3;"
               "  padding: 4px 6px;"
               "  min-height: %4px;"
               "}"
               "QLineEdit:focus { border: 2px solid %5; padding: 3px 5px; }"
               "QLineEdit:disabled { color: %6; background-color: %7; }")
        .arg(c.inputBorder,
             c.secondaryPanel,
             c.primaryText,
             QString::number(Spacing::rowHeight - 8),
             c.highlight,
             c.disabledText,
             c.disabledBg);
}

QString spinBoxStyleSheet()
{
    const Colors c = colors();
    const QColor panel(c.secondaryPanel);
    const QString gradTop = panel.lighter(108).name(QColor::HexRgb);
    const QString gradBottom = panel.darker(108).name(QColor::HexRgb);
    const QString grad = QStringLiteral("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
                             .arg(gradTop, gradBottom);
    const QString btnBg = panel.darker(105).name(QColor::HexRgb);
    const int h = Spacing::comboHeight;
    const int r = Spacing::comboRadius;
    const int btnW = Spacing::spinButtonWidth;
    const int halfH = h / 2;
    return QStringLiteral(
               "QSpinBox, QDoubleSpinBox {"
               "  border: 1px solid %1;"
               "  border-radius: %2px;"
               "  background-color: %3;"
               "  background: %4;"
               "  color: %5;"
               "  padding: 0px %6px 0px 6px;"
               "  min-height: %7px;"
               "  max-height: %7px;"
               "}"
               "QSpinBox:focus, QDoubleSpinBox:focus { border-color: %8; }"
               "QSpinBox:disabled, QDoubleSpinBox:disabled { color: %9; background: %10; }"
               "QSpinBox QLineEdit, QDoubleSpinBox QLineEdit {"
               "  border: none;"
               "  background: transparent;"
               "  padding: 0px;"
               "  margin: 0px;"
               "  color: %5;"
               "}"
               "QSpinBox::up-button, QDoubleSpinBox::up-button {"
               "  subcontrol-origin: border;"
               "  subcontrol-position: top right;"
               "  width: %6px;"
               "  height: %11px;"
               "  border-left: 1px solid %1;"
               "  border-bottom: 1px solid %1;"
               "  background: %12;"
               "  border-top-right-radius: %2px;"
               "}"
               "QSpinBox::down-button, QDoubleSpinBox::down-button {"
               "  subcontrol-origin: border;"
               "  subcontrol-position: bottom right;"
               "  width: %6px;"
               "  height: %11px;"
               "  border-left: 1px solid %1;"
               "  background: %12;"
               "  border-bottom-right-radius: %2px;"
               "}"
               "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
               "  width: 0; height: 0;"
               "  border-left: 3px solid transparent;"
               "  border-right: 3px solid transparent;"
               "  border-bottom: 4px solid %13;"
               "}"
               "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
               "  width: 0; height: 0;"
               "  border-left: 3px solid transparent;"
               "  border-right: 3px solid transparent;"
               "  border-top: 4px solid %13;"
               "}"
               "QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled,"
               "QDoubleSpinBox::up-arrow:disabled, QDoubleSpinBox::down-arrow:disabled {"
               "  border-bottom-color: %9; border-top-color: %9;"
               "}")
        .arg(c.border,
             QString::number(r),
             c.secondaryPanel,
             grad,
             c.primaryText,
             QString::number(btnW),
             QString::number(h),
             c.highlight,
             c.disabledText,
             c.disabledBg,
             QString::number(halfH),
             btnBg,
             c.secondaryText);
}

QString promptTextAreaStyleSheet(bool negative)
{
    const Colors c = colors();
    const QString bg = negative ? c.negativeTint : c.inputBg;
    return QStringLiteral(
               "QPlainTextEdit {"
               "  background-color: %1;"
               "  color: %2;"
               "  border: none;"
               "  padding: %3px;"
               "  selection-background-color: %4;"
               "}"
               "QPlainTextEdit:disabled { color: %5; }")
        .arg(bg, c.primaryText, QString::number(Spacing::unit), c.highlightSelection, c.disabledText);
}

QString promptStackFrameStyleSheet(bool focused)
{
    const Colors c = colors();
    const QString border = focused ? c.highlight : c.inputBorder;
    const int width = focused ? 2 : 1;
    return QStringLiteral(
               "QFrame#PromptStackWidget {"
               "  border: %1px solid %2;"
               "  background: %3;"
               "  border-radius: 2px;"
               "}")
        .arg(width)
        .arg(border)
        .arg(c.inputBg);
}

QString outlinedPanelStyleSheet(const QString &objectName)
{
    const Colors c = colors();
    return QStringLiteral(
               "QFrame#%1 {"
               "  border: 1px solid %2;"
               "  border-radius: 2px;"
               "  background: transparent;"
               "}")
        .arg(objectName, c.inputBorder);
}

QString primaryButtonStyleSheet()
{
    const Colors c = colors();
    const QString hover = lighten(c.secondaryPanel, 108);
    const QString pressed = QColor(c.secondaryPanel).darker(115).name(QColor::HexRgb);
    const int h = Spacing::primaryButtonHeight;
    return QStringLiteral(
               "QPushButton {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  border-radius: 2px;"
               "  color: %3;"
               "  padding: 0px %4px;"
               "  min-width: %5px;"
               "  min-height: %6px;"
               "  max-height: %6px;"
               "  font-weight: 600;"
               "}"
               "QPushButton:hover { background-color: %7; }"
               "QPushButton:pressed { background-color: %8; }"
               "QPushButton:disabled { background-color: %9; border-color: %10; color: %11; }")
        .arg(c.secondaryPanel,
             c.border,
             c.primaryText,
             QString::number(Spacing::iconTextGap + Spacing::unit),
             QString::number(Spacing::buttonMinWidth),
             QString::number(h),
             hover,
             pressed,
             c.disabledBg,
             c.inputBorder,
             c.disabledText);
}

QString secondaryButtonStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QPushButton {"
               "  background-color: transparent;"
               "  border: 1px solid %1;"
               "  border-radius: 2px;"
               "  color: %2;"
               "  padding: %3px %4px;"
               "  min-height: %5px;"
               "  font-weight: 600;"
               "}"
               "QPushButton:hover { background-color: %6; }"
               "QPushButton:pressed { background-color: %7; }"
               "QPushButton:disabled { color: %8; border-color: %9; }")
        .arg(c.border,
             c.primaryText,
             QString::number(Spacing::nestedPanel),
             QString::number(Spacing::iconTextGap + Spacing::unit),
             QString::number(Spacing::buttonHeight),
             c.secondaryPanel,
             QColor(c.secondaryPanel).darker(115).name(QColor::HexRgb),
             c.disabledText,
             c.inputBorder);
}

QString iconButtonStyleSheet()
{
    const Colors c = colors();
    const QString hover = lighten(c.secondaryPanel, 108);
    const int sz = Spacing::iconButtonSize;
    return QStringLiteral(
               "QToolButton, QPushButton {"
               "  border: none;"
               "  border-radius: 2px;"
               "  background: transparent;"
               "  padding: 0px;"
               "  min-width: %1px;"
               "  max-width: %1px;"
               "  min-height: %1px;"
               "  max-height: %1px;"
               "}"
               "QToolButton:hover, QPushButton:hover { background-color: %2; }"
               "QToolButton:pressed, QToolButton:checked,"
               "QPushButton:pressed, QPushButton:checked { background-color: %3; }"
               "QToolButton:disabled, QPushButton:disabled { color: %4; }")
        .arg(QString::number(sz), hover, c.highlightSelection, c.disabledText);
}

void applyIconToolButton(QToolButton *button)
{
    if (!button)
        return;
    button->setStyleSheet(iconButtonStyleSheet());
    button->setIconSize(QSize(Spacing::iconSmall, Spacing::iconSmall));
    button->setFixedSize(Spacing::iconButtonSize, Spacing::iconButtonSize);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void applyIconButton(QPushButton *button)
{
    if (!button)
        return;
    button->setStyleSheet(iconButtonStyleSheet());
    button->setIconSize(QSize(Spacing::iconSmall, Spacing::iconSmall));
    button->setFixedSize(Spacing::iconButtonSize, Spacing::iconButtonSize);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QString settingsNavListStyleSheet()
{
    const Colors c = colors();
    const QString hover = withAlpha(QColor(c.highlight), 40);
    return QStringLiteral(
               "QListWidget {"
               "  background-color: %1;"
               "  border: 1px solid %2;"
               "  outline: none;"
               "}"
               "QListWidget::item {"
               "  padding: %3px %4px;"
               "  min-height: %5px;"
               "  color: %6;"
               "  border-bottom: 1px solid %2;"
               "}"
               "QListWidget::item:selected {"
               "  background-color: %7;"
               "  color: white;"
               "}"
               "QListWidget::item:hover:!selected {"
               "  background-color: %8;"
               "}")
        .arg(c.panelBg,
             c.border,
             QString::number(Spacing::nestedPanel),
             QString::number(Spacing::sectionGap),
             QString::number(Spacing::rowHeight),
             c.primaryText,
             c.highlight,
             hover);
}

QString scrollBarStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QScrollBar:vertical {"
               "  background: %1;"
               "  width: 10px;"
               "  margin: 0;"
               "}"
               "QScrollBar::handle:vertical {"
               "  background: %2;"
               "  min-height: 24px;"
               "  border-radius: 3px;"
               "}"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
               "QScrollBar:horizontal { height: 0; }")
        .arg(c.scrollTrack, c.scrollThumb);
}

QString galleryListStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QListWidget {"
               "  background-color: %1;"
               "  border-top: 1px solid %2;"
               "  padding: 0px;"
               "}"
               "QListWidget::item {"
               "  background: transparent;"
               "  padding: 0px;"
               "  border-radius: 2px;"
               "}")
        .arg(c.secondaryPanel, c.border);
}

QString warningLabelStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral("color: %1; font-weight: bold; font-style: italic;").arg(c.yellow);
}

QString footerVersionStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral("color: %1; font-style: italic; font-size: 10px;").arg(c.secondaryText);
}

QString restoreDefaultsButtonStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QPushButton {"
               "  border: none;"
               "  background: transparent;"
               "  color: %1;"
               "  text-decoration: none;"
               "  padding: 0;"
               "}"
               "QPushButton:hover { text-decoration: underline; }")
        .arg(c.secondaryText);
}

QString overlayButtonStyleSheet()
{
    const Colors c = colors();
    const QString bg = withAlpha(QColor(c.panelBg), 170);
    const QString bgHover = withAlpha(QColor(c.secondaryPanel), 210);
    return QStringLiteral(
               "QPushButton {"
               "  border: 1px solid %1;"
               "  background: %2;"
               "  border-radius: 2px;"
               "  padding: 2px %3px;"
               "  min-height: %4px;"
               "}"
               "QPushButton:hover { background: %5; }")
        .arg(c.border, bg, QString::number(Spacing::unit), QString::number(Spacing::rowHeight - 4), bgHover);
}

void styleSectionTitle(QLabel *label)
{
    if (!label)
        return;
    QFont f = label->font();
    f.setBold(true);
    f.setPointSize(f.pointSize() + 2);
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(colors().primaryText));
}

void styleFormRowTitle(QLabel *label)
{
    if (!label)
        return;
    QFont f = label->font();
    f.setBold(true);
    f.setPointSize(qMax(9, f.pointSize()));
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(colors().primaryText));
}

void styleDescription(QLabel *label)
{
    if (!label)
        return;
    QFont f = label->font();
    f.setPointSize(qMax(8, f.pointSize() - 1));
    f.setBold(false);
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(colors().secondaryText));
}

void styleWarning(QLabel *label)
{
    if (!label)
        return;
    label->setStyleSheet(warningLabelStyleSheet());
}

void styleGalleryCaption(QLabel *label)
{
    if (!label)
        return;
    QFont f = label->font();
    f.setPointSize(10);
    f.setItalic(true);
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(colors().secondaryText));
}

void applySlider(QSlider *slider)
{
    if (!slider)
        return;
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG SLIDER legacy QSlider — use ComfyTrackSlider");
    ComfyUiLayoutDiagnostics::logSliderMetrics("legacyApplySlider", slider);
    slider->setFixedHeight(Spacing::sliderWidgetHeight);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    slider->setStyleSheet(sliderStyleSheet());
}

void applyComboBox(QWidget *widget)
{
    auto *combo = qobject_cast<QComboBox *>(widget);
    if (!combo)
        return;
    combo->setFixedHeight(Spacing::comboHeight);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (qobject_cast<ComfyComboBox *>(combo))
        return;
    combo->setStyleSheet(comboBoxStyleSheet());
}

void applyLineEdit(QLineEdit *edit)
{
    if (edit)
        edit->setStyleSheet(lineEditStyleSheet());
}

void applySpinBox(QSpinBox *spin)
{
    if (!spin)
        return;
    spin->setFixedHeight(Spacing::comboHeight);
    spin->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    if (qobject_cast<ComfySpinBox *>(spin))
        return;
    spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    spin->setStyleSheet(spinBoxStyleSheet());
}

void applyPromptTextArea(QPlainTextEdit *edit, bool negative)
{
    if (edit)
        edit->setStyleSheet(promptTextAreaStyleSheet(negative));
}

void applyPrimaryButton(QPushButton *button)
{
    if (!button)
        return;
    button->setStyleSheet(primaryButtonStyleSheet());
    button->setIconSize(QSize(Spacing::iconSmall, Spacing::iconSmall));
    button->setFixedHeight(Spacing::primaryButtonHeight);
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

void applySecondaryButton(QPushButton *button)
{
    if (button)
        button->setStyleSheet(secondaryButtonStyleSheet());
}

void applySettingsNavList(QListWidget *list)
{
    if (list)
        list->setStyleSheet(settingsNavListStyleSheet());
}

void applyScrollArea(QScrollArea *scroll)
{
    if (!scroll)
        return;
    scroll->setStyleSheet(scrollBarStyleSheet());
    if (QScrollBar *bar = scroll->verticalScrollBar())
        bar->setStyleSheet(scrollBarStyleSheet());
}

void applyGalleryList(QWidget *list)
{
    if (list)
        list->setStyleSheet(galleryListStyleSheet());
}

void applyDockerPanelLayout(QWidget *panel, int margins)
{
    if (!panel)
        return;
    if (QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(panel->layout())) {
        layout->setContentsMargins(margins, margins, margins, margins);
        layout->setSpacing(Spacing::rowGap);
    }
    panel->setStyleSheet(QStringLiteral("background-color: %1;").arg(colors().panelBg));
}

void applyTightRowLayout(QBoxLayout *layout, int spacing)
{
    if (!layout)
        return;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
}

QString flatComboStyleSheet()
{
    return comboBoxStyleSheet();
}

QString checkboxStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QCheckBox { spacing: %1px; color: %2; }"
               "QCheckBox::indicator {"
               "  width: %3px; height: %3px; border: 1px solid %4; border-radius: 2px;"
               "  background-color: %5;"
               "}"
               "QCheckBox::indicator:checked { background-color: %5; border-color: %4; }"
               "QCheckBox::indicator:disabled { border-color: %5; background-color: %6; }")
        .arg(QString::number(Spacing::unit),
             c.primaryText,
             QString::number(Spacing::checkboxSize),
             c.border,
             c.inputBorder,
             c.inputBg,
             c.disabledBg);
}

void applyCheckbox(QCheckBox *box)
{
    if (!box)
        return;
    if (qobject_cast<ComfyCheckBox *>(box)) {
        box->setStyleSheet(QString());
        return;
    }
    box->setStyleSheet(checkboxStyleSheet());
}

QString radioButtonStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QRadioButton { spacing: %1px; color: %2; }"
               "QRadioButton::indicator {"
               "  width: %3px; height: %3px; border: 1px solid %4; border-radius: %4px; background: transparent;"
               "}"
               "QRadioButton::indicator:checked { background: %5; border-color: %5; }"
               "QRadioButton::indicator:disabled { border-color: %6; }")
        .arg(QString::number(Spacing::unit),
             c.primaryText,
             QString::number(Spacing::checkboxSize),
             c.border,
             c.highlight,
             c.inputBorder);
}

QString progressBarStyleSheet(bool upload)
{
    const Colors c = colors();
    const ComfyTheme::Palette theme = ComfyTheme::palette();
    const QString chunk = upload ? theme.progressAlt : theme.active;
    return QStringLiteral(
               "QProgressBar { border: none; margin: 0; padding: 0;"
               " min-height: %1px; max-height: %1px; background: %2; }"
               "QProgressBar::chunk { margin: 0; border-radius: 1px; background: %3; }")
        .arg(QString::number(Spacing::progressBarHeight), c.sliderTrack, chunk);
}

QString expanderButtonStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral(
               "QToolButton { border: none; font-weight: bold; text-align: left; color: %1; padding: %2px 0; }"
               "QToolButton:hover { color: %3; }")
        .arg(c.primaryText, QString::number(Spacing::unit), c.highlight);
}

QString flatGroupBoxStyleSheet()
{
    return QStringLiteral("QGroupBox { border: 0; margin: 0; padding: 0; }");
}

QString logoPlaceholderStyleSheet()
{
    const Colors c = colors();
    return QStringLiteral("background: %1; border-radius: 4px;").arg(c.secondaryPanel);
}

QString statusLabelStyleSheet(StatusTone tone)
{
    const Colors c = colors();
    QString color = c.primaryText;
    switch (tone) {
    case StatusTone::Neutral:
        color = c.secondaryText;
        break;
    case StatusTone::Success:
        color = c.green;
        break;
    case StatusTone::Warning:
        color = c.yellow;
        break;
    case StatusTone::Error:
        color = c.red;
        break;
    }
    return QStringLiteral("color: %1;").arg(color);
}

void styleHint(QLabel *label)
{
    if (!label)
        return;
    QFont f = label->font();
    f.setItalic(true);
    label->setFont(f);
    styleDescription(label);
}

void styleCaption(QLabel *label)
{
    styleDescription(label);
}

void styleStatusLabel(QLabel *label, StatusTone tone)
{
    if (label)
        label->setStyleSheet(statusLabelStyleSheet(tone));
}

void resetLabelStyle(QLabel *label)
{
    if (label)
        label->setStyleSheet(QStringLiteral("color: %1;").arg(colors().primaryText));
}

void applyFlatCombo(QWidget *combo)
{
    applyComboBox(combo);
}

void applyDoubleSpinBox(QDoubleSpinBox *spin)
{
    if (!spin)
        return;
    spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    spin->setStyleSheet(spinBoxStyleSheet());
    spin->setFixedHeight(Spacing::comboHeight);
    spin->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

void applyRadioButton(QRadioButton *button)
{
    if (button)
        button->setStyleSheet(radioButtonStyleSheet());
}

void applyExpanderButton(QToolButton *button)
{
    if (button)
        button->setStyleSheet(expanderButtonStyleSheet());
}

void applyProgressBar(QProgressBar *bar, bool upload)
{
    if (!bar)
        return;
    bar->setStyleSheet(progressBarStyleSheet(upload));
    bar->setFixedHeight(Spacing::progressBarHeight);
}

void applyWidgetTree(QWidget *root)
{
    if (!root)
        return;
    for (QComboBox *combo : root->findChildren<QComboBox *>())
        applyComboBox(combo);
    for (QLineEdit *edit : root->findChildren<QLineEdit *>())
        applyLineEdit(edit);
    for (QSpinBox *spin : root->findChildren<QSpinBox *>())
        applySpinBox(spin);
    for (QDoubleSpinBox *spin : root->findChildren<QDoubleSpinBox *>())
        applyDoubleSpinBox(spin);
    for (QSlider *slider : root->findChildren<QSlider *>()) {
        if (slider->parentWidget() && slider->parentWidget()->inherits("ComfySlider"))
            continue;
        applySlider(slider);
    }
    for (ComfyTrackSlider *track : root->findChildren<ComfyTrackSlider *>())
        Q_UNUSED(track);
    for (QCheckBox *box : root->findChildren<QCheckBox *>())
        applyCheckbox(box);
    for (QRadioButton *radio : root->findChildren<QRadioButton *>())
        applyRadioButton(radio);
    for (QPlainTextEdit *edit : root->findChildren<QPlainTextEdit *>())
        applyPromptTextArea(edit, false);
    for (QScrollArea *scroll : root->findChildren<QScrollArea *>())
        applyScrollArea(scroll);
    for (QToolButton *btn : root->findChildren<QToolButton *>()) {
        if (qobject_cast<const ComfyQueueButton *>(btn))
            continue;
        if (qobject_cast<const ComfyWorkspaceSelectButton *>(btn))
            continue;
        if (!btn->icon().isNull() && btn->text().isEmpty())
            applyIconToolButton(btn);
    }
    for (QPushButton *button : root->findChildren<QPushButton *>()) {
        if (!button->styleSheet().isEmpty())
            continue;
        if (!button->icon().isNull() && button->text().isEmpty())
            applyIconButton(button);
        else
            applySecondaryButton(button);
    }
}

} // namespace ComfyUiStyle
