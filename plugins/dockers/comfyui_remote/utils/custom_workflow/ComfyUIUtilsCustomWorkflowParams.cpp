/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyResources.h"
#include "ComfyControlLayer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>

#include <kis_image.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>

#include <KisViewManager.h>

namespace ComfyUIUtils {

namespace {
QVariant jsonDefaultToVariant(const QJsonValue &v, CustomWorkflowParamSlot::Kind kind)
{
    switch (kind) {
    case CustomWorkflowParamSlot::Kind::ParameterBool:
        if (v.isBool())
            return v.toBool();
        if (v.isDouble())
            return v.toInt() != 0;
        if (v.isString()) {
            const QString s = v.toString().trimmed().toLower();
            return (s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes"));
        }
        return false;
    case CustomWorkflowParamSlot::Kind::ParameterInt:
        if (v.isDouble())
            return static_cast<int>(v.toDouble());
        if (v.isString())
            return v.toString().toInt();
        return 0;
    case CustomWorkflowParamSlot::Kind::ParameterFloat:
        if (v.isDouble())
            return v.toDouble();
        if (v.isString())
            return v.toString().toDouble();
        return 0.0;
    default:
        if (v.isBool())
            return v.toBool();
        if (v.isDouble())
            return v.toDouble();
        if (v.isString())
            return v.toString();
        return QVariant();
    }
}

} // namespace
QList<CustomWorkflowParamSlot> discoverCustomWorkflowParameterSlots(const QJsonObject &workflowRoot)
{
    QList<CustomWorkflowParamSlot> out;
    for (auto it = workflowRoot.begin(); it != workflowRoot.end(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject node = it.value().toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        CustomWorkflowParamSlot slot;
        slot.nodeId = it.key();
        if (ct == QLatin1String("ETN_Parameter")) {
            const QString ptype = inputs.value(QStringLiteral("type")).toString();
            if (ptype.isEmpty() || ptype == QLatin1String("auto"))
                continue;
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Parameter");
            slot.typeStr = ptype;
            const QJsonValue defV = inputs.value(QStringLiteral("default"));
            const double jmin = inputs.value(QStringLiteral("min")).toDouble(-2147483648.0);
            const double jmax = inputs.value(QStringLiteral("max")).toDouble(2147483647.0);
            slot.minV = jmin;
            slot.maxV = jmax;
            if (ptype == QLatin1String("number (integer)")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterInt;
                slot.defaultValue = jsonDefaultToVariant(defV, slot.kind);
            } else if (ptype == QLatin1String("number")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterFloat;
                slot.defaultValue = jsonDefaultToVariant(defV, slot.kind);
            } else if (ptype == QLatin1String("toggle")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterBool;
                slot.defaultValue = jsonDefaultToVariant(defV, slot.kind);
            } else if (ptype == QLatin1String("text")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterText;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
            } else if (ptype == QLatin1String("prompt (positive)")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterPromptPositive;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
            } else if (ptype == QLatin1String("prompt (negative)")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterPromptNegative;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
            } else if (ptype == QLatin1String("choice")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterChoice;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
                const QJsonArray ch = inputs.value(QStringLiteral("choices")).toArray();
                for (const QJsonValue &cv : ch)
                    slot.choices.append(cv.toString());
            } else {
                slot.kind = CustomWorkflowParamSlot::Kind::Unsupported;
                slot.paramName = inputs.value(QStringLiteral("name")).toString(QStringLiteral("?")) + QLatin1String(": ") + ptype;
            }
            out.append(slot);
        } else if (ct == QLatin1String("ETN_KritaStyle")) {
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Style");
            slot.kind = CustomWorkflowParamSlot::Kind::KritaStylePicker;
            out.append(slot);
        } else if (ct == QLatin1String("ETN_KritaImageLayer")) {
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Image");
            slot.kind = CustomWorkflowParamSlot::Kind::KritaImageLayer;
            out.append(slot);
        } else if (ct == QLatin1String("ETN_KritaMaskLayer")) {
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Mask");
            slot.kind = CustomWorkflowParamSlot::Kind::KritaMaskLayer;
            out.append(slot);
        }
    }
    std::sort(out.begin(), out.end(), [](const CustomWorkflowParamSlot &a, const CustomWorkflowParamSlot &b) {
        return a.paramName.localeAwareCompare(b.paramName) < 0;
    });
    return out;
}

QString paintLayerNameByUuid(KisImageSP image, const QString &uuidWithoutBraces)
{
    if (!image || uuidWithoutBraces.isEmpty())
        return QString();
    KisGroupLayerSP root = image->rootLayer();
    if (!root)
        return QString();
    QList<KisNodeSP> nodes;
    nodes.append(root);
    while (!nodes.isEmpty()) {
        KisNodeSP n = nodes.takeFirst();
        if (dynamic_cast<KisPaintLayer *>(n.data())
            && n->uuid().toString(QUuid::WithoutBraces) == uuidWithoutBraces) {
            return n->name();
        }
        for (quint32 i = 0; i < n->childCount(); ++i)
            nodes.append(n->at(i));
    }
    return QString();
}

void applyCustomWorkflowParameterValues(QJsonObject &workflowRoot,
                                        const QMap<QString, QVariant> &valuesByKey,
                                        KisImageSP layerResolutionImage)
{
    if (valuesByKey.isEmpty())
        return;
    const QStringList keys = workflowRoot.keys();
    for (const QString &nodeId : keys) {
        QJsonObject node = workflowRoot.value(nodeId).toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        QString pname = inputs.value(QStringLiteral("name")).toString();
        if (ct == QLatin1String("ETN_Parameter")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Parameter");
        } else if (ct == QLatin1String("ETN_KritaStyle")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Style");
        } else if (ct == QLatin1String("ETN_KritaImageLayer")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Image");
        } else if (ct == QLatin1String("ETN_KritaMaskLayer")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Mask");
        }
        if (ct == QLatin1String("ETN_KritaImageLayer") || ct == QLatin1String("ETN_KritaMaskLayer")) {
            if (!layerResolutionImage || !valuesByKey.contains(nodeId))
                continue;
            const QString resolved = paintLayerNameByUuid(layerResolutionImage, valuesByKey.value(nodeId).toString());
            if (!resolved.isEmpty())
                inputs.insert(QStringLiteral("name"), resolved);
            node.insert(QStringLiteral("inputs"), inputs);
            workflowRoot.insert(nodeId, node);
            continue;
        }
        if (!valuesByKey.contains(pname))
            continue;
        const QVariant v = valuesByKey.value(pname);
        if (ct == QLatin1String("ETN_Parameter")) {
            inputs.insert(QStringLiteral("default"), QJsonValue::fromVariant(v));
        }
        node.insert(QStringLiteral("inputs"), inputs);
        workflowRoot.insert(nodeId, node);
    }
}

// §13.103: At most one ETN_KritaStyleAndPrompt node in custom workflow
QPair<bool, QString> validateCustomWorkflowStyleAndPromptNodes(const QJsonObject &workflow)
{
    const QString nodeType = QStringLiteral("ETN_KritaStyleAndPrompt");
    int count = 0;
    for (auto it = workflow.begin(); it != workflow.end(); ++it) {
        if (!it.value().isObject()) continue;
        QString ct = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (ct == nodeType)
            count++;
    }
    if (count > 1)
        return qMakePair(false, ComfyTr::tr("Workflow contains multiple 'Krita Style & Prompt' nodes, but only one is allowed."));
    return qMakePair(true, QString());
}

QPair<bool, QString> validateCustomWorkflowApiGraph(const QJsonObject &workflow,
                                                    const QJsonObject &objectInfoRoot)
{
    if (workflow.isEmpty())
        return qMakePair(false, ComfyTr::tr("Custom workflow is empty. Paste API JSON in Settings → Workflow."));

    int nodeCount = 0;
    QStringList missingOnServer;
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject node = it.value().toObject();
        const QString classType = node.value(QStringLiteral("class_type")).toString();
        if (classType.isEmpty())
            return qMakePair(false,
                             ComfyTr::tr("Workflow node %1 is missing class_type.", it.key()));
        ++nodeCount;
        if (!objectInfoRoot.isEmpty() && !objectInfoRoot.contains(classType))
            missingOnServer.append(classType);
    }
    if (nodeCount == 0)
        return qMakePair(false, ComfyTr::tr("Custom workflow has no API nodes. Export API JSON from ComfyUI (File → Export API)."));

    missingOnServer.removeDuplicates();
    if (!missingOnServer.isEmpty()) {
        const int showMax = 8;
        QStringList shown = missingOnServer.mid(0, showMax);
        QString msg = ComfyTr::tr("Server is missing custom nodes required by this workflow: %1",
                                  shown.join(QStringLiteral(", ")));
        if (missingOnServer.size() > showMax)
            msg += ComfyTr::tr(" (and %1 more)", missingOnServer.size() - showMax);
        return qMakePair(false, msg);
    }
    if (!validateCustomWorkflowHasOutputNode(workflow)) {
        return qMakePair(false,
                         ComfyTr::tr("Custom workflow has no output node (SaveImage or ETN_ReturnImage). Add one "
                                     "before running."));
    }

    return qMakePair(true, QString());
}

bool validateCustomWorkflowHasOutputNode(const QJsonObject &workflow)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QString classType = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (classType == QLatin1String("SaveImage") || classType == QLatin1String("ETN_ReturnImage")
            || classType == QLatin1String("ETN_SendImage"))
            return true;
    }
    return false;
}
QString findFirstWorkflowNodeIdByClassType(const QJsonObject &workflow, const QString &classType)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == classType)
            return it.key();
    }
    return QString();
}

bool workflowContainsKritaInjectionNodes(const QJsonObject &workflow)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QString ct = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (ct == QLatin1String("ETN_KritaCanvas") || ct == QLatin1String("ETN_KritaSelection"))
            return true;
    }
    return false;
}

bool workflowNeedsCustomKritaExpansion(const QJsonObject &workflow)
{
    static const QSet<QString> expandedTypes = {
        QLatin1String("ETN_KritaCanvas"),
        QLatin1String("ETN_KritaSelection"),
        QLatin1String("ETN_Parameter"),
        QLatin1String("ETN_KritaImageLayer"),
        QLatin1String("ETN_KritaMaskLayer"),
        QLatin1String("ETN_KritaStyle"),
        QLatin1String("ETN_KritaStyleAndPrompt"),
    };
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        if (expandedTypes.contains(it.value().toObject().value(QStringLiteral("class_type")).toString()))
            return true;
    }
    return false;
}

bool isCustomWorkflowLiveCapture(const QJsonObject &workflow)
{
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject node = it.value().toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        if (ct != QLatin1String("ETN_KritaStyleAndPrompt") && ct != QLatin1String("ETN_KritaStyle"))
            continue;
        if (node.value(QStringLiteral("inputs"))
                .toObject()
                .value(QStringLiteral("sampler_preset"))
                .toString()
            == QLatin1String("live"))
            return true;
    }
    return false;
}

bool customWorkflowCaptureExcludesInternal(bool customGenerationModeLive)
{
    return !customGenerationModeLive;
}

bool customWorkflowNodeUsesLiveSampling(const QString &nodeSamplerPreset, bool customGenerationModeLive)
{
    const QString preset = nodeSamplerPreset.trimmed().toLower();
    if (preset == QLatin1String("live"))
        return true;
    if (preset == QLatin1String("auto"))
        return customGenerationModeLive;
    return false;
}

} // namespace ComfyUIUtils
