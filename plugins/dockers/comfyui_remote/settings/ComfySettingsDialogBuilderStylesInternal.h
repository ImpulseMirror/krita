/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfySettingsDialogBuilder.h"

class QCheckBox;
class QComboBox;
class QDialog;
class QLabel;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QWidget;

class ComfyStyleLoraListWidget;
class ComfyStyleSamplerWidget;
class ComfySwitchWidget;

namespace ComfySettingsDialogBuilderStylesInternal {

struct StylesWorkspace {
    ComfyUIRemoteDock *dock = nullptr;
    ComfyUIRemoteDock::Private *d = nullptr;
    QDialog *dialog = nullptr;
    QStackedWidget *stack = nullptr;

    QComboBox *stylesPresetMirror = nullptr;
    QToolButton *btnStylesAddPreset = nullptr;
    QToolButton *btnStylesDuplicate = nullptr;
    QToolButton *btnStylesDeletePreset = nullptr;
    QToolButton *btnStylesRefresh = nullptr;
    QLabel *lblBuiltinMessage = nullptr;
    QLabel *lblBuiltinCopyLink = nullptr;
    QCheckBox *checkShowBuiltinStyles = nullptr;
    QLineEdit *editStyleName = nullptr;
    QComboBox *stylesCkptMirror = nullptr;
    QToolButton *btnStylesCkptRefresh = nullptr;
    QLabel *stylesCkptWarning = nullptr;
    QToolButton *toggleAdvCkpt = nullptr;
    QWidget *advCkptBody = nullptr;
    QComboBox *comboStyleArchitecture = nullptr;
    QComboBox *comboStyleVae = nullptr;
    QSpinBox *spinStyleClipSkip = nullptr;
    QCheckBox *checkStyleClipSkipOverride = nullptr;
    QSpinBox *spinStylePreferredResolution = nullptr;
    QCheckBox *checkStylePreferredResolution = nullptr;
    ComfySwitchWidget *switchStyleZsnr = nullptr;
    ComfySwitchWidget *switchStyleSag = nullptr;
    QLabel *labelStyleZsnrState = nullptr;
    QLabel *labelStyleSagState = nullptr;
    ComfyStyleLoraListWidget *loraListWidget = nullptr;
    QLineEdit *editStylesPositive = nullptr;
    QLineEdit *editStylesNegative = nullptr;
    QComboBox *comboLinkedEditStyle = nullptr;
    QWidget *linkedEditStyleRow = nullptr;
    ComfyStyleSamplerWidget *qualitySamplerWidget = nullptr;
    ComfyStyleSamplerWidget *liveSamplerWidget = nullptr;
};

void buildStylesTabWidgets(StylesWorkspace &ws);
ComfySettingsDialogBuilder::StylesTabResult wireStylesTabSync(StylesWorkspace &ws);

} // namespace ComfySettingsDialogBuilderStylesInternal
