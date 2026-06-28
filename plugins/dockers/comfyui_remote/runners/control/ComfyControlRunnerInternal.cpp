/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyControlRunnerInternal.h"
#include "ComfyUIUtils.h"
#include "ComfyUIPoseLayers.h"

#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_node.h>
#include <kis_selection.h>
#include <kis_shape_layer.h>

namespace ComfyControlRunnerInternal {

KisNodeSP findNodeByName(KisNodeSP root, const QString &name)
{
    if (!root || name.isEmpty())
        return KisNodeSP();
    if (root->name() == name)
        return root;
    for (int i = 0; i < static_cast<int>(root->childCount()); ++i) {
        KisNodeSP found = findNodeByName(root->at(i), name);
        if (found)
            return found;
    }
    return KisNodeSP();
}

QImage canvasImageForControlLayerJob(KisImageSP image, KisViewManager *viewManager, const QString &mode)
{
    if (!image)
        return QImage();
    const auto docCapture = ComfyUIUtils::getDocumentImage(image, QRect(), {});
    if (!docCapture)
        return QImage();
    QImage canvasImg = docCapture.image;
    if (mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0 && viewManager) {
        if (KisLayerSP al = viewManager->activeLayer()) {
            if (auto *sl = qobject_cast<KisShapeLayer *>(al.data())) {
                const QSize docSz = image->bounds().size();
                QImage poseImg = ComfyUIPoseLayers::instance().rasterizedPoseImageForLayer(sl->uuid(), docSz);
                if (!poseImg.isNull()) {
                    if (poseImg.size() != canvasImg.size()) {
                        poseImg = poseImg.scaled(canvasImg.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    }
                    canvasImg = poseImg;
                }
            }
        }
    }
    return canvasImg;
}

QRect selectionCropLocalRect(KisImageSP image, KisViewManager *viewManager, const QSize &fullExportSize, QImage *canvasImg)
{
    QRect localCropRect(0, 0, fullExportSize.width(), fullExportSize.height());
    if (!image || !canvasImg || canvasImg->isNull())
        return localCropRect;
    const QRect docBounds = image->bounds();
    if (!viewManager)
        return localCropRect;
    if (KisSelectionSP sel = viewManager->selection()) {
        if (auto ps = sel->pixelSelection()) {
            QRect r = ps->selectedExactRect();
            r &= docBounds;
            if (!r.isEmpty() && r.size() != docBounds.size()) {
                const QRect local = r.translated(-docBounds.topLeft());
                if (local.left() >= 0 && local.top() >= 0 && local.right() < canvasImg->width()
                    && local.bottom() < canvasImg->height()) {
                    localCropRect = local;
                    *canvasImg = canvasImg->copy(local);
                }
            }
        }
    }
    return localCropRect;
}

} // namespace ComfyControlRunnerInternal
