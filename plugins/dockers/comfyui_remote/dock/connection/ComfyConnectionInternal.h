/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUIRemoteDockPrivate.h"

#include <QString>
#include <QStringList>

namespace ComfyConnectionInternal {

const QStringList &requiredObjectInfoNodes();
QString buildMissingNodesListFormat(const QStringList &missingNodes);
QString buildMissingResourcesDictFormatHtml(const QStringList &checkpointNames);
void syncDetectedModelsLabel(ComfyUIRemoteDock::Private *d);

} // namespace ComfyConnectionInternal
