/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_WORKFLOW_NORMALIZE_H_
#define COMFY_WORKFLOW_NORMALIZE_H_

#include <QJsonObject>
#include <QString>

namespace ComfyWorkflowNormalize {

/// Canonical API workflow for golden comparison (renumber nodes 1..N, sorted by class_type + inputs).
QJsonObject normalizeApiWorkflow(const QJsonObject &workflow);

/// Stable compact JSON string for equality checks.
QString canonicalJson(const QJsonObject &obj);

} // namespace ComfyWorkflowNormalize

#endif
