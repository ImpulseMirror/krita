/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyControlLayer.h"

#include "ComfyResources.h"
#include "ComfyUIUtils.h"

#include <QJsonValue>
#include <algorithm>

namespace ComfyControlLayer {

QStringList allModeKeys()
{
    return {
        QString::fromUtf8(ComfyResources::ControlMode::reference),
        QString::fromUtf8(ComfyResources::ControlMode::style),
        QString::fromUtf8(ComfyResources::ControlMode::composition),
        QString::fromUtf8(ComfyResources::ControlMode::face),
        QString::fromUtf8(ComfyResources::ControlMode::scribble),
        QString::fromUtf8(ComfyResources::ControlMode::line_art),
        QString::fromUtf8(ComfyResources::ControlMode::soft_edge),
        QString::fromUtf8(ComfyResources::ControlMode::canny_edge),
        QString::fromUtf8(ComfyResources::ControlMode::depth),
        QString::fromUtf8(ComfyResources::ControlMode::normal),
        QString::fromUtf8(ComfyResources::ControlMode::pose),
        QString::fromUtf8(ComfyResources::ControlMode::segmentation),
        QString::fromUtf8(ComfyResources::ControlMode::blur),
        QString::fromUtf8(ComfyResources::ControlMode::stencil),
        QString::fromUtf8(ComfyResources::ControlMode::hands),
        QString::fromUtf8(ComfyResources::ControlMode::inpaint),
        QString::fromUtf8(ComfyResources::ControlMode::universal),
    };
}

QString modeLabel(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("reference"))
        return QStringLiteral("Reference");
    if (m == QLatin1String("style"))
        return QStringLiteral("Style");
    if (m == QLatin1String("composition"))
        return QStringLiteral("Composition");
    if (m == QLatin1String("face"))
        return QStringLiteral("Face");
    if (m == QLatin1String("scribble"))
        return QStringLiteral("Scribble");
    if (m == QLatin1String("line_art"))
        return QStringLiteral("Line Art");
    if (m == QLatin1String("soft_edge"))
        return QStringLiteral("Soft Edge");
    if (m == QLatin1String("canny_edge"))
        return QStringLiteral("Canny Edge");
    if (m == QLatin1String("depth"))
        return QStringLiteral("Depth");
    if (m == QLatin1String("normal"))
        return QStringLiteral("Normal");
    if (m == QLatin1String("pose"))
        return QStringLiteral("Pose");
    if (m == QLatin1String("segmentation"))
        return QStringLiteral("Segment");
    if (m == QLatin1String("blur"))
        return QStringLiteral("Unblur");
    if (m == QLatin1String("stencil"))
        return QStringLiteral("Stencil");
    if (m == QLatin1String("hands"))
        return QStringLiteral("Hands");
    if (m == QLatin1String("inpaint"))
        return QStringLiteral("Inpaint");
    if (m == QLatin1String("universal"))
        return QStringLiteral("Universal");
    return mode;
}

bool modeHasPreprocessor(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    return m == QLatin1String("hands") || m == QLatin1String("scribble") || m == QLatin1String("line_art")
           || m == QLatin1String("soft_edge") || m == QLatin1String("canny_edge") || m == QLatin1String("depth")
           || m == QLatin1String("normal") || m == QLatin1String("pose") || m == QLatin1String("segmentation");
}

void applyPresetDefaults(ComfyControlLayerEntry *entry, const QString &archKey)
{
    if (!entry)
        return;
    const QJsonObject root = ComfyUIUtils::builtinControlPresetsRoot();
    const QString modeKey = entry->mode.trimmed().isEmpty() ? QStringLiteral("default") : entry->mode.trimmed();
    QList<ComfyUIUtils::ControlLayerPreset> presets =
        ComfyUIUtils::controlPresetsForMode(root, modeKey, archKey);
    if (presets.isEmpty())
        presets = ComfyUIUtils::controlPresetsForMode(root, QStringLiteral("default"), archKey);
    if (presets.isEmpty())
        return;
    const double t = qBound(0.0, entry->presetValue / static_cast<double>(maxPresetValue), 1.0);
    const int last = presets.size() - 1;
    const double scaled = t * last;
    const int i0 = qBound(0, static_cast<int>(scaled), last);
    const int i1 = qMin(i0 + 1, last);
    const double frac = scaled - i0;
    const ComfyUIUtils::ControlLayerPreset p0 = presets.at(i0);
    const ComfyUIUtils::ControlLayerPreset p1 = presets.at(i1);
    const double strength = p0.strength + (p1.strength - p0.strength) * frac;
    const double start = p0.start + (p1.start - p0.start) * frac;
    const double end = p0.end + (p1.end - p0.end) * frac;
    entry->strength = qBound(0, static_cast<int>(strength * strengthMultiplier + 0.5), 100);
    entry->start = start;
    entry->end = end;
}

double strengthAsFloat(int strengthPercent)
{
    return qBound(0, strengthPercent, 100) / static_cast<double>(strengthMultiplier);
}

QJsonObject ComfyControlLayerEntry::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("mode"), mode);
    if (!layerId.isEmpty())
        o.insert(QStringLiteral("layer_id"), layerId);
    if (!layerName.isEmpty())
        o.insert(QStringLiteral("layer_name"), layerName);
    o.insert(QStringLiteral("preset_value"), presetValue);
    o.insert(QStringLiteral("strength"), strength);
    o.insert(QStringLiteral("start"), start);
    o.insert(QStringLiteral("end"), end);
    o.insert(QStringLiteral("use_custom_strength"), useCustomStrength);
    return o;
}

ComfyControlLayerEntry ComfyControlLayerEntry::fromJson(const QJsonObject &o)
{
    ComfyControlLayerEntry e;
    e.mode = o.value(QStringLiteral("mode")).toString(e.mode);
    e.layerId = o.value(QStringLiteral("layer_id")).toString();
    e.layerName = o.value(QStringLiteral("layer_name")).toString();
    e.presetValue = o.value(QStringLiteral("preset_value")).toInt(e.presetValue);
    e.strength = o.value(QStringLiteral("strength")).toInt(e.strength);
    e.start = o.value(QStringLiteral("start")).toDouble(e.start);
    e.end = o.value(QStringLiteral("end")).toDouble(e.end);
    e.useCustomStrength = o.value(QStringLiteral("use_custom_strength")).toBool(false);
    return e;
}

QJsonArray toJsonArray(const QList<ComfyControlLayerEntry> &layers)
{
    QJsonArray arr;
    for (const ComfyControlLayerEntry &e : layers)
        arr.append(e.toJson());
    return arr;
}

QList<ComfyControlLayerEntry> fromJsonArray(const QJsonArray &arr)
{
    QList<ComfyControlLayerEntry> out;
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        out.append(ComfyControlLayerEntry::fromJson(v.toObject()));
    }
    return out;
}

bool needsGenerateUpload(const ComfyControlLayerEntry &entry)
{
    if (entry.layerName.isEmpty())
        return false;
    if (ComfyResources::ControlMode::isIpAdapter(entry.mode))
        return true;
    return ComfyResources::ControlMode::isStructural(entry.mode);
}

ComfyControlLayerEntry makeDefaultForLayer(const QString &layerName, const QString &archKey)
{
    ComfyControlLayerEntry e;
    e.mode = QStringLiteral("depth");
    e.layerName = layerName;
    e.presetValue = 2;
    applyPresetDefaults(&e, archKey);
    return e;
}

} // namespace ComfyControlLayer
