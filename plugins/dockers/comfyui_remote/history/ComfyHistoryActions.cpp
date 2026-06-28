/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyHistoryInternal.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyControlLayer.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyResources.h"
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
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <klocalizedstring.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <kis_image_manager.h>
#include <KisDocument.h>
#include <kis_image.h>
#include <kis_layer.h>

using namespace ComfyHistoryInternal;

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
        << (m_d->history.listHistory ? m_d->history.listHistory->currentRow() : -1)
        << " count=" << (m_d->history.listHistory ? m_d->history.listHistory->count() : -1)
        << " workspace=" << (m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1)
        << " previewHistoryJobId=" << m_d->history.previewHistoryJobId
        << " previewHistoryImageIndex=" << m_d->history.previewHistoryImageIndex
        << " currentPreviewLayerId=" << m_d->previewLayerId;
    // §10.1: Apply action — only Generate (0) and Live (2) workspaces
    if (m_d->comboWorkspace) {
        const int ws = m_d->comboWorkspace->currentIndex();
        if (ws != 0 && ws != 2) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotHistoryApply: workspace gate, ws=" << ws << "; aborting";
            return;
        }
    }
    if (m_d->history.listHistory) {
        if (!item && !m_d->history.previewHistoryJobId.isEmpty()) {
            item = findHistoryListItem(m_d->history.listHistory,
                                       m_d->history.previewHistoryJobId,
                                       m_d->history.previewHistoryImageIndex);
        }
        if (!item)
            item = m_d->history.listHistory->currentItem();
        if (item)
            m_d->history.listHistory->setCurrentItem(item);
        else if (m_d->history.listHistory->currentRow() < 0 && m_d->history.listHistory->count() > 0) {
            for (int i = m_d->history.listHistory->count() - 1; i >= 0; --i) {
                QListWidgetItem *it = m_d->history.listHistory->item(i);
                if (it && !historyEntryIsHeaderItem(it)) {
                    m_d->history.listHistory->setCurrentItem(it);
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
    QString path = historyPathForListItem(item, m_d->history.historyEntries, &entryIndex, &imageIndex);
    if (path.isEmpty() && !m_d->history.previewHistoryJobId.isEmpty()) {
        path = historyPathForIdentity(m_d->history.previewHistoryJobId,
                                      m_d->history.previewHistoryImageIndex,
                                      m_d->history.historyEntries,
                                      &entryIndex,
                                      &imageIndex);
    }
    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "slotHistoryApplyForItem resolved entryIndex=" << entryIndex
        << " imageIndex=" << imageIndex
        << " path=" << path
        << " fileExists=" << (path.isEmpty() ? false : QFile::exists(path));
    if (path.isEmpty() || !QFile::exists(path)) {
        if (!m_d->history.previewHistoryJobId.isEmpty())
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
    const QString applyJobId = item ? item->data(Qt::UserRole).toString() : m_d->history.previewHistoryJobId;
    const int applyImageIndex = item ? item->data(Qt::UserRole + 1).toInt() : m_d->history.previewHistoryImageIndex;
    const bool canCommitPreview = !m_d->previewLayerId.isEmpty()
                                  && applyJobId == m_d->history.previewHistoryJobId
                                  && applyImageIndex == m_d->history.previewHistoryImageIndex;

    KisLayerSP previewLayer;
    if (image && canCommitPreview)
        previewLayer = findPreviewLayerByUuidString(image, m_d->previewLayerId);

    auto clearPreviewTracking = [this]() {
        m_d->previewLayerId.clear();
        m_d->history.previewHistoryJobId.clear();
        m_d->history.previewHistoryImageIndex = -1;
        savePreviewLayerIdToDocument(QString());
    };

    // §13.184 / §3.5: apply_region_behavior vs apply_region_behavior_live when Live workspace is active
    if (entryIndex >= 0 && !m_d->history.historyEntries[entryIndex].regionLayerNames.isEmpty()) {
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
            m_d->history.historyEntries[entryIndex].imageInUse.insert(imageIndex, true);
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
    if (entryIndex >= 0 && entryIndex < m_d->history.historyEntries.size()) {
        const Private::HistoryEntry &entry = m_d->history.historyEntries.at(entryIndex);
        commitName = generatedLayerNameForEntry(entry);
        if (entry.hasMask) {
            const QImage previewImg = cachedHistoryPreviewImage(path, &m_d->history.historyPreviewImageCache);
            const QPoint offset = historyMaskedPreviewOffset(entry, previewImg.size());
            if (!previewImg.isNull())
                resultBounds = QRect(offset, previewImg.size());
            else if (!entry.contextBounds.isEmpty())
                resultBounds = entry.contextBounds;
        }
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
        m_d->history.historyEntries[entryIndex].imageInUse.insert(imageIndex, true);  // §13.28a: star overlay per thumbnail
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
    QListWidgetItem *item = m_d->history.listHistory->itemAt(pos);
    if (!item) return;
    m_d->history.listHistory->setCurrentItem(item);
    int entryIndex = -1;
    QString path = pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0) return;
    const Private::HistoryEntry &entry = m_d->history.historyEntries.at(entryIndex);
    QMenu menu(this);
    menu.addAction(ComfyTr::tr("Apply"), this, &ComfyUIRemoteDock::slotHistoryApply);
    menu.addAction(ComfyTr::tr("Copy Prompt"), this, &ComfyUIRemoteDock::slotHistoryCopyPrompt);
    menu.addAction(ComfyTr::tr("Copy Prompt (Evaluated)"), this, &ComfyUIRemoteDock::slotHistoryCopyPromptEvaluated);
    menu.addAction(ComfyTr::tr("Copy Strength"), this, &ComfyUIRemoteDock::slotHistoryCopyStrength);
    QAction *copyStyleAction = menu.addAction(ComfyTr::tr("Copy Style"), this, &ComfyUIRemoteDock::slotHistoryCopyStyle);
    if (m_d->generate.comboPreset) {
        int idx = m_d->generate.comboPreset->findText(entry.styleName);
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
    menu.exec(m_d->history.listHistory->mapToGlobal(pos));
}

void ComfyUIRemoteDock::slotHistoryCopyPrompt()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size()) return;
    QString text = m_d->history.historyEntries.at(entryIndex).prompt;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(text);
    m_d->labelStatus->setText(ComfyTr::tr("Prompt copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryCopyPromptEvaluated()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->history.historyEntries.at(entryIndex);
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
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size()) return;
    int strength = m_d->history.historyEntries.at(entryIndex).strength;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(QString::number(strength));
    if (m_d->generate.spinStrength)
        m_d->generate.spinStrength->setValue(qBound(1, strength, 100));
    setStatusMessage(ComfyTr::tr("Strength %1% copied and set.", strength));
}

void ComfyUIRemoteDock::slotHistoryCopyStyle()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size() || !m_d->generate.comboPreset) return;
    const QString styleName = m_d->history.historyEntries.at(entryIndex).styleName;
    int idx = m_d->generate.comboPreset->findText(styleName);
    if (idx >= 0) {
        m_d->generate.comboPreset->setCurrentIndex(idx);
        m_d->labelStatus->setText(ComfyTr::tr("Style set to \"%1\".", styleName));
    }
}

void ComfyUIRemoteDock::slotHistoryCopySeed()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size()) return;
    qint64 seed = m_d->history.historyEntries.at(entryIndex).seed;
    if (QClipboard *cb = QApplication::clipboard())
        cb->setText(QString::number(seed));
    m_d->labelStatus->setText(ComfyTr::tr("Seed copied to clipboard."));
}

void ComfyUIRemoteDock::slotHistoryCopyInfo()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->history.historyEntries.at(entryIndex);
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
    const Private::HistoryEntry &e = m_d->history.historyEntries.at(entryIndex);
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
    if (m_d->viewManager && m_d->viewManager->image()) {
        KisImageSP image = m_d->viewManager->image();
        const QList<KisNodeSP> excludeNodes = ComfyUIUtils::collectInpaintExcludeNodes(
            image, true, m_d->rootControlLayers, m_d->previewLayerId);
        img = ComfyUIUtils::compositeJobResultOnDocument(image, excludeNodes, img, e.contextBounds, e.hasMask);
    }
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
    const QString discardedJobId = m_d->history.historyEntries.at(entryIndex).jobId;
    const bool previewWasDiscarded = (discardedJobId == m_d->history.previewHistoryJobId
                                      && imageIndex == m_d->history.previewHistoryImageIndex);
    // §13.192: confirm_discard_image gates confirmation; Clear History always confirms (see slotHistoryClear)
    const bool confirmDiscard = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("ConfirmDiscardImage", true);
    if (confirmDiscard && QMessageBox::warning(this, ComfyTr::tr("Discard image"),
            ComfyTr::tr("Remove this image from history?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
        return;
    Private::HistoryEntry &e = m_d->history.historyEntries[entryIndex];
    const int slotToEvict = e.documentSlot;
    QStringList paths = e.resultImagePaths;
    if (paths.isEmpty() && !e.resultImagePath.isEmpty())
        paths << e.resultImagePath;
    // §13.131: Discard single image — remove from collection and delete file; only remove entry when no images left
    if (paths.size() <= 1) {
        if (QFile::exists(path)) QFile::remove(path);
        evictDocumentEmbeddedSlotIfAny(slotToEvict);
        m_d->history.historyEntries.removeAt(entryIndex);
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
    if (m_d->history.historyEntries.isEmpty()) return;
    clearHistoryPreviewState();
    // §13.140 / §13.192: Clear History always confirms via QMessageBox.warning; default No
    if (QMessageBox::warning(this, ComfyTr::tr("Clear history"),
            ComfyTr::tr("Discard all %1 generated images from history?", m_d->history.historyEntries.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (const Private::HistoryEntry &e : m_d->history.historyEntries) {
        evictDocumentEmbeddedSlotIfAny(e.documentSlot);
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p);
        }
    }
    m_d->history.historyEntries.clear();
    scheduleDocumentUiJsonSave();
    refreshHistoryList();
    updateHistoryUsageLabel();  // §13.145: update "Currently using X.X MB" if Configure → Performance is open
    m_d->history.btnHistoryReRun->setEnabled(false);
    m_d->history.btnHistoryApply->setEnabled(false);
    setStatusMessage(ComfyTr::tr("History cleared."));
}

void ComfyUIRemoteDock::slotHistoryReRun()
{
    int entryIndex = -1;
    pathForCurrentHistoryRow(&entryIndex, nullptr);
    if (entryIndex < 0 || entryIndex >= m_d->history.historyEntries.size()) return;
    const Private::HistoryEntry &e = m_d->history.historyEntries.at(entryIndex);
    m_d->generate.editPrompt->setPlainText(e.prompt);
    m_d->generate.editNegative->setPlainText(e.negative);
    if (m_d->generate.regionPromptWidget)
        m_d->generate.regionPromptWidget->refreshRootPromptFromDock();
    m_d->generate.spinWidth->setValue(e.width);
    m_d->generate.spinHeight->setValue(e.height);
    m_d->generate.spinSteps->setValue(e.steps);
    m_d->generate.spinCfg->setValue(e.cfg);
    if (m_d->generate.spinStrength)
        m_d->generate.spinStrength->setValue(qBound(1, e.strength, 100));
    m_d->generate.comboSampler->setCurrentText(e.samplerName.isEmpty() ? QString("euler") : e.samplerName);
    m_d->generate.checkFixedSeed->setChecked(true);
    m_d->generate.spinSeed->setValue(static_cast<int>(e.seed));
    int i = m_d->generate.comboCheckpoint->findText(e.checkpoint);
    if (i >= 0) m_d->generate.comboCheckpoint->setCurrentIndex(i);
    else m_d->generate.comboCheckpoint->setCurrentText(e.checkpoint);
    slotGenerate();
}
