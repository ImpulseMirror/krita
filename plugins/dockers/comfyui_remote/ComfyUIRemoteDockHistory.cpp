/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
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
    return true;
}

} // namespace

void ComfyUIRemoteDock::slotHistoryItemSelected()
{
    bool hasSelection = m_d->listHistory->currentRow() >= 0;
    m_d->btnHistoryReRun->setEnabled(hasSelection);
    m_d->btnHistoryApply->setEnabled(hasSelection);
}

void ComfyUIRemoteDock::slotHistoryApply()
{
    // §10.1: Apply action — only Generate (0) and Live (2) workspaces
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2)
            return;
    }
    int entryIndex = -1;
    int imageIndex = -1;
    QString path = pathForCurrentHistoryRow(&entryIndex, &imageIndex);
    if (path.isEmpty() || !QFile::exists(path)) {
        setStatusMessage(ComfyTr::tr("No result image to apply."), true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    // §13.184 / §3.5: apply_region_behavior vs apply_region_behavior_live when Live workspace is active
    if (entryIndex >= 0 && !m_d->historyEntries[entryIndex].regionLayerNames.isEmpty()) {
        const QJsonObject sset = ComfyUIUtils::loadSettingsJson();
        const bool liveWs = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
        QString behavior = liveWs ? sset.value(QStringLiteral("apply_region_behavior_live")).toString()
                                    : sset.value(QStringLiteral("apply_region_behavior")).toString();
        if (behavior.isEmpty())
            behavior = liveWs ? QStringLiteral("replace") : QStringLiteral("layer_group");
        if (behavior != QLatin1String("none") && applyResultToRegions(path, entryIndex, behavior)) {
            m_d->historyEntries[entryIndex].imageInUse.insert(imageIndex, true);
            refreshHistoryList();
            scheduleDocumentUiJsonSave();
            if (m_d->canvas) m_d->canvas->updateCanvas();
            m_d->labelStatus->setText(ComfyTr::tr("Applied result to region layers."));
            return;
        }
    }
    QString beh = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("apply_behavior")).toString();
    if (beh.isEmpty()) beh = QStringLiteral("layer");
    if (applyResultFileWithBehavior(path, beh) && entryIndex >= 0) {
        m_d->historyEntries[entryIndex].imageInUse.insert(imageIndex, true);  // §13.28a: star overlay per thumbnail
        refreshHistoryList();
        scheduleDocumentUiJsonSave();
        if (m_d->canvas) m_d->canvas->updateCanvas();
        m_d->labelStatus->setText(ComfyTr::tr("Applied result to the canvas."));
    } else {
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
        const QString posMeta =
            ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(ComfyUIUtils::stripPromptComments(e.prompt).trimmed());
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
    refreshHistoryList();
    updateHistoryUsageLabel();  // §13.145: update "Currently using X.X MB" if Configure → Performance is open
    setStatusMessage(ComfyTr::tr("Discarded from history."));
}

void ComfyUIRemoteDock::slotHistoryClear()
{
    if (m_d->historyEntries.isEmpty()) return;
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
    painter.beginTransaction(kundo2_ComfyTr::tr("ComfyUI animation frame"));
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

bool ComfyUIRemoteDock::applyResultFileWithBehavior(const QString &localPath, const QString &applyBehavior)
{
    if (localPath.isEmpty() || !QFile::exists(localPath))
        return false;
    if (!m_d->viewManager || !m_d->viewManager->imageManager())
        return false;
    KisImageSP image = m_d->viewManager->image();
    if (!image)
        return false;

    KisLayerSP activeBefore = m_d->viewManager->activeLayer();
    const qint32 n = m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(localPath), QStringLiteral("KisPaintLayer"));
    if (n <= 0)
        return false;

    KisLayerSP imported = m_d->viewManager->activeLayer();
    QString beh = applyBehavior;
    if (beh.isEmpty())
        beh = QStringLiteral("layer");

    if (beh == QLatin1String("replace")) {
        if (activeBefore && imported && imported != activeBefore) {
            image->mergeDown(imported, nullptr);
        }
    } else if (beh == QLatin1String("layer")) {
        KisNodeSP root = image->rootLayer();
        KisNodeSP importedNode = imported;
        if (root && importedNode) {
            image->removeNode(importedNode);
            KisNodeSP first = root->firstChild();
            if (first)
                image->addNode(importedNode, root, first);
            else
                image->addNode(importedNode, root, KisNodeSP());
        }
    }
    // layer_active: default import placement (above prior active) matches spec
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
    return true;
}

void ComfyUIRemoteDock::handleGenerationFinished(const QString &resultImagePath, bool skipAutoActions)
{
    refreshHistoryList();
    if (m_d->listHistory && m_d->listHistory->count() > 0)
        m_d->listHistory->setCurrentRow(0);
    updateHistoryUsageLabel();
    if (skipAutoActions || resultImagePath.isEmpty() || !QFile::exists(resultImagePath))
        return;
    if (!m_d->historyEntries.isEmpty() && !m_d->historyEntries.first().regionLayerNames.isEmpty())
        return;

    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QString action = s.value(QStringLiteral("generation_finished_action")).toString();
    if (action.isEmpty())
        action = QStringLiteral("preview");
    if (action == QLatin1String("none") || action == QLatin1String("preview"))
        return;
    if (action != QLatin1String("apply"))
        return;

    QString beh = s.value(QStringLiteral("apply_behavior")).toString();
    if (beh.isEmpty())
        beh = QStringLiteral("layer");
    if (applyResultFileWithBehavior(resultImagePath, beh) && !m_d->historyEntries.isEmpty()) {
        m_d->historyEntries[0].imageInUse.insert(0, true);
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
    Private::HistoryEntry &entry = m_d->historyEntries.first();
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
