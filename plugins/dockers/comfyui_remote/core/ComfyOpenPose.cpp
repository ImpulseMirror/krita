/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyOpenPose.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtMath>

namespace ComfyOpenPose {

namespace {

const char *const kColors[jointCount] = {
    "ff0000", "ff5500", "ffaa00", "ffff00", "aaff00", "55ff00", "00ff00", "00ff55",
    "00ffaa", "00ffff", "00aaff", "0055ff", "0000ff", "5500ff", "aa00ff", "ff00ff",
    "ff00aa", "ff0055",
};

const int kBones[jointCount - 1][2] = {
    {1, 2},  {1, 5},  {2, 3},  {3, 4},  {5, 6},  {6, 7},  {1, 8},  {8, 9},
    {9, 10}, {1, 11}, {11, 12}, {12, 13}, {1, 0},  {0, 14}, {14, 16}, {0, 15},
    {15, 17},
};

QJsonObject normalizeOpenPoseRoot(const QJsonValue &value)
{
    if (value.isObject())
        return value.toObject();
    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        if (!arr.isEmpty() && arr.at(0).isObject())
            return arr.at(0).toObject();
    }
    return QJsonObject();
}

QMap<JointIndex, QPointF> parseKeypoints(int person, const QJsonArray &keypoints)
{
    QMap<JointIndex, QPointF> out;
    const int n = keypoints.size();
    if (n < jointCount * 3)
        return out;
    for (int joint = 0; joint < jointCount; ++joint) {
        const int base = joint * 3;
        const double x = keypoints.at(base).toDouble();
        const double y = keypoints.at(base + 1).toDouble();
        const double confidence = keypoints.at(base + 2).toDouble();
        if (confidence > 0.1)
            out.insert(JointIndex{person, joint}, QPointF(x, y));
    }
    return out;
}

} // namespace

QString JointIndex::id() const
{
    return QStringLiteral("P%1_J%2").arg(person, 2, 10, QChar('0')).arg(joint, 2, 10, QChar('0'));
}

QString BoneIndex::id() const
{
    return QStringLiteral("P%1_B%2").arg(person, 2, 10, QChar('0')).arg(bone, 2, 10, QChar('0'));
}

Pose::Pose(const QSize &ext, int people, const QMap<JointIndex, QPointF> &j)
    : extent(ext)
    , peopleCount(people)
    , joints(j)
{
    updateMetrics();
}

void Pose::updateMetrics()
{
    const int w = qMax(1, extent.width());
    const int h = qMax(1, extent.height());
    const double diag = std::sqrt(static_cast<double>(w * w + h * h));
    strokeWidth = 4.0 + qMax(0.0, diag / 250.0 - 4.0);
    radius = strokeWidth;
}

Pose Pose::fromOpenPoseJson(const QJsonValue &value)
{
    const QJsonObject root = normalizeOpenPoseRoot(value);
    if (root.isEmpty())
        return Pose();
    const int cw = root.value(QStringLiteral("canvas_width")).toInt(512);
    const int ch = root.value(QStringLiteral("canvas_height")).toInt(512);
    Pose pose(QSize(cw, ch), 0, {});
    const QJsonArray people = root.value(QStringLiteral("people")).toArray();
    QMap<JointIndex, QPointF> all;
    int personIndex = 0;
    for (const QJsonValue &pv : people) {
        const QJsonObject person = pv.toObject();
        const QJsonArray kp = person.value(QStringLiteral("pose_keypoints_2d")).toArray();
        const QMap<JointIndex, QPointF> parsed = parseKeypoints(personIndex, kp);
        for (auto it = parsed.constBegin(); it != parsed.constEnd(); ++it)
            all.insert(it.key(), it.value());
        if (!parsed.isEmpty())
            personIndex++;
    }
    pose.peopleCount = personIndex;
    pose.joints = all;
    pose.updateMetrics();
    return pose;
}

void Pose::scaleToExtent(const QSize &target)
{
    if (!extent.isValid() || extent.width() <= 0 || extent.height() <= 0 || !target.isValid())
        return;
    const double sx = static_cast<double>(target.width()) / extent.width();
    const double sy = static_cast<double>(target.height()) / extent.height();
    for (auto it = joints.begin(); it != joints.end(); ++it) {
        it.value() = QPointF(it.value().x() * sx, it.value().y() * sy);
    }
    extent = target;
    updateMetrics();
}

QString Pose::drawBone(const BoneIndex &index, const QPointF &a, const QPointF &b) const
{
    const int colorIdx = qBound(0, index.bone, jointCount - 1);
    return QStringLiteral(
               "<line id=\"%1\" x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\""
               " stroke=\"#%6\" stroke-width=\"%7\" stroke-opacity=\"0.6\"/>")
        .arg(index.id())
        .arg(a.x())
        .arg(a.y())
        .arg(b.x())
        .arg(b.y())
        .arg(QString::fromLatin1(kColors[colorIdx]))
        .arg(strokeWidth, 0, 'g', 4);
}

QString Pose::drawJoint(const JointIndex &index, const QPointF &pos) const
{
    const int colorIdx = qBound(0, index.joint, jointCount - 1);
    return QStringLiteral("<circle id=\"%1\" cx=\"%2\" cy=\"%3\" r=\"%4\" fill=\"#%5\"/>")
        .arg(index.id())
        .arg(pos.x())
        .arg(pos.y())
        .arg(radius, 0, 'g', 4)
        .arg(QString::fromLatin1(kColors[colorIdx]));
}

QString Pose::toSvg() const
{
    const int w = qMax(1, extent.width());
    const int h = qMax(1, extent.height());
    QString svg = QStringLiteral(
                      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%2\""
                      " viewBox=\"0 0 %1 %2\">")
                      .arg(w)
                      .arg(h);
    for (int person = 0; person < peopleCount; ++person) {
        for (int i = 0; i < jointCount - 1; ++i) {
            const JointIndex ja{person, kBones[i][0]};
            const JointIndex jb{person, kBones[i][1]};
            const auto a = joints.constFind(ja);
            const auto b = joints.constFind(jb);
            if (a != joints.constEnd() && b != joints.constEnd())
                svg += drawBone(BoneIndex{person, i}, a.value(), b.value());
        }
    }
    for (auto it = joints.constBegin(); it != joints.constEnd(); ++it)
        svg += drawJoint(it.key(), it.value());
    svg += QStringLiteral("</svg>");
    return svg;
}

QJsonValue openPoseJsonFromHistoryOutputs(const QJsonObject &historyOutputs)
{
    for (auto it = historyOutputs.constBegin(); it != historyOutputs.constEnd(); ++it) {
        const QJsonValue v = openPoseJsonFromComfyOutputs(it.value().toObject());
        if (!v.isNull() && !(v.isArray() && v.toArray().isEmpty()))
            return v;
    }
    return QJsonValue();
}

QJsonValue openPoseJsonFromComfyOutputs(const QJsonObject &nodeOutputs)
{
    const QJsonArray arr = nodeOutputs.value(QStringLiteral("openpose_json")).toArray();
    if (arr.isEmpty())
        return QJsonValue();
    const QJsonValue first = arr.at(0);
    if (first.isObject() || first.isArray())
        return first;
    if (first.isString()) {
        const QJsonDocument doc = QJsonDocument::fromJson(first.toString().toUtf8());
        if (!doc.isNull())
            return doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array());
    }
    return QJsonValue();
}

} // namespace ComfyOpenPose
