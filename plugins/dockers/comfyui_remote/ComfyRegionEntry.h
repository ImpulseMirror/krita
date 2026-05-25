/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_REGION_ENTRY_H_
#define COMFY_REGION_ENTRY_H_

#include "ComfyControlLayer.h"

#include <QList>
#include <QString>

/// Region row persisted in document UI state (Python Region / dock regions list).
/// Extracted out of ComfyUIRemoteDock::Private so it can be used from other
/// translation units (notably ComfyRegionProcess) without pulling the full
/// dock private header (circular include).
struct ComfyRegionEntry {
    QString name;
    QString prompt;
    QString maskSource; // "selection" or "layer:LayerName"
    QString layerIds;   // comma-separated Krita layer UUIDs (Python layer_ids)
    QList<ComfyControlLayerEntry> controlLayers;
};

#endif
