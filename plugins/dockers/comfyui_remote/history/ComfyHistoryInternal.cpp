/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyHistoryInternal.h"
#include "ComfyUiLayoutDiagnostics.h"

#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidgetItem>
#include <QRegularExpression>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QUuid>

#include <kis_image.h>
#include <kis_layer.h>
#include <kis_node.h>
#include <kis_annotation.h>
#include <kis_paint_layer.h>
#include <kis_group_layer.h>
#include <kis_layer_utils.h>
#include <kis_painter.h>
#include <KisImageBarrierLock.h>
#include <KisViewManager.h>
#include <kis_node_manager.h>
#include <KoCompositeOpRegistry.h>
#include <KoColorSpaceConstants.h>
#include <kis_node_manager.h>
#include <KisViewManager.h>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace {

QRect opaqueAlphaBounds(const QImage &img, int alphaMin = 16)
{
    if (img.isNull())
        return QRect();
    const QImage argb = img.format() == QImage::Format_ARGB32
                            ? img
                            : img.convertToFormat(QImage::Format_ARGB32);
    QRect bounds;
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) > alphaMin) {
                if (bounds.isNull())
                    bounds = QRect(x, y, 1, 1);
                else
                    bounds |= QRect(x, y, 1, 1);
            }
        }
    }
    return bounds;
}

QPixmap cropPixmapToOpaqueContent(const QPixmap &pix, int margin = 1)
{
    if (pix.isNull())
        return pix;
    QRect bounds = opaqueAlphaBounds(pix.toImage());
    if (bounds.isNull())
        return pix;
    if (margin > 0)
        bounds = bounds.adjusted(-margin, -margin, margin, margin).intersected(pix.rect());
    return pix.copy(bounds);
}

} // namespace

namespace ComfyHistoryInternal {

QJsonObject historyEntryToJsonObject(const ComfyUIRemoteDock::Private::HistoryEntry &e, const QString &histFmt)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), e.jobId);
    o.insert(QStringLiteral("slot"), e.documentSlot);
    QJsonArray offArr;
    for (int x : e.documentBlobEndOffsets)
        offArr.append(x);
    o.insert(QStringLiteral("offsets"), offArr);
    o.insert(QStringLiteral("history_format"), histFmt);
    QJsonObject params;
    params.insert(QStringLiteral("prompt"), e.prompt);
    params.insert(QStringLiteral("negative"), e.negative);
    params.insert(QStringLiteral("width"), e.width);
    params.insert(QStringLiteral("height"), e.height);
    params.insert(QStringLiteral("steps"), e.steps);
    params.insert(QStringLiteral("cfg"), e.cfg);
    params.insert(QStringLiteral("strength"), e.strength);
    params.insert(QStringLiteral("sampler"), e.samplerName);
    params.insert(QStringLiteral("seed"), static_cast<double>(e.seed));
    params.insert(QStringLiteral("checkpoint"), e.checkpoint);
    params.insert(QStringLiteral("style"), e.styleName);
    if (!e.regionLayerNames.isEmpty()) {
        QJsonArray ra;
        for (const QString &rn : e.regionLayerNames)
            ra.append(rn);
        params.insert(QStringLiteral("region_layer_names"), ra);
    }
    if (e.hasMask)
        params.insert(QStringLiteral("has_mask"), true);
    if (!e.inpaintMode.isEmpty())
        params.insert(QStringLiteral("inpaint_mode"), e.inpaintMode);
    if (!e.contextBounds.isEmpty()) {
        QJsonObject bounds;
        bounds.insert(QStringLiteral("x"), e.contextBounds.x());
        bounds.insert(QStringLiteral("y"), e.contextBounds.y());
        bounds.insert(QStringLiteral("w"), e.contextBounds.width());
        bounds.insert(QStringLiteral("h"), e.contextBounds.height());
        params.insert(QStringLiteral("bounds"), bounds);
    }
    if (!e.targetBounds.isEmpty()) {
        QJsonObject targetBounds;
        targetBounds.insert(QStringLiteral("x"), e.targetBounds.x());
        targetBounds.insert(QStringLiteral("y"), e.targetBounds.y());
        targetBounds.insert(QStringLiteral("w"), e.targetBounds.width());
        targetBounds.insert(QStringLiteral("h"), e.targetBounds.height());
        params.insert(QStringLiteral("target_bounds"), targetBounds);
    }
    if (!e.customWorkflowMetadata.isEmpty())
        params.insert(QStringLiteral("custom_workflow_metadata"), e.customWorkflowMetadata);
    o.insert(QStringLiteral("params"), params);
    o.insert(QStringLiteral("kind"), QStringLiteral("image"));
    QJsonObject inUse;
    for (auto it = e.imageInUse.constBegin(); it != e.imageInUse.constEnd(); ++it) {
        if (it.value())
            inUse.insert(QString::number(it.key()), true);
    }
    o.insert(QStringLiteral("in_use"), inUse);
    return o;
}

void remapImageInUseAfterRemoval(QMap<int, bool> &m, int removedIndex)
{
    QMap<int, bool> n;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        const int k = it.key();
        if (k == removedIndex)
            continue;
        if (k < removedIndex)
            n.insert(k, it.value());
        else
            n.insert(k - 1, it.value());
    }
    m = n;
}

QString safeDocIdFragment(KisImageSP img)
{
    if (!img)
        return QStringLiteral("x");
    KisAnnotationSP idAnn = img->annotation(ComfyUIUtils::documentIdAnnotationKey());
    if (!idAnn || idAnn->annotation().isEmpty())
        return QStringLiteral("x");
    QString s = QString::fromUtf8(idAnn->annotation()).trimmed();
    s.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")), QStringLiteral("_"));
    return s.left(24);
}

bool historyJsonToEntry(const QJsonObject &ho, const QByteArray &blob, KisImageSP img, ComfyUIRemoteDock::Private::HistoryEntry *out)
{
    const int slot = ho.value(QStringLiteral("slot")).toInt(-1);
    if (slot < 0 || !out)
        return false;
    const QJsonArray offArr = ho.value(QStringLiteral("offsets")).toArray();
    QList<int> ends;
    ends.reserve(offArr.size());
    for (const QJsonValue &v : offArr)
        ends.append(v.toInt(0));

    const QString docFrag = safeDocIdFragment(img);
    const QString cacheBase = ComfyUIUtils::historyCacheDir() + QLatin1Char('/') + QStringLiteral("emb_") + docFrag + QLatin1Char('_')
        + QString::number(slot) + QLatin1Char('_');

    QStringList paths;
    int start = 0;
    if (ends.isEmpty()) {
        const QImage im = QImage::fromData(blob);
        if (im.isNull())
            return false;
        const QString p = cacheBase + QStringLiteral("0.png");
        if (QFile::exists(p))
            QFile::remove(p);
        if (!im.save(p))
            return false;
        paths << p;
    } else {
        int idx = 0;
        for (int end : ends) {
            if (end <= start)
                continue;
            const QByteArray part = blob.mid(start, end - start);
            start = end;
            const QImage im = QImage::fromData(part);
            if (im.isNull())
                return false;
            const QString p = cacheBase + QString::number(idx) + QStringLiteral(".png");
            if (QFile::exists(p))
                QFile::remove(p);
            if (!im.save(p))
                return false;
            paths << p;
            idx++;
        }
    }
    if (paths.isEmpty())
        return false;

    const QJsonObject p = ho.value(QStringLiteral("params")).toObject();
    out->jobId = ho.value(QStringLiteral("id")).toString();
    out->documentSlot = slot;
    out->prompt = p.value(QStringLiteral("prompt")).toString();
    out->negative = p.value(QStringLiteral("negative")).toString();
    out->width = p.value(QStringLiteral("width")).toInt(512);
    out->height = p.value(QStringLiteral("height")).toInt(512);
    out->steps = p.value(QStringLiteral("steps")).toInt(20);
    out->cfg = p.value(QStringLiteral("cfg")).toDouble(8.0);
    out->strength = p.value(QStringLiteral("strength")).toInt(100);
    out->samplerName = p.value(QStringLiteral("sampler")).toString();
    out->seed = static_cast<qint64>(p.value(QStringLiteral("seed")).toDouble(0));
    out->checkpoint = p.value(QStringLiteral("checkpoint")).toString();
    out->styleName = p.value(QStringLiteral("style")).toString();
    out->resultImagePaths = paths;
    out->resultImagePath = paths.first();
    out->documentBlobEndOffsets = ends;
    out->imageInUse.clear();
    const QJsonObject iu = ho.value(QStringLiteral("in_use")).toObject();
    for (auto it = iu.constBegin(); it != iu.constEnd(); ++it) {
        bool ok = false;
        const int k = it.key().toInt(&ok);
        if (ok && it.value().toBool(false))
            out->imageInUse.insert(k, true);
    }
    out->regionLayerNames.clear();
    const QJsonArray rla = p.value(QStringLiteral("region_layer_names")).toArray();
    for (const QJsonValue &v : rla)
        out->regionLayerNames.append(v.toString());
    out->hasMask = p.value(QStringLiteral("has_mask")).toBool(false);
    out->inpaintMode = p.value(QStringLiteral("inpaint_mode")).toString();
    const QJsonObject bounds = p.value(QStringLiteral("bounds")).toObject();
    if (!bounds.isEmpty()) {
        out->contextBounds = QRect(bounds.value(QStringLiteral("x")).toInt(),
                                   bounds.value(QStringLiteral("y")).toInt(),
                                   bounds.value(QStringLiteral("w")).toInt(),
                                   bounds.value(QStringLiteral("h")).toInt());
    } else {
        out->contextBounds = QRect();
    }
    const QJsonObject targetBounds = p.value(QStringLiteral("target_bounds")).toObject();
    if (!targetBounds.isEmpty()) {
        out->targetBounds = QRect(targetBounds.value(QStringLiteral("x")).toInt(),
                                  targetBounds.value(QStringLiteral("y")).toInt(),
                                  targetBounds.value(QStringLiteral("w")).toInt(),
                                  targetBounds.value(QStringLiteral("h")).toInt());
    } else {
        out->targetBounds = QRect();
    }
    out->customWorkflowMetadata = p.value(QStringLiteral("custom_workflow_metadata")).toObject();
    return true;
}

QString historyEntryDisplayName(const ComfyUIRemoteDock::Private::HistoryEntry &e, int maxLen)
{
    // FAITHFUL_PORT: ai_diffusion job.params.name — positive prompt after tag strip.
    QString s = ComfyUIUtils::stripPromptComments(e.prompt).trimmed();
    s.replace(QRegularExpression(QStringLiteral("<lora:[^>]*>")), QString());
    s.replace(QRegularExpression(QStringLiteral("<layer:[^>]*>")), QString());
    s = s.trimmed();
    if (s.isEmpty())
        s = e.styleName.trimmed();
    if (s.isEmpty())
        s = QStringLiteral("result");
    s.replace(QRegularExpression(QStringLiteral("[\r\n\t]+")), QStringLiteral(" "));
    s = s.simplified();
    if (maxLen > 0 && s.size() > maxLen)
        s = s.left(maxLen).trimmed() + QStringLiteral("…");
    return s;
}

QString historyEntryShortLabel(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    return historyEntryDisplayName(e, 40);
}

bool historyEntryIsHeaderItem(const QListWidgetItem *item)
{
    return item && item->data(HistoryItemIsHeaderRole).toInt() == 1;
}

bool historyParamsEqualIgnoreSeed(const ComfyUIRemoteDock::Private::HistoryEntry &a,
                                         const ComfyUIRemoteDock::Private::HistoryEntry &b)
{
    return a.prompt == b.prompt && a.negative == b.negative && a.checkpoint == b.checkpoint
           && a.styleName == b.styleName && a.width == b.width && a.height == b.height && a.steps == b.steps
           && qAbs(a.cfg - b.cfg) < 1e-6 && a.strength == b.strength && a.samplerName == b.samplerName
           && a.hasMask == b.hasMask && a.inpaintMode == b.inpaintMode && a.contextBounds == b.contextBounds
           && a.regionLayerNames == b.regionLayerNames;
}

QString historyEntryHeaderLabel(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    QString prompt = historyEntryDisplayName(e, 0);
    if (prompt.isEmpty())
        prompt = QStringLiteral("<no prompt>");
    const QString strength = e.strength != 100 ? QStringLiteral("%1% - ").arg(e.strength) : QString();
    const QString time =
        e.finishedAt.isValid() ? e.finishedAt.time().toString(QStringLiteral("HH:mm")) : QString();
    return QStringLiteral("%1 - %2%3").arg(time, strength, prompt);
}
QString previewLayerNameForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    // model.show_preview: trim_text(params.name, 77)
    return QStringLiteral("[Preview] %1").arg(historyEntryDisplayName(e, 77));
}

QString generatedLayerNameForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    // apply_result: trim_text(params.name, 200) + seed
    return QStringLiteral("[Generated] %1 (%2)").arg(historyEntryDisplayName(e, 200)).arg(e.seed);
}

QPoint historyMaskedPreviewOffset(const ComfyUIRemoteDock::Private::HistoryEntry &e, const QSize &imageSize)
{
    if (!e.hasMask)
        return QPoint();
    // Full composited context (fill + refine) always matches contextBounds dimensions.
    if (!e.contextBounds.isEmpty() && imageSize.isValid()
        && imageSize.width() == e.contextBounds.width() && imageSize.height() == e.contextBounds.height()) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "historyMaskedPreviewOffset: rule=contextBounds imageSize=" << imageSize
            << " contextBounds=" << e.contextBounds << " targetBounds=" << e.targetBounds
            << " offset=" << e.contextBounds.topLeft();
        return e.contextBounds.topLeft();
    }
    // Legacy: cached patch-only PNG sized to padded target bounds.
    if (!e.targetBounds.isEmpty() && imageSize.isValid() && imageSize == e.targetBounds.size()) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "historyMaskedPreviewOffset: rule=targetBounds(legacy_patch) imageSize=" << imageSize
            << " contextBounds=" << e.contextBounds << " targetBounds=" << e.targetBounds
            << " offset=" << e.targetBounds.topLeft();
        return e.targetBounds.topLeft();
    }
    if (!e.contextBounds.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "historyMaskedPreviewOffset: rule=contextBounds_fallback imageSize=" << imageSize
            << " contextBounds=" << e.contextBounds << " targetBounds=" << e.targetBounds
            << " offset=" << e.contextBounds.topLeft();
        return e.contextBounds.topLeft();
    }
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "historyMaskedPreviewOffset: rule=origin imageSize=" << imageSize
        << " contextBounds=" << e.contextBounds << " targetBounds=" << e.targetBounds;
    return QPoint();
}

// FAITHFUL_PORT: ai_diffusion layer.py — preview lock/visibility via direct node API, not undo.
void configurePreviewLayerState(KisNodeSP node, bool visible, bool locked)
{
    if (!node)
        return;
    node->setVisible(visible);
    node->setUserLocked(locked);
}

bool updatePreviewPaintLayerFromImage(KisViewManager *viewManager,
                                             KisImageSP image,
                                             KisPaintLayer *pl,
                                             const QImage &qimg,
                                             const QString &layerName,
                                             const QPoint &offset)
{
    if (!viewManager || !image || !pl || qimg.isNull())
        return false;
    if (!loadQImageIntoPaintLayer(pl, image, qimg, offset))
        return false;
    pl->setName(layerName);
    configurePreviewLayerState(pl, true, true);
    KisLayerSP layer = pl;
    raiseLayerToRootTop(viewManager, image, layer, false);
    nudgePreviewLayerProjection(layer);
    return true;
}

bool updatePreviewPaintLayerFromFile(KisViewManager *viewManager,
                                            KisImageSP image,
                                            KisPaintLayer *pl,
                                            const QString &path,
                                            const QString &layerName,
                                            const QPoint &offset,
                                            QHash<QString, QImage> *cache)
{
    if (!viewManager || !image || !pl || path.isEmpty() || !QFile::exists(path))
        return false;
    const QImage qimg = cachedHistoryPreviewImage(path, cache);
    return updatePreviewPaintLayerFromImage(viewManager, image, pl, qimg, layerName, offset);
}

bool addPreviewPaintLayerFromFile(KisViewManager *viewManager,
                                         KisImageSP image,
                                         const QString &path,
                                         const QString &layerName,
                                         KisLayerSP *outLayer,
                                         const QPoint &offset)
{
    if (!image || !viewManager || path.isEmpty() || !QFile::exists(path) || layerName.isEmpty()
        || !outLayer)
        return false;
    KisPaintLayerSP pl(new KisPaintLayer(image, layerName, OPACITY_OPAQUE_U8));
    const QImage qimg = cachedHistoryPreviewImage(path, nullptr);
    if (qimg.isNull() || !loadQImageIntoPaintLayer(pl.data(), image, qimg, offset))
        return false;
    configurePreviewLayerState(pl, true, true);
    KisNodeSP root = image->rootLayer();
    if (!root)
        return false;
    KisNodeSP above = topDirectRootChild(root);
    if (viewManager->nodeManager()) {
        KisNodeList nodes;
        nodes.append(pl);
        viewManager->nodeManager()->addNodesDirect(nodes, root, above);
        viewManager->nodeManager()->slotNonUiActivatedNode(pl);
    } else {
        image->addNode(pl, root, above);
    }
    image->waitForDone();
    *outLayer = pl;
    return true;
}

KisLayerSP findPreviewLayerByUuidString(KisImageSP image, const QString &layerId)
{
    if (!image || !image->rootLayer() || layerId.trimmed().isEmpty())
        return KisLayerSP();
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
        return KisLayerSP();
    KisNodeSP node = KisLayerUtils::findNodeByUuid(image->rootLayer(), uid);
    return KisLayerSP(qobject_cast<KisLayer *>(node.data()));
}

bool loadQImageIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QImage &qimg, const QPoint &offset)
{
    if (!pl || !image || qimg.isNull())
        return false;
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "loadQImageIntoPaintLayer: imageSize=" << qimg.size() << " docOffset=" << offset
        << " docBounds=" << image->bounds();
    QImage img = qimg;
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);
    KisPaintDeviceSP dst = pl->paintDevice();
    if (!dst)
        return false;
    KisPaintDeviceSP tmp = new KisPaintDevice(dst->colorSpace());
    tmp->convertFromQImage(img, nullptr);
    const QRect srcRect(QPoint(), img.size());
    dst->clear();
    KisPainter painter(dst);
    painter.bitBlt(offset, tmp, srcRect);
    pl->setVisible(true);
    pl->setDirty(QRect(offset, img.size()));
    return true;
}

QImage cachedHistoryPreviewImage(const QString &path, QHash<QString, QImage> *cache)
{
    if (path.isEmpty() || !QFile::exists(path))
        return QImage();
    if (cache) {
        const auto it = cache->constFind(path);
        if (it != cache->constEnd() && !it.value().isNull())
            return it.value();
    }
    QImage qimg;
    if (!qimg.load(path) || qimg.isNull())
        return QImage();
    if (qimg.format() != QImage::Format_ARGB32)
        qimg = qimg.convertToFormat(QImage::Format_ARGB32);
    if (cache) {
        trimHistoryPreviewImageCache(cache);
        cache->insert(path, qimg);
    }
    return qimg;
}

void trimHistoryPreviewImageCache(QHash<QString, QImage> *cache, int maxEntries)
{
    if (!cache || cache->size() < maxEntries)
        return;
    while (cache->size() >= maxEntries) {
        const auto it = cache->begin();
        if (it == cache->end())
            break;
        cache->erase(it);
    }
}

void nudgePreviewLayerProjection(KisLayerSP layer)
{
    if (!layer)
        return;
    const QString op = layer->compositeOpId();
    if (!op.isEmpty())
        layer->setCompositeOpId(op);
}

bool loadImageFileIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QString &path, const QPoint &offset)
{
    const QImage qimg = cachedHistoryPreviewImage(path, nullptr);
    if (qimg.isNull())
        return false;
    return loadQImageIntoPaintLayer(pl, image, qimg, offset);
}

KisNodeSP topDirectRootChild(KisNodeSP root)
{
    if (!root)
        return KisNodeSP();
    for (KisNodeSP child = root->lastChild(); child; child = child->prevSibling()) {
        if (child->parent().data() == root.data())
            return child;
    }
    return KisNodeSP();
}

bool isDirectChildOf(KisNodeSP parent, KisNodeSP child)
{
    return parent && child && child->parent().data() == parent.data();
}

void moveLayerInParent(KisViewManager *viewManager,
                              KisImageSP image,
                              KisNodeSP layer,
                              KisNodeSP parent,
                              KisNodeSP above,
                              bool waitForCompletion)
{
    if (!image || !layer || !parent)
        return;
    if (above && !isDirectChildOf(parent, above))
        above = KisNodeSP();
    if (above.data() == layer.data())
        above = KisNodeSP();

    if (viewManager && viewManager->nodeManager()) {
        KisNodeList nodes;
        nodes.append(layer);
        viewManager->nodeManager()->moveNodesDirect(nodes, parent, above);
        if (waitForCompletion)
            image->waitForDone();
        return;
    }

    KisNodeSP layerNode = layer;
    if (layerNode->parent())
        image->removeNode(layerNode);
    if (waitForCompletion)
        image->waitForDone();
    image->addNode(layerNode, parent, above);
    if (waitForCompletion)
        image->waitForDone();
}

void raiseLayerToRootTop(KisViewManager *viewManager, KisImageSP image, KisLayerSP layer, bool waitForCompletion)
{
    if (!image || !layer)
        return;
    KisNodeSP root = image->rootLayer();
    if (!root)
        return;
    KisNodeSP layerNode = layer;
    // Krita stores stack bottom at firstChild(), top at lastChild().
    if (isDirectChildOf(root, layerNode) && root->lastChild() == layerNode)
        return;

    KisNodeSP above = topDirectRootChild(root);
    if (above.data() == layerNode.data())
        above = KisNodeSP();
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "raiseLayerToRootTop layer=" << layer->name()
        << " above=" << (above ? above->name() : QStringLiteral("(bottom)"))
        << " wait=" << waitForCompletion;
    moveLayerInParent(viewManager, image, layerNode, root, above, waitForCompletion);
}

void collectPreviewLayers(KisNodeSP node, QList<KisLayerSP> *out)
{
    if (!node || !out)
        return;
    if (KisLayerSP layer = qobject_cast<KisLayer *>(node.data())) {
        if (layer->name().startsWith(QLatin1String("[Preview] ")))
            out->append(layer);
    }
    for (KisNodeSP child = node->firstChild(); child; child = child->nextSibling())
        collectPreviewLayers(child, out);
}

bool layerStillInDocument(KisImageSP image, KisLayerSP layer)
{
    if (!image || !layer || !image->rootLayer())
        return false;
    if (!layer->graphListener())
        return false;
    return KisLayerUtils::findNodeByUuid(image->rootLayer(), layer->uuid()) != nullptr;
}

KisNodeSP firstImportAnchorLayer(KisImageSP image, const QList<KisLayerSP> &excluding)
{
    KisGroupLayerSP root = image ? image->rootLayer() : KisGroupLayerSP();
    if (!root)
        return KisNodeSP();
    for (KisNodeSP child = root->lastChild(); child; child = child->prevSibling()) {
        if (!isDirectChildOf(root, child))
            continue;
        bool skip = false;
        for (const KisLayerSP &ex : excluding) {
            if (ex && ex.data() == child.data()) {
                skip = true;
                break;
            }
        }
        if (!skip && qobject_cast<KisLayer *>(child.data()))
            return child;
    }
    return topDirectRootChild(root);
}

void ensureActiveLayerValidForImport(KisViewManager *viewManager, KisImageSP image)
{
    if (!viewManager || !image || !viewManager->nodeManager())
        return;
    if (layerStillInDocument(image, viewManager->activeLayer()))
        return;
    if (KisNodeSP anchor = firstImportAnchorLayer(image))
        viewManager->nodeManager()->slotNonUiActivatedNode(anchor);
}

void removePreviewLayersFromImage(KisImageSP image, KisViewManager *viewManager, const QString &trackedLayerId)
{
    if (!image || !image->rootLayer())
        return;
    QList<KisLayerSP> toRemove;
    if (!trackedLayerId.isEmpty()) {
        if (KisLayerSP tracked = findPreviewLayerByUuidString(image, trackedLayerId))
            toRemove.append(tracked);
    }
    collectPreviewLayers(image->rootLayer(), &toRemove);
    QList<KisLayerSP> unique;
    for (const KisLayerSP &layer : toRemove) {
        if (!layer)
            continue;
        bool seen = false;
        for (const KisLayerSP &u : unique) {
            if (u.data() == layer.data()) {
                seen = true;
                break;
            }
        }
        if (!seen)
            unique.append(layer);
    }
    if (unique.isEmpty())
        return;

    KisLayerSP active = viewManager ? viewManager->activeLayer() : KisLayerSP();
    for (const KisLayerSP &layer : unique) {
        if (active && active.data() == layer.data()) {
            if (viewManager && viewManager->nodeManager()) {
                if (KisNodeSP anchor = firstImportAnchorLayer(image, unique))
                    viewManager->nodeManager()->slotNonUiActivatedNode(anchor);
            }
            break;
        }
    }
    for (const KisLayerSP &layer : unique)
        image->removeNode(layer);
    image->waitForDone();
    ensureActiveLayerValidForImport(viewManager, image);
}

void placeImportedLayerForBehavior(KisViewManager *viewManager,
                                          KisImageSP image,
                                          KisLayerSP imported,
                                          KisLayerSP activeBefore,
                                          const QString &behavior)
{
    if (!image || !imported || behavior == QLatin1String("replace"))
        return;
    if (behavior == QLatin1String("layer_active")) {
        KisNodeSP parent = image->rootLayer();
        if (activeBefore && activeBefore->parent())
            parent = activeBefore->parent();
        if (!parent)
            return;
        if (activeBefore && isDirectChildOf(parent, activeBefore)) {
            qCWarning(KIS_COMFYUI_REMOTE).nospace()
                << "placeImportedLayerForBehavior layer_active imported=" << imported->name()
                << " above=" << activeBefore->name();
            moveLayerInParent(viewManager, image, imported, parent, activeBefore, true);
        } else
            raiseLayerToRootTop(viewManager, image, imported);
        return;
    }
    raiseLayerToRootTop(viewManager, image, imported);
}

bool commitPreviewLayerForApply(KisViewManager *viewManager,
                                       KisImageSP image,
                                       KisLayerSP previewLayer,
                                       const QString &committedLayerName,
                                       const QString &applyBehavior)
{
    if (!viewManager || !image || !previewLayer)
        return false;
    configurePreviewLayerState(previewLayer, true, false);
    if (!committedLayerName.isEmpty())
        previewLayer->setName(committedLayerName);

    KisLayerSP activeBefore = viewManager->activeLayer();
    if (activeBefore && activeBefore.data() == previewLayer.data()) {
        if (viewManager->nodeManager()) {
            if (KisNodeSP anchor = firstImportAnchorLayer(image, {previewLayer}))
                viewManager->nodeManager()->slotNonUiActivatedNode(anchor);
        }
        activeBefore = viewManager->activeLayer();
    }

    QString beh = applyBehavior;
    if (beh.isEmpty())
        beh = QStringLiteral("layer");
    if (beh == QLatin1String("replace")) {
        if (activeBefore && activeBefore.data() != previewLayer.data())
            image->mergeDown(previewLayer, nullptr);
    } else {
        placeImportedLayerForBehavior(viewManager, image, previewLayer, activeBefore, beh);
    }
    image->waitForDone();
    activateAppliedResultLayer(viewManager, image, previewLayer, activeBefore, beh);
    return true;
}

QString applyBehaviorFromSettings(const ComfyUIRemoteDock::Private *d)
{
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const bool liveWs = d->comboWorkspace && d->comboWorkspace->currentIndex() == 2;
    QString beh = liveWs ? s.value(QStringLiteral("apply_behavior_live")).toString()
                         : s.value(QStringLiteral("apply_behavior")).toString();
    if (beh.isEmpty())
        beh = liveWs ? QStringLiteral("replace") : QStringLiteral("layer");
    return beh;
}

QString historyPathForListItem(const QListWidgetItem *item,
                                      const QList<ComfyUIRemoteDock::Private::HistoryEntry> &entries,
                                      int *outEntryIndex,
                                      int *outImageIndex)
{
    if (!item)
        return QString();
    const QString jobId = item->data(Qt::UserRole).toString();
    const int imageIndex = item->data(Qt::UserRole + 1).toInt();
    for (int i = 0; i < entries.size(); ++i) {
        const ComfyUIRemoteDock::Private::HistoryEntry &e = entries.at(i);
        if (e.jobId != jobId)
            continue;
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (imageIndex >= 0 && imageIndex < paths.size()) {
            if (outEntryIndex)
                *outEntryIndex = i;
            if (outImageIndex)
                *outImageIndex = imageIndex;
            return paths.at(imageIndex);
        }
        return QString();
    }
    return QString();
}

QString historyPathForIdentity(const QString &jobId,
                                      int imageIndex,
                                      const QList<ComfyUIRemoteDock::Private::HistoryEntry> &entries,
                                      int *outEntryIndex,
                                      int *outImageIndex)
{
    if (jobId.isEmpty())
        return QString();
    for (int i = 0; i < entries.size(); ++i) {
        const ComfyUIRemoteDock::Private::HistoryEntry &e = entries.at(i);
        if (e.jobId != jobId)
            continue;
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (imageIndex >= 0 && imageIndex < paths.size()) {
            if (outEntryIndex)
                *outEntryIndex = i;
            if (outImageIndex)
                *outImageIndex = imageIndex;
            return paths.at(imageIndex);
        }
        return QString();
    }
    return QString();
}

QListWidgetItem *findHistoryListItem(QListWidget *list, const QString &jobId, int imageIndex)
{
    if (!list || jobId.isEmpty())
        return nullptr;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (!item || historyEntryIsHeaderItem(item))
            continue;
        if (item->data(HistoryItemJobIdRole).toString() == jobId
            && item->data(HistoryItemImageIndexRole).toInt() == imageIndex)
            return item;
    }
    return nullptr;
}

QString historyCompositingMaskSidecarPath(const QString &resultImagePath)
{
    if (resultImagePath.isEmpty())
        return QString();
    return resultImagePath + QStringLiteral(".mask.png");
}

QString historyThumbnailSidecarPath(const QString &resultImagePath)
{
    if (resultImagePath.isEmpty())
        return QString();
    return resultImagePath + QStringLiteral(".thumb.png");
}

bool saveHistoryCompositingMaskSidecar(const QString &resultImagePath, const QImage &maskGray)
{
    if (resultImagePath.isEmpty() || maskGray.isNull())
        return false;
    QImage mask = maskGray.format() == QImage::Format_Grayscale8
                      ? maskGray
                      : maskGray.convertToFormat(QImage::Format_Grayscale8);
    const QString sidecar = historyCompositingMaskSidecarPath(resultImagePath);
    if (QFile::exists(sidecar))
        QFile::remove(sidecar);
    const bool ok = mask.save(sidecar);
    ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
        "maskSidecar.save",
        resultImagePath,
        QRect(),
        QRect(),
        true,
        mask.size(),
        mask.size(),
        QStringLiteral("path=") + sidecar + QStringLiteral(" ok=") + QString::number(ok) + QStringLiteral(" ")
            + ComfyUiLayoutDiagnostics::maskShapeDescription(mask));
    return ok;
}

static QImage cropHistoryImageToTarget(const ComfyUIRemoteDock::Private::HistoryEntry &entry, const QImage &source)
{
    if (source.isNull() || entry.targetBounds.isEmpty())
        return source;
    if (!entry.contextBounds.isEmpty() && source.size() == entry.contextBounds.size()) {
        const QRect local = entry.targetBounds.translated(-entry.contextBounds.topLeft());
        const QRect clip = local.intersected(QRect(QPoint(0, 0), source.size()));
        if (!clip.isEmpty())
            return source.copy(clip);
    }
    if (source.size() == entry.targetBounds.size())
        return source;
    return source;
}

static QImage loadHistoryCompositingMaskForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &entry,
                                                 const QString &resultPath,
                                                 const QSize &targetSize)
{
    const QString sidecar = historyCompositingMaskSidecarPath(resultPath);
    if (!QFile::exists(sidecar))
        return QImage();
    QImage mask;
    if (!mask.load(sidecar) || mask.isNull())
        return QImage();
    mask = mask.convertToFormat(QImage::Format_Grayscale8);
    if (!entry.contextBounds.isEmpty() && mask.size() == entry.contextBounds.size()
        && !entry.targetBounds.isEmpty()) {
        const QRect local = entry.targetBounds.translated(-entry.contextBounds.topLeft());
        const QRect clip = local.intersected(QRect(QPoint(0, 0), mask.size()));
        if (!clip.isEmpty())
            mask = mask.copy(clip);
    }
    if (targetSize.isValid() && mask.size() != targetSize)
        mask = mask.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return mask;
}

static void applyGrayscaleMaskAsAlpha(QImage *image, const QImage &maskGray)
{
    if (!image || image->isNull() || maskGray.isNull())
        return;
    if (image->format() != QImage::Format_ARGB32)
        *image = image->convertToFormat(QImage::Format_ARGB32);
    QImage mask = maskGray.format() == QImage::Format_Grayscale8
                      ? maskGray
                      : maskGray.convertToFormat(QImage::Format_Grayscale8);
    if (mask.size() != image->size())
        mask = mask.scaled(image->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < image->height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image->scanLine(y));
        for (int x = 0; x < image->width(); ++x) {
            const int maskAlpha = qGray(mask.pixel(x, y));
            const QRgb px = line[x];
            const int alpha = (qAlpha(px) * maskAlpha) / 255;
            line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), alpha);
        }
    }
}

static QImage buildHistoryDisplayThumbnail(const ComfyUIRemoteDock::Private::HistoryEntry &entry,
                                           const QString &resultImagePath,
                                           const QImage &resultImage,
                                           const QImage &compositingMaskGray)
{
    if (resultImage.isNull())
        return QImage();
    QImage img = cropHistoryImageToTarget(entry, resultImage);
    ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
        "build.cropped",
        resultImagePath,
        entry.contextBounds,
        entry.targetBounds,
        entry.hasMask,
        resultImage.size(),
        compositingMaskGray.size(),
        QStringLiteral("cropped=%1x%2").arg(img.width()).arg(img.height()));
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);

    QImage mask = compositingMaskGray;
    if (mask.isNull())
        mask = loadHistoryCompositingMaskForEntry(entry, resultImagePath, img.size());
    if (mask.isNull()) {
        ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
            "build.no_mask",
            resultImagePath,
            entry.contextBounds,
            entry.targetBounds,
            entry.hasMask,
            img.size(),
            QSize(),
            QStringLiteral("maskSidecarExists=")
                + QString::number(QFile::exists(historyCompositingMaskSidecarPath(resultImagePath))));
        return img;
    }

    if (!entry.contextBounds.isEmpty() && mask.size() == entry.contextBounds.size()
        && !entry.targetBounds.isEmpty()) {
        const QRect local = entry.targetBounds.translated(-entry.contextBounds.topLeft());
        const QRect clip = local.intersected(QRect(QPoint(0, 0), mask.size()));
        if (!clip.isEmpty())
            mask = mask.copy(clip);
    }
    if (mask.size() != img.size())
        mask = mask.scaled(img.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    applyGrayscaleMaskAsAlpha(&img, mask);
    ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
        "build.masked",
        resultImagePath,
        entry.contextBounds,
        entry.targetBounds,
        entry.hasMask,
        img.size(),
        mask.size(),
        QStringLiteral("ok ") + ComfyUiLayoutDiagnostics::maskShapeDescription(mask) + QStringLiteral(" ")
            + ComfyUiLayoutDiagnostics::imageAlphaCornerStats(img));
    return img;
}

bool saveHistoryDisplayThumbnail(const QString &resultImagePath,
                               const ComfyUIRemoteDock::Private::HistoryEntry &entry,
                               const QImage &resultImage,
                               const QImage &compositingMaskGray)
{
    if (resultImagePath.isEmpty())
        return false;
    const QImage thumb = buildHistoryDisplayThumbnail(entry, resultImagePath, resultImage, compositingMaskGray);
    if (thumb.isNull()) {
        ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
            "save.fail",
            resultImagePath,
            entry.contextBounds,
            entry.targetBounds,
            entry.hasMask,
            resultImage.size(),
            compositingMaskGray.size(),
            QStringLiteral("build returned null"));
        return false;
    }
    const QString sidecar = historyThumbnailSidecarPath(resultImagePath);
    if (QFile::exists(sidecar))
        QFile::remove(sidecar);
    const bool ok = thumb.save(sidecar);
    ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
        "save.done",
        resultImagePath,
        entry.contextBounds,
        entry.targetBounds,
        entry.hasMask,
        thumb.size(),
        compositingMaskGray.size(),
        QStringLiteral("sidecar=") + sidecar + QStringLiteral(" ok=") + QString::number(ok));
    return ok;
}

int historyListRowWidth(const QListWidget *list)
{
    if (!list)
        return 96;
    const int viewportW = list->viewport()->width();
    const int iconW = list->iconSize().width();
    return qMax(iconW, viewportW > 0 ? viewportW : iconW);
}

QSize historyHeaderItemSizeHint(const QListWidget *list, int headerHeight)
{
    return QSize(historyListRowWidth(list), qMax(1, headerHeight));
}

int historyThumbnailHorizontalCellPadding()
{
  // FAITHFUL_PORT: generation.HistoryWidget — QListWidget default inter-item spacing is 4px.
  // Baked into cell width so header→thumb vertical gap stays tight (list spacing stays 0).
    return 4;
}

QSize historyThumbnailItemSizeHint(const QListWidget *list, const QSize &pixmapSize)
{
    Q_UNUSED(list);
    const int pad = historyThumbnailHorizontalCellPadding();
    const int rowW = qMax(16, pixmapSize.width() + pad);
    const int rowH = qMax(16, pixmapSize.height() + 2);
    return QSize(rowW, rowH);
}

void syncHistoryListItemWidths(QListWidget *list)
{
    if (!list)
        return;
    const int headerW = historyListRowWidth(list);
    if (headerW <= 0)
        return;
    bool changed = false;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (!item)
            continue;
        QSize hint = item->sizeHint();
        if (!hint.isValid())
            continue;
        if (historyEntryIsHeaderItem(item)) {
            if (hint.width() == headerW)
                continue;
            item->setSizeHint(QSize(headerW, hint.height()));
            changed = true;
            continue;
        }
        const QList<QSize> avail = item->icon().availableSizes();
        const int thumbW = avail.isEmpty() ? list->iconSize().width() : avail.first().width();
        const int cellW = thumbW + historyThumbnailHorizontalCellPadding();
        if (hint.width() > cellW) {
            item->setSizeHint(QSize(cellW, hint.height()));
            changed = true;
        }
    }
    if (changed)
        list->doItemsLayout();
}

QPixmap historyThumbnailPixmap(const ComfyUIRemoteDock::Private::HistoryEntry &entry,
                               const QString &path,
                               const QSize &iconSize,
                               QHash<QString, QImage> *cache)
{
    const QString thumbSidecar = historyThumbnailSidecarPath(path);
    QImage img;
    const bool hasThumbSidecar = QFile::exists(thumbSidecar);
    if (hasThumbSidecar)
        img = cachedHistoryPreviewImage(thumbSidecar, cache);
    const char *source = hasThumbSidecar && !img.isNull() ? "thumbSidecar" : "build";
    if (img.isNull())
        img = buildHistoryDisplayThumbnail(entry, path, cachedHistoryPreviewImage(path, cache), QImage());
    if (img.isNull()) {
        ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
            "pixmap.fail",
            path,
            entry.contextBounds,
            entry.targetBounds,
            entry.hasMask,
            QSize(),
            QSize(),
            QStringLiteral("source=") + QLatin1String(source));
        return QPixmap();
    }
    ComfyUiLayoutDiagnostics::logHistoryThumbnailStage(
        "pixmap.ok",
        path,
        entry.contextBounds,
        entry.targetBounds,
        entry.hasMask,
        img.size(),
        QSize(),
        QStringLiteral("source=") + QLatin1String(source) + QStringLiteral(" ")
            + ComfyUiLayoutDiagnostics::imageAlphaCornerStats(img));

    QPixmap scaled = QPixmap::fromImage(img).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return cropPixmapToOpaqueContent(scaled);
}

void activateAppliedResultLayer(KisViewManager *viewManager,
                                KisImageSP image,
                                KisLayerSP imported,
                                KisLayerSP activeBefore,
                                const QString &behavior)
{
    if (!viewManager || !viewManager->nodeManager())
        return;
    KisNodeSP toSelect;
    if (behavior == QLatin1String("replace")) {
        if (activeBefore && layerStillInDocument(image, activeBefore))
            toSelect = activeBefore;
        else if (imported && layerStillInDocument(image, imported))
            toSelect = imported;
    } else if (imported && layerStillInDocument(image, imported)) {
        toSelect = imported;
    }
    if (toSelect)
        viewManager->nodeManager()->slotNonUiActivatedNode(toSelect);
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG apply.selectLayer behavior=") << behavior
        << QStringLiteral("selected=") << (toSelect ? toSelect->name() : QStringLiteral("<none>"))
        << QStringLiteral("imported=") << (imported ? imported->name() : QStringLiteral("<none>"));
}

} // namespace ComfyHistoryInternal
