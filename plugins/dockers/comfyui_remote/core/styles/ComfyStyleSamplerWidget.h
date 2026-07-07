/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_STYLE_SAMPLER_WIDGET_H_
#define COMFY_STYLE_SAMPLER_WIDGET_H_

#include "ComfyStyleCollection.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QAbstractSlider;
class QToolButton;

/// Mirrors ai_diffusion/ui/style.py SamplerWidget (without “edit custom presets” link).
class ComfyStyleSamplerWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Quality, Live };

    explicit ComfyStyleSamplerWidget(Kind kind, QWidget *parent = nullptr);

    void readFromStyle(const ComfyStyleEntry &style);
    void writeToStyle(ComfyStyleEntry *style) const;

    void setEditingEnabled(bool enabled);

Q_SIGNALS:
    void valueChanged();

private:
    void refillPresetCombo(const QString &selectName);
    void updateInfoLabel();
    void onPresetChanged(int index);

    Kind m_kind;
    QToolButton *m_expander = nullptr;
    QComboBox *m_preset = nullptr;
    QLabel *m_samplerInfo = nullptr;
    QAbstractSlider *m_steps = nullptr;
    QLabel *m_stepsValue = nullptr;
    QAbstractSlider *m_cfg = nullptr;
    QLabel *m_cfgValue = nullptr;
    QWidget *m_extended = nullptr;
    bool m_loading = false;
};

#endif
