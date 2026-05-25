/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyWorkflowNormalize.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <algorithm>

namespace ComfyWorkflowNormalize {

namespace {

struct NodeEntry {
    QString id;
    QString classType;
    QJsonObject inputs;
    QByteArray sortKey;
};

QJsonValue remapValue(const QJsonValue &v, const QMap<QString, QString> &idMap)
{
    if (v.isArray()) {
        const QJsonArray arr = v.toArray();
        if (arr.size() >= 2 && arr.at(0).isString()) {
            const QString oldId = arr.at(0).toString();
            const QString newId = idMap.value(oldId, oldId);
            return QJsonArray{newId, arr.at(1)};
        }
        QJsonArray out;
        for (const QJsonValue &item : arr)
            out.append(remapValue(item, idMap));
        return out;
    }
    if (v.isObject()) {
        QJsonObject out;
        const QJsonObject obj = v.toObject();
        for (auto it = obj.begin(); it != obj.end(); ++it)
            out.insert(it.key(), remapValue(it.value(), idMap));
        return out;
    }
    return v;
}

} // namespace

QJsonObject normalizeApiWorkflow(const QJsonObject &workflow)
{
    QList<NodeEntry> nodes;
    nodes.reserve(workflow.size());
    for (auto it = workflow.begin(); it != workflow.end(); ++it) {
        const QJsonObject node = it.value().toObject();
        NodeEntry e;
        e.id = it.key();
        e.classType = node.value(QStringLiteral("class_type")).toString();
        e.inputs = node.value(QStringLiteral("inputs")).toObject();
        QJsonObject sortObj;
        sortObj.insert(QStringLiteral("class_type"), e.classType);
        sortObj.insert(QStringLiteral("inputs"), e.inputs);
        e.sortKey = QJsonDocument(sortObj).toJson(QJsonDocument::Compact);
        nodes.append(e);
    }
    std::sort(nodes.begin(), nodes.end(), [](const NodeEntry &a, const NodeEntry &b) {
        if (a.classType != b.classType)
            return a.classType < b.classType;
        return a.sortKey < b.sortKey;
    });

    QMap<QString, QString> idMap;
    for (int i = 0; i < nodes.size(); ++i)
        idMap.insert(nodes.at(i).id, QString::number(i + 1));

    QJsonObject out;
    for (int i = 0; i < nodes.size(); ++i) {
        const QString newId = QString::number(i + 1);
        const QJsonObject inputs = remapValue(nodes.at(i).inputs, idMap).toObject();
        out.insert(newId,
                   QJsonObject{{QStringLiteral("class_type"), nodes.at(i).classType},
                               {QStringLiteral("inputs"), inputs}});
    }
    return out;
}

QString canonicalJson(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace ComfyWorkflowNormalize
