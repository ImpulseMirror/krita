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

namespace
{
struct LinkEntry {
    int id = -1;
    int fromNode = -1;
    int fromSlot = -1;
    int toNode = -1;
    int toSlot = -1;
};

int jsonIntFlexible(const QJsonValue &v)
{
    if (v.isDouble())
        return static_cast<int>(v.toDouble());
    if (v.isString())
        return v.toString().toInt();
    return v.toInt();
}

bool isWidgetTypeTag(const QString &t)
{
    return t == QStringLiteral("INT") || t == QStringLiteral("FLOAT") || t == QStringLiteral("BOOLEAN")
        || t == QStringLiteral("BOOL") || t == QStringLiteral("STRING") || t == QStringLiteral("COMBO")
        || t == QStringLiteral("LIST");
}

bool isConnectionInputSpec(const QJsonValue &specVal)
{
    if (!specVal.isArray())
        return false;
    const QJsonArray a = specVal.toArray();
    if (a.isEmpty())
        return false;
    return !isWidgetTypeTag(a.at(0).toString());
}

bool uiInputHasLink(const QJsonObject &inObj)
{
    if (!inObj.contains(QStringLiteral("link")))
        return false;
    const QJsonValue v = inObj.value(QStringLiteral("link"));
    if (v.isNull() || v.isUndefined())
        return false;
    if (v.isBool())
        return false;
    if (v.isDouble())
        return v.toDouble() != 0.0;
    if (v.isString()) {
        const QString s = v.toString().trimmed();
        return !s.isEmpty() && s != QStringLiteral("null");
    }
    return true;
}

QJsonValue coerceWidgetForSpec(const QJsonValue &w, const QJsonValue &specVal)
{
    if (!specVal.isArray())
        return w;
    const QJsonArray a = specVal.toArray();
    if (a.isEmpty())
        return w;
    const QString t = a.at(0).toString();
    if (t == QStringLiteral("INT")) {
        if (w.isDouble())
            return static_cast<int>(w.toDouble());
        if (w.isString())
            return w.toString().toInt();
    }
    return w;
}

QJsonValue resolveOutputToApi(const QHash<int, QJsonObject> &nodesById,
                              const QHash<int, LinkEntry> &linksById,
                              int fromNode,
                              int fromSlot,
                              int depth)
{
    if (depth > 64)
        return QJsonValue();
    const QJsonObject srcNode = nodesById.value(fromNode);
    if (srcNode.isEmpty())
        return QJsonValue();
    const QString stype = srcNode.value(QStringLiteral("type")).toString();
    if (stype == QStringLiteral("Reroute")) {
        for (auto it = linksById.constBegin(); it != linksById.constEnd(); ++it) {
            const LinkEntry &e = it.value();
            if (e.toNode != fromNode)
                continue;
            const QJsonValue r = resolveOutputToApi(nodesById, linksById, e.fromNode, e.fromSlot, depth + 1);
            if (!r.isNull() && !r.isUndefined())
                return r;
        }
        return QJsonValue();
    }
    if (stype == QStringLiteral("PrimitiveNode") || stype.startsWith(QStringLiteral("Primitive"))) {
        const QJsonArray wv = srcNode.value(QStringLiteral("widgets_values")).toArray();
        if (!wv.isEmpty())
            return wv.at(0);
        return QJsonValue();
    }
    QJsonArray ref;
    ref.append(QString::number(fromNode));
    ref.append(fromSlot);
    return ref;
}

bool parseLinksArray(const QJsonArray &linksArr, QHash<int, LinkEntry> *out)
{
    for (const QJsonValue &lv : linksArr) {
        if (lv.isArray()) {
            const QJsonArray a = lv.toArray();
            if (a.size() >= 6) {
                LinkEntry e;
                e.id = jsonIntFlexible(a.at(0));
                e.fromNode = jsonIntFlexible(a.at(1));
                e.fromSlot = jsonIntFlexible(a.at(2));
                e.toNode = jsonIntFlexible(a.at(3));
                e.toSlot = jsonIntFlexible(a.at(4));
                if (e.id >= 0)
                    out->insert(e.id, e);
            } else if (a.size() >= 3) {
                // §13.101: [link_id, source_node_id, source_output_slot] — target inferred from node inputs
                LinkEntry e;
                e.id = jsonIntFlexible(a.at(0));
                e.fromNode = jsonIntFlexible(a.at(1));
                e.fromSlot = jsonIntFlexible(a.at(2));
                e.toNode = -1;
                e.toSlot = -1;
                if (e.id >= 0)
                    out->insert(e.id, e);
            }
        } else if (lv.isObject()) {
            const QJsonObject o = lv.toObject();
            LinkEntry e;
            e.id = jsonIntFlexible(o.value(QStringLiteral("id")));
            e.fromNode = jsonIntFlexible(o.value(QStringLiteral("origin_id")));
            e.fromSlot = jsonIntFlexible(o.value(QStringLiteral("origin_slot")));
            e.toNode = jsonIntFlexible(o.value(QStringLiteral("target_id")));
            e.toSlot = jsonIntFlexible(o.value(QStringLiteral("target_slot")));
            if (e.id >= 0)
                out->insert(e.id, e);
        }
    }
    return true;
}

void enrichLinkTargetsFromNodeInputs(const QHash<int, QJsonObject> &nodesById, QHash<int, LinkEntry> *linksById)
{
    for (auto nit = nodesById.constBegin(); nit != nodesById.constEnd(); ++nit) {
        const int nodeId = nit.key();
        const QJsonArray uiInputs = nit.value().value(QStringLiteral("inputs")).toArray();
        for (int i = 0; i < uiInputs.size(); ++i) {
            const QJsonObject inObj = uiInputs.at(i).toObject();
            if (!uiInputHasLink(inObj))
                continue;
            const int linkId = jsonIntFlexible(inObj.value(QStringLiteral("link")));
            if (!linksById->contains(linkId))
                continue;
            LinkEntry e = linksById->value(linkId);
            if (e.toNode >= 0)
                continue;
            e.toNode = nodeId;
            e.toSlot = i;
            linksById->insert(linkId, e);
        }
    }
}

bool shouldSkipUiNodeForApi(const QString &type)
{
    return type == QStringLiteral("Note") || type == QStringLiteral("MarkdownNote") || type == QStringLiteral("Reroute")
        || type == QStringLiteral("PrimitiveNode") || type.startsWith(QStringLiteral("Primitive"));
}

} // namespace

QPair<bool, QString> convertComfyUiWorkflowUiToApi(const QJsonObject &uiWorkflow,
                                                 const QJsonObject &objectInfoRoot,
                                                 QJsonObject *outApi)
{
    if (!outApi)
        return qMakePair(false, QString());
    *outApi = QJsonObject();
    if (objectInfoRoot.isEmpty())
        return qMakePair(false,
                         ComfyTr::tr("UI workflow conversion needs ComfyUI node definitions (connect and refresh object_info)."));

    const QJsonArray nodesArr = uiWorkflow.value(QStringLiteral("nodes")).toArray();
    const QJsonArray linksArr = uiWorkflow.value(QStringLiteral("links")).toArray();
    if (nodesArr.isEmpty())
        return qMakePair(false, ComfyTr::tr("UI workflow has no nodes."));

    QHash<int, QJsonObject> nodesById;
    for (const QJsonValue &nv : nodesArr) {
        if (!nv.isObject())
            continue;
        const QJsonObject n = nv.toObject();
        const int id = jsonIntFlexible(n.value(QStringLiteral("id")));
        if (id >= 0)
            nodesById.insert(id, n);
    }

    QHash<int, LinkEntry> linksById;
    parseLinksArray(linksArr, &linksById);
    enrichLinkTargetsFromNodeInputs(nodesById, &linksById);

    for (const QJsonValue &nv : nodesArr) {
        if (!nv.isObject())
            continue;
        const QJsonObject uiNode = nv.toObject();
        const int nodeId = jsonIntFlexible(uiNode.value(QStringLiteral("id")));
        const QString classType = uiNode.value(QStringLiteral("type")).toString();
        if (nodeId < 0 || classType.isEmpty() || shouldSkipUiNodeForApi(classType))
            continue;

        const QJsonObject nodeDef = objectInfoRoot.value(classType).toObject();
        const QJsonObject inputWrapper = nodeDef.value(QStringLiteral("input")).toObject();
        const QJsonObject required = inputWrapper.value(QStringLiteral("required")).toObject();
        const QJsonObject optional = inputWrapper.value(QStringLiteral("optional")).toObject();
        if (required.isEmpty() && optional.isEmpty())
            return qMakePair(false,
                             ComfyTr::tr("Node type \"%1\" is not in object_info — connect to the matching ComfyUI server.",
                                  classType));

        const QJsonArray uiInputs = uiNode.value(QStringLiteral("inputs")).toArray();
        const QJsonArray widgetsValues = uiNode.value(QStringLiteral("widgets_values")).toArray();
        int widgetIdx = 0;
        QJsonObject inputs;

        for (int i = 0; i < uiInputs.size(); ++i) {
            const QJsonObject inObj = uiInputs.at(i).toObject();
            const QString name = inObj.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            QJsonValue specVal;
            if (required.contains(name))
                specVal = required.value(name);
            else if (optional.contains(name))
                specVal = optional.value(name);
            else
                continue;

            if (uiInputHasLink(inObj)) {
                const int linkId = jsonIntFlexible(inObj.value(QStringLiteral("link")));
                if (!linksById.contains(linkId))
                    return qMakePair(false,
                                     ComfyTr::tr("Unknown link id %1 on node %2, input \"%3\".", linkId, nodeId, name));
                const LinkEntry &le = linksById.value(linkId);
                if (le.toNode >= 0 && le.toNode != nodeId)
                    return qMakePair(false,
                                     ComfyTr::tr("Link %1 does not target node %2 (input \"%3\").", linkId, nodeId, name));
                const QJsonValue resolved =
                    resolveOutputToApi(nodesById, linksById, le.fromNode, le.fromSlot, 0);
                if (resolved.isNull() || resolved.isUndefined())
                    return qMakePair(false,
                                     ComfyTr::tr("Could not resolve link %1 (node %2, input \"%3\").", linkId, nodeId, name));
                inputs.insert(name, resolved);
            } else {
                if (isConnectionInputSpec(specVal)) {
                    // Unconnected optional socket — omit
                    continue;
                }
                if (widgetIdx >= widgetsValues.size())
                    return qMakePair(false,
                                     ComfyTr::tr("Not enough widget values for node %1 (input \"%2\").", nodeId, name));
                const QJsonValue w = widgetsValues.at(widgetIdx++);
                inputs.insert(name, coerceWidgetForSpec(w, specVal));
            }
        }

        // ComfyUI often saves nodes with no `inputs` array — only widgets_values in object_info order
        if (uiInputs.isEmpty() && !widgetsValues.isEmpty()) {
            int wix = 0;
            const auto appendWidgetInputs = [&](const QJsonObject &section) {
                for (auto it = section.begin(); it != section.end(); ++it) {
                    if (wix >= widgetsValues.size())
                        return;
                    const QString key = it.key();
                    if (inputs.contains(key))
                        continue;
                    if (isConnectionInputSpec(it.value()))
                        continue;
                    inputs.insert(key, coerceWidgetForSpec(widgetsValues.at(wix++), it.value()));
                }
            };
            appendWidgetInputs(required);
            appendWidgetInputs(optional);
        }

        QJsonObject apiNode;
        apiNode.insert(QStringLiteral("class_type"), classType);
        apiNode.insert(QStringLiteral("inputs"), inputs);
        outApi->insert(QString::number(nodeId), apiNode);
    }

    if (outApi->isEmpty())
        return qMakePair(false, ComfyTr::tr("No exportable nodes found in UI workflow (after filtering notes/primitives)."));
    return qMakePair(true, QString());
}

bool tryResolveCustomWorkflowJsonToApi(QJsonObject *inOut, const QJsonObject &objectInfoRoot, QString *errorOut)
{
    if (!inOut)
        return false;
    if (!inOut->contains(QStringLiteral("nodes")) || !inOut->contains(QStringLiteral("links")))
        return true;
    const QJsonValue nodesV = inOut->value(QStringLiteral("nodes"));
    const QJsonValue linksV = inOut->value(QStringLiteral("links"));
    if (!nodesV.isArray() || !linksV.isArray())
        return true;
    QJsonObject api;
    const auto r = convertComfyUiWorkflowUiToApi(*inOut, objectInfoRoot, &api);
    if (!r.first) {
        if (errorOut)
            *errorOut = r.second;
        return false;
    }
    *inOut = api;
    return true;
}

} // namespace ComfyUIUtils
