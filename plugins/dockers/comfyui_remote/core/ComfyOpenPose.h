/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_OPEN_POSE_H_
#define COMFY_OPEN_POSE_H_

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QPointF>
#include <QSize>
#include <QString>

/// Port of ai_diffusion/pose.py — OpenPose JSON → editable stick-figure SVG for vector layers.
namespace ComfyOpenPose {

struct JointIndex {
    int person = 0;
    int joint = 0;
    QString id() const;
    bool operator<(const JointIndex &o) const
    {
        return person < o.person || (person == o.person && joint < o.joint);
    }
};

struct BoneIndex {
    int person = 0;
    int bone = 0;
    QString id() const;
};

constexpr int jointCount = 18;

class Pose
{
public:
    QSize extent;
    int peopleCount = 0;
    QMap<JointIndex, QPointF> joints;

    Pose() = default;
    Pose(const QSize &extent, int peopleCount, const QMap<JointIndex, QPointF> &joints);

    static Pose fromOpenPoseJson(const QJsonValue &value);
    void scaleToExtent(const QSize &target);
    QString toSvg() const;

private:
    double strokeWidth = 4.0;
    double radius = 4.0;
    void updateMetrics();
    QString drawBone(const BoneIndex &index, const QPointF &a, const QPointF &b) const;
    QString drawJoint(const JointIndex &index, const QPointF &pos) const;
};

/// ComfyUI history / WebSocket executed output — DWPreprocessor openpose_json field.
QJsonValue openPoseJsonFromComfyOutputs(const QJsonObject &nodeOutputs);

/// Scan all nodes in GET /history outputs for the first openpose_json payload.
QJsonValue openPoseJsonFromHistoryOutputs(const QJsonObject &historyOutputs);

} // namespace ComfyOpenPose

#endif
