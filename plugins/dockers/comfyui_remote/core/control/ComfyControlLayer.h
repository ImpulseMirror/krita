/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_CONTROL_LAYER_H_
#define COMFY_CONTROL_LAYER_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QList>

/// One control layer row (parity with ai_diffusion/control.py ControlLayer persisted fields).
struct ComfyControlLayerEntry {
    QString mode = QStringLiteral("depth");
    QString layerId;
    QString layerName;
    int presetValue = 2;
    int strength = 50;
    double start = 0.0;
    double end = 1.0;
    bool useCustomStrength = false;

    QJsonObject toJson() const;
    static ComfyControlLayerEntry fromJson(const QJsonObject &o);
};

namespace ComfyControlLayer {

constexpr int maxPresetValue = 4;
constexpr int strengthMultiplier = 50;

QStringList allModeKeys();
/// Modes shown in control-layer UI (excludes inpaint, universal — Python ControlMode.is_internal).
QStringList uiModeKeys();
bool modeHasRange(const QString &mode);
QString modeLabel(const QString &mode);
bool modeHasPreprocessor(const QString &mode);
void applyPresetDefaults(ComfyControlLayerEntry *entry, const QString &archKey);
double strengthAsFloat(int strengthPercent);

QJsonArray toJsonArray(const QList<ComfyControlLayerEntry> &layers);
QList<ComfyControlLayerEntry> fromJsonArray(const QJsonArray &arr);

ComfyControlLayerEntry makeDefaultForLayer(const QString &layerName, const QString &archKey);

/// Layer contributes to generate upload (ControlNet and/or IP-Adapter).
bool needsGenerateUpload(const ComfyControlLayerEntry &entry);

/// `model.py::_add_reference_layers` — reference controls for `<layer:name>` tags (0.5 strength, 0.2–0.8 range).
QList<ComfyControlLayerEntry> referenceLayersFromPromptTags(const QStringList &layerNames);

/// Python ControlLayer.can_generate — preprocessor modes with a named target layer.
bool canGenerateJob(const ComfyControlLayerEntry &entry);

bool hasStructuralControlAmong(const QList<ComfyControlLayerEntry> &layers);
bool anyNeedsGenerateUpload(const QList<ComfyControlLayerEntry> &layers);

} // namespace ComfyControlLayer

#endif
