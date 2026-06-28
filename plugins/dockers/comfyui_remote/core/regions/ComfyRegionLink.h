/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_REGION_LINK_H_
#define COMFY_REGION_LINK_H_

#include <QString>
#include <QStringList>
#include <QUuid>

#include <kis_types.h>

#include "ComfyUIRemoteDockPrivate.h"

namespace ComfyRegionLink {

constexpr int kRootRegionIndex = -1;
constexpr int kUnlinkedRegionIndex = -2;

enum class LinkMode {
    Direct,
    Indirect,
    Any,
};

QStringList parseLayerIds(const QString &csv);
QString joinLayerIds(const QStringList &ids);
bool containsLayerId(const QString &csv, const QUuid &id);
QString toggleLayerId(const QString &csv, const QUuid &id);
QString maskSourceForLayer(KisLayerSP layer);

KisLayerSP findLayerByUuid(KisImageSP image, const QUuid &id);
KisLayerSP linkTarget(KisLayerSP layer);
bool isDescendantOf(KisNodeSP node, KisNodeSP ancestor);

bool isLayerLinkedToRegion(const ComfyUIRemoteDock::Private::RegionEntry &entry,
                           KisImageSP image,
                           KisLayerSP layer,
                           LinkMode mode = LinkMode::Any);
int findRegionIndexForLayer(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions,
                            KisImageSP image,
                            KisLayerSP layer,
                            LinkMode mode = LinkMode::Any);
bool isLayerLinkedToAnyRegion(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions,
                               KisImageSP image,
                               KisLayerSP layer,
                               int excludeRegionIndex = -1,
                               LinkMode mode = LinkMode::Direct);

QString effectiveMaskSource(const ComfyUIRemoteDock::Private::RegionEntry &entry, KisImageSP image);
void syncMaskSourceFromLinks(ComfyUIRemoteDock::Private::RegionEntry *entry, KisImageSP image);
QString regionDisplayName(const ComfyUIRemoteDock::Private::RegionEntry &entry, KisImageSP image);

void linkLayer(ComfyUIRemoteDock::Private::RegionEntry *entry, KisLayerSP layer);
void unlinkLayer(ComfyUIRemoteDock::Private::RegionEntry *entry, KisLayerSP layer);
void toggleActiveLayerLink(ComfyUIRemoteDock::Private::RegionEntry *entry, KisLayerSP activeLayer);

struct ActiveLayerLinkUi {
    bool canToggleLink = false;
    bool isDirectLinked = false;
    bool isIndirectLinked = false;
    bool linkedToOtherRegion = false;
    QString toolTip;
    QString iconStem; // theme stem: link, link-active, link-off, link-disabled
};

ActiveLayerLinkUi linkUiForRegion(const ComfyUIRemoteDock::Private::RegionEntry *entry,
                                  const QList<ComfyUIRemoteDock::Private::RegionEntry> &allRegions,
                                  int regionIndex,
                                  KisImageSP image,
                                  KisLayerSP activeLayer);

} // namespace ComfyRegionLink

#endif
