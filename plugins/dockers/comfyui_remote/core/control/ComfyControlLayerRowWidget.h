/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_CONTROL_LAYER_ROW_WIDGET_H_
#define COMFY_CONTROL_LAYER_ROW_WIDGET_H_

#include "ComfyControlLayer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QToolButton>
#include <QWidget>
#include <functional>

class KisViewManager;
class ComfyTrackSlider;
class ComfyUIIntervalSlider;

class ComfyControlLayerRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComfyControlLayerRowWidget(ComfyControlLayerEntry *entry,
                                        int index,
                                        KisViewManager *viewManager,
                                        std::function<QString()> archKeyProvider,
                                        QWidget *parent = nullptr);

    void refreshLayerCombo();
    void updateVisibility();
    void setGenerateEnabled(bool enabled);

Q_SIGNALS:
    void entryEdited();
    void generateRequested(int index);
    void addPoseCharacterRequested(int index);
    void removeRequested(int index);

private:
    void applyPresetFromSlider();
    void syncRangeLabels();
    void onModeChanged(int comboIndex);
    void onLayerChanged(int comboIndex);
    void updateSupportState();
    bool isPoseVectorLayer() const;

    ComfyControlLayerEntry *m_entry = nullptr;
    int m_index = -1;
    KisViewManager *m_viewManager = nullptr;
    std::function<QString()> m_archKeyProvider;

    QComboBox *m_modeCombo = nullptr;
    QWidget *m_middleControls = nullptr;
    QLabel *m_errorLabel = nullptr;
    QComboBox *m_layerCombo = nullptr;
    ComfyTrackSlider *m_presetSlider = nullptr;
    QToolButton *m_btnGenerate = nullptr;
    QToolButton *m_btnAddPose = nullptr;
    QToolButton *m_btnExpand = nullptr;
    QToolButton *m_btnRemove = nullptr;
    QWidget *m_extended = nullptr;
    QCheckBox *m_customStrength = nullptr;
    ComfyTrackSlider *m_strengthSlider = nullptr;
    QLabel *m_strengthLabel = nullptr;
    QLabel *m_rangeLabel = nullptr;
    ComfyUIIntervalSlider *m_rangeSlider = nullptr;
    QLabel *m_rangeStartLabel = nullptr;
    QLabel *m_rangeEndLabel = nullptr;
};

#endif
