/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMenu>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QPixmap>
#include <QPainter>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QRegularExpression>
#include <QMap>
#include <QUuid>
#include <QtGlobal>
#include <climits>
#include <kis_annotation.h>
#include <klocalizedstring.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <kis_image_manager.h>
#include <kis_node_manager.h>
#include <KisDocument.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_layer_utils.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_node.h>
#include <kis_painter.h>
#include <KisImageBarrierLock.h>
#include <kundo2magicstring.h>
#include <KoCompositeOpRegistry.h>
#include <KoColorSpaceConstants.h>  // OPACITY_OPAQUE_U8 for KisGroupLayer

namespace {

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

static void remapImageInUseAfterRemoval(QMap<int, bool> &m, int removedIndex)
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
    return true;
}

} // namespace

static QString historyEntryDisplayName(const ComfyUIRemoteDock::Private::HistoryEntry &e, int maxLen)
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

static QString historyEntryShortLabel(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    return historyEntryDisplayName(e, 40);
}

enum HistoryListItemRole {
    HistoryItemJobIdRole = Qt::UserRole,
    HistoryItemImageIndexRole = Qt::UserRole + 1,
    HistoryItemIsHeaderRole = Qt::UserRole + 2,
};

static bool historyEntryIsHeaderItem(const QListWidgetItem *item)
{
    return item && item->data(HistoryItemIsHeaderRole).toInt() == 1;
}

static bool historyParamsEqualIgnoreSeed(const ComfyUIRemoteDock::Private::HistoryEntry &a,
                                         const ComfyUIRemoteDock::Private::HistoryEntry &b)
{
    return a.prompt == b.prompt && a.negative == b.negative && a.checkpoint == b.checkpoint
           && a.styleName == b.styleName && a.width == b.width && a.height == b.height && a.steps == b.steps
           && qAbs(a.cfg - b.cfg) < 1e-6 && a.strength == b.strength && a.samplerName == b.samplerName
           && a.hasMask == b.hasMask && a.inpaintMode == b.inpaintMode && a.contextBounds == b.contextBounds
           && a.regionLayerNames == b.regionLayerNames;
}

static QString historyEntryHeaderLabel(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    QString prompt = historyEntryDisplayName(e, 0);
    if (prompt.isEmpty())
        prompt = QStringLiteral("<no prompt>");
    const QString strength = e.strength != 100 ? QStringLiteral("%1% - ").arg(e.strength) : QString();
    const QString time =
        e.finishedAt.isValid() ? e.finishedAt.time().toString(QStringLiteral("HH:mm")) : QString();
    return QStringLiteral("%1 - %2%3").arg(time, strength, prompt);
}

void ComfyUIRemoteDock::refreshHistoryList(bool scrollToBottom)
{
    if (!m_d->listHistory)
        return;

    QString keepJobId;
    int keepImageIndex = -1;
    if (m_d->listHistory->currentItem() && !historyEntryIsHeaderItem(m_d->listHistory->currentItem())) {
        keepJobId = m_d->listHistory->currentItem()->data(HistoryItemJobIdRole).toString();
        keepImageIndex = m_d->listHistory->currentItem()->data(HistoryItemImageIndexRole).toInt();
    } else if (!m_d->previewHistoryJobId.isEmpty()) {
        keepJobId = m_d->previewHistoryJobId;
        keepImageIndex = m_d->previewHistoryImageIndex;
    }

    m_d->listHistory->clear();
    const QSize iconSize = m_d->listHistory->iconSize();
    const int thumbW = iconSize.width();
    const int thumbH = iconSize.height();
    const int starX = thumbW - 28;
    const int starY = 4;
    const int starSize = 24;
    QIcon starIcon = ComfyTheme::icon(QStringLiteral("star"));
    QPixmap starPix = starIcon.pixmap(starSize, starSize);
    int selectRow = -1;
    int row = 0;
    bool haveLastParams = false;
    Private::HistoryEntry lastParams;

    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (paths.isEmpty())
            continue;

        if (!haveLastParams || !historyParamsEqualIgnoreSeed(lastParams, e)) {
            lastParams = e;
            haveLastParams = true;
            QListWidgetItem *header = new QListWidgetItem(historyEntryHeaderLabel(e));
            header->setFlags(Qt::NoItemFlags);
            header->setData(HistoryItemJobIdRole, e.jobId);
            header->setData(HistoryItemImageIndexRole, -1);
            header->setData(HistoryItemIsHeaderRole, 1);
            header->setToolTip(e.prompt);
            const int headerH = m_d->listHistory->fontMetrics().lineSpacing() + 4;
            header->setSizeHint(QSize(9999, headerH));
            header->setTextAlignment(Qt::AlignLeft);
            m_d->listHistory->addItem(header);
            ++row;
        }

        const QString snippet = historyEntryShortLabel(e);
        for (int imageIndex = 0; imageIndex < paths.size(); ++imageIndex) {
            const QString path = paths.at(imageIndex);
            QString tip = QStringLiteral("%1 (%2×%3)\nSeed: %4")
                              .arg(snippet)
                              .arg(e.width)
                              .arg(e.height)
                              .arg(e.seed);
            if (paths.size() > 1)
                tip = ComfyTr::tr("Image %1 of %2", imageIndex + 1, paths.size()) + QStringLiteral("\n") + tip;
            QListWidgetItem *item = new QListWidgetItem();
            if (!path.isEmpty() && QFile::exists(path)) {
                QPixmap pix(path);
                if (!pix.isNull()) {
                    pix = pix.scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    if (e.imageInUse.value(imageIndex, false) && !starPix.isNull()) {
                        QPixmap composite(thumbW, thumbH);
                        composite.fill(Qt::transparent);
                        QPainter p(&composite);
                        p.drawPixmap(0, 0, pix);
                        p.drawPixmap(starX, starY, starPix);
                        p.end();
                        item->setIcon(QIcon(composite));
                    } else {
                        item->setIcon(QIcon(pix));
                    }
                }
            }
            if (item->icon().isNull()) {
                item->setText(paths.size() > 1
                                  ? snippet + QStringLiteral(" [%1/%2]").arg(imageIndex + 1).arg(paths.size())
                                  : snippet);
            }
            item->setToolTip(tip);
            item->setData(HistoryItemJobIdRole, e.jobId);
            item->setData(HistoryItemImageIndexRole, imageIndex);
            item->setData(HistoryItemIsHeaderRole, 0);
            m_d->listHistory->addItem(item);
            if (!keepJobId.isEmpty() && e.jobId == keepJobId && imageIndex == keepImageIndex)
                selectRow = row;
            ++row;
        }
    }

    if (selectRow >= 0)
        m_d->listHistory->setCurrentRow(selectRow);
    else if (scrollToBottom)
        m_d->listHistory->scrollToBottom();
    m_d->listHistory->updateOverlayButtons();
}

void ComfyUIRemoteDock::slotHistoryItemSelected()
{
    QListWidgetItem *current = m_d->listHistory ? m_d->listHistory->currentItem() : nullptr;
    const bool hasSelection = current && !historyEntryIsHeaderItem(current);
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotHistoryItemSelected currentRow=" << (m_d->listHistory ? m_d->listHistory->currentRow() : -1)
        << " count=" << (m_d->listHistory ? m_d->listHistory->count() : 0)
        << " hasSelection=" << hasSelection;
    m_d->btnHistoryReRun->setEnabled(hasSelection);
    m_d->btnHistoryApply->setEnabled(hasSelection);
    if (m_d->listHistory)
        m_d->listHistory->updateOverlayButtons();
    updateHistoryPreviewFromSelection();
}

static QString previewLayerNameForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    // model.show_preview: trim_text(params.name, 77)
    return QStringLiteral("[Preview] %1").arg(historyEntryDisplayName(e, 77));
}

static QString generatedLayerNameForEntry(const ComfyUIRemoteDock::Private::HistoryEntry &e)
{
    // apply_result: trim_text(params.name, 200) + seed
    return QStringLiteral("[Generated] %1 (%2)").arg(historyEntryDisplayName(e, 200)).arg(e.seed);
}

static bool loadImageFileIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QString &path, const QPoint &offset = QPoint());
static bool loadQImageIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QImage &qimg, const QPoint &offset);
static QImage cachedHistoryPreviewImage(const QString &path, QHash<QString, QImage> *cache);
static void trimHistoryPreviewImageCache(QHash<QString, QImage> *cache, int maxEntries = 48);
static void nudgePreviewLayerProjection(KisLayerSP layer);
static KisNodeSP topDirectRootChild(KisNodeSP root);
static void raiseLayerToRootTop(KisViewManager *viewManager, KisImageSP image, KisLayerSP layer, bool waitForCompletion = true);

// FAITHFUL_PORT: ai_diffusion layer.py — preview lock/visibility via direct node API, not undo.
static void configurePreviewLayerState(KisNodeSP node, bool visible, bool locked)
{
    if (!node)
        return;
    node->setVisible(visible);
    node->setUserLocked(locked);
}

static bool updatePreviewPaintLayerFromImage(KisViewManager *viewManager,
                                             KisImageSP image,
                                             KisPaintLayer *pl,
                                             const QImage &qimg,
                                             const QString &layerName,
                                             const QPoint &offset = QPoint())
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

static bool updatePreviewPaintLayerFromFile(KisViewManager *viewManager,
                                            KisImageSP image,
                                            KisPaintLayer *pl,
                                            const QString &path,
                                            const QString &layerName,
                                            const QPoint &offset = QPoint(),
                                            QHash<QString, QImage> *cache = nullptr)
{
    if (!viewManager || !image || !pl || path.isEmpty() || !QFile::exists(path))
        return false;
    const QImage qimg = cachedHistoryPreviewImage(path, cache);
    return updatePreviewPaintLayerFromImage(viewManager, image, pl, qimg, layerName, offset);
}

static bool addPreviewPaintLayerFromFile(KisViewManager *viewManager,
                                         KisImageSP image,
                                         const QString &path,
                                         const QString &layerName,
                                         KisLayerSP *outLayer,
                                         const QPoint &offset = QPoint())
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

static KisLayerSP findPreviewLayerByUuidString(KisImageSP image, const QString &layerId)
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

static bool loadQImageIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QImage &qimg, const QPoint &offset)
{
    if (!pl || !image || qimg.isNull())
        return false;
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

static QImage cachedHistoryPreviewImage(const QString &path, QHash<QString, QImage> *cache)
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

static void trimHistoryPreviewImageCache(QHash<QString, QImage> *cache, int maxEntries)
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

static void nudgePreviewLayerProjection(KisLayerSP layer)
{
    if (!layer)
        return;
    const QString op = layer->compositeOpId();
    if (!op.isEmpty())
        layer->setCompositeOpId(op);
}

static bool loadImageFileIntoPaintLayer(KisPaintLayer *pl, KisImageSP image, const QString &path, const QPoint &offset)
{
    const QImage qimg = cachedHistoryPreviewImage(path, nullptr);
    if (qimg.isNull())
        return false;
    return loadQImageIntoPaintLayer(pl, image, qimg, offset);
}

static KisNodeSP topDirectRootChild(KisNodeSP root)
{
    if (!root)
        return KisNodeSP();
    for (KisNodeSP child = root->lastChild(); child; child = child->prevSibling()) {
        if (child->parent().data() == root.data())
            return child;
    }
    return KisNodeSP();
}

static bool isDirectChildOf(KisNodeSP parent, KisNodeSP child)
{
    return parent && child && child->parent().data() == parent.data();
}

static void moveLayerInParent(KisViewManager *viewManager,
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

static void raiseLayerToRootTop(KisViewManager *viewManager, KisImageSP image, KisLayerSP layer, bool waitForCompletion)
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

static void collectPreviewLayers(KisNodeSP node, QList<KisLayerSP> *out)
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

static bool layerStillInDocument(KisImageSP image, KisLayerSP layer)
{
    if (!image || !layer || !image->rootLayer())
        return false;
    if (!layer->graphListener())
        return false;
    return KisLayerUtils::findNodeByUuid(image->rootLayer(), layer->uuid()) != nullptr;
}

static KisNodeSP firstImportAnchorLayer(KisImageSP image, const QList<KisLayerSP> &excluding = {})
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

static void ensureActiveLayerValidForImport(KisViewManager *viewManager, KisImageSP image)
{
    if (!viewManager || !image || !viewManager->nodeManager())
        return;
    if (layerStillInDocument(image, viewManager->activeLayer()))
        return;
    if (KisNodeSP anchor = firstImportAnchorLayer(image))
        viewManager->nodeManager()->slotNonUiActivatedNode(anchor);
}

static void removePreviewLayersFromImage(KisImageSP image, KisViewManager *viewManager, const QString &trackedLayerId)
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

static void placeImportedLayerForBehavior(KisViewManager *viewManager,
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

static bool commitPreviewLayerForApply(KisViewManager *viewManager,
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
    return true;
}

static QString applyBehaviorFromSettings(const ComfyUIRemoteDock::Private *d)
{
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const bool liveWs = d->comboWorkspace && d->comboWorkspace->currentIndex() == 2;
    QString beh = liveWs ? s.value(QStringLiteral("apply_behavior_live")).toString()
                         : s.value(QStringLiteral("apply_behavior")).toString();
    if (beh.isEmpty())
        beh = liveWs ? QStringLiteral("replace") : QStringLiteral("layer");
    return beh;
}

static QString historyPathForListItem(const QListWidgetItem *item,
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

static QString historyPathForIdentity(const QString &jobId,
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

static QListWidgetItem *findHistoryListItem(QListWidget *list, const QString &jobId, int imageIndex)
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

void ComfyUIRemoteDock::slotHistoryPreview()
{
    if (!m_d->listHistory)
        return;
    showHistoryPreviewForItem(m_d->listHistory->currentItem());
}

void ComfyUIRemoteDock::slotHistoryPreviewForItem(QListWidgetItem *item)
{
    showHistoryPreviewForItem(item);
}

void ComfyUIRemoteDock::clearHistoryListSelection()
{
    if (!m_d->listHistory)
        return;
    m_d->historyPreviewUpdateBlocked = true;
    m_d->listHistory->clearSelection();
    m_d->listHistory->setCurrentItem(nullptr);
    m_d->historyPreviewUpdateBlocked = false;
}

void ComfyUIRemoteDock::tryBindPreviewLayerFromDocument()
{
    if (m_d->previewLayerId.isEmpty() || !m_d->viewManager)
        return;
    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return;
    if (!findPreviewLayerByUuidString(image, m_d->previewLayerId)) {
        m_d->previewLayerId.clear();
        m_d->previewHistoryJobId.clear();
        m_d->previewHistoryImageIndex = -1;
        savePreviewLayerIdToDocument(QString());
    }
}

void ComfyUIRemoteDock::hideHistoryPreview(bool deleteLayer)
{
    if (deleteLayer) {
        clearHistoryPreviewState();
        return;
    }
    if (!m_d->viewManager)
        return;
    KisImageSP image = m_d->viewManager->image();
    if (!image || m_d->previewLayerId.isEmpty())
        return;
    KisLayerSP layer = findPreviewLayerByUuidString(image, m_d->previewLayerId);
    if (!layer) {
        m_d->previewLayerId.clear();
        m_d->previewHistoryJobId.clear();
        m_d->previewHistoryImageIndex = -1;
        savePreviewLayerIdToDocument(QString());
        return;
    }
    configurePreviewLayerState(layer, false, layer->userLocked());
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
}

void ComfyUIRemoteDock::rememberHistoryPreviewImage(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull())
        return;
    QImage img = image;
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);
    trimHistoryPreviewImageCache(&m_d->historyPreviewImageCache);
    m_d->historyPreviewImageCache.insert(path, img);
}

void ComfyUIRemoteDock::updateHistoryPreviewFromSelection()
{
    if (m_d->historyPreviewUpdateBlocked || !m_d->listHistory)
        return;
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2)
            return;
    }
    QListWidgetItem *item = m_d->listHistory->currentItem();
    if (!item || historyEntryIsHeaderItem(item)) {
        hideHistoryPreview(false);
        return;
    }
    showHistoryPreviewForItem(item);
}

void ComfyUIRemoteDock::showHistoryPreviewForItem(QListWidgetItem *item)
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "showHistoryPreviewForItem item="
        << (item ? item->data(Qt::UserRole).toString() : QStringLiteral("null"))
        << " currentRow=" << (m_d->listHistory ? m_d->listHistory->currentRow() : -1)
        << " count=" << (m_d->listHistory ? m_d->listHistory->count() : -1)
        << " workspace=" << (m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1)
        << " currentPreviewLayerId=" << m_d->previewLayerId;
    if (!item || historyEntryIsHeaderItem(item))
        return;
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2) {
            qCWarning(KIS_COMFYUI_REMOTE) << "showHistoryPreviewForItem: workspace gate, ws=" << ws << "; aborting";
            return;
        }
    }
    if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    if (!image) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    int entryIndex = -1;
    int imageIndex = -1;
    const QString path = historyPathForListItem(item, m_d->historyEntries, &entryIndex, &imageIndex);
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "showHistoryPreviewForItem resolved entryIndex=" << entryIndex
        << " imageIndex=" << imageIndex << " path=" << path
        << " fileExists=" << (path.isEmpty() ? false : QFile::exists(path));
    if (path.isEmpty() || !QFile::exists(path)) {
        setStatusMessage(ComfyTr::tr("No result image to preview."), true);
        return;
    }

    QString label;
    QString previewName = QStringLiteral("[Preview] result");
    QPoint previewOffset;
    if (entryIndex >= 0 && entryIndex < m_d->historyEntries.size()) {
        const Private::HistoryEntry &entry = m_d->historyEntries.at(entryIndex);
        label = historyEntryShortLabel(entry);
        previewName = previewLayerNameForEntry(entry);
        if (entry.hasMask && !entry.contextBounds.isEmpty())
            previewOffset = entry.contextBounds.topLeft();
    }
    if (label.isEmpty())
        label = QStringLiteral("result");

    KisLayerSP previewLayer = findPreviewLayerByUuidString(image, m_d->previewLayerId);
    KisLayerSP imported;
    if (previewLayer && previewLayer->name().startsWith(QLatin1String("[Preview]"))) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "showHistoryPreviewForItem: reusing preview layer name=" << previewLayer->name();
        if (auto *pl = qobject_cast<KisPaintLayer *>(previewLayer.data())) {
            const QImage previewImg = cachedHistoryPreviewImage(path, &m_d->historyPreviewImageCache);
            if (previewImg.isNull()
                || !updatePreviewPaintLayerFromImage(m_d->viewManager.data(), image, pl, previewImg, previewName,
                                                     previewOffset))
                previewLayer = KisLayerSP();
        } else {
            previewLayer = KisLayerSP();
        }
        imported = previewLayer;
    }
    if (!imported) {
        hideHistoryPreview(true);
        const QImage previewImg = cachedHistoryPreviewImage(path, &m_d->historyPreviewImageCache);
        if (previewImg.isNull()) {
            setStatusMessage(ComfyTr::tr("No result image to preview."), true);
            return;
        }
        KisPaintLayerSP pl(new KisPaintLayer(image, previewName, OPACITY_OPAQUE_U8));
        if (!loadQImageIntoPaintLayer(pl.data(), image, previewImg, previewOffset)) {
            setStatusMessage(ComfyTr::tr("Could not import preview image."), true);
            return;
        }
        configurePreviewLayerState(pl, true, true);
        KisNodeSP root = image->rootLayer();
        if (!root) {
            setStatusMessage(ComfyTr::tr("Could not import preview image."), true);
            return;
        }
        KisNodeSP above = topDirectRootChild(root);
        if (m_d->viewManager->nodeManager()) {
            KisNodeList nodes;
            nodes.append(pl);
            m_d->viewManager->nodeManager()->addNodesDirect(nodes, root, above);
            m_d->viewManager->nodeManager()->slotNonUiActivatedNode(pl);
        } else {
            image->addNode(pl, root, above);
        }
        imported = pl;
        nudgePreviewLayerProjection(imported);
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "showHistoryPreviewForItem: created preview layer name=" << imported->name();
    }

    const QString uidStr = imported->uuid().toString(QUuid::WithoutBraces);
    m_d->previewLayerId = uidStr;
    m_d->previewHistoryJobId = item->data(Qt::UserRole).toString();
    m_d->previewHistoryImageIndex = item->data(Qt::UserRole + 1).toInt();
    savePreviewLayerIdToDocument(uidStr);

    nudgePreviewLayerProjection(imported);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    if (m_d->listHistory)
        m_d->listHistory->updateOverlayButtons();
    setStatusMessage(ComfyTr::tr("Previewing \"%1\". Tap Apply to keep, or tap another thumbnail.", label));
}

void ComfyUIRemoteDock::clearHistoryPreviewState()
{
    if (m_d->viewManager && m_d->viewManager->image()) {
        removePreviewLayersFromImage(m_d->viewManager->image(),
                                     m_d->viewManager.data(),
                                     m_d->previewLayerId);
    }
    m_d->previewLayerId.clear();
    m_d->previewHistoryJobId.clear();
    m_d->previewHistoryImageIndex = -1;
    savePreviewLayerIdToDocument(QString());
}

void ComfyUIRemoteDock::slotHistoryApply()
{
    slotHistoryApplyForItem(nullptr);
}

void ComfyUIRemoteDock::slotHistoryApplyForItem(QListWidgetItem *item)
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotHistoryApplyForItem ENTER explicitItem="
        << (item ? item->data(Qt::UserRole).toString() : QStringLiteral("null"))
        << " currentRow="
        << (m_d->listHistory ? m_d->listHistory->currentRow() : -1)
        << " count=" << (m_d->listHistory ? m_d->listHistory->count() : -1)
        << " workspace=" << (m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1)
        << " previewHistoryJobId=" << m_d->previewHistoryJobId
        << " previewHistoryImageIndex=" << m_d->previewHistoryImageIndex
        << " currentPreviewLayerId=" << m_d->previewLayerId;
    // §10.1: Apply action — only Generate (0) and Live (2) workspaces
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotHistoryApply: workspace gate, ws=" << ws << "; aborting";
            return;
        }
    }
    if (m_d->listHistory) {
        if (!item && !m_d->previewHistoryJobId.isEmpty()) {
            item = findHistoryListItem(m_d->listHistory,
                                       m_d->previewHistoryJobId,
                                       m_d->previewHistoryImageIndex);
        }
        if (!item)
            item = m_d->listHistory->currentItem();
        if (item)
            m_d->listHistory->setCurrentItem(item);
        else if (m_d->listHistory->currentRow() < 0 && m_d->listHistory->count() > 0) {
            for (int i = m_d->listHistory->count() - 1; i >= 0; --i) {
                QListWidgetItem *it = m_d->listHistory->item(i);
                if (it && !historyEntryIsHeaderItem(it)) {
                    m_d->listHistory->setCurrentItem(it);
                    item = it;
                    break;
                }
            }
        }
    }
    if (historyEntryIsHeaderItem(item))
        return;
    int entryIndex = -1;
    int imageIndex = -1;
    QString path = historyPathForListItem(item, m_d->historyEntries, &entryIndex, &imageIndex);
    if (path.isEmpty() && !m_d->previewHistoryJobId.isEmpty()) {
        path = historyPathForIdentity(m_d->previewHistoryJobId,
                                      m_d->previewHistoryImageIndex,
                                      m_d->historyEntries,
                                      &entryIndex,
                                      &imageIndex);
    }
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotHistoryApplyForItem resolved entryIndex=" << entryIndex
        << " imageIndex=" << imageIndex
        << " path=" << path
        << " fileExists=" << (path.isEmpty() ? false : QFile::exists(path));
    if (path.isEmpty() || !QFile::exists(path)) {
        if (!m_d->previewHistoryJobId.isEmpty())
            clearHistoryPreviewState();
        setStatusMessage(ComfyTr::tr("No result image to apply."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotHistoryApply: viewManager / imageManager unavailable";
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    const QString applyJobId = item ? item->data(Qt::UserRole).toString() : m_d->previewHistoryJobId;
    const int applyImageIndex = item ? item->data(Qt::UserRole + 1).toInt() : m_d->previewHistoryImageIndex;
    const bool canCommitPreview = !m_d->previewLayerId.isEmpty()
                                  && applyJobId == m_d->previewHistoryJobId
                                  && applyImageIndex == m_d->previewHistoryImageIndex;

    KisLayerSP previewLayer;
    if (image && canCommitPreview)
        previewLayer = findPreviewLayerByUuidString(image, m_d->previewLayerId);

    auto clearPreviewTracking = [this]() {
        m_d->previewLayerId.clear();
        m_d->previewHistoryJobId.clear();
        m_d->previewHistoryImageIndex = -1;
        savePreviewLayerIdToDocument(QString());
    };

    // §13.184 / §3.5: apply_region_behavior vs apply_region_behavior_live when Live workspace is active
    if (entryIndex >= 0 && !m_d->historyEntries[entryIndex].regionLayerNames.isEmpty()) {
        if (image) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotHistoryApply: removing preview layer(s) before region apply";
            removePreviewLayersFromImage(image, m_d->viewManager.data(), m_d->previewLayerId);
            clearPreviewTracking();
        }
        const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
        const bool liveWs = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
        QString behavior = liveWs ? sset.value(QStringLiteral("apply_region_behavior_live")).toString()
                                    : sset.value(QStringLiteral("apply_region_behavior")).toString();
        if (behavior.isEmpty())
            behavior = liveWs ? QStringLiteral("replace") : QStringLiteral("layer_group");
        if (behavior != QLatin1String("none") && applyResultToRegions(path, entryIndex, behavior)) {
            m_d->historyEntries[entryIndex].imageInUse.insert(imageIndex, true);
            clearHistoryListSelection();
            refreshHistoryList();
            scheduleDocumentUiJsonSave();
            if (m_d->canvas) m_d->canvas->updateCanvas();
            m_d->labelStatus->setText(ComfyTr::tr("Applied result to region layers."));
            return;
        }
    }
    const QString beh = applyBehaviorFromSettings(m_d.data());
    QString commitName;
    QRect resultBounds;
    if (entryIndex >= 0 && entryIndex < m_d->historyEntries.size()) {
        const Private::HistoryEntry &entry = m_d->historyEntries.at(entryIndex);
        commitName = generatedLayerNameForEntry(entry);
        if (entry.hasMask && !entry.contextBounds.isEmpty())
            resultBounds = entry.contextBounds;
    }

    bool applied = false;
    if (previewLayer && previewLayer->name().startsWith(QLatin1String("[Preview]"))) {
        qCWarning(KIS_COMFYUI_REMOTE).nospace()
            << "slotHistoryApply: committing preview layer name=" << previewLayer->name();
        applied = commitPreviewLayerForApply(m_d->viewManager.data(), image, previewLayer, commitName, beh);
        if (applied)
            clearPreviewTracking();
    } else {
        if (image) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotHistoryApply: removing preview layer(s) before apply";
            removePreviewLayersFromImage(image, m_d->viewManager.data(), m_d->previewLayerId);
            clearPreviewTracking();
        }
        applied = applyResultFileWithBehavior(path, beh, commitName, resultBounds);
    }
    if (applied && entryIndex >= 0) {
        m_d->historyEntries[entryIndex].imageInUse.insert(imageIndex, true);  // §13.28a: star overlay per thumbnail
        clearHistoryListSelection();
        refreshHistoryList();
        scheduleDocumentUiJsonSave();
        if (m_d->canvas) m_d->canvas->updateCanvas();
        m_d->labelStatus->setText(ComfyTr::tr("Applied result to the canvas."));
    } else if (!applied) {
        setStatusMessage(ComfyTr::tr("Could not import image."), true);
    }
}

void ComfyUIRemoteDock::slotHistoryContextMenu(QPoint pos)
{
    QListWidgetItem *item = m_d->listHistory->itemAt(pos);
    if (!item) return;
    m_d->listHistory->setCurrentItem(item);
    int entryIndex = -1;
    QString path = pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0) return;
    const Private::HistoryEntry &entry = m_d->historyEntries.at(entryIndex);
    QMenu menu(this);
    menu.addAction(ComfyTr::tr("Apply"), this, &ComfyUIRemoteDock::slotHistoryApply);
    menu.addAction(ComfyTr::tr("Copy Prompt"), this, &ComfyUIRemoteDock::slotHistoryCopyPrompt);
    menu.addAction(ComfyTr::tr("Copy Prompt (Evaluated)"), this, &ComfyUIRemoteDock::slotHistoryCopyPromptEvaluated);
    menu.addAction(ComfyTr::tr("Copy Strength"), this, &ComfyUIRemoteDock::slotHistoryCopyStrength);
    QAction *copyStyleAction = menu.addAction(ComfyTr::tr("Copy Style"), this, &ComfyUIRemoteDock::slotHistoryCopyStyle);
    if (m_d->comboPreset) {
        int idx = m_d->comboPreset->findText(entry.styleName);
        copyStyleAction->setEnabled(idx >= 0);
    } else {
        copyStyleAction->setEnabled(false);
    }
    menu.addAction(ComfyTr::tr("Copy Seed"), this, &ComfyUIRemoteDock::slotHistoryCopySeed);
    menu.addAction(ComfyTr::tr("Info to Clipboard"), this, &ComfyUIRemoteDock::slotHistoryCopyInfo);
    menu.addSeparator();
    QAction *saveAction = menu.addAction(ComfyTr::tr("Save Image"), this, &ComfyUIRemoteDock::slotHistorySaveImage);
    const bool docSaved = m_d->canvas && m_d->canvas->imageView() && m_d->canvas->imageView()->document()
        && !m_d->canvas->imageView()->document()->path().isEmpty();
    // §13.28: Save Image disabled if document unsaved
    saveAction->setEnabled(!path.isEmpty() && QFile::exists(path) && docSaved);
    menu.addAction(ComfyTr::tr("Discard Image"), this, &ComfyUIRemoteDock::slotHistoryDiscard);
    menu.addSeparator();
    menu.addAction(ComfyTr::tr("Clear History"), this, &ComfyUIRemoteDock::slotHistoryClear);
    menu.exec(m_d->listHistory->mapToGlobal(pos));
}

void ComfyUIRemoteDock::slotHistoryCopyPrompt()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size()) return;
    QString text = m_d->historyEntries.at(entryIndex).prompt;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(text);
    m_d->labelStatus->setText(ComfyTr::tr("Prompt copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryCopyPromptEvaluated()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->historyEntries.at(entryIndex);
    QString evaluated = ComfyUIUtils::stripPromptComments(e.prompt).trimmed();
    evaluated = ComfyUIUtils::evalWildcards(evaluated, static_cast<quint32>(e.seed & 0xFFFFFFFFu));
    ComfyUIUtils::extractLayerPlaceholders(evaluated);  // §13.35: <layer:name> → "Picture {n}"
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(evaluated);
    m_d->labelStatus->setText(ComfyTr::tr("Evaluated prompt copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryCopyStrength()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size()) return;
    int strength = m_d->historyEntries.at(entryIndex).strength;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(QString::number(strength));
    if (m_d->spinStrength)
        m_d->spinStrength->setValue(qBound(1, strength, 100));
    setStatusMessage(ComfyTr::tr("Strength %1% copied and set.", strength));
}

void ComfyUIRemoteDock::slotHistoryCopyStyle()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size() || !m_d->comboPreset) return;
    const QString styleName = m_d->historyEntries.at(entryIndex).styleName;
    int idx = m_d->comboPreset->findText(styleName);
    if (idx >= 0) {
        m_d->comboPreset->setCurrentIndex(idx);
        m_d->labelStatus->setText(ComfyTr::tr("Style set to \"%1\".", styleName));
    }
}

void ComfyUIRemoteDock::slotHistoryCopySeed()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size()) return;
    qint64 seed = m_d->historyEntries.at(entryIndex).seed;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(QString::number(seed));
    m_d->labelStatus->setText(ComfyTr::tr("Seed copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryCopyInfo()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->historyEntries.at(entryIndex);
    QString info = QStringLiteral("Prompt: %1\nNegative: %2\nSize: %3×%4\nSteps: %5 CFG: %6\nStrength: %7%\nSampler: %8\nSeed: %9\nCheckpoint: %10")
        .arg(e.prompt.left(200))
        .arg(e.negative.left(200))
        .arg(e.width).arg(e.height)
        .arg(e.steps).arg(e.cfg)
        .arg(e.strength)
        .arg(e.samplerName)
        .arg(e.seed)
        .arg(e.checkpoint);
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(info);
    m_d->labelStatus->setText(ComfyTr::tr("Info copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistorySaveImage()
{
    int entryIndex = -1;
    QString sourcePath = pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || sourcePath.isEmpty() || !QFile::exists(sourcePath)) {
        setStatusMessage(ComfyTr::tr("No result image to save."), true);
        return;
    }
    if (!m_d->canvas || !m_d->canvas->imageView() || !m_d->canvas->imageView()->document()
        || m_d->canvas->imageView()->document()->path().isEmpty()) {
        setStatusMessage(ComfyTr::tr("Save the document first to save images from history."), true);
        return;
    }
    const Private::HistoryEntry &e = m_d->historyEntries.at(entryIndex);
    QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const QString formatKey = sset.value(QStringLiteral("save_image_format")).toString();
    // §4.7 / §3.5: png | png_small | webp | webp_lossless | jpeg
    QString ext = QStringLiteral("png");
    QString filter = ComfyTr::tr("PNG images (*.png);;All files (*)");
    int quality = 90;
    if (formatKey == QLatin1String("jpeg")) {
        ext = QStringLiteral("jpg");
        filter = ComfyTr::tr("JPEG images (*.jpg);;All files (*)");
        quality = ComfyUIUtils::saveImageQualityJpeg(sset);
    } else if (formatKey == QLatin1String("webp") || formatKey == QLatin1String("webp_lossless")) {
        ext = QStringLiteral("webp");
        filter = ComfyTr::tr("WebP images (*.webp);;All files (*)");
        quality = (formatKey == QLatin1String("webp_lossless")) ? 100 : ComfyUIUtils::saveImageQualityWebp(sset);
    } else if (formatKey == QLatin1String("png_small")) {
        ext = QStringLiteral("png");
        quality = 75;
    }
    // §13.94: save_image_file_name_format — {document_name}, {job_timestamp}, {job_index}, {prompt}
    QString documentName = QStringLiteral("image");
    if (m_d->canvas && m_d->canvas->imageView() && m_d->canvas->imageView()->document()) {
        QString docPath = m_d->canvas->imageView()->document()->path();
        if (!docPath.isEmpty())
            documentName = QFileInfo(docPath).completeBaseName();
    }
    const QString jobTimestamp =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).replace(QLatin1Char(':'), QLatin1Char('-'));
    const QString nameTpl = sset.value(QStringLiteral("save_image_file_name_format")).toString();
    const QString suggestedName = ComfyUIUtils::formatSaveImageFileName(
        nameTpl, documentName, jobTimestamp, entryIndex + 1, e.prompt.trimmed());
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (defaultPath.isEmpty()) defaultPath = QDir::homePath();
    defaultPath += QDir::separator() + suggestedName + QLatin1Char('.') + ext;
    QString savePath = QFileDialog::getSaveFileName(this, ComfyTr::tr("Save image"), defaultPath, filter);
    if (savePath.isEmpty()) return;
    QImage img(sourcePath);
    if (img.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not load image to save."), true);
        return;
    }
    const bool embedMeta = sset.value(QStringLiteral("save_image_metadata")).toBool(true);
    const bool pngMeta = embedMeta && (ext == QLatin1String("png"));
    QImageWriter writer(savePath);
    QByteArray fmt = ext == QLatin1String("jpg") ? QByteArrayLiteral("jpeg") : ext.toLatin1();
    writer.setFormat(fmt);
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("webp"))
        writer.setQuality(quality);
    if (pngMeta) {
        // §13.36: full prompt with LoRA tags (same merge as workflow build)
        const QString posMeta = ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(
            ComfyUIUtils::stripPromptComments(e.prompt).trimmed(), currentStyleLoras());
        const QString negMeta = ComfyUIUtils::stripPromptComments(e.negative).trimmed();
        QString metadata = ComfyUIUtils::createImgMetadata(posMeta, negMeta, e.steps, e.cfg, e.seed, e.width, e.height,
                                                            e.strength, e.samplerName, e.checkpoint);
        writer.setText(QStringLiteral("parameters"), metadata);
    }
    if (writer.write(img)) {
        setStatusMessage(ComfyTr::tr("Saved to %1", savePath));
    } else {
        setStatusMessage(ComfyTr::tr("Could not save: %1", savePath), true);
    }
}

void ComfyUIRemoteDock::slotHistoryDiscard()
{
    int entryIndex = -1;
    int imageIndex = -1;
    QString path = pathForCurrentHistoryRow(&entryIndex, &imageIndex);
    if (entryIndex < 0 || path.isEmpty()) return;
    const QString discardedJobId = m_d->historyEntries.at(entryIndex).jobId;
    const bool previewWasDiscarded = (discardedJobId == m_d->previewHistoryJobId
                                      && imageIndex == m_d->previewHistoryImageIndex);
    // §13.192: confirm_discard_image gates confirmation; Clear History always confirms (see slotHistoryClear)
    const bool confirmDiscard = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ConfirmDiscardImage", true);
    if (confirmDiscard && QMessageBox::warning(this, ComfyTr::tr("Discard image"),
            ComfyTr::tr("Remove this image from history?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;
    Private::HistoryEntry &e = m_d->historyEntries[entryIndex];
    const int slotToEvict = e.documentSlot;
    QStringList paths = e.resultImagePaths;
    if (paths.isEmpty() && !e.resultImagePath.isEmpty())
        paths << e.resultImagePath;
    // §13.131: Discard single image — remove from collection and delete file; only remove entry when no images left
    if (paths.size() <= 1) {
        if (QFile::exists(path)) QFile::remove(path);
        evictDocumentEmbeddedSlotIfAny(slotToEvict);
        m_d->historyEntries.removeAt(entryIndex);
        scheduleDocumentUiJsonSave();
    } else {
        if (imageIndex >= 0 && imageIndex < paths.size()) {
            if (QFile::exists(path)) QFile::remove(path);
            paths.removeAt(imageIndex);
            remapImageInUseAfterRemoval(e.imageInUse, imageIndex);
            e.resultImagePaths = paths;
            if (paths.size() == 1)
                e.resultImagePath = paths.first();
            else
                e.resultImagePath.clear();
            reEmbedHistoryEntryAtIndex(entryIndex);
        }
    }
    if (previewWasDiscarded)
        clearHistoryPreviewState();
    clearHistoryListSelection();
    refreshHistoryList();
    updateHistoryUsageLabel();  // §13.145: update "Currently using X.X MB" if Configure → Performance is open
    setStatusMessage(ComfyTr::tr("Discarded from history."));
}

void ComfyUIRemoteDock::slotHistoryClear()
{
    if (m_d->historyEntries.isEmpty()) return;
    clearHistoryPreviewState();
    // §13.140 / §13.192: Clear History always confirms via QMessageBox.warning; default No
    if (QMessageBox::warning(this, ComfyTr::tr("Clear history"),
            ComfyTr::tr("Discard all %1 generated images from history?", m_d->historyEntries.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        evictDocumentEmbeddedSlotIfAny(e.documentSlot);
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p);
        }
    }
    m_d->historyEntries.clear();
    scheduleDocumentUiJsonSave();
    refreshHistoryList();
    updateHistoryUsageLabel();  // §13.145: update "Currently using X.X MB" if Configure → Performance is open
    m_d->btnHistoryReRun->setEnabled(false);
    m_d->btnHistoryApply->setEnabled(false);
    setStatusMessage(ComfyTr::tr("History cleared."));
}

void ComfyUIRemoteDock::slotHistoryReRun()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->historyEntries.at(entryIndex);
    m_d->editPrompt->setPlainText(e.prompt);
    m_d->editNegative->setPlainText(e.negative);
    if (m_d->regionPromptWidget)
        m_d->regionPromptWidget->refreshRootPromptFromDock();
    m_d->spinWidth->setValue(e.width);
    m_d->spinHeight->setValue(e.height);
    m_d->spinSteps->setValue(e.steps);
    m_d->spinCfg->setValue(e.cfg);
    if (m_d->spinStrength)
        m_d->spinStrength->setValue(qBound(1, e.strength, 100));
    m_d->comboSampler->setCurrentText(e.samplerName.isEmpty() ? QString("euler") : e.samplerName);
    m_d->checkFixedSeed->setChecked(true);
    m_d->spinSeed->setValue(static_cast<int>(e.seed));
    int i = m_d->comboCheckpoint->findText(e.checkpoint);
    if (i >= 0) m_d->comboCheckpoint->setCurrentIndex(i);
    else m_d->comboCheckpoint->setCurrentText(e.checkpoint);
    slotGenerate();
}

// §13.184: create_result_layer — apply result image to each region layer per ApplyRegionBehavior
bool ComfyUIRemoteDock::applyResultToRegions(const QString &resultPath, int entryIndex, const QString &regionApplyBehavior)
{
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size())
        return false;
    const Private::HistoryEntry &entry = m_d->historyEntries.at(entryIndex);
    if (entry.regionLayerNames.isEmpty())
        return false;
    if (!m_d->canvas || !m_d->viewManager || !m_d->viewManager->imageManager())
        return false;
    KisImageSP image = m_d->canvas->image().toStrongRef();
    if (!image || !image->rootLayer())
        return false;
    QString behavior = regionApplyBehavior;
    if (behavior == QLatin1String("none"))
        return false;
    KisNodeSP root = image->rootLayer();
    bool any = false;
    for (const QString &layerName : entry.regionLayerNames) {
        KisNodeSP regionNode = KisLayerUtils::findNodeByName(root, layerName);
        if (!regionNode)
            continue;
        KisLayerSP regionLayer = dynamic_cast<KisLayer*>(regionNode.data());
        if (!regionLayer)
            continue;
        if (behavior == QLatin1String("layer_group")) {
            KisGroupLayerSP group = new KisGroupLayer(image, ComfyTr::tr("Result: %1", layerName), OPACITY_OPAQUE_U8, image->colorSpace());
            if (!image->addNode(group, regionLayer->parent(), regionLayer))
                continue;
            qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(resultPath), "KisPaintLayer");
            if (n > 0) {
                KisNodeSP newLayer = root->firstChild();
                if (newLayer) {
                    image->removeNode(newLayer);
                    image->addNode(newLayer, group, KisNodeSP());
                    any = true;
                }
            }
        } else if (behavior == QLatin1String("replace")) {
            qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(resultPath), "KisPaintLayer");
            if (n > 0) {
                KisNodeSP newLayer = root->firstChild();
                if (newLayer) {
                    image->removeNode(newLayer);
                    image->addNode(newLayer, regionLayer->parent(), regionLayer);
                    KisLayerSP newLayerLayer = dynamic_cast<KisLayer*>(newLayer.data());
                    if (newLayerLayer) {
                        image->mergeDown(newLayerLayer, nullptr);
                        any = true;
                    }
                }
            }
        } else if (behavior == QLatin1String("no_hide")) {
            qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(resultPath), "KisPaintLayer");
            if (n > 0) {
                KisNodeSP newLayer = root->firstChild();
                if (newLayer) {
                    image->removeNode(newLayer);
                    image->addNode(newLayer, regionLayer->parent(), regionLayer);
                    any = true;
                }
            }
        } else if (behavior == QLatin1String("transparency_mask")) {
            // §13.184: Full transparency_mask would use region_layer.get_mask + create_mask("Transparency Mask", ...).
            // Fallback: add result as new layer above region so the option has effect.
            qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(resultPath), "KisPaintLayer");
            if (n > 0) {
                KisNodeSP newLayer = root->firstChild();
                if (newLayer) {
                    image->removeNode(newLayer);
                    image->addNode(newLayer, regionLayer->parent(), regionLayer);
                    any = true;
                }
            }
        }
    }
    return any;
}

namespace {
QUuid comfyUiLayerUuidFromIdString(const QString &layerId)
{
    const QString lid = layerId.trimmed();
    if (lid.isEmpty())
        return QUuid();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QUuid::fromString(QStringView{lid});
#else
    QString s = lid;
    if (s.length() == 32 && !s.contains(QLatin1Char('-'))) {
        s = QStringLiteral("{%1-%2-%3-%4-%5}")
                .arg(s.mid(0, 8), s.mid(8, 4), s.mid(12, 4), s.mid(16, 4), s.mid(20, 12));
    }
    return QUuid(s);
#endif
}
} // namespace

bool ComfyUIRemoteDock::tryApplyAnimationSingleFrameToTargetLayer(const QString &localPath, bool timelineMismatch)
{
    if (localPath.isEmpty() || !QFile::exists(localPath))
        return false;
    if (!m_d->comboWorkspace || m_d->comboWorkspace->currentIndex() != 3)
        return false;
    if (!m_d->radioSingleFrame || !m_d->radioSingleFrame->isChecked())
        return false;
    if (!m_d->comboAnimationTargetLayer)
        return false;
    const QString layerId = m_d->comboAnimationTargetLayer->currentData().toString().trimmed();
    if (layerId.isEmpty())
        return false;
    if (!m_d->viewManager)
        return false;
    KisImageSP kisImage = m_d->viewManager->image();
    if (!kisImage)
        return false;

    const QUuid uid = comfyUiLayerUuidFromIdString(layerId);
    if (uid.isNull())
        return false;

    KisNodeSP root = kisImage->rootLayer();
    if (!root)
        return false;
    KisNodeSP node = KisLayerUtils::findNodeByUuid(root, uid);
    if (!node) {
        setStatusMessage(ComfyTr::tr("Animation target layer was not found."), true);
        return false;
    }
    auto *pl = qobject_cast<KisPaintLayer *>(node.data());
    if (!pl)
        return false;

    QImage qimg;
    if (!qimg.load(localPath) || qimg.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not load result image for the animation target layer."), true);
        return false;
    }

    KisPaintDeviceSP dst = pl->paintDevice();
    if (!dst)
        return false;

    QRect dstRect = dst->extent();
    QSize sz = dstRect.size();
    if (!sz.isValid() || sz.isEmpty())
        sz = QSize(kisImage->width(), kisImage->height());
    QImage scaled = qimg.scaled(sz, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (scaled.format() != QImage::Format_ARGB32)
        scaled = scaled.convertToFormat(QImage::Format_ARGB32);

    KisPaintDeviceSP tmp = new KisPaintDevice(dst->colorSpace());
    tmp->convertFromQImage(scaled, nullptr);

    const QPoint dstPt = dstRect.isEmpty() ? QPoint(0, 0) : dstRect.topLeft();
    const QRect srcRect(QPoint(0, 0), scaled.size());

    KisImageBarrierLock barrier(kisImage);
    Q_UNUSED(barrier);

    KisPainter painter(dst);
    painter.setCompositeOpId(COMPOSITE_COPY);
    painter.beginTransaction(kundo2_noi18n(ComfyTr::tr("ComfyUI animation frame")));
    painter.bitBlt(dstPt, tmp, srcRect);
    painter.endTransaction(kisImage->undoAdapter());

    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    if (timelineMismatch) {
        setStatusMessage(
            ComfyTr::tr("Frame written to layer \"%1\". Generated frame does not match current time.", pl->name()),
            false,
            true);
    } else {
        setStatusMessage(ComfyTr::tr("Frame written to layer \"%1\".", pl->name()));
    }
    return true;
}

bool ComfyUIRemoteDock::applyResultFileWithBehavior(const QString &localPath,
                                                    const QString &applyBehavior,
                                                    const QString &committedLayerName,
                                                    const QRect &resultBounds)
{
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "applyResultFileWithBehavior ENTER path=" << localPath
        << " behavior=" << applyBehavior
        << " committedLayerName=" << committedLayerName
        << " exists=" << (localPath.isEmpty() ? false : QFile::exists(localPath));
    if (localPath.isEmpty() || !QFile::exists(localPath))
        return false;
    if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "applyResultFileWithBehavior: viewManager / imageManager unavailable";
        return false;
    }
    KisImageSP image = m_d->viewManager->image();
    if (!image) {
        qCWarning(KIS_COMFYUI_REMOTE) << "applyResultFileWithBehavior: image is null";
        return false;
    }

    KisLayerSP activeBefore = m_d->viewManager->activeLayer();
    KisLayerSP imported;
    if (!resultBounds.isEmpty()) {
        const QString initialName = committedLayerName.isEmpty() ? QStringLiteral("[Generated] result") : committedLayerName;
        KisPaintLayerSP pl(new KisPaintLayer(image, initialName, OPACITY_OPAQUE_U8));
        if (!loadImageFileIntoPaintLayer(pl.data(), image, localPath, resultBounds.topLeft()))
            return false;
        KisNodeSP root = image->rootLayer();
        if (!root)
            return false;
        KisNodeSP above = topDirectRootChild(root);
        if (m_d->viewManager->nodeManager()) {
            KisNodeList nodes;
            nodes.append(pl);
            m_d->viewManager->nodeManager()->addNodesDirect(nodes, root, above);
            m_d->viewManager->nodeManager()->slotNonUiActivatedNode(pl);
        } else {
            image->addNode(pl, root, above);
        }
        image->waitForDone();
        imported = pl;
        qCWarning(KIS_COMFYUI_REMOTE) << "applyResultFileWithBehavior: created offset layer bounds=" << resultBounds;
    } else {
        ensureActiveLayerValidForImport(m_d->viewManager.data(), image);
        const qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(localPath), QStringLiteral("KisPaintLayer"));
        qCWarning(KIS_COMFYUI_REMOTE) << "applyResultFileWithBehavior: importImage returned" << n;
        if (n <= 0)
            return false;
        imported = m_d->viewManager->activeLayer();
    }
    QString beh = applyBehavior;
    if (beh.isEmpty())
        beh = QStringLiteral("layer");

    if (beh == QLatin1String("replace")) {
        if (activeBefore && imported && imported != activeBefore) {
            image->mergeDown(imported, nullptr);
        }
    } else {
        placeImportedLayerForBehavior(m_d->viewManager.data(), image, imported, activeBefore, beh);
    }
    if (!committedLayerName.isEmpty() && imported)
        imported->setName(committedLayerName);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    return true;
}

void ComfyUIRemoteDock::handleGenerationFinished(const QString &resultImagePath, bool skipAutoActions)
{
    refreshHistoryList(true);
    updateHistoryUsageLabel();
    if (skipAutoActions || resultImagePath.isEmpty() || !QFile::exists(resultImagePath))
        return;
    if (!m_d->historyEntries.isEmpty() && !m_d->historyEntries.last().regionLayerNames.isEmpty())
        return;

    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QString action = s.value(QStringLiteral("generation_finished_action")).toString();
    if (action.isEmpty())
        action = QStringLiteral("preview");

    if (action == QLatin1String("preview")) {
        if (m_d->listHistory) {
            for (int i = m_d->listHistory->count() - 1; i >= 0; --i) {
                QListWidgetItem *item = m_d->listHistory->item(i);
                if (!item || historyEntryIsHeaderItem(item))
                    continue;
                m_d->listHistory->setCurrentItem(item);
                showHistoryPreviewForItem(item);
                break;
            }
        }
        return;
    }
    if (action == QLatin1String("none"))
        return;
    if (action != QLatin1String("apply"))
        return;

    QString beh = applyBehaviorFromSettings(m_d.data());
    QRect resultBounds;
    if (!m_d->historyEntries.isEmpty() && m_d->historyEntries.last().hasMask
        && !m_d->historyEntries.last().contextBounds.isEmpty()) {
        resultBounds = m_d->historyEntries.last().contextBounds;
    }
    if (applyResultFileWithBehavior(resultImagePath, beh, QString(), resultBounds) && !m_d->historyEntries.isEmpty()) {
        m_d->historyEntries.last().imageInUse.insert(0, true);
        clearHistoryListSelection();
        refreshHistoryList();
        scheduleDocumentUiJsonSave();
        m_d->labelStatus->setText(ComfyTr::tr("Generation finished — result applied."));
    }
}

void ComfyUIRemoteDock::removeDocumentHistoryBlobForSlot(KisImageSP img, int slot)
{
    if (!img || slot < 0)
        return;
    const QString withW = ComfyUIUtils::documentAnnotationKey(ComfyUIUtils::historyResultLogicalKey(slot));
    const QString noW = ComfyUIUtils::documentAnnotationKey(QStringLiteral("result%1").arg(slot));
    img->removeAnnotation(withW);
    img->removeAnnotation(noW);
}

void ComfyUIRemoteDock::evictDocumentEmbeddedSlotIfAny(int documentSlot)
{
    if (documentSlot < 0)
        return;
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    removeDocumentHistoryBlobForSlot(img, documentSlot);
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::scheduleDocumentUiJsonSave()
{
    if (!m_d->documentUiJsonSaveTimer) {
        m_d->documentUiJsonSaveTimer = new QTimer(this);
        m_d->documentUiJsonSaveTimer->setSingleShot(true);
        connect(m_d->documentUiJsonSaveTimer, &QTimer::timeout, this, [this]() {
            flushDocumentUiJsonNow();
        });
    }
    m_d->documentUiJsonSaveTimer->start(1000);
}

void ComfyUIRemoteDock::flushDocumentUiJsonNow()
{
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    ui.insert(QStringLiteral("version"), ComfyUIUtils::persistenceFormatVersion);
    // §13.189: mirror slices that we store as separate annotations into ui.json for Python-plugin / .kra parity.
    {
        KisAnnotationSP annInpaint = img->annotation(ComfyUIUtils::inpaintWorkspaceAnnotationKey());
        if (annInpaint && !annInpaint->annotation().isEmpty()) {
            const QJsonObject slice = QJsonDocument::fromJson(annInpaint->annotation()).object();
            if (!slice.isEmpty())
                ui.insert(QStringLiteral("inpaint"), slice);
        }
        KisAnnotationSP annLive = img->annotation(ComfyUIUtils::liveWorkspaceAnnotationKey());
        if (annLive && !annLive->annotation().isEmpty()) {
            const QJsonObject slice = QJsonDocument::fromJson(annLive->annotation()).object();
            if (!slice.isEmpty())
                ui.insert(QStringLiteral("live"), slice);
        }
        KisAnnotationSP annPreview = img->annotation(ComfyUIUtils::previewLayerAnnotationKey());
        if (annPreview && !annPreview->annotation().isEmpty()) {
            const QString id = QString::fromUtf8(annPreview->annotation()).trimmed();
            if (!id.isEmpty())
                ui.insert(QStringLiteral("preview_layer"), id);
        }
    }
    mergeDocumentModelIntoUiJson(&ui, img);
    const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const QString histFmt = sset.value(QStringLiteral("history_format")).toString(QStringLiteral("webp"));
    QJsonArray hist;
    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        if (e.documentSlot < 0)
            continue;
        hist.append(historyEntryToJsonObject(e, histFmt));
    }
    ui.insert(QStringLiteral("history"), hist);
    ui.insert(QStringLiteral("animation"), animationWorkspaceToJson());
    const QString key = ComfyUIUtils::documentUiJsonAnnotationKey();
    img->removeAnnotation(key);
    const QByteArray bytes = QJsonDocument(ui).toJson(QJsonDocument::Compact);
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("ui.json")), bytes)));
}

void ComfyUIRemoteDock::persistTopHistoryEntryToDocument(bool skipForAnimationFrame)
{
    if (skipForAnimationFrame || m_d->historyEntries.isEmpty())
        return;
    Private::HistoryEntry &entry = m_d->historyEntries.last();
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    QStringList paths = entry.resultImagePaths;
    if (paths.isEmpty() && !entry.resultImagePath.isEmpty())
        paths << entry.resultImagePath;
    if (paths.isEmpty())
        return;
    for (const QString &p : paths) {
        if (!QFile::exists(p))
            return;
    }
    QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const int slot = ComfyUIUtils::maxHistorySlotFromDocument(img, ui) + 1;
    const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const ComfyUIUtils::HistoryImageEncodeResult enc = ComfyUIUtils::encodeHistoryImagesFromPaths(paths, sset);
    if (enc.data.isEmpty())
        return;
    const QString logical = ComfyUIUtils::historyResultLogicalKey(slot);
    const QString annKey = ComfyUIUtils::documentAnnotationKey(logical);
    img->removeAnnotation(annKey);
    img->removeAnnotation(ComfyUIUtils::documentAnnotationKey(QStringLiteral("result%1").arg(slot)));
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(annKey, ComfyUIUtils::documentAnnotationDescription(logical), enc.data)));
    entry.documentSlot = slot;
    entry.documentBlobEndOffsets = enc.offsets;
    scheduleDocumentUiJsonSave();
    pruneDocumentEmbeddedHistoryIfNeeded();
}

void ComfyUIRemoteDock::reEmbedHistoryEntryAtIndex(int entryIndex)
{
    if (entryIndex < 0 || entryIndex >= m_d->historyEntries.size())
        return;
    Private::HistoryEntry &entry = m_d->historyEntries[entryIndex];
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    QStringList paths = entry.resultImagePaths;
    if (paths.isEmpty() && !entry.resultImagePath.isEmpty())
        paths << entry.resultImagePath;
    if (paths.isEmpty())
        return;
    for (const QString &p : paths) {
        if (!QFile::exists(p))
            return;
    }
    const int oldSlot = entry.documentSlot;
    if (oldSlot >= 0)
        removeDocumentHistoryBlobForSlot(img, oldSlot);
    QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const int slot = ComfyUIUtils::maxHistorySlotFromDocument(img, ui) + 1;
    const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
    const ComfyUIUtils::HistoryImageEncodeResult enc = ComfyUIUtils::encodeHistoryImagesFromPaths(paths, sset);
    if (enc.data.isEmpty())
        return;
    const QString logical = ComfyUIUtils::historyResultLogicalKey(slot);
    const QString annKey = ComfyUIUtils::documentAnnotationKey(logical);
    img->removeAnnotation(annKey);
    img->removeAnnotation(ComfyUIUtils::documentAnnotationKey(QStringLiteral("result%1").arg(slot)));
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(annKey, ComfyUIUtils::documentAnnotationDescription(logical), enc.data)));
    entry.documentSlot = slot;
    entry.documentBlobEndOffsets = enc.offsets;
    scheduleDocumentUiJsonSave();
    pruneDocumentEmbeddedHistoryIfNeeded();
}

void ComfyUIRemoteDock::pruneDocumentEmbeddedHistoryIfNeeded()
{
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    const qint64 limit = ComfyUIUtils::documentEmbeddedHistoryLimitBytes(ComfyUIUtils::loadSettingsJson());
    int guard = 0;
    while (ComfyUIUtils::documentEmbeddedHistoryStorageBytes(img) > limit && guard++ < 512) {
        int minIdx = -1;
        int minSlot = INT_MAX;
        for (int i = 0; i < m_d->historyEntries.size(); ++i) {
            const int s = m_d->historyEntries.at(i).documentSlot;
            if (s >= 0 && s < minSlot) {
                minSlot = s;
                minIdx = i;
            }
        }
        if (minIdx < 0)
            break;
        Private::HistoryEntry old = m_d->historyEntries.takeAt(minIdx);
        removeDocumentHistoryBlobForSlot(img, old.documentSlot);
        QStringList paths = old.resultImagePaths;
        if (paths.isEmpty() && !old.resultImagePath.isEmpty())
            paths << old.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty() && QFile::exists(p))
                QFile::remove(p);
        }
    }
    scheduleDocumentUiJsonSave();
    refreshHistoryList();
    updateHistoryUsageLabel();
}

void ComfyUIRemoteDock::loadDocumentHistoryFromAnnotations()
{
    m_d->historyEntries.clear();
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img)
        return;
    const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const QJsonArray h = ui.value(QStringLiteral("history")).toArray();
    for (const QJsonValue &v : h) {
        const QJsonObject ho = v.toObject();
        const int slot = ho.value(QStringLiteral("slot")).toInt(-1);
        if (slot < 0)
            continue;
        const auto keys = ComfyUIUtils::documentAnnotationKeysWithFallback(QStringLiteral("result%1").arg(slot), QStringLiteral("webp"));
        KisAnnotationSP ann = img->annotation(keys.first);
        if (!ann)
            ann = img->annotation(keys.second);
        if (!ann || ann->annotation().isEmpty())
            continue;
        Private::HistoryEntry e;
        if (!historyJsonToEntry(ho, ann->annotation(), img, &e))
            continue;
        m_d->historyEntries.append(e);
    }
    refreshHistoryList();
    updateHistoryUsageLabel();
}
