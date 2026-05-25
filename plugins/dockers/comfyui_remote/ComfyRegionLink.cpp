/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyRegionLink.h"
#include "ComfyLocalization.h"

#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_layer_utils.h>
#include <kis_node.h>

#include <klocalizedstring.h>

namespace ComfyRegionLink {

QStringList parseLayerIds(const QString &csv)
{
    QStringList out;
    for (const QString &part : csv.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (!t.isEmpty())
            out.append(t);
    }
    return out;
}

QString joinLayerIds(const QStringList &ids)
{
    QStringList cleaned;
    for (const QString &id : ids) {
        const QString t = id.trimmed();
        if (!t.isEmpty())
            cleaned.append(t);
    }
    return cleaned.join(QLatin1Char(','));
}

bool containsLayerId(const QString &csv, const QUuid &id)
{
    const QString needle = id.toString(QUuid::WithoutBraces);
    for (const QString &part : parseLayerIds(csv)) {
        if (part.compare(needle, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString toggleLayerId(const QString &csv, const QUuid &id)
{
    QStringList ids = parseLayerIds(csv);
    const QString needle = id.toString(QUuid::WithoutBraces);
    bool found = false;
    for (int i = ids.size() - 1; i >= 0; --i) {
        if (ids.at(i).compare(needle, Qt::CaseInsensitive) == 0) {
            ids.removeAt(i);
            found = true;
        }
    }
    if (!found)
        ids.append(needle);
    return joinLayerIds(ids);
}

QString maskSourceForLayer(KisLayerSP layer)
{
    if (!layer || layer->name().isEmpty())
        return QStringLiteral("selection");
    return QStringLiteral("layer:") + layer->name();
}

KisLayerSP findLayerByUuid(KisImageSP image, const QUuid &id)
{
    if (!image || !image->rootLayer() || id.isNull())
        return KisLayerSP();
    KisNodeSP node = KisLayerUtils::findNodeByUuid(image->rootLayer(), id);
    return dynamic_cast<KisLayer *>(node.data());
}

KisLayerSP linkTarget(KisLayerSP layer)
{
    if (!layer)
        return KisLayerSP();
    if (dynamic_cast<KisGroupLayer *>(layer.data()))
        return layer;
    KisNodeSP parent = layer->parent();
    if (parent && parent->parent() && dynamic_cast<KisGroupLayer *>(parent.data()))
        return dynamic_cast<KisLayer *>(parent.data());
    return layer;
}

bool isDescendantOf(KisNodeSP node, KisNodeSP ancestor)
{
    if (!node || !ancestor)
        return false;
    for (KisNodeSP p = node->parent(); p; p = p->parent()) {
        if (p == ancestor)
            return true;
    }
    return false;
}

bool isLayerLinkedToRegion(const ComfyUIRemoteDock::Private::RegionEntry &entry,
                           KisImageSP image,
                           KisLayerSP layer,
                           LinkMode mode)
{
    if (!layer || !image)
        return false;
    const KisLayerSP target = linkTarget(layer);
    for (const QString &idStr : parseLayerIds(entry.layerIds)) {
        const QUuid uid(idStr);
        KisLayerSP linked = findLayerByUuid(image, uid);
        if (!linked)
            continue;
        if (linked->uuid() == layer->uuid() || linked->uuid() == target->uuid())
            return true;
        if (mode == LinkMode::Indirect || mode == LinkMode::Any) {
            if (isDescendantOf(layer, linked))
                return true;
        }
    }
    return false;
}

int findRegionIndexForLayer(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions,
                            KisImageSP image,
                            KisLayerSP layer,
                            LinkMode mode)
{
    if (!layer)
        return -1;
    for (int i = 0; i < regions.size(); ++i) {
        if (isLayerLinkedToRegion(regions.at(i), image, layer, mode))
            return i;
    }
    return -1;
}

bool isLayerLinkedToAnyRegion(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions,
                               KisImageSP image,
                               KisLayerSP layer,
                               int excludeRegionIndex,
                               LinkMode mode)
{
    for (int i = 0; i < regions.size(); ++i) {
        if (i == excludeRegionIndex)
            continue;
        if (isLayerLinkedToRegion(regions.at(i), image, layer, mode))
            return true;
    }
    return false;
}

QString effectiveMaskSource(const ComfyUIRemoteDock::Private::RegionEntry &entry, KisImageSP image)
{
    for (const QString &idStr : parseLayerIds(entry.layerIds)) {
        if (KisLayerSP layer = findLayerByUuid(image, QUuid(idStr)))
            return maskSourceForLayer(layer);
    }
    return entry.maskSource;
}

void syncMaskSourceFromLinks(ComfyUIRemoteDock::Private::RegionEntry *entry, KisImageSP image)
{
    if (!entry)
        return;
    const QString ms = effectiveMaskSource(*entry, image);
    if (ms != QStringLiteral("selection") || entry->maskSource.isEmpty())
        entry->maskSource = ms;
}

QString regionDisplayName(const ComfyUIRemoteDock::Private::RegionEntry &entry, KisImageSP image)
{
    const QStringList ids = parseLayerIds(entry.layerIds);
    if (ids.isEmpty())
        return entry.name;
    QStringList names;
    for (const QString &idStr : ids) {
        if (KisLayerSP layer = findLayerByUuid(image, QUuid(idStr)))
            names.append(layer->name());
    }
    if (!names.isEmpty())
        return names.join(QStringLiteral(", "));
    return entry.name;
}

void linkLayer(ComfyUIRemoteDock::Private::RegionEntry *entry, KisLayerSP layer)
{
    if (!entry || !layer)
        return;
    const KisLayerSP target = linkTarget(layer);
    if (!containsLayerId(entry->layerIds, target->uuid()))
        entry->layerIds = joinLayerIds(parseLayerIds(entry->layerIds)
                                       << target->uuid().toString(QUuid::WithoutBraces));
    entry->maskSource = maskSourceForLayer(target);
}

void unlinkLayer(ComfyUIRemoteDock::Private::RegionEntry *entry, KisLayerSP layer)
{
    if (!entry || !layer)
        return;
    const KisLayerSP target = linkTarget(layer);
    QStringList ids = parseLayerIds(entry->layerIds);
    const QString needle = target->uuid().toString(QUuid::WithoutBraces);
    ids.removeAll(needle);
    entry->layerIds = joinLayerIds(ids);
}

void toggleActiveLayerLink(ComfyUIRemoteDock::Private::RegionEntry *entry, KisLayerSP activeLayer)
{
    if (!entry || !activeLayer)
        return;
    const KisLayerSP target = linkTarget(activeLayer);
    if (containsLayerId(entry->layerIds, target->uuid()))
        unlinkLayer(entry, activeLayer);
    else
        linkLayer(entry, activeLayer);
}

ActiveLayerLinkUi linkUiForRegion(const ComfyUIRemoteDock::Private::RegionEntry *entry,
                                  const QList<ComfyUIRemoteDock::Private::RegionEntry> &allRegions,
                                  int regionIndex,
                                  KisImageSP image,
                                  KisLayerSP activeLayer)
{
    ActiveLayerLinkUi ui;
    if (!entry || !activeLayer) {
        ui.iconStem = QStringLiteral("link-disabled");
        ui.toolTip = ComfyTr::tr("Select a layer in the layer docker first");
        return ui;
    }

    if (isLayerLinkedToRegion(*entry, image, activeLayer, LinkMode::Direct)) {
        ui.isDirectLinked = true;
        ui.canToggleLink = true;
        ui.iconStem = QStringLiteral("link-active");
        ui.toolTip = ComfyTr::tr("Active layer is linked to this region — click to unlink");
        return ui;
    }
    if (isLayerLinkedToRegion(*entry, image, activeLayer, LinkMode::Indirect)) {
        ui.isIndirectLinked = true;
        ui.iconStem = QStringLiteral("link");
        ui.toolTip = ComfyTr::tr("Active layer is linked to this region via a group layer");
        return ui;
    }

    const bool otherDirect =
        isLayerLinkedToAnyRegion(allRegions, image, activeLayer, regionIndex, LinkMode::Direct);
    if (otherDirect) {
        ui.linkedToOtherRegion = true;
        ui.iconStem = QStringLiteral("link-disabled");
        ui.toolTip = ComfyTr::tr("Active layer is already linked to another region");
        return ui;
    }

    const KisLayerSP target = linkTarget(activeLayer);
    if (target && target != activeLayer && dynamic_cast<KisGroupLayer *>(activeLayer.data()) == nullptr) {
        ui.iconStem = QStringLiteral("link-disabled");
        ui.toolTip = ComfyTr::tr("Active layer is part of a group — select the group layer to link it");
        return ui;
    }

    ui.canToggleLink = true;
    ui.iconStem = QStringLiteral("link-off");
    ui.toolTip = ComfyTr::tr("Active layer is not linked — click to link it to this region");
    return ui;
}

} // namespace ComfyRegionLink
