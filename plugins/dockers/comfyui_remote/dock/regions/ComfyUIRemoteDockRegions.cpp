/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyRegionLink.h"
#include "ComfyRegionProcess.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyUIUtils.h"

#include <QUrl>
#include <QRandomGenerator>

#include <klocalizedstring.h>
#include <KisViewManager.h>
#include <kis_types.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_image.h>
#include <KoColorSpaceConstants.h>

void ComfyUIRemoteDock::slotAddRegion()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    QStringList maskSources;
    maskSources << QStringLiteral("selection");
    QString createdLayerName;
    KisLayerSP createdLayer;
    if (m_d->viewManager && m_d->viewManager->image()) {
        KisImageSP image = m_d->viewManager->image();
        KisGroupLayerSP root = image->rootLayer();
        if (root && image->colorSpace()) {
            const int n = regs.size() + 1;
            const QString baseName = ComfyTr::tr("Region %1", n);
            const bool isLive = (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2);
            if (isLive) {
                KisPaintLayerSP paintLayer = new KisPaintLayer(image, baseName, OPACITY_OPAQUE_U8);
                if (image->addNode(paintLayer, root, root->firstChild())) {
                    createdLayerName = baseName;
                    createdLayer = paintLayer;
                }
            } else {
                KisGroupLayerSP group = new KisGroupLayer(image, baseName, OPACITY_OPAQUE_U8, image->colorSpace());
                KisPaintLayerSP paintLayer =
                    new KisPaintLayer(image, image->nextLayerName(ComfyTr::tr("Paint layer")), OPACITY_OPAQUE_U8);
                if (image->addNode(group, root, root->firstChild()) && image->addNode(paintLayer, group, KisNodeSP())) {
                    createdLayerName = baseName;
                    createdLayer = group;
                }
            }
            if (!createdLayerName.isEmpty())
                maskSources << QStringLiteral("layer:") + createdLayerName;
        }
    }

    Private::RegionEntry e;
    e.name = createdLayerName.isEmpty() ? ComfyTr::tr("Region %1", regs.size() + 1) : createdLayerName;
    e.prompt.clear();
    e.maskSource = createdLayerName.isEmpty() ? QStringLiteral("selection")
                                              : QStringLiteral("layer:") + createdLayerName;
    if (createdLayer)
        ComfyRegionLink::linkLayer(&e, createdLayer);
    regs.append(e);
    m_d->activeRegionIndex = regs.size() - 1;
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->labelStatus->setText(ComfyTr::tr("Added region \"%1\".", e.name));
}

void ComfyUIRemoteDock::slotRemoveRegion()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int row = comfyActiveRegionRow(m_d.data());
    if (row < 0 || row >= regs.size())
        return;
    const QString name = regs.at(row).name;
    regs.removeAt(row);
    if (m_d->activeRegionIndex >= regs.size())
        m_d->activeRegionIndex = regs.isEmpty() ? ComfyRegionLink::kRootRegionIndex : qMax(0, regs.size() - 1);
    saveRegionsToConfig();
    refreshRegionsList();
    m_d->labelStatus->setText(ComfyTr::tr("Removed region \"%1\".", name));
}

void ComfyUIRemoteDock::slotMoveRegionUp()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int row = comfyActiveRegionRow(m_d.data());
    if (row <= 0 || row >= regs.size())
        return;
    regs.move(row, row - 1);
    m_d->activeRegionIndex = row - 1;
    saveRegionsToConfig();
    refreshRegionsList();
}

void ComfyUIRemoteDock::slotMoveRegionDown()
{
    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
    const int row = comfyActiveRegionRow(m_d.data());
    if (row < 0 || row >= regs.size() - 1)
        return;
    regs.move(row, row + 1);
    m_d->activeRegionIndex = row + 1;
    saveRegionsToConfig();
    refreshRegionsList();
}

void ComfyUIRemoteDock::slotEditRegion()
{
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->focusPromptEditor();
}

void ComfyUIRemoteDock::slotGenerateRegions()
{
    if (comfyActiveRegionEntries(m_d.data()).isEmpty()) {
        setStatusMessage(ComfyTr::tr("Add at least one region (name, prompt, mask source)."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    // Python model._prepare_workflow always calls process_regions; main Generate is the regional path.
    setStatusMessage(ComfyTr::tr("Generating with regional conditioning…"));
    slotGenerate();
}
