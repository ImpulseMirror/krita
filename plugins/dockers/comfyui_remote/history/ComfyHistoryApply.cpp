/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyHistoryInternal.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlLayer.h"
#include "ComfyResources.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyLiveRunnerInternal.h"
#include "ComfyPrepareLiveWorkflow.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QLoggingCategory>
#include <QMenu>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QUrl>
#include <QUuid>

#include <klocalizedstring.h>
#include <KoColorSpaceConstants.h>
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
#include <kis_image_animation_interface.h>
#include <commands/KisNodeRenameCommand.h>
#include <KisViewManager.h>

using namespace ComfyHistoryInternal;

// §13.184: create_result_layer — apply result image to each region layer per ApplyRegionBehavior
bool ComfyUIRemoteDock::applyResultToNamedRegionLayers(const QString &resultPath,
                                                       const QStringList &layerNames,
                                                       const QString &regionApplyBehavior)
{
    if (layerNames.isEmpty())
        return false;
    if (!m_d->canvas || !m_d->viewManager || !m_d->viewManager->imageManager())
        return false;
    KisImageSP image = m_d->canvas->image().toStrongRef();
    if (!image || !image->rootLayer())
        return false;
    QString behavior = regionApplyBehavior;
    if (behavior.isEmpty())
        behavior = QStringLiteral("replace");
    if (behavior == QLatin1String("none"))
        return false;
    KisNodeSP root = image->rootLayer();
    bool any = false;
    for (const QString &layerName : layerNames) {
        KisNodeSP regionNode = KisLayerUtils::findNodeByName(root, layerName);
        if (!regionNode)
            continue;
        KisLayerSP regionLayer = dynamic_cast<KisLayer *>(regionNode.data());
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
                    KisLayerSP newLayerLayer = dynamic_cast<KisLayer *>(newLayer.data());
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
    if (any) {
        refreshCanvasProjectionAfterApply(image, KisLayerSP());
    }
    return any;
}

bool ComfyUIRemoteDock::applyResultToRegions(const QString &resultPath, int entryIndex, const QString &regionApplyBehavior)
{
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size())
        return false;
    const Private::HistoryEntry &entry = m_d->history.historyEntries.at(entryIndex);
    return applyResultToNamedRegionLayers(resultPath, entry.regionLayerNames, regionApplyBehavior);
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

QString ComfyUIRemoteDock::resolveLiveApplyImagePath() const
{
    if (m_d->liveRt.lastLiveResultImagePath.isEmpty())
        return QString();

    const ComfyPrepareLiveWorkflow::Result &prep = m_d->liveRt.livePrepared;
    if (!prep.hasMask || m_d->liveRt.lastLiveRawResultImagePath.isEmpty() || !m_d->viewManager)
        return m_d->liveRt.lastLiveResultImagePath;

    QImage raw;
    if (!raw.load(m_d->liveRt.lastLiveRawResultImagePath))
        return m_d->liveRt.lastLiveResultImagePath;

    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return m_d->liveRt.lastLiveResultImagePath;

    const QImage composite =
        ComfyLiveRunnerInternal::compositeLiveServerResultAtApply(raw, prep, image);
    if (composite.isNull())
        return m_d->liveRt.lastLiveResultImagePath;

    const QString path =
        QDir(ComfyUIUtils::historyCacheDir()).filePath(QStringLiteral("last_live_apply_composite.png"));
    QFile::remove(path);
    if (!composite.save(path))
        return m_d->liveRt.lastLiveResultImagePath;

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE apply: recomposited with fresh context grow=") << prep.preprocess.grow
        << QStringLiteral(" feather=") << prep.preprocess.feather << QStringLiteral(" blend=") << prep.preprocess.blend;
    return path;
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

    KisImageBarrierLock barrier(image);
    image->waitForDone();

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
        image->addNode(pl, root, above);
        image->waitForDone();
        imported = pl;
        qCWarning(KIS_COMFYUI_REMOTE) << "applyResultFileWithBehavior: created offset layer bounds=" << resultBounds;
    } else {
        const QString initialName =
            committedLayerName.isEmpty() ? QStringLiteral("[Generated] result") : committedLayerName;
        KisPaintLayerSP pl(new KisPaintLayer(image, initialName, OPACITY_OPAQUE_U8));
        if (!loadImageFileIntoPaintLayer(pl.data(), image, localPath, QPoint()))
            return false;
        KisNodeSP root = image->rootLayer();
        if (!root)
            return false;
        KisNodeSP above = topDirectRootChild(root);
        image->addNode(pl, root, above);
        image->waitForDone();
        imported = pl;
        qCWarning(KIS_COMFYUI_REMOTE) << "applyResultFileWithBehavior: created full-canvas layer";
    }
    QString beh = applyBehavior;
    if (beh.isEmpty())
        beh = QStringLiteral("layer");

    KisLayerSP nameTarget = imported;
    if (beh == QLatin1String("replace")) {
        if (activeBefore && imported && imported != activeBefore)
            mergeImportedForReplace(m_d->viewManager.data(), image, imported, activeBefore);
        if (activeBefore && layerStillInDocument(image, activeBefore))
            nameTarget = activeBefore;
        activateAppliedResultLayer(m_d->viewManager.data(), image, imported, activeBefore, beh);
    } else {
        placeImportedLayerForBehavior(m_d->viewManager.data(), image, imported, activeBefore, beh);
        activateAppliedResultLayer(m_d->viewManager.data(), image, imported, activeBefore, beh);
    }
    image->waitForDone();
    if (!committedLayerName.isEmpty() && nameTarget && layerStillInDocument(image, nameTarget))
        nameTarget->setName(committedLayerName);
    refreshCanvasProjectionAfterApply(image, nameTarget);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    return true;
}

void ComfyUIRemoteDock::handleGenerationFinished(const QString &resultImagePath, bool skipAutoActions)
{
    refreshHistoryList(true);
    updateHistoryUsageLabel();
    updateGenerateOptions();
    if (skipAutoActions || resultImagePath.isEmpty() || !QFile::exists(resultImagePath))
        return;
    if (!m_d->history.historyEntries.isEmpty() && !m_d->history.historyEntries.last().regionLayerNames.isEmpty())
        return;

    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QString action = s.value(QStringLiteral("generation_finished_action")).toString();
    if (action.isEmpty())
        action = QStringLiteral("preview");

    if (action == QLatin1String("preview")) {
        if (m_d->history.listHistory) {
            for (int i = m_d->history.listHistory->count() - 1; i >= 0; --i) {
                QListWidgetItem *item = m_d->history.listHistory->item(i);
                if (!item || historyEntryIsHeaderItem(item))
                    continue;
                m_d->history.listHistory->setCurrentItem(item);
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
    if (!m_d->history.historyEntries.isEmpty()) {
        const Private::HistoryEntry &entry = m_d->history.historyEntries.last();
        if (entry.hasMask) {
            const QImage previewImg =
                cachedHistoryPreviewImage(resultImagePath, &m_d->history.historyPreviewImageCache);
            const QPoint offset = historyMaskedPreviewOffset(entry, previewImg.size());
            if (!previewImg.isNull())
                resultBounds = QRect(offset, previewImg.size());
            else if (!entry.contextBounds.isEmpty())
                resultBounds = entry.contextBounds;
        }
    }
    if (applyResultFileWithBehavior(resultImagePath, beh, QString(), resultBounds) && !m_d->history.historyEntries.isEmpty()) {
        m_d->history.historyEntries.last().imageInUse.insert(0, true);
        clearHistoryListSelection();
        refreshHistoryList();
        scheduleDocumentUiJsonSave();
        m_d->labelStatus->setText(ComfyTr::tr("Generation finished — result applied."));
    }
}
