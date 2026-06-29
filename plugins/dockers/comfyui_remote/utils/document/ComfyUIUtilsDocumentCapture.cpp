/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyControlLayer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QImage>
#include <QImageWriter>
#include <QBuffer>
#include <QHash>
#include <QSet>
#include <QPainter>
#include <QUuid>

#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_annotation.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>

#include <KoColorConversionTransformation.h>
#include <KoColorProfile.h>


namespace ComfyUIUtils {

QImage cropImageToDocumentRect(const QImage &image, const QRect &cropInDocCoords, const QRect &docBounds)
{
    if (image.isNull() || cropInDocCoords.isEmpty())
        return QImage();
    QRect local = cropInDocCoords.translated(-docBounds.topLeft());
    local &= QRect(0, 0, image.width(), image.height());
    if (local.isEmpty())
        return QImage();
    return image.copy(local);
}

void blitImageInto(QImage &dest, const QImage &src, QPoint topLeft)
{
    if (dest.isNull() || src.isNull())
        return;
    if (dest.format() != QImage::Format_ARGB32)
        dest = dest.convertToFormat(QImage::Format_ARGB32);
    const QImage s = src.format() == QImage::Format_ARGB32 ? src : src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < s.height(); ++y) {
        const int dy = topLeft.y() + y;
        if (dy < 0 || dy >= dest.height())
            continue;
        for (int x = 0; x < s.width(); ++x) {
            const int dx = topLeft.x() + x;
            if (dx < 0 || dx >= dest.width())
                continue;
            dest.setPixel(dx, dy, s.pixel(x, y));
        }
    }
}

// §13.102: Force bounds to square (max of w,h), centered and clamped to image

QImage compositeJobResultOnDocument(KisImageSP image,
                                    const QList<KisNodeSP> &excludeNodes,
                                    const QImage &result,
                                    const QRect &placementBounds,
                                    bool hasMask)
{
    if (result.isNull())
        return result;
    if (!image)
        return result;
    const QRect docBounds = image->bounds();
    const DocumentImageResult capture = getDocumentImage(image, docBounds, excludeNodes);
    if (!capture)
        return result;
    QImage canvas = capture.image.convertToFormat(QImage::Format_ARGB32);
    if (canvas.isNull())
        return result;

    QRect place = placementBounds;
    if (!hasMask || place.isEmpty())
        place = QRect(QPoint(0, 0), result.size());
    place = place.intersected(docBounds);
    if (place.isEmpty())
        return canvas;

    QRect local = place.translated(-docBounds.topLeft());
    local = local.intersected(QRect(QPoint(0, 0), canvas.size()));
    if (local.isEmpty())
        return canvas;

    QImage patch = result;
    if (patch.size() != local.size())
        patch = patch.scaled(local.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    blitImageInto(canvas, patch, local.topLeft());
    return canvas;
}

QImage getCanvasAsQImage(KisImageSP image)
{
    return getDocumentImage(image, QRect(), {}).image;
}

static KisNodeSP findNodeByName(KisNodeSP root, const QString &layerName);

static KisNodeSP findNodeByUuidString(KisNodeSP root, const QString &layerId)
{
    if (!root || layerId.trimmed().isEmpty())
        return KisNodeSP();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QUuid uid = QUuid::fromString(QStringView{layerId.trimmed()});
#else
    QString s = layerId.trimmed();
    if (s.length() == 32 && !s.contains(QLatin1Char('-'))) {
        s = QStringLiteral("{%1-%2-%3-%4-%5}")
                .arg(s.mid(0, 8), s.mid(8, 4), s.mid(12, 4), s.mid(16, 4), s.mid(20, 12));
    }
    const QUuid uid(s);
#endif
    if (uid.isNull())
        return KisNodeSP();
    return KisLayerUtils::findNodeByUuid(root, uid);
}

static KisNodeSP findControlLayerNode(KisImageSP image, const ComfyControlLayerEntry &entry)
{
    if (!image)
        return KisNodeSP();
    KisNodeSP root = image->rootLayer();
    if (!root)
        return KisNodeSP();
    if (!entry.layerId.trimmed().isEmpty()) {
        if (KisNodeSP byId = findNodeByUuidString(root, entry.layerId))
            return byId;
    }
    return findNodeByName(root, entry.layerName);
}

static bool hasVisiblePaintLayer(KisImageSP image, const QSet<KisNodeSP> &excludeSet)
{
    if (!image || !image->rootLayer())
        return false;
    QList<KisNodeSP> stack;
    stack.append(image->rootLayer());
    while (!stack.isEmpty()) {
        KisNodeSP n = stack.takeFirst();
        for (int i = 0; i < static_cast<int>(n->childCount()); ++i)
            stack.append(n->at(i));
        if (!n->visible())
            continue;
        if (excludeSet.contains(n))
            continue;
        if (dynamic_cast<KisPaintLayer *>(n.data()))
            return true;
    }
    return false;
}

DocumentImageResult getDocumentImage(KisImageSP image, const QRect &boundsIn, const QList<KisNodeSP> &excludeNodes)
{
    DocumentImageResult out;
    if (!image || !image->projection()) {
        out.errorMessage = ComfyTr::tr("Could not export canvas.");
        return out;
    }
    QRect bounds = boundsIn.isValid() ? boundsIn.intersected(image->bounds()) : image->bounds();
    if (bounds.isEmpty()) {
        out.errorMessage = ComfyTr::tr("Could not export canvas.");
        return out;
    }

    QSet<KisNodeSP> excludeSet;
    QList<KisNodeSP> hidden;
    for (KisNodeSP node : excludeNodes) {
        if (!node || excludeSet.contains(node))
            continue;
        excludeSet.insert(node);
        if (node->visible()) {
            node->setVisible(false);
            hidden.append(node);
        }
    }

    if (!hidden.isEmpty()) {
        image->refreshGraphAsync();
        image->waitForDone();
    }

    if (!excludeSet.isEmpty() && !hasVisiblePaintLayer(image, excludeSet)) {
        for (KisNodeSP node : hidden)
            node->setVisible(true);
        if (!hidden.isEmpty()) {
            image->refreshGraphAsync();
            image->waitForDone();
        }
        out.errorMessage = ComfyTr::tr(
            "Tried to capture the current image, but there are no visible layers! Preview and control layers are not "
            "considered to be part of the input image.");
        return out;
    }

    const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    out.image = image->projection()->convertToQImage(profile, bounds,
                                                     KoColorConversionTransformation::internalRenderingIntent(),
                                                     KoColorConversionTransformation::internalConversionFlags());

    for (KisNodeSP node : hidden)
        node->setVisible(true);
    if (!hidden.isEmpty()) {
        image->refreshGraphAsync();
        image->waitForDone();
    }

    if (out.image.isNull())
        out.errorMessage = ComfyTr::tr("Could not export canvas.");
    return out;
}

namespace {

QImage compositeLiveResultPreviewOntoContext(QImage canvas,
                                            const QRect &contextBoundsInDoc,
                                            const QRect &resultPlacementInDoc,
                                            const QImage &result,
                                            bool drawGeneratingOverlay)
{
    if (canvas.isNull())
        return result;

    QRect local = resultPlacementInDoc.translated(-contextBoundsInDoc.topLeft());
    local = local.intersected(QRect(QPoint(0, 0), canvas.size()));
    if (local.isEmpty() && !result.isNull())
        local = QRect(QPoint(0, 0), canvas.size().boundedTo(result.size()));

    QPainter painter(&canvas);
    if (drawGeneratingOverlay) {
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        painter.setBrush(QBrush(QColor(0, 0, 96, 192), Qt::DiagCrossPattern));
        painter.setPen(Qt::NoPen);
        painter.drawRect(canvas.rect());
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    if (!result.isNull()) {
        if (result.size() == local.size())
            painter.drawImage(local.topLeft(), result);
        else
            painter.drawImage(local, result.scaled(local.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    painter.end();
    return canvas;
}

} // namespace

QImage compositeLiveResultPreviewFromContext(const QImage &contextCapture,
                                             const QRect &contextBoundsInDoc,
                                             const QRect &resultPlacementInDoc,
                                             const QImage &result,
                                             bool drawGeneratingOverlay)
{
    if (contextCapture.isNull())
        return result;
    QImage canvas = contextCapture.convertToFormat(QImage::Format_ARGB32);
    if (canvas.isNull())
        return result;
    return compositeLiveResultPreviewOntoContext(canvas,
                                                 contextBoundsInDoc,
                                                 resultPlacementInDoc,
                                                 result,
                                                 drawGeneratingOverlay);
}

QImage compositeLiveResultPreview(KisImageSP image,
                                  const QRect &contextBoundsInDoc,
                                  const QRect &resultPlacementInDoc,
                                  const QImage &result,
                                  bool drawGeneratingOverlay,
                                  const QList<KisNodeSP> &excludeNodes)
{
    if (!image)
        return result;
    const QRect doc = image->bounds();
    QRect contextBounds = contextBoundsInDoc.isValid() ? contextBoundsInDoc.intersected(doc) : doc;
    if (contextBounds.isEmpty())
        contextBounds = doc;
    const DocumentImageResult capture = getDocumentImage(image, contextBounds, excludeNodes);
    if (!capture)
        return result;
    QImage canvas = capture.image.convertToFormat(QImage::Format_ARGB32);
    if (canvas.isNull())
        return result;
    return compositeLiveResultPreviewOntoContext(canvas,
                                                 contextBounds,
                                                 resultPlacementInDoc,
                                                 result,
                                                 drawGeneratingOverlay);
}

QList<KisNodeSP> collectInpaintExcludeNodes(KisImageSP image,
                                          bool excludeInternal,
                                          const QList<ComfyControlLayerEntry> &rootControlLayers,
                                          const QString &previewLayerId)
{
    QList<KisNodeSP> out;
    if (!excludeInternal || !image)
        return out;

    QSet<KisNodeSP> seen;
    for (const ComfyControlLayerEntry &entry : rootControlLayers) {
        if (ComfyResources::ControlMode::isPartOfImage(entry.mode))
            continue;
        KisNodeSP node = findControlLayerNode(image, entry);
        if (!node || seen.contains(node))
            continue;
        seen.insert(node);
        out.append(node);
    }

    KisNodeSP root = image->rootLayer();
    if (root && !previewLayerId.trimmed().isEmpty()) {
        if (KisNodeSP preview = findNodeByUuidString(root, previewLayerId)) {
            if (!seen.contains(preview)) {
                seen.insert(preview);
                out.append(preview);
            }
        }
    }
    return out;
}

static KisNodeSP findNodeByName(KisNodeSP root, const QString &layerName)
{
    if (!root || layerName.isEmpty())
        return KisNodeSP();
    QList<KisNodeSP> nodes;
    nodes.append(root);
    while (!nodes.isEmpty()) {
        KisNodeSP n = nodes.takeFirst();
        if (n->name() == layerName)
            return n;
        for (int i = 0; i < static_cast<int>(n->childCount()); i++)
            nodes.append(n->at(i));
    }
    return KisNodeSP();
}

static QImage layerNodeProjectionAsQImage(KisImageSP image, KisNodeSP found)
{
    if (!image || !found)
        return QImage();
    QRect bounds = image->bounds();
    if (bounds.isEmpty())
        return QImage();
    const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    if (auto *layer = dynamic_cast<KisLayer *>(found.data())) {
        if (!layer->projection())
            return QImage();
        return layer->projection()->convertToQImage(profile, bounds,
                                                   KoColorConversionTransformation::internalRenderingIntent(),
                                                   KoColorConversionTransformation::internalConversionFlags());
    }
    if (auto *mask = dynamic_cast<KisMask *>(found.data())) {
        KisPaintDeviceSP dev = mask->projection();
        if (!dev)
            return QImage();
        QRect rect = dev->exactBounds() & bounds;
        if (rect.isEmpty())
            return QImage();
        return dev->convertToQImage(profile, rect.x(), rect.y(), rect.width(), rect.height(),
                                  KoColorConversionTransformation::internalRenderingIntent(),
                                  KoColorConversionTransformation::internalConversionFlags());
    }
    return QImage();
}

QImage getLayerProjectionAsQImage(KisImageSP image, const QString &layerName)
{
    if (!image || layerName.isEmpty())
        return QImage();
    KisNodeSP root = image->rootLayer();
    if (!root)
        return QImage();
    KisNodeSP found = findNodeByName(root, layerName);
    if (!found)
        return QImage();
    return layerNodeProjectionAsQImage(image, found);
}

QImage getLayerProjectionByUuid(KisImageSP image, const QString &layerId)
{
    if (!image || layerId.trimmed().isEmpty())
        return QImage();
    KisNodeSP root = image->rootLayer();
    if (!root)
        return QImage();
    KisNodeSP found = findNodeByUuidString(root, layerId);
    if (!found)
        return QImage();
    return layerNodeProjectionAsQImage(image, found);
}

} // namespace ComfyUIUtils
