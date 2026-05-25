/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyStyleCollection.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyFileLibrary.h"
#include "ComfyControlLayer.h"
#include "ComfyUIUtils.h"
#include "ComfyResources.h"
#include "ComfyTheme.h"
#include "ComfySwitchWidget.h"
#include "ComfyQueueButton.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyUIPoseLayers.h"
#include "ComfyRegionPromptWidget.h"
#include "ComfyRegionLink.h"
#include "ComfyPromptResizeHandle.h"

#include <kis_shape_layer.h>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QMap>
#include <QSharedPointer>
#include <QTemporaryFile>
#include <QMessageBox>
#include <QTimer>
#include <QRandomGenerator>
#include <QPointer>
#include <QInputDialog>
#include <QListView>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QCompleter>
#include <QStringListModel>
#include <QTextCursor>
#include <QScrollArea>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QHttpMultiPart>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QProgressBar>
#include <QRadioButton>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QEvent>
#include <QPaintEvent>
#include <QMenu>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QApplication>
#include <QFontMetrics>
#include <QClipboard>
#include <QTabWidget>
#include <QStackedWidget>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>
#include <QDesktopServices>
#include <QToolButton>
#include <QWidgetAction>
#include <QUuid>
#include <QCryptographicHash>
#include <QSet>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>

#include <QMouseEvent>
#include <kis_action.h>
#include <kis_action_manager.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>
#include <kis_icon_utils.h>
#include <KisViewManager.h>
#include <kis_canvas2.h>
#include <kis_signal_auto_connection.h>
#include <kis_image_manager.h>
#include <kis_selection.h>
#include <kis_types.h>
#include <KoUpdater.h>
#include <kis_animation_importer.h>
#include <kis_annotation.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <kis_node.h>
#include <KisPart.h>
#include <KisDocument.h>
#include <KisImportExportErrorCode.h>
#include <commands/KisNodeRenameCommand.h>
#include <kis_layer_properties_icons.h>
#include <kis_layer_utils.h>
#include <KisImageBarrierLock.h>
#include <kis_undo_adapter.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorProfile.h>
#include <KoColorConversionTransformation.h>

namespace {

/// §13.196: while tag completer popup is visible, Tab must not move focus away from the prompt.
class ComfyPromptPlainTextEdit : public QPlainTextEdit
{
public:
    explicit ComfyPromptPlainTextEdit(QCompleter *completer, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
        , m_completer(completer)
    {
    }

protected:
    bool focusNextPrevChild(bool next) override
    {
        if (!m_completer.isNull()) {
            QWidget *pop = m_completer->popup();
            if (pop && pop->isVisible())
                return false;
        }
        return QPlainTextEdit::focusNextPrevChild(next);
    }

private:
    QPointer<QCompleter> m_completer;
};

static QUuid comfyParseLayerUuidString(const QString &layerId)
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

static KisPaintLayer *findPaintLayerByUuidInTree(KisNodeSP node, const QString &uuidWithoutBraces)
{
    if (!node || uuidWithoutBraces.isEmpty())
        return nullptr;
    KisPaintLayer *pl = dynamic_cast<KisPaintLayer *>(node.data());
    if (pl && node->uuid().toString(QUuid::WithoutBraces) == uuidWithoutBraces)
        return pl;
    for (quint32 i = 0; i < node->childCount(); ++i) {
        if (KisPaintLayer *found = findPaintLayerByUuidInTree(node->at(i), uuidWithoutBraces))
            return found;
    }
    return nullptr;
}

static void collectPaintLayerNodes(KisNodeSP node, QVector<QPair<QString, QString>> *out)
{
    if (!node || !out)
        return;
    if (dynamic_cast<KisPaintLayer *>(node.data())) {
        const QString id = node->uuid().toString(QUuid::WithoutBraces);
        out->append(qMakePair(id, node->name()));
    }
    for (quint32 i = 0; i < node->childCount(); ++i)
        collectPaintLayerNodes(node->at(i), out);
}
} // namespace

// §13.32: Strength spinbox snaps to valid step boundaries (arrow keys / scroll)
class StrengthSpinBox : public QSpinBox
{
    QPointer<QSpinBox> m_steps;
public:
    explicit StrengthSpinBox(QSpinBox *stepsSpinBox, QWidget *parent = nullptr)
        : QSpinBox(parent), m_steps(stepsSpinBox) {}
    void stepBy(int step) override {
        const int steps = m_steps ? qMax(1, m_steps->value()) : 20;
        int idx = qRound(value() * steps / 100.0);
        idx = qBound(1, idx, steps);
        const int newIdx = qBound(1, idx + step, steps);
        setValue(qRound(100.0 * newIdx / steps));
    }
};

// §13.105: SpinnerWidget — compact progress for Live view (48×20, 120° arc + percentage, 50 ms timer)
class LiveSpinnerWidget : public QWidget
{
public:
    explicit LiveSpinnerWidget(QWidget *parent = nullptr) : QWidget(parent), m_progress(0), m_angle(0)
    {
        setFixedSize(48, 20);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            m_angle = (m_angle + 12) % 360;
            update();
        });
    }
    void setProgress(int progress) { m_progress = qBound(0, progress, 100); update(); }
    void startAnimation() { m_timer->start(50); show(); }
    void stopAnimation() { m_timer->stop(); hide(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int w = width();
        const int h = height();
        const int span = 120 * 16;  // 120° in sixteenths of a degree
        const int startAngle = (90 - m_angle) * 16;
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::WindowText));
        p.drawPie(2, 0, w - 4, h - 4, startAngle, span);
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(QRect(0, 0, w, h), Qt::AlignCenter, QString::number(m_progress) + QLatin1Char('%'));
    }
private:
    QTimer *m_timer;
    int m_progress;
    int m_angle;
};

ComfyUIRemoteDock::ComfyUIRemoteDock()
    : QDockWidget()
    , m_d(new Private)
{
    m_d->nam = new QNetworkAccessManager(this);
    m_d->pollTimer = new QTimer(this);
    m_d->pollTimer->setSingleShot(true);
    m_d->inpaintPollTimer = new QTimer(this);
    m_d->inpaintPollTimer->setSingleShot(true);
    connect(m_d->inpaintPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotInpaintPoll);
    m_d->upscalePollTimer = new QTimer(this);
    m_d->upscalePollTimer->setSingleShot(true);
    connect(m_d->upscalePollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotUpscalePoll);
    m_d->liveTimer = new QTimer(this);
    m_d->liveTimer->setSingleShot(true);
    connect(m_d->liveTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotLiveTick);
    m_d->livePollTimer = new QTimer(this);
    m_d->livePollTimer->setSingleShot(true);
    connect(m_d->livePollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotLivePoll);
    m_d->controlPreviewPollTimer = new QTimer(this);
    m_d->controlPreviewPollTimer->setSingleShot(true);
    connect(m_d->controlPreviewPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotControlPreviewPoll);
    m_d->controlLayerJobPollTimer = new QTimer(this);
    m_d->controlLayerJobPollTimer->setSingleShot(true);
    connect(m_d->controlLayerJobPollTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::slotControlLayerJobPoll);
    m_d->documentSyncPoller = new QTimer(this);
    m_d->documentSyncPoller->setInterval(20);
    connect(m_d->documentSyncPoller, &QTimer::timeout, this, &ComfyUIRemoteDock::slotDocumentSyncPoll);
    m_d->animationPreviewDebounce = new QTimer(this);
    m_d->animationPreviewDebounce->setSingleShot(true);
    m_d->animationPreviewDebounce->setInterval(100);
    connect(m_d->animationPreviewDebounce, &QTimer::timeout, this, &ComfyUIRemoteDock::slotDebouncedAnimationTargetPreview);
    m_d->documentDefaultsSaveTimer = new QTimer(this);
    m_d->documentDefaultsSaveTimer->setSingleShot(true);
    connect(m_d->documentDefaultsSaveTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::persistDocumentDefaultsToSettings);
    connect(m_d->pollTimer, &QTimer::timeout, this, [this]() {
        if (m_d->currentPromptId.isEmpty()) return;
        QUrl base(m_d->editServerUrl->text().trimmed());
        if (!base.isValid()) return;
        base.setPath(base.path() + "/history/" + m_d->currentPromptId);
        QNetworkRequest req(base);
        ComfyUIUtils::setComfyUIRequestHeaders(req);
        QNetworkReply *reply = m_d->nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("History error: %1", reply->errorString()), true);
                m_d->progressBar->setValue(0);
                m_d->currentPromptId.clear();
                if (!m_d->jobQueue.isEmpty()) {
                    m_d->currentPromptId = m_d->jobQueue.takeFirst();
                    m_d->pollCount = 0;
                    startPolling();
                } else {
                    m_d->btnGenerate->setEnabled(true);
                }
                updateQueueStatus();
                return;
            }
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject hist = doc.object().value(m_d->currentPromptId).toObject();
            QJsonObject outputs = hist.value("outputs").toObject();
            if (outputs.isEmpty()) {
                m_d->pollCount++;
                if (m_d->pollCount >= Private::maxPollCount) {
                    setStatusMessage(ComfyTr::tr("Generation timed out."), true);
                    m_d->progressBar->setValue(0);
                    m_d->currentPromptId.clear();
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                updateQueueStatus();
                m_d->pollTimer->start(1000);
                return;
            }
            // §13.131: Collect all images from output (multi-image result per job)
            QVector<QPair<QString, QString>> imageInfos;
            for (const QString &nodeId : outputs.keys()) {
                QJsonObject nodeOut = outputs.value(nodeId).toObject();
                QJsonArray images = nodeOut.value("images").toArray();
                for (int i = 0; i < images.size(); i++) {
                    QJsonObject img = images.at(i).toObject();
                    QString fn = img.value("filename").toString();
                    if (!fn.isEmpty())
                        imageInfos.append(qMakePair(fn, img.value("subfolder").toString()));
                }
                if (!imageInfos.isEmpty()) break;
            }
            if (imageInfos.isEmpty()) {
                setStatusMessage(ComfyTr::tr("No image in output."), true);
                m_d->progressBar->setValue(0);
                m_d->currentPromptId.clear();
                if (!m_d->jobQueue.isEmpty()) {
                    m_d->currentPromptId = m_d->jobQueue.takeFirst();
                    m_d->pollCount = 0;
                    startPolling();
                } else {
                    m_d->btnGenerate->setEnabled(true);
                }
                updateQueueStatus();
                return;
            }
            QString completedId = m_d->currentPromptId;
            m_d->currentPromptId.clear();
            if (m_d->jobQueue.isEmpty()) {
                m_d->btnGenerate->setEnabled(true);
            }
            QUrl baseUrl(m_d->editServerUrl->text().trimmed());
            const int totalImages = imageInfos.size();
            // §13.131: Multi-image result — download all images and store in one entry
            if (totalImages > 1 && m_d->pendingHistoryByPromptId.contains(completedId)) {
                Private::HistoryEntry entry = m_d->pendingHistoryByPromptId.take(completedId);
                entry.jobId = completedId;
                QSharedPointer<QMap<int, QString>> pathsByIndex(new QMap<int, QString>());
                for (int i = 0; i < totalImages; i++) {
                    QUrl viewUrl(baseUrl);
                    QString path = viewUrl.path();
                    if (!path.endsWith('/')) path += '/';
                    path += "view";
                    viewUrl.setPath(path);
                    QUrlQuery q;
                    q.addQueryItem("filename", imageInfos.at(i).first);
                    if (!imageInfos.at(i).second.isEmpty()) q.addQueryItem("subfolder", imageInfos.at(i).second);
                    viewUrl.setQuery(q);
                    QNetworkRequest req(viewUrl);
                    ComfyUIUtils::setComfyUIRequestHeaders(req);
                    QNetworkReply *getReply = m_d->nam->get(req);
                    connect(getReply, &QNetworkReply::finished, this, [this, getReply, completedId, entry, totalImages, pathsByIndex, i, baseUrl]() mutable {
                        getReply->deleteLater();
                        if (getReply->error() != QNetworkReply::NoError) {
                            setStatusMessage(ComfyTr::tr("Download error: %1", getReply->errorString()), true);
                            m_d->progressBar->setValue(0);
                            if (!m_d->jobQueue.isEmpty()) {
                                m_d->currentPromptId = m_d->jobQueue.takeFirst();
                                m_d->pollCount = 0;
                                startPolling();
                            } else {
                                m_d->btnGenerate->setEnabled(true);
                            }
                            updateQueueStatus();
                            return;
                        }
                        QByteArray data = getReply->readAll();
                        QString suffix = "png";
                        if (data.startsWith("\x89PNG")) suffix = "png";
                        else if (data.startsWith("\xff\xd8")) suffix = "jpg";
                        QString cachePath = ComfyUIUtils::historyCacheDir() + QStringLiteral("/") + completedId + QStringLiteral("_") + QString::number(i) + QStringLiteral(".") + suffix;
                        if (QFile::exists(cachePath)) QFile::remove(cachePath);
                        QFile f(cachePath);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(data);
                            f.close();
                            pathsByIndex->insert(i, cachePath);
                        }
                        if (pathsByIndex->size() == totalImages) {
                            Private::HistoryEntry e = entry;
                            for (int j = 0; j < totalImages; j++)
                                e.resultImagePaths.append(pathsByIndex->value(j));
                            e.resultImagePath = e.resultImagePaths.isEmpty() ? QString() : e.resultImagePaths.first();
                            m_d->progressBar->setValue(100);
                            m_d->historyEntries.prepend(e);
                            while (m_d->historyEntries.size() > Private::maxHistoryEntries) {
                                Private::HistoryEntry old = m_d->historyEntries.takeLast();
                                evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                                QStringList paths = old.resultImagePaths;
                                if (paths.isEmpty() && !old.resultImagePath.isEmpty()) paths << old.resultImagePath;
                                for (const QString &p : paths) { if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p); }
                            }
                            pruneHistoryToStorageLimit();
                            persistTopHistoryEntryToDocument(false);
                            handleGenerationFinished(e.resultImagePath, false);
                            if (!m_d->jobQueue.isEmpty()) {
                                m_d->currentPromptId = m_d->jobQueue.takeFirst();
                                m_d->pollCount = 0;
                                startPolling();
                            }
                            updateQueueStatus();
                        }
                    });
                }
                return;
            }
            // Single image: one GET
            QUrl viewUrl(baseUrl);
            QString path = viewUrl.path();
            if (!path.endsWith('/')) path += '/';
            path += "view";
            viewUrl.setPath(path);
            QUrlQuery q;
            q.addQueryItem("filename", imageInfos.at(0).first);
            if (!imageInfos.at(0).second.isEmpty()) q.addQueryItem("subfolder", imageInfos.at(0).second);
            viewUrl.setQuery(q);
            QNetworkRequest req(viewUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(req);
            QNetworkReply *getReply = m_d->nam->get(req);
            connect(getReply, &QNetworkReply::finished, this, [this, getReply, completedId]() {
                getReply->deleteLater();
                if (getReply->error() != QNetworkReply::NoError) {
                    setStatusMessage(ComfyTr::tr("Download error: %1", getReply->errorString()), true);
                    m_d->progressBar->setValue(0);
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                QByteArray data = getReply->readAll();
                QString suffix = "png";
                if (data.startsWith("\x89PNG")) suffix = "png";
                else if (data.startsWith("\xff\xd8")) suffix = "jpg";
                QTemporaryFile tmp;
                tmp.setFileTemplate(tmp.fileTemplate() + "." + suffix);
                if (!tmp.open()) {
                    setStatusMessage(ComfyTr::tr("Could not create temp file."), true);
                    m_d->progressBar->setValue(0);
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                tmp.write(data);
                tmp.close();
                QString cachePath;
                if (m_d->pendingHistoryByPromptId.contains(completedId)) {
                    cachePath = ComfyUIUtils::historyCacheDir() + QStringLiteral("/") + completedId + QStringLiteral(".png");
                    if (QFile::exists(cachePath)) QFile::remove(cachePath);
                    if (QFile::copy(tmp.fileName(), cachePath)) {
                        m_d->pendingHistoryByPromptId[completedId].resultImagePath = cachePath;
                        m_d->pendingHistoryByPromptId[completedId].resultImagePaths = QStringList() << cachePath;
                        m_d->pendingHistoryByPromptId[completedId].jobId = completedId;
                    }
                }
                if (!m_d->viewManager || !m_d->viewManager->imageManager()) {
                    setStatusMessage(ComfyTr::tr("No document open."), true);
                    m_d->progressBar->setValue(0);
                    if (!m_d->jobQueue.isEmpty()) {
                        m_d->currentPromptId = m_d->jobQueue.takeFirst();
                        m_d->pollCount = 0;
                        startPolling();
                    } else {
                        m_d->btnGenerate->setEnabled(true);
                    }
                    updateQueueStatus();
                    return;
                }
                m_d->progressBar->setValue(100);
                if (m_d->pendingHistoryByPromptId.contains(completedId)) {
                    Private::HistoryEntry entry = m_d->pendingHistoryByPromptId.take(completedId);
                    entry.jobId = completedId;
                    if (entry.resultImagePaths.isEmpty() && !entry.resultImagePath.isEmpty())
                        entry.resultImagePaths = QStringList() << entry.resultImagePath;
                    const bool skipGenFinishedActions =
                        m_d->isFullAnimationBatch && m_d->animationBatchPromptIdToIndex.contains(completedId);
                    // §13.45 / §13.74: Full Animation — collect per-frame results; on last frame, build keyframes list
                    // (cached execution: same bytes as previous frame → reuse previous path in list).
                    if (m_d->isFullAnimationBatch && m_d->animationBatchPromptIdToIndex.contains(completedId)) {
                        const int frameIdx = m_d->animationBatchPromptIdToIndex.take(completedId);
                        const QString docPath = (m_d->canvas && m_d->canvas->imageView() && m_d->canvas->imageView()->document())
                            ? m_d->canvas->imageView()->document()->path() : QString();
                        QString srcPath = entry.resultImagePath;
                        if (srcPath.isEmpty() && !entry.resultImagePaths.isEmpty())
                            srcPath = entry.resultImagePaths.first();
                        if (!srcPath.isEmpty() && QFile::exists(srcPath))
                            m_d->animationBatchSourcePathByFrame.insert(frameIdx, srcPath);
                        if (m_d->animationBatchPromptIdToIndex.isEmpty()) {
                            m_d->isFullAnimationBatch = false;
                            m_d->batchNeedsPerFrameReference = false;
                            if (!docPath.isEmpty() && m_d->canvas && m_d->canvas->image()) {
                                QVector<int> frameOrder;
                                if (!m_d->animationBatchFrameTimes.isEmpty()) {
                                    frameOrder = m_d->animationBatchFrameTimes;
                                } else {
                                    for (int f = m_d->animationBatchRangeStart; f <= m_d->animationBatchRangeEnd; ++f)
                                        frameOrder.append(f);
                                }
                                if (frameOrder.isEmpty()) {
                                    QList<int> keys = m_d->animationBatchSourcePathByFrame.keys();
                                    std::sort(keys.begin(), keys.end());
                                    for (int k : keys)
                                        frameOrder.append(k);
                                }
                                QStringList keyframePaths;
                                QString prevListPath;
                                QString prevSrcPath;
                                bool batchOk = true;
                                for (int ft : frameOrder) {
                                    const QString src = m_d->animationBatchSourcePathByFrame.value(ft);
                                    if (src.isEmpty() || !QFile::exists(src)) {
                                        batchOk = false;
                                        break;
                                    }
                                    if (!keyframePaths.isEmpty() && !prevSrcPath.isEmpty()
                                        && ComfyUIUtils::filesContentsEqual(src, prevSrcPath)) {
                                        keyframePaths.append(prevListPath);
                                        prevSrcPath = src;
                                        continue;
                                    }
                                    const QString destPath = ComfyUIUtils::animationFramePath(docPath, ft);
                                    QDir().mkpath(QFileInfo(destPath).absolutePath());
                                    if (QFile::exists(destPath))
                                        QFile::remove(destPath);
                                    if (!QFile::copy(src, destPath)) {
                                        batchOk = false;
                                        break;
                                    }
                                    keyframePaths.append(destPath);
                                    prevListPath = destPath;
                                    prevSrcPath = src;
                                }
                                m_d->animationBatchSourcePathByFrame.clear();
                                if (!batchOk || keyframePaths.size() != frameOrder.size()) {
                                    if (!batchOk)
                                        setStatusMessage(ComfyTr::tr("Animation batch incomplete: missing or invalid frame files."), true);
                                } else if (!keyframePaths.isEmpty()) {
                                    KisImageSP img = m_d->canvas->image().toStrongRef();
                                    if (img) {
                                        KisAnimationImporter importer(img);
                                        const int firstFrame = m_d->animationImportStartFrame;
                                        KisImportExportErrorCode impRes =
                                            importer.import(keyframePaths, firstFrame, 1, false, false, 1);
                                        if (m_d->canvas)
                                            m_d->canvas->updateCanvas();
                                        if (impRes.isOk() || impRes.isInternalError()) {
                                            setStatusMessage(ComfyTr::tr("Imported %1 animation frames.", keyframePaths.size()));
                                            // §13.74: rename imported layer "[Generated] {start}-{end}: {params.name}"
                                            if (m_d->viewManager) {
                                                KisNodeSP an = m_d->viewManager->activeNode();
                                                if (an && img->undoAdapter()) {
                                                    QString pfx = ComfyUIUtils::stripPromptComments(
                                                                      m_d->editPrompt->toPlainText())
                                                                      .trimmed();
                                                    if (pfx.length() > 48)
                                                        pfx = pfx.left(48) + QStringLiteral("...");
                                                    if (pfx.isEmpty())
                                                        pfx = ComfyTr::tr("Animation");
                                                    const QString newName = ComfyTr::tr("[Generated] %1-%2: %3",
                                                        m_d->animationBatchRangeStart,
                                                        m_d->animationBatchRangeEnd,
                                                        pfx);
                                                    img->undoAdapter()->addCommand(
                                                        new KisNodeRenameCommand(an, an->name(), newName));
                                                }
                                            }
                                        } else {
                                            setStatusMessage(
                                                impRes.errorMessage().isEmpty()
                                                    ? ComfyTr::tr("Animation import failed.")
                                                    : impRes.errorMessage(),
                                                true);
                                        }
                                        m_d->animationBatchFrameTimes.clear();
                                        m_d->animationBatchGroupId.clear();
                                    }
                                }
                            }
                        }
                    }
                    m_d->historyEntries.prepend(entry);
                    while (m_d->historyEntries.size() > Private::maxHistoryEntries) {
                        Private::HistoryEntry old = m_d->historyEntries.takeLast();
                        evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                        QStringList paths = old.resultImagePaths;
                        if (paths.isEmpty() && !old.resultImagePath.isEmpty()) paths << old.resultImagePath;
                        for (const QString &p : paths) { if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p); }
                    }
                    pruneHistoryToStorageLimit();
                    persistTopHistoryEntryToDocument(skipGenFinishedActions);
                    const QString finishPath = entry.resultImagePath.isEmpty()
                        ? (entry.resultImagePaths.isEmpty() ? QString() : entry.resultImagePaths.first())
                        : entry.resultImagePath;
                    bool animTimelineMismatch = false;
                    if (!skipGenFinishedActions && m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3
                        && m_d->radioSingleFrame && m_d->radioSingleFrame->isChecked()
                        && entry.animationSubmitTime >= 0 && m_d->viewManager) {
                        KisImageSP ki = m_d->viewManager->image();
                        if (ki && ki->animationInterface() && ki->animationInterface()->hasAnimation())
                            animTimelineMismatch =
                                (ki->animationInterface()->currentTime() != entry.animationSubmitTime);
                    }
                    const bool animTarget = !skipGenFinishedActions
                        && tryApplyAnimationSingleFrameToTargetLayer(finishPath, animTimelineMismatch);
                    if (!skipGenFinishedActions && m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3
                        && m_d->radioSingleFrame && m_d->radioSingleFrame->isChecked()) {
                        updateAnimationResultPreview(finishPath);
                    }
                    if (animTimelineMismatch && !animTarget)
                        setStatusMessage(ComfyTr::tr("Generated frame does not match current time."), false, true);
                    handleGenerationFinished(finishPath, skipGenFinishedActions || animTarget);
                }
                if (!m_d->jobQueue.isEmpty()) {
                    m_d->currentPromptId = m_d->jobQueue.takeFirst();
                    m_d->pollCount = 0;
                    startPolling();
                }
                updateQueueStatus();
            });
        });
    });

    QWidget *widget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(widget);
    // §13.1 Dock stack: index 0 = Welcome, 1 = workspace content (Generate/Upscale/Live/Animation/Graph via combo)
    m_d->mainStack = new QStackedWidget(widget);

    // §5.2 Welcome view: logo, title "AI Image\nGeneration", connection status, Configure button, footer links
    m_d->welcomePage = new QWidget(widget);
    QVBoxLayout *welcomeLayout = new QVBoxLayout(m_d->welcomePage);
    QLabel *logoLabel = new QLabel(m_d->welcomePage);
    logoLabel->setFixedSize(64, 64);
    QPixmap logoPix = ComfyTheme::logoPixmap(64);
    if (!logoPix.isNull()) {
        logoLabel->setPixmap(logoPix.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        logoLabel->setStyleSheet(QStringLiteral("background: palette(mid); border-radius: 4px;"));
    }
    QLabel *welcomeTitle = new QLabel(ComfyTr::tr("AI Image\nGeneration"), m_d->welcomePage);
    QFont titleFont = welcomeTitle->font();
    titleFont.setPointSize(qMax(12, titleFont.pointSize()));
    welcomeTitle->setFont(titleFont);
    welcomeTitle->setAlignment(Qt::AlignCenter);
    welcomeTitle->setTextFormat(Qt::PlainText);
    QHBoxLayout *headerRow = new QHBoxLayout();
    headerRow->addWidget(logoLabel);
    headerRow->addWidget(welcomeTitle, 1);
    welcomeLayout->addLayout(headerRow);
    welcomeLayout->addSpacing(12);
    // §13.190 order: AutoUpdateWidget (3), NewsWidget (4), ConnectionWidget (5). At most one visible.
    m_d->welcomeUpdateWidget = new QWidget(m_d->welcomePage);
    QVBoxLayout *updateLayout = new QVBoxLayout(m_d->welcomeUpdateWidget);
    m_d->welcomeUpdateTitleLabel = new QLabel(m_d->welcomeUpdateWidget);
    m_d->welcomeUpdateTitleLabel->setWordWrap(true);
    updateLayout->addWidget(m_d->welcomeUpdateTitleLabel);
    m_d->welcomeUpdateVersionLabel = new QLabel(m_d->welcomeUpdateWidget);
    m_d->welcomeUpdateVersionLabel->setWordWrap(true);
    m_d->welcomeUpdateVersionLabel->hide();
    updateLayout->addWidget(m_d->welcomeUpdateVersionLabel);
    m_d->welcomeUpdateProgressBar = new QProgressBar(m_d->welcomeUpdateWidget);
    m_d->welcomeUpdateProgressBar->setRange(0, 0);
    m_d->welcomeUpdateProgressBar->setTextVisible(false);
    m_d->welcomeUpdateProgressBar->hide();
    updateLayout->addWidget(m_d->welcomeUpdateProgressBar);
    m_d->welcomeCheckAutoUpdate = new QCheckBox(ComfyTr::tr("Check for updates on startup"), m_d->welcomeUpdateWidget);
    m_d->welcomeCheckAutoUpdate->setChecked(ComfyUIUtils::loadSettingsJson().value(QStringLiteral("auto_update")).toBool(true));
    m_d->welcomeCheckAutoUpdate->setToolTip(ComfyTr::tr("When enabled, the Welcome view will check for a new plugin version when shown."));
    connect(m_d->welcomeCheckAutoUpdate, &QCheckBox::toggled, this, [this](bool checked) {
        QJsonObject s = ComfyUIUtils::loadSettingsJson();
        s.insert(QStringLiteral("auto_update"), checked);
        ComfyUIUtils::saveSettingsJson(s);
        updateWelcomeVisibility();
    });
    updateLayout->addWidget(m_d->welcomeCheckAutoUpdate);
    m_d->welcomeUpdateButton = new QPushButton(ComfyTr::tr("Download and Install"), m_d->welcomeUpdateWidget);
    connect(m_d->welcomeUpdateButton, &QPushButton::clicked, this, [this](bool) {
        if (m_d->pluginUpdateState == Private::PluginUpdateState::RestartRequired) {
            const QString p = m_d->updateExtractPath;
            if (!p.isEmpty() && (QFileInfo::exists(p)))
                QDesktopServices::openUrl(QUrl::fromLocalFile(p));
            return;
        }
        startPluginUpdateDownload();
    });
    updateLayout->addWidget(m_d->welcomeUpdateButton);
    m_d->welcomeUpdateWidget->hide();
    welcomeLayout->addWidget(m_d->welcomeUpdateWidget);
    m_d->welcomeNewsWidget = new QWidget(m_d->welcomePage);
    QVBoxLayout *newsLayout = new QVBoxLayout(m_d->welcomeNewsWidget);
    m_d->welcomeNewsLabel = new QLabel(m_d->welcomeNewsWidget);
    m_d->welcomeNewsLabel->setWordWrap(true);
    m_d->welcomeNewsLabel->setObjectName(QStringLiteral("newsText"));
    newsLayout->addWidget(m_d->welcomeNewsLabel);
    QPushButton *btnNewsOk = new QPushButton(ComfyTr::tr("Ok"), m_d->welcomeNewsWidget);
    connect(btnNewsOk, &QPushButton::clicked, this, [this](bool) {
        m_d->hasUnseenNews = false;
        QJsonObject s = ComfyUIUtils::loadSettingsJson();
        s.insert(QStringLiteral("last_news"), m_d->lastNewsDigest);
        ComfyUIUtils::saveSettingsJson(s);
        updateWelcomeVisibility();
    });
    newsLayout->addWidget(btnNewsOk);
    m_d->welcomeNewsWidget->hide();
    welcomeLayout->addWidget(m_d->welcomeNewsWidget);
    m_d->welcomeConnectionWidget = new QWidget(m_d->welcomePage);
    QVBoxLayout *connWidgetLayout = new QVBoxLayout(m_d->welcomeConnectionWidget);
    connWidgetLayout->setContentsMargins(0, 0, 0, 0);
    m_d->welcomeStatusLabel = new QLabel(ComfyTr::tr("Not connected to server."), m_d->welcomeConnectionWidget);
    m_d->welcomeStatusLabel->setWordWrap(true);
    m_d->welcomeErrorLabel = new QLabel(m_d->welcomeConnectionWidget);
    m_d->welcomeErrorLabel->setWordWrap(true);
    m_d->welcomeErrorLabel->setStyleSheet(QStringLiteral("color: #b58900;"));  // Yellow for error line (§13.73)
    m_d->welcomeErrorLabel->hide();
    QPushButton *btnConfigure = new QPushButton(ComfyTr::tr("Configure"), m_d->welcomeConnectionWidget);
    btnConfigure->setIcon(KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("settings"))));
    connect(btnConfigure, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotConfigureHelp);
    connWidgetLayout->addWidget(m_d->welcomeStatusLabel);
    connWidgetLayout->addWidget(m_d->welcomeErrorLabel);
    connWidgetLayout->addWidget(btnConfigure);
    welcomeLayout->addWidget(m_d->welcomeConnectionWidget);
    // §5.2 Welcome layout: ConnectionWidget → 24pt spacing → footer links → stretch (fill below footer)
    welcomeLayout->addSpacing(24);
    QHBoxLayout *footerLinks = new QHBoxLayout();
    footerLinks->addStretch();
    QPushButton *linkInterstice = new QPushButton(QStringLiteral("Interstice.cloud"), m_d->welcomePage);
    linkInterstice->setFlat(true);
    linkInterstice->setCursor(Qt::PointingHandCursor);
    connect(linkInterstice, &QPushButton::clicked, this, [](bool) {
        QDesktopServices::openUrl(QUrl(ComfyUIUtils::intersticeWebBaseUrl()));
    });
    QPushButton *linkGitHub = new QPushButton(ComfyTr::tr("GitHub Project"), m_d->welcomePage);
    linkGitHub->setFlat(true);
    linkGitHub->setCursor(Qt::PointingHandCursor);
    connect(linkGitHub, &QPushButton::clicked, this, [](bool) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Acly/krita-ai-diffusion")));
    });
    QPushButton *linkDiscord = new QPushButton(QStringLiteral("Discord"), m_d->welcomePage);
    linkDiscord->setFlat(true);
    linkDiscord->setCursor(Qt::PointingHandCursor);
    connect(linkDiscord, &QPushButton::clicked, this, [](bool) {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://discord.gg/pWyzHfHHhU")));
    });
    footerLinks->addWidget(linkInterstice);
    footerLinks->addWidget(new QLabel(QStringLiteral("|"), m_d->welcomePage));
    footerLinks->addWidget(linkGitHub);
    footerLinks->addWidget(new QLabel(QStringLiteral("|"), m_d->welcomePage));
    footerLinks->addWidget(linkDiscord);
    welcomeLayout->addLayout(footerLinks);
    welcomeLayout->addStretch();
    m_d->mainStack->addWidget(m_d->welcomePage);

    QWidget *contentPage = new QWidget(widget);
    QVBoxLayout *contentLayout = new QVBoxLayout(contentPage);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget *scrollContent = new QWidget();
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);

    QGroupBox *connGroup = new QGroupBox(ComfyTr::tr("Connection"));
    QVBoxLayout *connLayout = new QVBoxLayout(connGroup);
    m_d->editServerUrl = new QLineEdit();
    {
        // §3.1: Prefer user_data_dir/settings.json, fallback to KConfig
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        QString savedUrl;
        if (settings.contains(QStringLiteral("server_url")))
            savedUrl = settings.value(QStringLiteral("server_url")).toString();
        if (savedUrl.isEmpty()) {
            KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
            savedUrl = cfg.readEntry("ServerUrl", QStringLiteral("127.0.0.1:8188"));
        }
        m_d->editServerUrl->setText(savedUrl);
    }
    m_d->editServerUrl->setPlaceholderText(ComfyTr::tr("e.g. 127.0.0.1:8188"));
    m_d->editServerUrl->setClearButtonEnabled(true);
    connect(m_d->editServerUrl, &QLineEdit::editingFinished, this, [this]() {
        QString url = m_d->editServerUrl->text().trimmed();
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ServerUrl", url);
        // §3.1: Persist to settings.json
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        settings.insert(QStringLiteral("server_url"), url);
        ComfyUIUtils::saveSettingsJson(settings);
    });

    m_d->comboCheckpoint = new QComboBox();
    m_d->comboCheckpoint->setEditable(true);
    m_d->comboCheckpoint->setInsertPolicy(QComboBox::NoInsert);
    m_d->comboCheckpoint->addItem("v1-5-pruned-emaonly.safetensors");
    m_d->btnRefreshCheckpoints = new QPushButton(ComfyTr::tr("Refresh"));
    m_d->btnRefreshCheckpoints->setToolTip(ComfyTr::tr("Load checkpoint list from server"));
    connect(m_d->btnRefreshCheckpoints, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRefreshCheckpoints);
    connect(m_d->comboCheckpoint, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        schedulePersistDocumentDefaults();
    });
    if (m_d->comboCheckpoint->lineEdit()) {
        connect(m_d->comboCheckpoint->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            schedulePersistDocumentDefaults();
        });
    }

    m_d->comboPreset = new QComboBox();
    rebuildPresetComboItems();
    connect(m_d->comboPreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComfyUIRemoteDock::slotPresetChanged);
    m_d->btnSaveAsPreset = new QPushButton(ComfyTr::tr("Save as preset"));
    m_d->btnDeletePreset = new QPushButton(ComfyTr::tr("Delete preset"));
    connect(m_d->btnSaveAsPreset, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotSaveAsPreset);
    connect(m_d->btnDeletePreset, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotDeletePreset);
    // §13.34: SamplingQuality (fast/quality) — affects steps; animation uses same spinSteps via slotGenerate
    m_d->comboQuality = new QComboBox();
    m_d->comboQuality->addItem(ComfyTr::tr("Fast"));
    m_d->comboQuality->addItem(ComfyTr::tr("Quality"));
    m_d->comboQuality->setCurrentIndex(1);
    m_d->comboQuality->setToolTip(ComfyTr::tr("Fast: fewer steps, quicker results. Quality: more steps, better details."));
    connect(m_d->comboQuality, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (!m_d->spinSteps) return;
        if (idx == 0) { // Fast
            int v = m_d->spinSteps->value();
            m_d->spinSteps->setValue(qMax(1, v / 2));
        } else { // Quality
            if (m_d->spinSteps->value() < 20)
                m_d->spinSteps->setValue(20);
        }
        if (m_d->canvas && m_d->canvas->image())
            scheduleDocumentUiJsonSave();
    });

    QHBoxLayout *presetRow = new QHBoxLayout();
    presetRow->addWidget(m_d->btnSaveAsPreset);
    presetRow->addWidget(m_d->btnDeletePreset);
    presetRow->addStretch();
    connLayout->addLayout(presetRow);
    m_d->btnDeletePreset->setEnabled(false);

    // Widgets for advanced configuration (shown in settings dialog instead of main dock)
    m_d->checkUseReferenceImage = new QCheckBox(ComfyTr::tr("Use current layer as reference (replace REFERENCE_IMAGE in workflow)"));
    m_d->checkUseReferenceImage->setToolTip(ComfyTr::tr("Export current layer, upload to server, and replace REFERENCE_IMAGE in your workflow JSON with the uploaded filename."));
    m_d->editCustomWorkflow = new QPlainTextEdit();
    m_d->editCustomWorkflow->setPlaceholderText(
        ComfyTr::tr("Paste ComfyUI workflow: API export (File → Export), or saved UI JSON (nodes/links) after connecting to the server."));
    m_d->editCustomWorkflow->setMaximumHeight(80);
    m_d->btnLoadWorkflow = new QPushButton(ComfyTr::tr("Load from file…"));
    connect(m_d->btnLoadWorkflow, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotLoadWorkflowFromFile);
    m_d->customWorkflowDocumentSaveTimer = new QTimer(this);
    m_d->customWorkflowDocumentSaveTimer->setSingleShot(true);
    connect(m_d->customWorkflowDocumentSaveTimer, &QTimer::timeout, this, [this]() {
        saveEmbeddedCustomWorkflowToDocument();
    });
    connect(m_d->editCustomWorkflow, &QPlainTextEdit::textChanged, this, &ComfyUIRemoteDock::scheduleSaveEmbeddedCustomWorkflowToDocument);
    m_d->customWorkflowParamsRefreshTimer = new QTimer(this);
    m_d->customWorkflowParamsRefreshTimer->setSingleShot(true);
    connect(m_d->customWorkflowParamsRefreshTimer, &QTimer::timeout, this, &ComfyUIRemoteDock::refreshCustomWorkflowParameterPanel);
    connect(m_d->editCustomWorkflow, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_d->customWorkflowParamsRefreshTimer)
            m_d->customWorkflowParamsRefreshTimer->start(450);
    });

    // Open settings dialog (connection + workflow) instead of exposing config directly
    QPushButton *btnSettings = new QPushButton(ComfyTr::tr("Settings…"));
    btnSettings->setIcon(KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("settings"))));
    connect(btnSettings, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotConfigureHelp);
    connLayout->addWidget(btnSettings);
    scrollLayout->addWidget(connGroup);

    QGroupBox *genGroup = new QGroupBox(ComfyTr::tr("Generate"));
    m_d->genGroupBox = genGroup;
    QVBoxLayout *genLayout = new QVBoxLayout(genGroup);

    // §5.3 Workspace selector: Generate (sparkle/magic icon), Upscale, Live, Animation, Graph; order and labels per spec
    m_d->comboWorkspace = new QComboBox();
    m_d->comboWorkspace->addItem(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("workspace-generation"))), ComfyTr::tr("Generate"));
    m_d->comboWorkspace->addItem(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("workspace-upscaling"))), ComfyTr::tr("Upscale"));
    m_d->comboWorkspace->addItem(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("workspace-live"))), ComfyTr::tr("Live"));
    m_d->comboWorkspace->addItem(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("workspace-animation"))), ComfyTr::tr("Animation"));
    m_d->comboWorkspace->addItem(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("workspace-custom"))), ComfyTr::tr("Graph"));
    m_d->comboWorkspace->setToolTip(ComfyTr::tr("Choose workspace: image generation, upscaling, live painting, animation, or custom graph workflow."));
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        int storedWorkspace = cfg.readEntry("WorkspaceIndex", 0);
        if (storedWorkspace < 0 || storedWorkspace >= m_d->comboWorkspace->count()) {
            storedWorkspace = 0;
        }
        m_d->comboWorkspace->setCurrentIndex(storedWorkspace);
        m_d->lastWorkspaceIndex = storedWorkspace;
    }
    // §13.72: Per-workspace dispatch — Generate / Upscale / Live / Animation / Graph each show their action button
    connect(m_d->comboWorkspace, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        // Toggle visibility of mode-specific controls
        if (!m_d->genGroupBox) return;
        const bool isGenerate = (idx == 0);
        const bool isUpscale = (idx == 1);
        const bool isLive = (idx == 2);
        const bool isAnimation = (idx == 3);
        const bool isGraph = (idx == 4);

        // §13.149: LiveWorkspace persistence — save live strength when leaving Live, restore when entering
        KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
        const int prevWorkspace = m_d->lastWorkspaceIndex;
        const QString liveKey = ComfyUIUtils::liveWorkspaceAnnotationKey();
        if (m_d->lastWorkspaceIndex == 2 && img && m_d->spinStrength) {
            QJsonObject o;
            o.insert(QStringLiteral("strength"), m_d->spinStrength->value() / 100.0);
            img->removeAnnotation(liveKey);
            img->addAnnotation(KisAnnotationSP(new KisAnnotation(liveKey, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("live")), QJsonDocument(o).toJson(QJsonDocument::Compact))));
        }
        // §13.169: CustomInpaint — save inpaint UI to document when leaving Generate
        if (prevWorkspace == 0 && idx != 0 && img)
            saveInpaintWorkspaceToDocument();
        if (idx == 2 && img && m_d->spinStrength) {
            KisAnnotationSP liveAnn = img->annotation(liveKey);
            if (liveAnn && !liveAnn->annotation().isEmpty()) {
                QJsonObject o = QJsonDocument::fromJson(liveAnn->annotation()).object();
                double s = o.value(QStringLiteral("strength")).toDouble(0.75);
                m_d->spinStrength->setValue(qBound(1, qRound(s * 100.0), 100));
            }
        }
        m_d->lastWorkspaceIndex = idx;
        if (idx == 0 && img)
            loadInpaintWorkspaceFromDocument();

        if (m_d->btnGenerate) m_d->btnGenerate->setVisible(isGenerate || isGraph);
        if (m_d->btnInpaint) m_d->btnInpaint->setVisible(isGenerate);
        if (m_d->comboInpaintMode) m_d->comboInpaintMode->setVisible(isGenerate);
        if (m_d->comboFillMode) m_d->comboFillMode->setVisible(isGenerate);
        if (m_d->comboInpaintContext) m_d->comboInpaintContext->setVisible(isGenerate);
        if (m_d->checkInpaintUseModel) m_d->checkInpaintUseModel->setVisible(isGenerate);
        if (m_d->checkInpaintUsePromptFocus) m_d->checkInpaintUsePromptFocus->setVisible(isGenerate);
        if (m_d->btnUpscale) m_d->btnUpscale->setVisible(isUpscale);
        if (m_d->btnGenerateAnimation) m_d->btnGenerateAnimation->setVisible(isAnimation);
        if (m_d->checkLiveMode) m_d->checkLiveMode->setVisible(isLive);
        if (m_d->checkLiveRecord) m_d->checkLiveRecord->setVisible(isLive);
        if (!isLive) stopLiveSpinner();
        if (m_d->batchModeRow) m_d->batchModeRow->setVisible(isLive || isAnimation);
        if (isAnimation)
            refreshAnimationTargetLayerCombo();
        updateAnimationTargetLayerRowVisibility();
        if (m_d->btnImportAnimation) m_d->btnImportAnimation->setVisible(isLive || isAnimation);
        if (m_d->regionsGroupBox) m_d->regionsGroupBox->setVisible(isGenerate);
        if (m_d->controlPreviewGroupBox) m_d->controlPreviewGroupBox->setVisible(isGenerate);
        if (!isGenerate)
            stopControlPreviewPolling();
        else {
            syncControlPreviewRangeFromSettings();
            syncPoseGuidePeopleCountFromSettings();
        }
        if (isGenerate)
            refreshRegionsList();  // §13.125: show active region set when returning to Generate
        if (m_d->upscaleFactorRow) m_d->upscaleFactorRow->setVisible(isUpscale);
        if (m_d->upscaleRefineBlock) m_d->upscaleRefineBlock->setVisible(isUpscale);
        if (isUpscale) updateUpscaleTargetSize();
        updateAnimationButtonLabel();

        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("WorkspaceIndex", idx);
        if (m_d->genContentContainer) m_d->genContentContainer->setVisible(!isGraph);
        if (m_d->graphPlaceholderWidget) m_d->graphPlaceholderWidget->setVisible(isGraph);
        reparentCustomWorkflowEditor(isGraph);
        if (m_d->histGroupBox) m_d->histGroupBox->setVisible(isGenerate || isGraph);
        if (m_d->queueButtonRowWidget)
            m_d->queueButtonRowWidget->setVisible(isGenerate || isAnimation || isGraph);
        refreshQueuePopupSupportsBatch();
        updateQueueStatus();
        applyInterfaceAppearanceSettings(); // §3.5: prompt_line_count_live when Live workspace
    });
    updateAnimationButtonLabel();  // §5.7: initial label from persisted FullAnimation
    genLayout->addWidget(m_d->comboWorkspace);

    m_d->genContentContainer = new QWidget(genGroup);
    QVBoxLayout *genContentLayout = new QVBoxLayout(m_d->genContentContainer);
    genContentLayout->setContentsMargins(0, 0, 0, 0);

    // §13.179: Upscale FactorWidget — slider (1.0–4.0), spinbox "Scale: X.XXx", "Target size: W x H"
    m_d->upscaleFactorRow = new QWidget(genGroup);
    QHBoxLayout *upscaleFactorLayout = new QHBoxLayout(m_d->upscaleFactorRow);
    upscaleFactorLayout->setContentsMargins(0, 0, 0, 0);
    m_d->sliderUpscaleFactor = new QSlider(Qt::Horizontal, m_d->upscaleFactorRow);
    m_d->sliderUpscaleFactor->setRange(10, 40);  // 1.0–4.0 as int*10
    m_d->sliderUpscaleFactor->setValue(20);
    m_d->sliderUpscaleFactor->setToolTip(ComfyTr::tr("Upscale factor"));
    m_d->spinUpscaleFactor = new QDoubleSpinBox(m_d->upscaleFactorRow);
    m_d->spinUpscaleFactor->setRange(1.0, 4.0);
    m_d->spinUpscaleFactor->setValue(2.0);
    m_d->spinUpscaleFactor->setDecimals(2);
    m_d->spinUpscaleFactor->setSingleStep(0.1);
    m_d->spinUpscaleFactor->setSuffix(ComfyTr::trc("scale factor suffix", "×"));
    m_d->spinUpscaleFactor->setToolTip(ComfyTr::tr("Scale: X.XX×"));
    m_d->labelUpscaleTargetSize = new QLabel(ComfyTr::tr("Target size: — × —"), m_d->upscaleFactorRow);
    m_d->labelUpscaleTargetSize->setToolTip(ComfyTr::tr("Target size: W x H (from document extent × scale)"));
    upscaleFactorLayout->addWidget(m_d->sliderUpscaleFactor, 1);
    upscaleFactorLayout->addWidget(m_d->spinUpscaleFactor);
    upscaleFactorLayout->addWidget(m_d->labelUpscaleTargetSize);
    connect(m_d->sliderUpscaleFactor, &QSlider::valueChanged, this, [this](int v) {
        m_d->upscaleFactor = v / 10.0;
        if (m_d->spinUpscaleFactor && qAbs(m_d->spinUpscaleFactor->value() - m_d->upscaleFactor) > 0.005)
            m_d->spinUpscaleFactor->setValue(m_d->upscaleFactor);
        updateUpscaleTargetSize();
    });
    connect(m_d->spinUpscaleFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        m_d->upscaleFactor = v;
        if (m_d->sliderUpscaleFactor)
            m_d->sliderUpscaleFactor->setValue(qRound(v * 10));
        updateUpscaleTargetSize();
    });
    genContentLayout->addWidget(m_d->upscaleFactorRow);
    m_d->upscaleFactorRow->setVisible(m_d->comboWorkspace->currentIndex() == 1);

    // §5.5 Upscale: Refine upscaled image (tile overlap shown when refine is enabled, per spec)
    m_d->upscaleRefineBlock = new QWidget(genGroup);
    QVBoxLayout *refineBlockLay = new QVBoxLayout(m_d->upscaleRefineBlock);
    refineBlockLay->setContentsMargins(0, 0, 0, 0);
    m_d->checkUpscaleRefine = new QCheckBox(ComfyTr::tr("Refine upscaled image"), m_d->upscaleRefineBlock);
    refineBlockLay->addWidget(m_d->checkUpscaleRefine);
    m_d->upscaleRefineDetails = new QWidget(m_d->upscaleRefineBlock);
    QVBoxLayout *refineLay = new QVBoxLayout(m_d->upscaleRefineDetails);
    refineLay->setContentsMargins(0, 0, 0, 0);
    refineLay->addWidget(new QLabel(ComfyTr::tr("Refinement model:"), m_d->upscaleRefineDetails));
    m_d->comboUpscaleRefinementModel = new QComboBox(m_d->upscaleRefineDetails);
    refineLay->addWidget(m_d->comboUpscaleRefinementModel);
    {
        QHBoxLayout *strLay = new QHBoxLayout();
        strLay->addWidget(new QLabel(ComfyTr::tr("Strength:"), m_d->upscaleRefineDetails));
        m_d->sliderUpscaleRefineStrength = new QSlider(Qt::Horizontal, m_d->upscaleRefineDetails);
        m_d->sliderUpscaleRefineStrength->setRange(1, 100);
        strLay->addWidget(m_d->sliderUpscaleRefineStrength, 1);
        m_d->labelUpscaleRefineStrength = new QLabel(m_d->upscaleRefineDetails);
        m_d->labelUpscaleRefineStrength->setMinimumWidth(40);
        strLay->addWidget(m_d->labelUpscaleRefineStrength);
        refineLay->addLayout(strLay);
    }
    {
        QHBoxLayout *gLay = new QHBoxLayout();
        gLay->addWidget(new QLabel(ComfyTr::tr("Image guidance:"), m_d->upscaleRefineDetails));
        m_d->sliderUpscaleRefineGuidance = new QSlider(Qt::Horizontal, m_d->upscaleRefineDetails);
        m_d->sliderUpscaleRefineGuidance->setRange(1, 100);
        gLay->addWidget(m_d->sliderUpscaleRefineGuidance, 1);
        m_d->labelUpscaleRefineGuidance = new QLabel(m_d->upscaleRefineDetails);
        m_d->labelUpscaleRefineGuidance->setMinimumWidth(40);
        gLay->addWidget(m_d->labelUpscaleRefineGuidance);
        refineLay->addLayout(gLay);
    }
    // §13.147: Tile Overlap — Automatic or X px (§5.5 — inside refine block)
    m_d->upscaleTileOverlapRow = new QWidget(m_d->upscaleRefineDetails);
    QHBoxLayout *tileOverlapLayout = new QHBoxLayout(m_d->upscaleTileOverlapRow);
    tileOverlapLayout->setContentsMargins(0, 0, 0, 0);
    tileOverlapLayout->addWidget(new QLabel(ComfyTr::tr("Tile overlap:"), m_d->upscaleTileOverlapRow));
    m_d->comboTileOverlapMode = new QComboBox(m_d->upscaleTileOverlapRow);
    m_d->comboTileOverlapMode->addItem(ComfyTr::tr("Automatic"), 0);
    m_d->comboTileOverlapMode->addItem(ComfyTr::tr("Custom"), 1);
    m_d->comboTileOverlapMode->setCurrentIndex(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("TileOverlapMode", 0));
    m_d->tileOverlapMode = m_d->comboTileOverlapMode->currentIndex();
    m_d->spinTileOverlap = new QSpinBox(m_d->upscaleTileOverlapRow);
    m_d->spinTileOverlap->setRange(0, 512);
    m_d->spinTileOverlap->setValue(KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("TileOverlap", 32));
    m_d->tileOverlap = m_d->spinTileOverlap->value();
    m_d->spinTileOverlap->setSuffix(ComfyTr::tr(" px"));
    m_d->spinTileOverlap->setToolTip(ComfyTr::tr("Tile overlap in pixels when Custom is selected."));
    tileOverlapLayout->addWidget(m_d->comboTileOverlapMode);
    tileOverlapLayout->addWidget(m_d->spinTileOverlap);
    tileOverlapLayout->addStretch();
    refineLay->addWidget(m_d->upscaleTileOverlapRow);
    m_d->checkUpscaleUsePrompt = new ComfySwitchWidget(m_d->upscaleRefineDetails);
    {
        QHBoxLayout *upscalePromptRow = new QHBoxLayout();
        upscalePromptRow->setContentsMargins(0, 0, 0, 0);
        upscalePromptRow->addWidget(m_d->checkUpscaleUsePrompt);
        upscalePromptRow->addWidget(new QLabel(ComfyTr::tr("Use Prompt"), m_d->upscaleRefineDetails), 1);
        refineLay->addLayout(upscalePromptRow);
    }
    m_d->checkUpscaleUsePrompt->setToolTip(ComfyTr::tr("When refining, include the positive prompt in the diffusion pass (when supported)."));
    refineBlockLay->addWidget(m_d->upscaleRefineDetails);
    genContentLayout->addWidget(m_d->upscaleRefineBlock);
    m_d->upscaleRefineBlock->setVisible(m_d->comboWorkspace->currentIndex() == 1);
    m_d->spinTileOverlap->setVisible(m_d->tileOverlapMode == 1);
    {
        KConfigGroup ucfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        m_d->checkUpscaleRefine->setChecked(ucfg.readEntry("UpscaleRefineEnabled", false));
        m_d->sliderUpscaleRefineStrength->setValue(ucfg.readEntry("UpscaleRefineStrength", 30));
        m_d->sliderUpscaleRefineGuidance->setValue(ucfg.readEntry("UpscaleRefineGuidance", 50));
        m_d->checkUpscaleUsePrompt->setChecked(ucfg.readEntry("UpscaleUsePrompt", false));
    }
    auto updateStrengthLabel = [this]() {
        if (m_d->labelUpscaleRefineStrength && m_d->sliderUpscaleRefineStrength)
            m_d->labelUpscaleRefineStrength->setText(QString::number(m_d->sliderUpscaleRefineStrength->value()) + QLatin1Char('%'));
    };
    auto updateGuidanceLabel = [this]() {
        if (m_d->labelUpscaleRefineGuidance && m_d->sliderUpscaleRefineGuidance)
            m_d->labelUpscaleRefineGuidance->setText(QString::number(m_d->sliderUpscaleRefineGuidance->value()) + QLatin1Char('%'));
    };
    updateStrengthLabel();
    updateGuidanceLabel();
    connect(m_d->sliderUpscaleRefineStrength, &QSlider::valueChanged, this, [this, updateStrengthLabel](int) {
        updateStrengthLabel();
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineStrength", m_d->sliderUpscaleRefineStrength->value());
    });
    connect(m_d->sliderUpscaleRefineGuidance, &QSlider::valueChanged, this, [this, updateGuidanceLabel](int) {
        updateGuidanceLabel();
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineGuidance", m_d->sliderUpscaleRefineGuidance->value());
    });
    connect(m_d->checkUpscaleRefine, &QCheckBox::toggled, this, [this](bool on) {
        if (m_d->upscaleRefineDetails)
            m_d->upscaleRefineDetails->setVisible(on);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefineEnabled", on);
    });
    connect(m_d->checkUpscaleUsePrompt, &QAbstractButton::toggled, this, [](bool on) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleUsePrompt", on);
    });
    m_d->upscaleRefineDetails->setVisible(m_d->checkUpscaleRefine->isChecked());
    connect(m_d->comboTileOverlapMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_d->tileOverlapMode = idx;
        if (m_d->spinTileOverlap) m_d->spinTileOverlap->setVisible(idx == 1);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("TileOverlapMode", idx);
        updateUpscaleTargetSize();
    });
    connect(m_d->spinTileOverlap, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_d->tileOverlap = v;
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("TileOverlap", v);
        updateUpscaleTargetSize();
    });
    syncUpscaleRefinementModelFromPresetCombo();
    connect(m_d->comboUpscaleRefinementModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("UpscaleRefinementModelIndex", m_d->comboUpscaleRefinementModel->currentIndex());
    });

    genContentLayout->addWidget(new QLabel(ComfyTr::tr("Prompt:")));
    // Tag autocomplete model/completers (must exist before ComfyPromptPlainTextEdit; §13.196 Tab + popup)
    m_d->tagKeywordModel = new QStringListModel(this);
    m_d->promptTagCompleter = new QCompleter(m_d->tagKeywordModel, this);
    m_d->promptTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_d->promptTagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_d->negativePromptTagCompleter = new QCompleter(m_d->tagKeywordModel, this);
    m_d->negativePromptTagCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_d->negativePromptTagCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_d->editPrompt = new ComfyPromptPlainTextEdit(m_d->promptTagCompleter);
    m_d->promptTagCompleter->setWidget(m_d->editPrompt);
    m_d->editPrompt->setTabChangesFocus(true);  // §13.196: Tab moves focus, not inserted
    m_d->editPrompt->installEventFilter(this);  // §13.196: Shift+Enter → Generate, ShortcutOverride
    m_d->editPrompt->setPlaceholderText(ComfyTr::tr("Describe the content you want to see, or leave empty."));
    m_d->editPrompt->setToolTip(ComfyTr::tr(
        "Tip: (word) for emphasis, [word] to reduce strength. Use commas to separate concepts. Shift+Enter to generate. "
        "Ctrl+Space for tag completion (CSV files in Settings → Interface)."));
    m_d->editPrompt->setMaximumHeight(60);
    {
        m_d->rootPromptColumnWidget = new QWidget(genGroup);
        QWidget *promptColumn = m_d->rootPromptColumnWidget;
        QVBoxLayout *promptColLayout = new QVBoxLayout(promptColumn);
        promptColLayout->setContentsMargins(0, 0, 0, 0);
        promptColLayout->setSpacing(0);
        promptColLayout->addWidget(m_d->editPrompt);
        m_d->promptResizeHandle = new ComfyPromptResizeHandle(
            m_d->editPrompt,
            [this](int lines) {
                QJsonObject st = ComfyUIUtils::loadSettingsJson();
                const bool live = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
                st.insert(live ? QStringLiteral("prompt_line_count_live") : QStringLiteral("prompt_line_count"), lines);
                ComfyUIUtils::saveSettingsJson(st);
            },
            40,
            promptColumn);
        promptColLayout->addWidget(m_d->promptResizeHandle);
        genContentLayout->addWidget(promptColumn);
    }

    m_d->negativePromptBlock = new QWidget(this);
    QVBoxLayout *negBlockLayout = new QVBoxLayout(m_d->negativePromptBlock);
    negBlockLayout->setContentsMargins(0, 0, 0, 0);
    QHBoxLayout *negativePromptRow = new QHBoxLayout();
    negativePromptRow->addWidget(new QLabel(ComfyTr::tr("Negative prompt:")));
    negativePromptRow->addStretch();
    m_d->labelNegativePromptAlert = new QLabel(m_d->negativePromptBlock);
    m_d->labelNegativePromptAlert->setToolTip(ComfyTr::tr("The selected Style does not use the negative prompt."));
    m_d->labelNegativePromptAlert->setPixmap(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("alert"))).pixmap(16, 16));
    m_d->labelNegativePromptAlert->setVisible(false);
    negativePromptRow->addWidget(m_d->labelNegativePromptAlert);
    negBlockLayout->addLayout(negativePromptRow);
    m_d->editNegative = new ComfyPromptPlainTextEdit(m_d->negativePromptTagCompleter);
    m_d->negativePromptTagCompleter->setWidget(m_d->editNegative);
    m_d->editNegative->setTabChangesFocus(true);  // §13.196: Tab moves focus
    m_d->editNegative->setPlaceholderText(ComfyTr::tr("Describe content you want to avoid."));
    m_d->editNegative->setToolTip(ComfyTr::tr("Ctrl+Space: tag completion (same lists as positive prompt)."));
    m_d->editNegative->setMaximumHeight(400);
    m_d->editNegative->installEventFilter(this);
    negBlockLayout->addWidget(m_d->editNegative);
    m_d->negativeResizeHandle = new ComfyPromptResizeHandle(
        m_d->editNegative,
        [this](int lines) {
            QJsonObject st = ComfyUIUtils::loadSettingsJson();
            st.insert(QStringLiteral("negative_prompt_line_count"), lines);
            ComfyUIUtils::saveSettingsJson(st);
        },
        28,
        m_d->negativePromptBlock);
    negBlockLayout->addWidget(m_d->negativeResizeHandle);
    genContentLayout->addWidget(m_d->negativePromptBlock);
    updateNegativePromptAlertVisibility();  // §13.143: initial state (e.g. "None" selected)

    // §13.48: Tag autocomplete — shared model, Ctrl+Space in positive/negative prompts
    connect(m_d->promptTagCompleter, QOverload<const QString &>::of(&QCompleter::activated), this, [this](const QString &text) {
        insertPromptTagCompletion(m_d->editPrompt, text);
    });
    connect(m_d->negativePromptTagCompleter, QOverload<const QString &>::of(&QCompleter::activated), this, [this](const QString &text) {
        insertPromptTagCompletion(m_d->editNegative, text);
    });
    refreshPromptTagCompleter();

    m_d->stepsParametersWidget = new QWidget(this);
    QHBoxLayout *stepsCfgRow = new QHBoxLayout(m_d->stepsParametersWidget);
    stepsCfgRow->setContentsMargins(0, 0, 0, 0);
    m_d->spinSteps = new QSpinBox();
    m_d->spinSteps->setRange(1, 150);
    m_d->spinSteps->setValue(20);
    m_d->spinSteps->setToolTip(ComfyTr::tr("Sampler steps"));
    m_d->spinCfg = new QDoubleSpinBox();
    m_d->spinCfg->setRange(1.0, 30.0);
    m_d->spinCfg->setValue(8.0);
    m_d->spinCfg->setDecimals(1);
    m_d->spinCfg->setToolTip(ComfyTr::tr("CFG scale (guidance strength)"));
    m_d->comboSampler = new QComboBox();
    m_d->comboSampler->setEditable(true);
    m_d->comboSampler->addItem("euler");
    m_d->comboSampler->addItem("euler_ancestral");
    m_d->comboSampler->addItem("dpmpp_2m");
    m_d->comboSampler->addItem("dpmpp_2s_ancestral");
    m_d->comboSampler->addItem("heun");
    m_d->comboSampler->addItem("dpm_2");
    m_d->comboSampler->addItem("dpm_2_ancestral");
    m_d->btnRefreshSamplers = new QPushButton(ComfyTr::tr("Refresh"));
    m_d->btnRefreshSamplers->setToolTip(ComfyTr::tr("Load sampler list from server"));
    connect(m_d->btnRefreshSamplers, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRefreshSamplers);
    stepsCfgRow->addWidget(new QLabel(ComfyTr::tr("Steps:")));
    stepsCfgRow->addWidget(m_d->spinSteps);
    stepsCfgRow->addWidget(new QLabel(ComfyTr::tr("CFG:")));
    stepsCfgRow->addWidget(m_d->spinCfg);
    stepsCfgRow->addWidget(new QLabel(ComfyTr::tr("Sampler:")));
    stepsCfgRow->addWidget(m_d->comboSampler, 1);
    stepsCfgRow->addWidget(m_d->btnRefreshSamplers);
    genContentLayout->addWidget(m_d->stepsParametersWidget);

    // §5.4: Strength (1–100%, denoise = strength/100). §13.32: stepBy snaps to valid step boundaries.
    m_d->spinStrength = new StrengthSpinBox(m_d->spinSteps, this);
    m_d->spinStrength->setRange(1, 100);
    m_d->spinStrength->setValue(100);
    m_d->spinStrength->setSuffix(QStringLiteral("%"));
    m_d->spinStrength->setToolTip(ComfyTr::tr("Strength: 100% = full generation, lower = more preserved (refine)."));
    QHBoxLayout *strengthRow = new QHBoxLayout();
    strengthRow->addWidget(new QLabel(ComfyTr::tr("Strength:")));
    strengthRow->addWidget(m_d->spinStrength);
    genContentLayout->addLayout(strengthRow);
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        m_d->spinStrength->setValue(qBound(1, cfg.readEntry("Strength", 100), 100));
    }
    connect(m_d->spinStrength, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("Strength", v);
        // §13.149: When in Live workspace, persist strength to document (ui.json live key)
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2) {
            KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
            if (img) {
                const QString liveKey = ComfyUIUtils::liveWorkspaceAnnotationKey();
                QJsonObject o;
                o.insert(QStringLiteral("strength"), v / 100.0);
                img->removeAnnotation(liveKey);
                img->addAnnotation(KisAnnotationSP(new KisAnnotation(liveKey, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("live")), QJsonDocument(o).toJson(QJsonDocument::Compact))));
            }
        }
    });

    // §5.4: Region-only toggle; when set, only active region mask and prompt are used
    m_d->checkRegionOnly = new QCheckBox(ComfyTr::tr("Region-only"));
    m_d->checkRegionOnly->setToolTip(ComfyTr::tr("Limit generation to the active region only."));
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        m_d->checkRegionOnly->setChecked(cfg.readEntry("RegionOnly", false));
    }
    connect(m_d->checkRegionOnly, &QCheckBox::toggled, this, [this](bool checked) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("RegionOnly", checked);
    });
    genContentLayout->addWidget(m_d->checkRegionOnly);

    // §5.4: Edit mode toggle (instruction-based editing; uses linked_edit_style when set)
    m_d->checkEditMode = new QCheckBox(ComfyTr::tr("Edit"));
    m_d->checkEditMode->setToolTip(
        ComfyTr::tr("Use instruction-based editing (alternative style when set). On Generate, the Regions list switches to a separate set while Edit is checked, so normal and edit workflows do not share the same regions."));
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        m_d->checkEditMode->setChecked(cfg.readEntry("EditMode", false));
    }
    connect(m_d->checkEditMode, &QCheckBox::toggled, this, [this](bool checked) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("EditMode", checked);
        refreshRegionsList();  // §13.125: switch between root and edit region lists
    });
    genContentLayout->addWidget(m_d->checkEditMode);

    // §5.4: Layer count (1–8); visible only when style architecture is Qwen Layered (Arch.qwen_l)
    m_d->layerCountRow = new QWidget(genGroup);
    QHBoxLayout *layerCountLayout = new QHBoxLayout(m_d->layerCountRow);
    layerCountLayout->setContentsMargins(0, 0, 0, 0);
    m_d->spinLayerCount = new QSpinBox(m_d->layerCountRow);
    m_d->spinLayerCount->setRange(1, 8);
    m_d->spinLayerCount->setValue(1);
    m_d->spinLayerCount->setToolTip(ComfyTr::tr("Number of output layers for Qwen Layered generation."));
    layerCountLayout->addWidget(new QLabel(ComfyTr::tr("Layer count:"), m_d->layerCountRow));
    layerCountLayout->addWidget(m_d->spinLayerCount);
    layerCountLayout->addStretch();
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        m_d->spinLayerCount->setValue(qBound(1, cfg.readEntry("LayerCount", 1), 8));
    }
    connect(m_d->spinLayerCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("LayerCount", v);
    });
    genContentLayout->addWidget(m_d->layerCountRow);
    m_d->layerCountRow->setVisible(false);  // Shown only when current style arch is qwen_l; no arch in presets yet

    m_d->checkFixedSeed = new QCheckBox(ComfyTr::tr("Fixed seed"));
    m_d->spinSeed = new QSpinBox();
    m_d->spinSeed->setRange(0, 2147483647);  // §13.209: 32-bit non-negative (0 to 2^31−1)
    m_d->spinSeed->setValue(0);
    m_d->btnRandomSeed = new QPushButton();
    m_d->btnRandomSeed->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("random"))));  // §5.4: dice icon for random seed
    m_d->btnRandomSeed->setToolTip(ComfyTr::tr("Pick a new random seed."));
    m_d->btnRandomSeed->setAccessibleName(ComfyTr::tr("Random seed"));
    connect(m_d->btnRandomSeed, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRandomSeed);
    QHBoxLayout *seedRow = new QHBoxLayout();
    seedRow->addWidget(new QLabel(ComfyTr::tr("Seed:")));
    seedRow->addWidget(m_d->checkFixedSeed);
    seedRow->addWidget(m_d->spinSeed);
    seedRow->addWidget(m_d->btnRandomSeed);
    genContentLayout->addLayout(seedRow);

    m_d->comboSizePreset = new QComboBox();
    m_d->comboSizePreset->addItem(ComfyTr::tr("512×512 (default)"), QSize(512, 512));
    m_d->comboSizePreset->addItem(ComfyTr::tr("768×768"), QSize(768, 768));
    m_d->comboSizePreset->addItem(ComfyTr::tr("1024×1024"), QSize(1024, 1024));
    m_d->comboSizePreset->addItem(ComfyTr::tr("2048×2048 (4k)"), QSize(2048, 2048));
    m_d->comboSizePreset->addItem(ComfyTr::tr("4096×4096 (8k)"), QSize(4096, 4096));
    connect(m_d->comboSizePreset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        QSize s = m_d->comboSizePreset->itemData(idx).toSize();
        if (s.isValid()) {
            m_d->spinWidth->setValue(s.width());
            m_d->spinHeight->setValue(s.height());
        }
    });
    m_d->spinWidth = new QSpinBox();
    m_d->spinWidth->setRange(64, 8192);
    m_d->spinWidth->setValue(512);
    m_d->spinHeight = new QSpinBox();
    m_d->spinHeight->setRange(64, 8192);
    m_d->spinHeight->setValue(512);

    QHBoxLayout *sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(ComfyTr::tr("Size:")));
    sizeRow->addWidget(m_d->comboSizePreset, 1);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("W:")));
    sizeRow->addWidget(m_d->spinWidth);
    sizeRow->addWidget(new QLabel(ComfyTr::tr("H:")));
    sizeRow->addWidget(m_d->spinHeight);
    genContentLayout->addLayout(sizeRow);

    m_d->btnGenerate = new QPushButton(ComfyTr::tr("Generate"));
    m_d->btnGenerate->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("generate"))));  // §5.4: sparkle / magic-style icon
    connect(m_d->btnGenerate, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotGenerate);
    genContentLayout->addWidget(m_d->btnGenerate);

    m_d->btnInpaint = new QPushButton(ComfyTr::tr("Inpaint (selection)"));
    m_d->btnInpaint->setToolTip(ComfyTr::tr("Generate in selection."));
    connect(m_d->btnInpaint, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotInpaint);
    genContentLayout->addWidget(m_d->btnInpaint);
    // §13.29: Main action menu — switch Generate / Refine / Edit / region / custom without leaving Generate
    {
        QHBoxLayout *opLayout = new QHBoxLayout();
        opLayout->addWidget(new QLabel(ComfyTr::tr("Actions:"), genGroup));
        m_d->btnGenerateViewOperations = new QToolButton(genGroup);
        m_d->btnGenerateViewOperations->setText(ComfyTr::tr("Mode && operations"));
        m_d->btnGenerateViewOperations->setToolTip(ComfyTr::tr(
            "Choose Generate, Refine, Edit, region workflows, or custom graph without leaving this workspace."));
        m_d->btnGenerateViewOperations->setPopupMode(QToolButton::InstantPopup);
        QMenu *opMenu = new QMenu(m_d->btnGenerateViewOperations);
        opMenu->addAction(ComfyTr::tr("Generate"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() != 0)
                m_d->comboWorkspace->setCurrentIndex(0);
            if (m_d->checkEditMode)
                m_d->checkEditMode->setChecked(false);
            setStatusMessage(ComfyTr::tr("Mode: Generate — use the Generate button when ready."));
        });
        opMenu->addAction(ComfyTr::tr("Refine"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() != 0)
                m_d->comboWorkspace->setCurrentIndex(0);
            if (m_d->checkEditMode)
                m_d->checkEditMode->setChecked(false);
            if (m_d->comboInpaintMode) {
                const int fillIdx = m_d->comboInpaintMode->findData(QStringLiteral("fill"));
                if (fillIdx >= 0)
                    m_d->comboInpaintMode->setCurrentIndex(fillIdx);
            }
            setStatusMessage(ComfyTr::tr("Mode: Refine — use Inpaint (selection) with a selection mask."));
        });
        opMenu->addAction(ComfyTr::tr("Edit (instruction-based)"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() != 0)
                m_d->comboWorkspace->setCurrentIndex(0);
            if (m_d->checkEditMode)
                m_d->checkEditMode->setChecked(true);
            setStatusMessage(ComfyTr::tr("Mode: Edit — instruction-based editing; use the Regions list for per-area prompts."));
        });
        opMenu->addAction(ComfyTr::tr("Refine region"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() != 0)
                m_d->comboWorkspace->setCurrentIndex(0);
            if (m_d->checkEditMode)
                m_d->checkEditMode->setChecked(false);
            if (m_d->comboInpaintMode)
                m_d->comboInpaintMode->setCurrentIndex(0);
            setStatusMessage(ComfyTr::tr("Mode: Refine region — add regions below, then Generate regions."));
            if (m_d->regionPromptWidget)
                m_d->regionPromptWidget->setFocus(Qt::OtherFocusReason);
        });
        opMenu->addAction(ComfyTr::tr("Edit (per region)"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() != 0)
                m_d->comboWorkspace->setCurrentIndex(0);
            if (m_d->checkEditMode)
                m_d->checkEditMode->setChecked(true);
            setStatusMessage(ComfyTr::tr("Mode: Edit per region — use the Regions panel, then Generate regions."));
            if (m_d->regionPromptWidget)
                m_d->regionPromptWidget->setFocus(Qt::OtherFocusReason);
        });
        opMenu->addAction(ComfyTr::tr("Edit (Custom)"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->count() > 4)
                m_d->comboWorkspace->setCurrentIndex(4);
            setStatusMessage(ComfyTr::tr("Mode: Custom workflow — set API JSON under Settings → Workflow, then use Generate."));
        });
        opMenu->addAction(ComfyTr::tr("Generate region"), this, [this]() {
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() != 0)
                m_d->comboWorkspace->setCurrentIndex(0);
            if (m_d->checkEditMode)
                m_d->checkEditMode->setChecked(false);
            setStatusMessage(ComfyTr::tr("Mode: Generate region — choose a region below, then Generate regions."));
            if (m_d->regionPromptWidget)
                m_d->regionPromptWidget->setFocus(Qt::OtherFocusReason);
        });
        m_d->btnGenerateViewOperations->setMenu(opMenu);
        opLayout->addWidget(m_d->btnGenerateViewOperations);
        opLayout->addStretch();
        genContentLayout->addLayout(opLayout);
    }
    // §13.206 / P4.1: InpaintMode — all seven Python modes with theme icons
    auto addInpaintComboItem = [](QComboBox *cb, const QString &label, const QString &data, const char *iconStem) {
        cb->addItem(ComfyTheme::icon(QString::fromUtf8(iconStem)), label, data);
    };
    m_d->comboInpaintMode = new QComboBox(genGroup);
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Default (Auto-detect)"), QStringLiteral("automatic"), "inpaint-automatic");
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Fill"), QStringLiteral("fill"), "inpaint-fill");
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Expand"), QStringLiteral("expand"), "inpaint-expand");
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Add Content"), QStringLiteral("add_object"), "inpaint-add_object");
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Remove Content"), QStringLiteral("remove_object"), "inpaint-remove_object");
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Replace Background"), QStringLiteral("replace_background"),
                        "inpaint-replace_background");
    addInpaintComboItem(m_d->comboInpaintMode, ComfyTr::tr("Generate (Custom)"), QStringLiteral("custom"), "inpaint-custom");
    m_d->comboInpaintMode->setToolTip(
        ComfyTr::tr("Automatic: expand if selection touches canvas edge, else fill. Other modes set fill semantics and prompt instructions."));
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        QString savedMode = cfg.readEntry(QStringLiteral("InpaintModeKey"), QString());
        if (savedMode.isEmpty()) {
            static const char *legacyModes[] = {"automatic", "fill", "expand"};
            const int legacyIdx = qBound(0, cfg.readEntry("InpaintMode", 0), 2);
            savedMode = QString::fromUtf8(legacyModes[legacyIdx]);
        }
        setComboCurrentItemData(m_d->comboInpaintMode, savedMode, 0);
    }
    connect(m_d->comboInpaintMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry(
            QStringLiteral("InpaintModeKey"), m_d->comboInpaintMode->currentData().toString());
        if (m_d->comboFillMode) {
            const QString mode = m_d->comboInpaintMode->currentData().toString();
            if (mode != QLatin1String("automatic")) {
                QSignalBlocker b(m_d->comboFillMode);
                setComboCurrentItemData(m_d->comboFillMode, ComfyUIUtils::defaultFillKindForInpaintMode(mode), 2);
            }
        }
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0)
            saveInpaintWorkspaceToDocument();
        schedulePersistDocumentDefaults();
    });
    genContentLayout->addWidget(m_d->comboInpaintMode);
    // §13.188: FillMode UI — five options (None, Neutral, Blur, Border, Inpaint); replace/green internal only
    m_d->comboFillMode = new QComboBox(genGroup);
    const QIcon fillIcon = ComfyTheme::icon(QStringLiteral("fill"));
    m_d->comboFillMode->addItem(ComfyTheme::icon(QStringLiteral("fill-empty")),
                                ComfyTr::tr("None"),
                                QStringLiteral("none"));
    m_d->comboFillMode->addItem(fillIcon, ComfyTr::tr("Neutral"), QStringLiteral("neutral"));
    m_d->comboFillMode->addItem(fillIcon, ComfyTr::tr("Blur"), QStringLiteral("blur"));
    m_d->comboFillMode->addItem(fillIcon, ComfyTr::tr("Border"), QStringLiteral("border"));
    m_d->comboFillMode->addItem(fillIcon, ComfyTr::tr("Inpaint"), QStringLiteral("inpaint"));
    m_d->comboFillMode->setToolTip(ComfyTr::tr("Pre-fill the selected region before diffusion"));
    int savedFillMode = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("FillMode", 2);  // default Blur
    m_d->comboFillMode->setCurrentIndex(qBound(0, savedFillMode, m_d->comboFillMode->count() - 1));
    connect(m_d->comboFillMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("FillMode", idx);
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0)
            saveInpaintWorkspaceToDocument();
        schedulePersistDocumentDefaults();
    });
    genContentLayout->addWidget(m_d->comboFillMode);
    // §13.169 / §13.194: Inpaint context (Python InpaintContext; JSON uses underscores)
    m_d->comboInpaintContext = new QComboBox(genGroup);
    addInpaintComboItem(m_d->comboInpaintContext, ComfyTr::tr("Automatic"), QStringLiteral("automatic"), "context-automatic");
    addInpaintComboItem(m_d->comboInpaintContext, ComfyTr::tr("Entire image"), QStringLiteral("entire_image"), "context-image");
    addInpaintComboItem(m_d->comboInpaintContext, ComfyTr::tr("Mask bounds"), QStringLiteral("mask_bounds"), "context-mask");
    addInpaintComboItem(m_d->comboInpaintContext, ComfyTr::tr("Layer bounds"), QStringLiteral("layer_bounds"), "context-layer");
    m_d->comboInpaintContext->setToolTip(ComfyTr::tr("Region of the canvas and mask sent to the server for inpaint / selection."));
    {
        const QString savedCtx = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry(QStringLiteral("InpaintContext"), QStringLiteral("automatic"));
        const int cix = m_d->comboInpaintContext->findData(savedCtx);
        m_d->comboInpaintContext->setCurrentIndex(cix >= 0 ? cix : 0);
    }
    connect(m_d->comboInpaintContext, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry(QStringLiteral("InpaintContext"),
                                                                       m_d->comboInpaintContext->currentData().toString());
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0)
            saveInpaintWorkspaceToDocument();
        schedulePersistDocumentDefaults();
    });
    genContentLayout->addWidget(m_d->comboInpaintContext);
    // §13.107 / §13.169: CustomInpaint toggles (Python: Seamless / Focus)
    m_d->checkInpaintUseModel = new ComfySwitchWidget(genGroup);
    {
        QLabel *seamlessLabel = new QLabel(ComfyTr::tr("Seamless"), genGroup);
        QHBoxLayout *seamlessRow = new QHBoxLayout();
        seamlessRow->setContentsMargins(0, 0, 0, 0);
        seamlessRow->addWidget(m_d->checkInpaintUseModel);
        seamlessRow->addWidget(seamlessLabel, 1);
        genContentLayout->addLayout(seamlessRow);
    }
    m_d->checkInpaintUseModel->setToolTip(ComfyTr::tr("Generate content which blends into the surroundings"));
    m_d->checkInpaintUseModel->setChecked(
        KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry(QStringLiteral("InpaintUseModel"), true));
    m_d->inpaintPersistUseModel = m_d->checkInpaintUseModel->isChecked();
    connect(m_d->checkInpaintUseModel, &QAbstractButton::toggled, this, [this](bool on) {
        m_d->inpaintPersistUseModel = on;
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry(QStringLiteral("InpaintUseModel"), on);
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0)
            saveInpaintWorkspaceToDocument();
        schedulePersistDocumentDefaults();
    });
    m_d->checkInpaintUsePromptFocus = new ComfySwitchWidget(genGroup);
    {
        QLabel *focusLabel = new QLabel(ComfyTr::tr("Focus"), genGroup);
        QHBoxLayout *focusRow = new QHBoxLayout();
        focusRow->setContentsMargins(0, 0, 0, 0);
        focusRow->addWidget(m_d->checkInpaintUsePromptFocus);
        focusRow->addWidget(focusLabel, 1);
        genContentLayout->addLayout(focusRow);
    }
    m_d->checkInpaintUsePromptFocus->setToolTip(
        ComfyTr::tr("Focus generation on the masked area using prompt conditioning (SD 1.5 / SDXL)."));
    m_d->checkInpaintUsePromptFocus->setChecked(
        KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry(QStringLiteral("InpaintUsePromptFocus"), false));
    m_d->inpaintPersistUsePromptFocus = m_d->checkInpaintUsePromptFocus->isChecked();
    connect(m_d->checkInpaintUsePromptFocus, &QAbstractButton::toggled, this, [this](bool on) {
        m_d->inpaintPersistUsePromptFocus = on;
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry(QStringLiteral("InpaintUsePromptFocus"), on);
        if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0)
            saveInpaintWorkspaceToDocument();
        schedulePersistDocumentDefaults();
    });
    connect(m_d->comboCheckpoint, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        updateInpaintControlsForArch();
    });
    if (m_d->comboCheckpoint->lineEdit()) {
        connect(m_d->comboCheckpoint->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
            updateInpaintControlsForArch();
        });
    }
    if (m_d->spinStrength) {
        connect(m_d->spinStrength, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            updateInpaintControlsForArch();
        });
    }
    if (m_d->checkEditMode) {
        connect(m_d->checkEditMode, &QCheckBox::toggled, this, [this](bool) { updateInpaintControlsForArch(); });
    }
    updateInpaintControlsForArch();

    ComfyTheme::applyFlatComboStyle(m_d->comboInpaintMode);
    ComfyTheme::applyFlatComboStyle(m_d->comboFillMode);
    ComfyTheme::applyFlatComboStyle(m_d->comboInpaintContext);
    ComfyTheme::applyFlatComboStyle(m_d->comboPreset);
    ComfyTheme::applyFlatComboStyle(m_d->comboCheckpoint);
    ComfyTheme::applyFlatComboStyle(m_d->comboWorkspace);
    ComfyTheme::applyFlatComboStyle(m_d->comboQuality);
    ComfyTheme::applyFlatComboStyle(m_d->comboQueueMode);
    ComfyTheme::applyFlatComboStyle(m_d->comboSampler);

    m_d->btnUpscale = new QPushButton(ComfyTr::tr("Upscale"));
    m_d->btnUpscale->setToolTip(ComfyTr::tr(
        "Upscale the canvas at the scale factor above (ComfyUI ImageScale). With \"Refine upscaled image\" enabled, runs a diffusion pass after scaling."));
    connect(m_d->btnUpscale, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotUpscale);
    genContentLayout->addWidget(m_d->btnUpscale);

    // §5.6 Live / §5.7 Animation: Full Animation / Single Frame radio (batch_mode); visible when workspace is Live or Animation
    m_d->batchModeRow = new QWidget(genGroup);
    QHBoxLayout *batchModeLayout = new QHBoxLayout(m_d->batchModeRow);
    m_d->batchModeRow->setContentsMargins(0, 0, 0, 0);
    m_d->radioSingleFrame = new QRadioButton(ComfyTr::tr("Single Frame"), m_d->batchModeRow);
    m_d->radioFullAnimation = new QRadioButton(ComfyTr::tr("Full Animation"), m_d->batchModeRow);
    m_d->radioSingleFrame->setToolTip(ComfyTr::tr("Generate a single image at current time."));
    m_d->radioFullAnimation->setToolTip(ComfyTr::tr("Generate multiple frames (animation)."));
    m_d->batchModeGroup = new QButtonGroup(m_d->batchModeRow);
    m_d->batchModeGroup->addButton(m_d->radioSingleFrame);
    m_d->batchModeGroup->addButton(m_d->radioFullAnimation);
    bool fullAnimation = KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("FullAnimation", false);
    m_d->radioFullAnimation->setChecked(fullAnimation);
    m_d->radioSingleFrame->setChecked(!fullAnimation);
    batchModeLayout->addWidget(m_d->radioSingleFrame);
    batchModeLayout->addWidget(m_d->radioFullAnimation);
    batchModeLayout->addStretch();
    genContentLayout->addWidget(m_d->batchModeRow);
    m_d->batchModeRow->setVisible(false);

    // §13.74: Single Frame — choose paint layer that receives output (persisted in ui.json animation.target_layer)
    m_d->animationTargetRow = new QWidget(genGroup);
    QHBoxLayout *animTargetLayout = new QHBoxLayout(m_d->animationTargetRow);
    m_d->animationTargetRow->setContentsMargins(0, 0, 0, 0);
    // §5.7: dropdown lists each paint layer as "Target layer: {name}" (no separate label — text is per item)
    m_d->comboAnimationTargetLayer = new QComboBox(m_d->animationTargetRow);
    m_d->comboAnimationTargetLayer->setAccessibleName(ComfyTr::tr("Target layer"));
    m_d->comboAnimationTargetLayer->setToolTip(
        ComfyTr::tr("Paint layer that receives Single Frame generation output (Animation workspace)."));
    connect(m_d->comboAnimationTargetLayer, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_d->canvas && m_d->canvas->image())
            scheduleDocumentUiJsonSave();
        if (m_d->animationPreviewRow && m_d->animationPreviewRow->isVisible() && m_d->animationPreviewDebounce) {
            m_d->animationPreviewDebounce->stop();
            m_d->animationPreviewDebounce->start();
        }
    });
    animTargetLayout->addWidget(m_d->comboAnimationTargetLayer, 1);
    genContentLayout->addWidget(m_d->animationTargetRow);
    m_d->animationTargetRow->setVisible(false);

    // §13.74: preview of last Single Frame result (Animation workspace)
    m_d->animationPreviewRow = new QWidget(genGroup);
    QVBoxLayout *animPreviewLayout = new QVBoxLayout(m_d->animationPreviewRow);
    m_d->animationPreviewRow->setContentsMargins(0, 0, 0, 0);
    animPreviewLayout->addWidget(new QLabel(ComfyTr::tr("Frame preview:"), m_d->animationPreviewRow));
    m_d->labelAnimationPreview = new QLabel(m_d->animationPreviewRow);
    m_d->labelAnimationPreview->setAlignment(Qt::AlignCenter);
    m_d->labelAnimationPreview->setMinimumHeight(96);
    m_d->labelAnimationPreview->setMaximumHeight(220);
    m_d->labelAnimationPreview->setScaledContents(false);
    m_d->labelAnimationPreview->setFrameShape(QFrame::StyledPanel);
    m_d->labelAnimationPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    animPreviewLayout->addWidget(m_d->labelAnimationPreview);
    genContentLayout->addWidget(m_d->animationPreviewRow);
    m_d->animationPreviewRow->setVisible(false);

    connect(m_d->batchModeGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this](QAbstractButton *) {
        const bool full = m_d->radioFullAnimation && m_d->radioFullAnimation->isChecked();
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("FullAnimation", full);
        updateAnimationButtonLabel();
        updateAnimationTargetLayerRowVisibility();
        if (m_d->canvas && m_d->canvas->image())
            scheduleDocumentUiJsonSave();
    });

    QHBoxLayout *animRow = new QHBoxLayout();
    m_d->spinAnimationFrames = new QSpinBox();
    m_d->spinAnimationFrames->setRange(2, 16);
    m_d->spinAnimationFrames->setValue(4);
    m_d->spinAnimationFrames->setToolTip(ComfyTr::tr("Number of frames (seeds: seed, seed+1, …)"));
    m_d->btnGenerateAnimation = new QPushButton(ComfyTr::tr("Generate animation"));
    m_d->btnGenerateAnimation->setToolTip(ComfyTr::tr("Generate N images with sequential seeds as new layers."));
    connect(m_d->btnGenerateAnimation, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotGenerateAnimation);
    animRow->addWidget(new QLabel(ComfyTr::tr("Frames:")));
    animRow->addWidget(m_d->spinAnimationFrames);
    animRow->addWidget(m_d->btnGenerateAnimation);
    genContentLayout->addLayout(animRow);

    // §13.45: Import Animation — import frames from .animation or .live-frames into document
    m_d->btnImportAnimation = new QPushButton(ComfyTr::tr("Import Animation"), genGroup);
    m_d->btnImportAnimation->setToolTip(ComfyTr::tr("Import frame images from the document's .animation or .live-frames folder as keyframes."));
    connect(m_d->btnImportAnimation, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotImportAnimation);
    genContentLayout->addWidget(m_d->btnImportAnimation);
    m_d->btnImportAnimation->setVisible(false);

    m_d->checkLiveMode = new QCheckBox(ComfyTr::tr("Live (periodic img2img from canvas)"));
    m_d->checkLiveMode->setToolTip(ComfyTr::tr("Every 30 s: export canvas, run img2img, apply result as new layer. Stop by unchecking."));
    connect(m_d->checkLiveMode, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) m_d->liveTimer->start(30000);
        else { m_d->liveTimer->stop(); m_d->livePollTimer->stop(); stopLiveSpinner(); }
    });
    genContentLayout->addWidget(m_d->checkLiveMode);
    // §13.45: Record — save each live result to .live-frames/frame-N.webp for later Import Animation
    m_d->checkLiveRecord = new QCheckBox(ComfyTr::tr("Record (save frames to .live-frames)"));
    m_d->checkLiveRecord->setToolTip(ComfyTr::tr("When enabled, each live result is saved to the document's .live-frames folder as frame-N.webp. Use Import Animation to add them to the document."));
    connect(m_d->checkLiveRecord, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_d->liveFrameIndex = 0;
        } else {
            // §13.149: When recording stops, import_animation is called (frames from .live-frames)
            slotImportAnimation();
        }
    });
    genContentLayout->addWidget(m_d->checkLiveRecord);
    // §13.105: compact progress indicator for Live view (next to live checkbox)
    m_d->liveSpinner = new LiveSpinnerWidget(this);
    m_d->liveSpinner->hide();
    genContentLayout->addWidget(m_d->liveSpinner);

    // Queue popup (similar to krita-ai Queue button)
    m_d->labelQueueCount = new QLabel(ComfyTr::tr("Queue: 0"));
    m_d->comboQueueMode = new QComboBox();
    m_d->comboQueueMode->addItem(ComfyTr::tr("at the Back"), 0);
    m_d->comboQueueMode->addItem(ComfyTr::tr("in Front (new jobs first)"), 1);
    m_d->comboQueueMode->addItem(ComfyTr::tr("Replace Queue"), 2);
    m_d->comboQueueMode->setToolTip(ComfyTr::tr("at the Back: add after current jobs. in Front: new jobs run first. Replace Queue: clear queue then add."));
    m_d->spinBatchCount = new QSpinBox();
    m_d->spinBatchCount->setRange(1, 10);
    m_d->spinBatchCount->setToolTip(ComfyTr::tr("Number of images to generate per click"));

    m_d->btnQueuePopup = new ComfyQueueButton();
    m_d->btnQueuePopup->setToolTip(ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));

    QMenu *queueMenu = new QMenu(m_d->btnQueuePopup);
    QWidget *queueWidget = new QWidget(queueMenu);
    QVBoxLayout *queueLayout = new QVBoxLayout(queueWidget);
    queueLayout->setContentsMargins(8, 8, 8, 8);

    QHBoxLayout *countsLayout = new QHBoxLayout();
    countsLayout->addWidget(new QLabel(ComfyTr::tr("Jobs:"), queueWidget));
    countsLayout->addWidget(m_d->labelQueueCount, 1);
    queueLayout->addLayout(countsLayout);

    m_d->queueBatchOptionsRow = new QWidget(queueWidget);
    QHBoxLayout *batchLayout = new QHBoxLayout(m_d->queueBatchOptionsRow);
    batchLayout->setContentsMargins(0, 0, 0, 0);
    batchLayout->addWidget(new QLabel(ComfyTr::tr("Batch:"), m_d->queueBatchOptionsRow));
    batchLayout->addWidget(m_d->spinBatchCount, 1);
    queueLayout->addWidget(m_d->queueBatchOptionsRow);

    // Resolution multiplier (similar to krita-ai)
    m_d->sliderResolutionMultiplier = new QSlider(Qt::Horizontal, queueWidget);
    m_d->sliderResolutionMultiplier->setRange(3, 15); // §13.213: 3–15 → 0.3–1.5
    m_d->labelResolutionMultiplier = new QLabel(queueWidget);
    m_d->labelResolutionMultiplier->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int storedBatch = cfg.readEntry("BatchCount", 1);
        m_d->spinBatchCount->setValue(qBound(1, storedBatch, 10));
        const int storedQueueMode = cfg.readEntry("QueueMode", 0);
        if (storedQueueMode >= 0 && storedQueueMode < m_d->comboQueueMode->count()) {
            m_d->comboQueueMode->setCurrentIndex(storedQueueMode);
        } else {
            m_d->comboQueueMode->setCurrentIndex(0);
        }
        const double storedMul = cfg.readEntry("ResolutionMultiplier", 1.0);
        m_d->resolutionMultiplier = storedMul <= 0.0 ? 1.0 : storedMul;
        int sliderValue = qRound(m_d->resolutionMultiplier * 10.0);
        sliderValue = qBound(3, sliderValue, 15);
        m_d->sliderResolutionMultiplier->setValue(sliderValue);
        m_d->labelResolutionMultiplier->setText(QString::number(m_d->resolutionMultiplier, 'f', 1) + QLatin1String("×"));
        const bool fixedSeed = cfg.readEntry("FixedSeed", false);
        const qint64 seedValue = cfg.readEntry("SeedValue", qint64(0));
        m_d->checkFixedSeed->setChecked(fixedSeed);
        m_d->spinSeed->setValue(static_cast<int>(seedValue));
    }
    connect(m_d->sliderResolutionMultiplier, &QSlider::valueChanged, this, [this](int v) {
        m_d->resolutionMultiplier = qMax(0.3, v / 10.0);
        m_d->labelResolutionMultiplier->setText(QString::number(m_d->resolutionMultiplier, 'f', 1) + QLatin1String("×"));
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ResolutionMultiplier", m_d->resolutionMultiplier);
    });
    connect(m_d->spinBatchCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("BatchCount", value);
        schedulePersistDocumentDefaults();
    });
    connect(m_d->comboQueueMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("QueueMode", index);
    });
    connect(m_d->checkFixedSeed, &QCheckBox::toggled, this, [this](bool) {
        persistSeedToConfig();
        syncQueueSeedWidgetsFromMain();
    });
    connect(m_d->spinSeed, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        persistSeedToConfig();
        syncQueueSeedWidgetsFromMain();
    });
    m_d->queueResolutionRow = new QWidget(queueWidget);
    QHBoxLayout *resLayout = new QHBoxLayout(m_d->queueResolutionRow);
    resLayout->setContentsMargins(0, 0, 0, 0);
    resLayout->addWidget(new QLabel(ComfyTr::tr("Resolution:"), m_d->queueResolutionRow));
    resLayout->addWidget(m_d->sliderResolutionMultiplier, 1);
    resLayout->addWidget(m_d->labelResolutionMultiplier);
    queueLayout->addWidget(m_d->queueResolutionRow);

    m_d->queueCheckFixedSeed = new QCheckBox(ComfyTr::tr("Fixed seed"), queueWidget);
    m_d->queueSpinSeed = new QSpinBox(queueWidget);
    m_d->queueSpinSeed->setRange(0, 2147483647);
    m_d->queueSpinSeed->setValue(m_d->spinSeed->value());
    m_d->queueCheckFixedSeed->setChecked(m_d->checkFixedSeed->isChecked());
    m_d->queueBtnRandomSeed = new QPushButton(queueWidget);
    m_d->queueBtnRandomSeed->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("random"))));
    m_d->queueBtnRandomSeed->setToolTip(ComfyTr::tr("Pick a new random seed."));
    m_d->queueBtnRandomSeed->setAccessibleName(ComfyTr::tr("Random seed"));
    connect(m_d->queueBtnRandomSeed, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRandomSeed);
    auto copyQueueSeedToMain = [this]() {
        if (!m_d->queueSpinSeed || !m_d->spinSeed || !m_d->checkFixedSeed || !m_d->queueCheckFixedSeed)
            return;
        m_d->checkFixedSeed->blockSignals(true);
        m_d->spinSeed->blockSignals(true);
        m_d->checkFixedSeed->setChecked(m_d->queueCheckFixedSeed->isChecked());
        m_d->spinSeed->setValue(m_d->queueSpinSeed->value());
        m_d->checkFixedSeed->blockSignals(false);
        m_d->spinSeed->blockSignals(false);
        persistSeedToConfig();
    };
    connect(m_d->queueSpinSeed, QOverload<int>::of(&QSpinBox::valueChanged), this, [copyQueueSeedToMain](int) {
        copyQueueSeedToMain();
    });
    connect(m_d->queueCheckFixedSeed, &QCheckBox::toggled, this, [copyQueueSeedToMain](bool) {
        copyQueueSeedToMain();
    });

    QHBoxLayout *seedLayout = new QHBoxLayout();
    seedLayout->addWidget(m_d->queueCheckFixedSeed);
    seedLayout->addWidget(m_d->queueSpinSeed, 1);
    seedLayout->addWidget(m_d->queueBtnRandomSeed);
    queueLayout->addLayout(seedLayout);

    connect(queueMenu, &QMenu::aboutToShow, this, [this]() {
        syncQueueSeedWidgetsFromMain();
        refreshQueuePopupSupportsBatch();
    });

    m_d->queueEnqueueModeRow = new QWidget(queueWidget);
    QHBoxLayout *modeLayout = new QHBoxLayout(m_d->queueEnqueueModeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->addWidget(new QLabel(ComfyTr::tr("Enqueue:"), m_d->queueEnqueueModeRow));
    modeLayout->addWidget(m_d->comboQueueMode, 1);
    queueLayout->addWidget(m_d->queueEnqueueModeRow);

    QPushButton *popupCancel = new QPushButton(ComfyTr::tr("Cancel all"), queueWidget);
    popupCancel->setIcon(KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("cancel"))));
    popupCancel->setToolTip(ComfyTr::tr("Stop the running job and clear the queue (Cancel All)."));
    connect(popupCancel, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotCancelQueue);
    queueLayout->addWidget(popupCancel);

    QWidgetAction *queueAction = new QWidgetAction(queueMenu);
    queueAction->setDefaultWidget(queueWidget);
    queueMenu->addAction(queueAction);
    m_d->btnQueuePopup->setMenu(queueMenu);

    m_d->queueButtonRowWidget = new QWidget(m_d->genContentContainer);
    QHBoxLayout *queueRow = new QHBoxLayout(m_d->queueButtonRowWidget);
    queueRow->setContentsMargins(0, 0, 0, 0);
    queueRow->addWidget(m_d->btnQueuePopup);
    queueRow->addStretch();
    genContentLayout->addWidget(m_d->queueButtonRowWidget);

    m_d->btnCancelQueue = popupCancel;
    m_d->btnCancelQueue->setEnabled(false);

    m_d->progressBar = new QProgressBar();
    m_d->progressBar->setMinimum(0);
    m_d->progressBar->setMaximum(100);
    m_d->progressBar->setValue(0);
    m_d->progressBar->setTextVisible(false);
    m_d->progressBar->setFixedHeight(6);
    setProgressBarKind(false);  // §13.18: default = generation
    genContentLayout->addWidget(m_d->progressBar);

    setupRootControlLayersUi(m_d->genContentContainer, genContentLayout);

    // §13.49 / §13.53: Control-layer timing range + server preprocessor preview (Generate workspace only)
    m_d->controlPreviewGroupBox = new QGroupBox(ComfyTr::tr("Control preprocessor preview"), m_d->genContentContainer);
    QVBoxLayout *cpLay = new QVBoxLayout(m_d->controlPreviewGroupBox);
    m_d->comboControlPreviewMode = new QComboBox(m_d->controlPreviewGroupBox);
    m_d->comboControlPreviewMode->setToolTip(
        ComfyTr::tr("Preprocessor applied to the current canvas image on the ComfyUI server (control.json modes)."));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Depth"), QStringLiteral("depth"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Canny edge"), QStringLiteral("canny_edge"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Scribble"), QStringLiteral("scribble"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Line art"), QStringLiteral("line_art"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Soft edge"), QStringLiteral("soft_edge"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Hands"), QStringLiteral("hands"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Normal map"), QStringLiteral("normal"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Pose"), QStringLiteral("pose"));
    m_d->comboControlPreviewMode->addItem(ComfyTr::tr("Segmentation"), QStringLiteral("segmentation"));
    cpLay->addWidget(new QLabel(ComfyTr::tr("Mode:"), m_d->controlPreviewGroupBox));
    cpLay->addWidget(m_d->comboControlPreviewMode);
    cpLay->addWidget(new QLabel(ComfyTr::tr("Control timing range (%):"), m_d->controlPreviewGroupBox));
    m_d->controlPreviewRangeSlider = new ComfyUIIntervalSlider(m_d->controlPreviewGroupBox);
    m_d->controlPreviewRangeSlider->setRange(0, 100);
    m_d->controlPreviewRangeSlider->setToolTip(
        ComfyTr::tr("Low and high timing range for control strength (persisted; used when adding control layers)."));
    cpLay->addWidget(m_d->controlPreviewRangeSlider);
    connect(m_d->controlPreviewRangeSlider, &ComfyUIIntervalSlider::intervalChanged, this, [](int low, int high) {
        KConfigGroup g = KSharedConfig::openConfig()->group("ComfyUIRemote");
        g.writeEntry("control_layer_timing_low_pct", low);
        g.writeEntry("control_layer_timing_high_pct", high);
    });
    m_d->btnControlPreviewRun = new QPushButton(ComfyTr::tr("Run preprocessor preview"), m_d->controlPreviewGroupBox);
    m_d->btnControlPreviewRun->setToolTip(
        ComfyTr::tr("Upload the canvas, run the selected preprocessor on the server, and show the result below."));
    connect(m_d->btnControlPreviewRun, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotControlPreviewRun);
    cpLay->addWidget(m_d->btnControlPreviewRun);
    {
        QHBoxLayout *poseRow = new QHBoxLayout();
        poseRow->addWidget(new QLabel(ComfyTr::tr("Pose guide people:"), m_d->controlPreviewGroupBox));
        m_d->spinPoseGuidePeopleCount = new QSpinBox(m_d->controlPreviewGroupBox);
        m_d->spinPoseGuidePeopleCount->setRange(1, 3);
        m_d->spinPoseGuidePeopleCount->setToolTip(
            ComfyTr::tr("Number of default stick figures to add (Pose.create_default people_count)."));
        connect(m_d->spinPoseGuidePeopleCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int v) {
            KConfigGroup g = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
            g.writeEntry(QStringLiteral("pose_guide_people_count"), v);
        });
        poseRow->addWidget(m_d->spinPoseGuidePeopleCount);
        m_d->btnAddPoseGuide = new QPushButton(ComfyTr::tr("Add pose guide (vector layer)"), m_d->controlPreviewGroupBox);
        m_d->btnAddPoseGuide->setToolTip(
            ComfyTr::tr("Adds default stick-figure skeleton(s) to the selected vector layer and refreshes pose data from its SVG every 500 ms (same idea as the reference PoseLayers singleton)."));
        connect(m_d->btnAddPoseGuide, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotAddPoseGuideToVectorLayer);
        poseRow->addWidget(m_d->btnAddPoseGuide);
        poseRow->addStretch();
        cpLay->addLayout(poseRow);
    }
    m_d->labelControlPreviewImage = new QLabel(m_d->controlPreviewGroupBox);
    m_d->labelControlPreviewImage->setMinimumSize(160, 160);
    m_d->labelControlPreviewImage->setMaximumHeight(200);
    m_d->labelControlPreviewImage->setAlignment(Qt::AlignCenter);
    m_d->labelControlPreviewImage->setFrameShape(QFrame::Box);
    m_d->labelControlPreviewImage->setScaledContents(false);
    m_d->labelControlPreviewImage->setWordWrap(true);
    cpLay->addWidget(m_d->labelControlPreviewImage);
    genContentLayout->addWidget(m_d->controlPreviewGroupBox);
    m_d->controlPreviewGroupBox->setVisible(m_d->comboWorkspace->currentIndex() == 0);
    syncControlPreviewRangeFromSettings();
    syncPoseGuidePeopleCountFromSettings();

    genLayout->addWidget(m_d->genContentContainer);

    m_d->graphPlaceholderWidget = new QWidget(genGroup);
    m_d->graphWorkflowEditorLayout = new QVBoxLayout(m_d->graphPlaceholderWidget);
    QLabel *graphLabel = new QLabel(ComfyTr::tr("Paste ComfyUI API JSON below, then click Generate (results in History)."));
    graphLabel->setWordWrap(true);
    m_d->graphWorkflowEditorLayout->addWidget(graphLabel);
    QPushButton *btnGraphLoadWorkflow = new QPushButton(ComfyTr::tr("Load from file…"), m_d->graphPlaceholderWidget);
    connect(btnGraphLoadWorkflow, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotLoadWorkflowFromFile);
    m_d->graphWorkflowEditorLayout->addWidget(btnGraphLoadWorkflow);
    m_d->graphWorkflowEditorLayout->insertWidget(1, m_d->editCustomWorkflow);
    m_d->editCustomWorkflow->setVisible(true);
    // §13.170: Open Web UI — open client.url in default browser (QDesktopServices::openUrl)
    QPushButton *btnOpenWebUI = new QPushButton(ComfyTr::tr("Open Web UI"));
    btnOpenWebUI->setToolTip(ComfyTr::tr("Open Web UI to create custom workflows"));
    connect(btnOpenWebUI, &QPushButton::clicked, this, [this](bool) {
        QString urlStr = m_d->editServerUrl->text().trimmed();
        if (urlStr.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Set server URL in Settings first."), true);
            return;
        }
        QUrl url(urlStr);
        if (!url.scheme().isEmpty() && url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
            urlStr = QStringLiteral("http://") + urlStr;
        else if (url.scheme().isEmpty())
            urlStr = QStringLiteral("http://") + urlStr;
        QDesktopServices::openUrl(QUrl(urlStr));
        beginWebWorkflowSwitch();
    });
    m_d->graphWorkflowEditorLayout->addWidget(btnOpenWebUI);
    QPushButton *btnOpenSettingsForGraph = new QPushButton(ComfyTr::tr("Open Settings"));
    connect(btnOpenSettingsForGraph, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotConfigureHelp);
    m_d->graphWorkflowEditorLayout->addWidget(btnOpenSettingsForGraph);
    m_d->graphPlaceholderWidget->setVisible(false);
    genLayout->addWidget(m_d->graphPlaceholderWidget);

    scrollLayout->addWidget(genGroup);

    QGroupBox *histGroup = new QGroupBox(ComfyTr::tr("History"));
    m_d->histGroupBox = histGroup;
    QVBoxLayout *histLayout = new QVBoxLayout(histGroup);
    histLayout->addWidget(new QLabel(ComfyTr::tr("Results (double-click Apply, right-click for menu):")));
    m_d->listHistory = new QListWidget();
    m_d->listHistory->setMaximumHeight(140);
    m_d->listHistory->setViewMode(QListWidget::IconMode);
    m_d->listHistory->setIconSize(QSize(96, 96));  // §13.28a: thumbnail size 96×96 px
    m_d->listHistory->setFlow(QListView::LeftToRight);
    m_d->listHistory->setResizeMode(QListView::Adjust);
    m_d->listHistory->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_d->listHistory->setFrameShape(QFrame::NoFrame);
    m_d->listHistory->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // §13.28a: vertical scroll only
    m_d->listHistory->setSpacing(4);
    m_d->listHistory->setMovement(QListWidget::Static);
    m_d->listHistory->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_d->listHistory, &QListWidget::customContextMenuRequested, this, &ComfyUIRemoteDock::slotHistoryContextMenu);
    connect(m_d->listHistory, &QListWidget::itemSelectionChanged, this, &ComfyUIRemoteDock::slotHistoryItemSelected);
    connect(m_d->listHistory, &QListWidget::doubleClicked, this, &ComfyUIRemoteDock::slotHistoryApply);
    histLayout->addWidget(m_d->listHistory);
    QHBoxLayout *historyBtns = new QHBoxLayout();
    m_d->btnHistoryReRun = new QPushButton(ComfyTr::tr("Re-run"));
    m_d->btnHistoryApply = new QPushButton(ComfyTr::tr("Apply"));
    connect(m_d->btnHistoryReRun, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotHistoryReRun);
    connect(m_d->btnHistoryApply, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotHistoryApply);
    historyBtns->addWidget(m_d->btnHistoryReRun);
    historyBtns->addWidget(m_d->btnHistoryApply);
    histLayout->addLayout(historyBtns);
    m_d->btnHistoryReRun->setEnabled(false);
    m_d->btnHistoryApply->setEnabled(false);
    scrollLayout->addWidget(histGroup);

    m_d->regionsGroupBox = new QGroupBox(ComfyTr::tr("Regions"));
    QVBoxLayout *regLayout = new QVBoxLayout(m_d->regionsGroupBox);
    // §13.90: PromptHeader — full (title + description), icon (icon only), none (no header)
    QHBoxLayout *regionHeaderRow = new QHBoxLayout();
    regionHeaderRow->addWidget(new QLabel(ComfyTr::tr("Header:")));
    m_d->regionHeaderCombo = new QComboBox();
    m_d->regionHeaderCombo->addItem(ComfyTr::tr("Full"), 0);
    m_d->regionHeaderCombo->addItem(ComfyTr::tr("Icon"), 1);
    m_d->regionHeaderCombo->addItem(ComfyTr::tr("None"), 2);
    m_d->regionHeaderCombo->setCurrentIndex(qBound(0, m_d->promptHeaderMode, 2));
    m_d->regionHeaderCombo->setToolTip(ComfyTr::tr("Region prompt header style: Full text, Icon only, or None (compact)."));
    connect(m_d->regionHeaderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_d->promptHeaderMode = qBound(0, idx, 2);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("PromptHeader", m_d->promptHeaderMode);
        applyPromptHeader();
    });
    regionHeaderRow->addWidget(m_d->regionHeaderCombo);
    regionHeaderRow->addStretch();
    regLayout->addLayout(regionHeaderRow);
    m_d->regionHeaderLabel = new QLabel(ComfyTr::tr("Different prompt per area (layer or selection):"));
    regLayout->addWidget(m_d->regionHeaderLabel);
    m_d->regionPromptWidget = new ComfyRegionPromptWidget(m_d->regionsGroupBox);
    m_d->regionPromptWidget->setPromptHeaderMode(m_d->promptHeaderMode);
    m_d->regionPromptWidget->setRootPromptEditors(m_d->editPrompt, m_d->editNegative);
    m_d->regionPromptWidget->setShowNegativePrompt(m_d->negativePromptBlock && m_d->negativePromptBlock->isVisible());
    {
        QJsonObject st = ComfyUIUtils::loadSettingsJson();
        m_d->regionPromptWidget->setPromptTranslationCode(st.value(QStringLiteral("prompt_translation")).toString());
    }
    m_d->regionPromptWidget->bind(&comfyActiveRegionEntries(m_d.data()), &m_d->activeRegionIndex);
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::activeIndexChanged, this,
            &ComfyUIRemoteDock::refreshRegionControlLayersList);
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::regionEdited, this, [this]() {
        saveRegionsToConfig();
        refreshRegionsList();
    });
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::removeRegionRequested, this,
            &ComfyUIRemoteDock::slotRemoveRegion);
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::requestAddRegion, this,
            &ComfyUIRemoteDock::slotAddRegion);
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::translatePromptRequested, this,
            [this](bool negative) {
                if (!m_d->nam || !m_d->editServerUrl)
                    return;
                const QString url = m_d->editServerUrl->text().trimmed();
                if (url.isEmpty())
                    return;
                QJsonObject st = ComfyUIUtils::loadSettingsJson();
                if (!st.value(QStringLiteral("translation_enabled")).toBool(false))
                    return;
                const QString lang = st.value(QStringLiteral("prompt_translation")).toString();
                if (lang.isEmpty() || lang == QLatin1String("disabled"))
                    return;
                QString source;
                if (m_d->activeRegionIndex == ComfyRegionLink::kRootRegionIndex) {
                    source = negative && m_d->editNegative ? m_d->editNegative->toPlainText()
                                                           : (m_d->editPrompt ? m_d->editPrompt->toPlainText()
                                                                              : QString());
                } else if (m_d->activeRegionIndex >= 0) {
                    QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
                    if (m_d->activeRegionIndex < regs.size())
                        source = regs.at(m_d->activeRegionIndex).prompt;
                }
                if (source.trimmed().isEmpty())
                    return;
                ComfyUIUtils::requestEtnPromptTranslation(
                    m_d->nam, url, lang, source, this, [this, negative](bool ok, const QString &translated) {
                        if (!ok)
                            return;
                        if (m_d->activeRegionIndex == ComfyRegionLink::kRootRegionIndex) {
                            if (negative && m_d->editNegative)
                                m_d->editNegative->setPlainText(translated);
                            else if (m_d->editPrompt)
                                m_d->editPrompt->setPlainText(translated);
                        } else if (m_d->activeRegionIndex >= 0) {
                            QList<Private::RegionEntry> &regs = comfyActiveRegionEntries(m_d.data());
                            if (m_d->activeRegionIndex < regs.size())
                                regs[m_d->activeRegionIndex].prompt = translated;
                            saveRegionsToConfig();
                        }
                        if (m_d->regionPromptWidget)
                            m_d->regionPromptWidget->refresh();
                    });
            });
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::activated, this, [this]() {
        if (m_d->regionPromptWidget)
            m_d->regionPromptWidget->setFocus(Qt::OtherFocusReason);
    });
    connect(m_d->regionPromptWidget, &ComfyRegionPromptWidget::editingModeChanged, this, [this](int idx) {
        const bool showDockRoot = idx == ComfyRegionLink::kRootRegionIndex
                                || comfyActiveRegionEntries(m_d.data()).isEmpty();
        if (m_d->rootPromptColumnWidget)
            m_d->rootPromptColumnWidget->setVisible(showDockRoot);
        if (m_d->negativePromptBlock) {
            const bool showNeg =
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_negative_prompt")).toBool(true);
            m_d->negativePromptBlock->setVisible(showDockRoot && showNeg);
        }
    });
    regLayout->addWidget(m_d->regionPromptWidget);
    QHBoxLayout *regionBtns = new QHBoxLayout();
    m_d->btnAddRegion = new QPushButton(ComfyTr::tr("Add"));
    m_d->btnRemoveRegion = new QPushButton(ComfyTr::tr("Remove"));
    m_d->btnMoveRegionUp = new QPushButton(ComfyTr::tr("Up"));
    m_d->btnMoveRegionDown = new QPushButton(ComfyTr::tr("Down"));
    m_d->btnGenerateRegions = new QPushButton(ComfyTr::tr("Generate regions"));
    m_d->btnGenerateRegions->setToolTip(
        ComfyTr::tr("Same as Generate: builds one job with regional prompts and masks (Python process_regions path)."));
    connect(m_d->btnAddRegion, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotAddRegion);
    connect(m_d->btnRemoveRegion, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotRemoveRegion);
    connect(m_d->btnMoveRegionUp, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotMoveRegionUp);
    connect(m_d->btnMoveRegionDown, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotMoveRegionDown);
    connect(m_d->btnGenerateRegions, &QPushButton::clicked, this, &ComfyUIRemoteDock::slotGenerateRegions);
    regionBtns->addWidget(m_d->btnAddRegion);
    regionBtns->addWidget(m_d->btnRemoveRegion);
    regionBtns->addWidget(m_d->btnMoveRegionUp);
    regionBtns->addWidget(m_d->btnMoveRegionDown);
    regionBtns->addWidget(m_d->btnGenerateRegions);
    regLayout->addLayout(regionBtns);
    setupRegionControlLayersUi(m_d->regionsGroupBox, nullptr);
    if (m_d->regionPromptWidget && m_d->regionControlLayersGroupBox)
        m_d->regionPromptWidget->embedRegionControlPanel(m_d->regionControlLayersGroupBox);
    scrollLayout->addWidget(m_d->regionsGroupBox);

    // §13.90: Apply PromptHeader from config (full / icon / none)
    m_d->promptHeaderMode = qBound(0, KSharedConfig::openConfig()->group("ComfyUIRemote").readEntry("PromptHeader", 0), 2);
    if (m_d->regionHeaderCombo) m_d->regionHeaderCombo->setCurrentIndex(m_d->promptHeaderMode);
    applyPromptHeader();

    scrollLayout->addStretch();
    scroll->setWidget(scrollContent);
    contentLayout->addWidget(scroll);
    m_d->labelStatus = new QLabel(ComfyTr::tr("Use Settings to configure server URL and advanced options."));
    m_d->labelStatus->setWordWrap(true);
    contentLayout->addWidget(m_d->labelStatus);
    m_d->mainStack->addWidget(contentPage);
    layout->addWidget(m_d->mainStack);
    setWidget(widget);
    setWindowTitle(ComfyTr::tr("AI Image Generation"));
    setEnabled(false);

    loadRegionsFromConfig();
    refreshRegionsList();

    int ws = m_d->comboWorkspace->currentIndex();
    const bool isGraph = (ws == 4);
    const bool isGenerate = (ws == 0);
    if (m_d->genContentContainer) m_d->genContentContainer->setVisible(!isGraph);
    if (m_d->graphPlaceholderWidget) m_d->graphPlaceholderWidget->setVisible(isGraph);
    reparentCustomWorkflowEditor(isGraph);
    if (m_d->histGroupBox) m_d->histGroupBox->setVisible(isGenerate || isGraph);
    if (m_d->queueButtonRowWidget)
        m_d->queueButtonRowWidget->setVisible(ws == 0 || ws == 3 || isGraph);
    refreshQueuePopupSupportsBatch();
    if (m_d->btnQueuePopup) {
        const bool animWs = (ws == 3);
        m_d->btnQueuePopup->setToolTip(animWs
            ? ComfyTr::tr("Idle. Click to adjust seed or cancel jobs (Animation has no batch enqueue options).")
            : ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs."));
    }

    updateWelcomeVisibility();
    applyInterfaceAppearanceSettings();
    applyQualitySamplerPresetFromSettings();
    refreshQueueResolutionRowVisibility();
    refreshAnimationTargetLayerCombo();
    updateAnimationTargetLayerRowVisibility();

    // §13.81: deferred autostart probe (undefined server_mode only)
    QTimer::singleShot(400, this, &ComfyUIRemoteDock::tryAutostartServerFallback);
}

namespace {

void setComboCurrentItemData(QComboBox *c, const QString &data, int fallbackIndex)
{
    if (!c || c->count() <= 0)
        return;
    for (int i = 0; i < c->count(); ++i) {
        if (c->itemData(i).toString() == data) {
            c->setCurrentIndex(i);
            return;
        }
    }
    c->setCurrentIndex(qBound(0, fallbackIndex, c->count() - 1));
}

} // namespace

void ComfyUIRemoteDock::persistOpenCustomWorkflowToDocument()
{
    saveEmbeddedCustomWorkflowToDocument();
}

void ComfyUIRemoteDock::saveEmbeddedCustomWorkflowToDocument()
{
    if (!m_d->canvas || !m_d->editCustomWorkflow)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    const QString key = ComfyUIUtils::customWorkflowAnnotationKey();
    const QString text = m_d->editCustomWorkflow->toPlainText();
    if (text.trimmed().isEmpty()) {
        img->removeAnnotation(key);
        return;
    }
    img->removeAnnotation(key);
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(
        key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("custom_workflow")), text.toUtf8())));
}

void ComfyUIRemoteDock::loadEmbeddedCustomWorkflowFromDocument()
{
    if (!m_d->canvas || !m_d->editCustomWorkflow)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    m_d->customWorkflowParamOverrides.clear();
    QByteArray wfBytes;
    if (KisAnnotationSP ann = img->annotation(ComfyUIUtils::customWorkflowAnnotationKey())) {
        if (!ann->annotation().isEmpty())
            wfBytes = ann->annotation();
    }
    if (wfBytes.isEmpty()) {
        const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
        const QJsonObject cust = ui.value(QStringLiteral("custom")).toObject();
        const QJsonObject wfObj = cust.value(QStringLiteral("workflow")).toObject();
        if (!wfObj.isEmpty())
            wfBytes = QJsonDocument(wfObj).toJson(QJsonDocument::Compact);
        else
            wfBytes = cust.value(QStringLiteral("workflow_text")).toString().toUtf8();
        const QJsonObject pr = cust.value(QStringLiteral("params")).toObject();
        for (auto it = pr.begin(); it != pr.end(); ++it)
            m_d->customWorkflowParamOverrides.insert(it.key(), it.value().toVariant());
    }
    if (wfBytes.isEmpty())
        return;
    QSignalBlocker b(m_d->editCustomWorkflow);
    m_d->editCustomWorkflow->setPlainText(QString::fromUtf8(wfBytes));
    refreshCustomWorkflowParameterPanel();
}

static QString comfyCustomWorkflowStorageKey(const ComfyUIUtils::CustomWorkflowParamSlot &sl)
{
    using Kind = ComfyUIUtils::CustomWorkflowParamSlot::Kind;
    if (sl.kind == Kind::KritaImageLayer || sl.kind == Kind::KritaMaskLayer)
        return sl.nodeId;
    return sl.paramName;
}

bool ComfyUIRemoteDock::tryResolveCustomWorkflowInPlace(QJsonObject *workflow)
{
    if (!workflow)
        return false;
    QString err;
    if (!ComfyUIUtils::tryResolveCustomWorkflowJsonToApi(workflow, m_d->lastObjectInfoRoot, &err)) {
        setStatusMessage(err, true);
        return false;
    }
    return true;
}

void ComfyUIRemoteDock::reparentCustomWorkflowEditor(bool toGraphWorkspace)
{
    if (!m_d->editCustomWorkflow)
        return;
    QVBoxLayout *from = toGraphWorkspace ? m_d->customWorkflowSettingsLayout : m_d->graphWorkflowEditorLayout;
    QVBoxLayout *to = toGraphWorkspace ? m_d->graphWorkflowEditorLayout : m_d->customWorkflowSettingsLayout;
    if (!to)
        return;
    if (from)
        from->removeWidget(m_d->editCustomWorkflow);
    to->insertWidget(toGraphWorkspace ? 1 : 2, m_d->editCustomWorkflow);
    m_d->editCustomWorkflow->setMaximumHeight(toGraphWorkspace ? 160 : 80);
    m_d->editCustomWorkflow->setVisible(true);
    if (toGraphWorkspace)
        refreshCustomWorkflowParameterPanel();
}

void ComfyUIRemoteDock::refreshCustomWorkflowParameterPanel()
{
    if (!m_d->customWorkflowParamsForm || !m_d->customWorkflowParamsGroup || !m_d->editCustomWorkflow)
        return;

    while (QLayoutItem *item = m_d->customWorkflowParamsForm->takeAt(0)) {
        if (item->widget())
            delete item->widget();
        delete item;
    }

    const QString json = m_d->editCustomWorkflow->toPlainText().trimmed();
    if (json.isEmpty()) {
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(json.toUtf8()), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }
    QJsonObject wfObj = doc.object();
    QString convErr;
    if (!ComfyUIUtils::tryResolveCustomWorkflowJsonToApi(&wfObj, m_d->lastObjectInfoRoot, &convErr)) {
        Q_UNUSED(convErr);
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }
    const QList<ComfyUIUtils::CustomWorkflowParamSlot> slots =
        ComfyUIUtils::discoverCustomWorkflowParameterSlots(wfObj);
    if (slots.isEmpty()) {
        m_d->customWorkflowParamsGroup->setVisible(false);
        return;
    }

    QSet<QString> storageKeysInUse;
    for (const auto &s : slots)
        storageKeysInUse.insert(comfyCustomWorkflowStorageKey(s));
    QStringList stale;
    for (auto it = m_d->customWorkflowParamOverrides.constBegin(); it != m_d->customWorkflowParamOverrides.constEnd(); ++it) {
        if (!storageKeysInUse.contains(it.key()))
            stale.append(it.key());
    }
    for (const QString &k : stale)
        m_d->customWorkflowParamOverrides.remove(k);

    m_d->customWorkflowParamsGroup->setVisible(true);
    m_d->customWorkflowParamsGroup->setTitle(ComfyTr::tr("Workflow parameters (ETN)"));

    for (const ComfyUIUtils::CustomWorkflowParamSlot &sl : slots) {
        const QString storageKey = comfyCustomWorkflowStorageKey(sl);
        const QVariant cur = m_d->customWorkflowParamOverrides.contains(storageKey)
            ? m_d->customWorkflowParamOverrides.value(storageKey)
            : sl.defaultValue;
        QString labelText = sl.paramName;
        if (!sl.typeStr.isEmpty() && sl.kind != ComfyUIUtils::CustomWorkflowParamSlot::Kind::Unsupported)
            labelText = QStringLiteral("%1 [%2]").arg(sl.paramName, sl.typeStr);

        switch (sl.kind) {
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterInt: {
            auto *sp = new QSpinBox(m_d->customWorkflowParamsGroup);
            int lo = static_cast<int>(qBound(-2147483648.0, sl.minV, 2147483647.0));
            int hi = static_cast<int>(qBound(-2147483648.0, sl.maxV, 2147483647.0));
            if (lo > hi)
                std::swap(lo, hi);
            sp->setRange(lo, hi);
            {
                bool ok = false;
                int v = cur.toInt(&ok);
                sp->setValue(ok ? v : sl.defaultValue.toInt());
            }
            const QString sk = storageKey;
            connect(sp, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, sk](int v) {
                m_d->customWorkflowParamOverrides.insert(sk, v);
            });
            m_d->customWorkflowParamOverrides.insert(sk, sp->value());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), sp);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterFloat: {
            auto *sp = new QDoubleSpinBox(m_d->customWorkflowParamsGroup);
            sp->setDecimals(4);
            sp->setRange(sl.minV, sl.maxV);
            {
                bool ok = false;
                double v = cur.toDouble(&ok);
                sp->setValue(ok ? v : sl.defaultValue.toDouble());
            }
            const QString sk = storageKey;
            connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, sk](double v) {
                m_d->customWorkflowParamOverrides.insert(sk, v);
            });
            m_d->customWorkflowParamOverrides.insert(sk, sp->value());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), sp);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterBool: {
            auto *cb = new QCheckBox(m_d->customWorkflowParamsGroup);
            bool chk = sl.defaultValue.toBool();
            if (!cur.isNull())
                chk = cur.toBool();
            cb->setChecked(chk);
            const QString sk = storageKey;
            connect(cb, &QCheckBox::toggled, this, [this, sk](bool on) {
                m_d->customWorkflowParamOverrides.insert(sk, on);
            });
            m_d->customWorkflowParamOverrides.insert(sk, cb->isChecked());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterText:
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterPromptPositive:
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterPromptNegative: {
            auto *le = new QLineEdit(m_d->customWorkflowParamsGroup);
            le->setText(cur.isNull() ? sl.defaultValue.toString() : cur.toString());
            const QString sk = storageKey;
            connect(le, &QLineEdit::editingFinished, this, [this, sk, le]() {
                m_d->customWorkflowParamOverrides.insert(sk, le->text());
            });
            m_d->customWorkflowParamOverrides.insert(sk, le->text());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), le);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::ParameterChoice: {
            const QString sk = storageKey;
            if (!sl.choices.isEmpty()) {
                auto *cb = new QComboBox(m_d->customWorkflowParamsGroup);
                for (const QString &ch : sl.choices)
                    cb->addItem(ch, ch);
                const QString pick = cur.isNull() ? sl.defaultValue.toString() : cur.toString();
                int ix = cb->findData(pick);
                if (ix < 0)
                    ix = 0;
                cb->setCurrentIndex(ix);
                connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sk, cb](int) {
                    m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
                });
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
                m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            } else {
                auto *le = new QLineEdit(m_d->customWorkflowParamsGroup);
                le->setText(cur.isNull() ? sl.defaultValue.toString() : cur.toString());
                connect(le, &QLineEdit::editingFinished, this, [this, sk, le]() {
                    m_d->customWorkflowParamOverrides.insert(sk, le->text());
                });
                m_d->customWorkflowParamOverrides.insert(sk, le->text());
                m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), le);
            }
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaStyleSampler: {
            auto *cb = new QComboBox(m_d->customWorkflowParamsGroup);
            cb->addItem(ComfyTr::tr("Auto"), QStringLiteral("auto"));
            cb->addItem(ComfyTr::tr("Regular"), QStringLiteral("regular"));
            cb->addItem(ComfyTr::tr("Live"), QStringLiteral("live"));
            const QString pick = cur.isNull() ? sl.defaultValue.toString() : cur.toString();
            int ix = cb->findData(pick);
            if (ix < 0)
                ix = 0;
            cb->setCurrentIndex(ix);
            const QString sk = storageKey;
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sk, cb](int) {
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            });
            m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaImageLayer:
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaMaskLayer: {
            labelText = QStringLiteral("%1 [%2]").arg(
                sl.paramName,
                sl.kind == ComfyUIUtils::CustomWorkflowParamSlot::Kind::KritaMaskLayer
                    ? QStringLiteral("mask layer")
                    : QStringLiteral("image layer"));
            auto *cb = new QComboBox(m_d->customWorkflowParamsGroup);
            cb->setMinimumContentsLength(18);
            cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
            QVector<QPair<QString, QString>> items;
            KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
            if (img && img->rootLayer())
                collectPaintLayerNodes(img->rootLayer(), &items);
            for (const QPair<QString, QString> &p : items)
                cb->addItem(p.second, p.first);
            QString wantUuid = cur.toString();
            if (wantUuid.isEmpty() && !items.isEmpty())
                wantUuid = items.first().first;
            int selIx = cb->findData(wantUuid);
            if (selIx < 0 && cb->count() > 0)
                selIx = 0;
            cb->setCurrentIndex(selIx);
            const QString sk = storageKey;
            connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, sk, cb](int) {
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            });
            if (cb->count() > 0)
                m_d->customWorkflowParamOverrides.insert(sk, cb->currentData().toString());
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), cb);
            break;
        }
        case ComfyUIUtils::CustomWorkflowParamSlot::Kind::Unsupported:
        default: {
            auto *lb = new QLabel(
                ComfyTr::tr("Unsupported parameter type in this port: %1", sl.paramName), m_d->customWorkflowParamsGroup);
            lb->setWordWrap(true);
            m_d->customWorkflowParamsForm->addRow(new QLabel(labelText, m_d->customWorkflowParamsGroup), lb);
            break;
        }
        }
    }
}

void ComfyUIRemoteDock::scheduleSaveEmbeddedCustomWorkflowToDocument()
{
    if (!m_d->customWorkflowDocumentSaveTimer)
        return;
    m_d->customWorkflowDocumentSaveTimer->start(800);
}

void ComfyUIRemoteDock::updateInpaintControlsForArch()
{
    if (!m_d->comboCheckpoint)
        return;
    const QString ckpt = m_d->comboCheckpoint->currentText().trimmed();
    const ComfyResources::Arch arch =
        ComfyResources::archFromKey(ComfyUIUtils::classifyCheckpointArch(ckpt.isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors")
                                                                                         : ckpt));
    const bool editArch = ComfyUIUtils::isArchEdit(ckpt);
    const bool editUi = m_d->checkEditMode && m_d->checkEditMode->isChecked();
    if (m_d->checkInpaintUseModel)
        m_d->checkInpaintUseModel->setEnabled(ComfyResources::isSdxlLike(arch) || ComfyResources::hasControlnetInpaint(arch));
    if (m_d->checkInpaintUsePromptFocus)
        m_d->checkInpaintUsePromptFocus->setVisible(arch == ComfyResources::Arch::Sd15 || ComfyResources::isSdxlLike(arch));
    if (m_d->comboFillMode) {
        const bool strengthFull = m_d->spinStrength && m_d->spinStrength->value() >= 100;
        m_d->comboFillMode->setEnabled(strengthFull && !editArch && !editUi);
    }
}

void ComfyUIRemoteDock::saveInpaintWorkspaceToDocument()
{
    if (!m_d->canvas)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    const QString key = ComfyUIUtils::inpaintWorkspaceAnnotationKey();
    QJsonObject o;
    o.insert(QStringLiteral("mode"),
             m_d->comboInpaintMode ? m_d->comboInpaintMode->currentData().toString() : QStringLiteral("automatic"));
    o.insert(QStringLiteral("fill"),
             m_d->comboFillMode ? m_d->comboFillMode->currentData().toString() : QStringLiteral("blur"));
    o.insert(QStringLiteral("use_inpaint"), m_d->inpaintPersistUseModel);
    o.insert(QStringLiteral("use_prompt_focus"), m_d->inpaintPersistUsePromptFocus);
    o.insert(QStringLiteral("context"),
             m_d->comboInpaintContext ? m_d->comboInpaintContext->currentData().toString() : QStringLiteral("automatic"));
    o.insert(QStringLiteral("context_layer_id"), QString());
    img->removeAnnotation(key);
    img->addAnnotation(KisAnnotationSP(new KisAnnotation(
        key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("inpaint")),
        QJsonDocument(o).toJson(QJsonDocument::Compact))));
}

void ComfyUIRemoteDock::loadInpaintWorkspaceFromDocument()
{
    if (!m_d->canvas)
        return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img)
        return;
    QJsonObject root;
    if (KisAnnotationSP ann = img->annotation(ComfyUIUtils::inpaintWorkspaceAnnotationKey())) {
        if (!ann->annotation().isEmpty())
            root = QJsonDocument::fromJson(ann->annotation()).object();
    }
    if (root.isEmpty()) {
        const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
        root = ui.value(QStringLiteral("inpaint")).toObject();
    }
    if (root.isEmpty())
        return;
    const QString mode = root.value(QStringLiteral("mode")).toString();
    const QString fill = root.value(QStringLiteral("fill")).toString();
    if (m_d->comboInpaintMode) {
        QSignalBlocker b(m_d->comboInpaintMode);
        setComboCurrentItemData(m_d->comboInpaintMode, mode, 0);
    }
    if (m_d->comboFillMode) {
        QSignalBlocker b(m_d->comboFillMode);
        setComboCurrentItemData(m_d->comboFillMode, fill, 2);
    }
    m_d->inpaintPersistUseModel = root.value(QStringLiteral("use_inpaint")).toBool(true);
    m_d->inpaintPersistUsePromptFocus = root.value(QStringLiteral("use_prompt_focus")).toBool(false);
    if (m_d->checkInpaintUseModel) {
        QSignalBlocker b(m_d->checkInpaintUseModel);
        m_d->checkInpaintUseModel->setChecked(m_d->inpaintPersistUseModel);
    }
    if (m_d->checkInpaintUsePromptFocus) {
        QSignalBlocker b(m_d->checkInpaintUsePromptFocus);
        m_d->checkInpaintUsePromptFocus->setChecked(m_d->inpaintPersistUsePromptFocus);
    }
    const QString ctx = root.value(QStringLiteral("context")).toString().trimmed();
    if (m_d->comboInpaintContext && !ctx.isEmpty()) {
        QString norm = ctx;
        norm.replace(QLatin1Char(' '), QLatin1Char('_'));
        QSignalBlocker b(m_d->comboInpaintContext);
        int cix = m_d->comboInpaintContext->findData(norm);
        if (cix < 0)
            cix = m_d->comboInpaintContext->findData(ctx);
        if (cix >= 0)
            m_d->comboInpaintContext->setCurrentIndex(cix);
    }
}

void ComfyUIRemoteDock::schedulePersistDocumentDefaults()
{
    if (!m_d->documentDefaultsSaveTimer)
        return;
    m_d->documentDefaultsSaveTimer->start(350);
}

QString ComfyUIRemoteDock::encodeStyleIdForDocumentDefaults() const
{
    return encodeStyleIdFromPresetCombo(m_d->comboPreset);
}

QString ComfyUIRemoteDock::encodeStyleIdFromPresetCombo(const QComboBox *cb) const
{
    if (!cb)
        return QStringLiteral("none");
    const int idx = cb->currentIndex();
    if (idx <= 0)
        return QStringLiteral("none");
    const QVariant data = cb->itemData(idx);
    if (data.isValid() && !data.toString().isEmpty())
        return data.toString();
    return QStringLiteral("custom:") + cb->currentText();
}

void ComfyUIRemoteDock::applyStyleIdFromDocumentDefaults(const QString &styleId)
{
    if (!m_d->comboPreset)
        return;
    applyStyleIdToPresetCombo(m_d->comboPreset, styleId);
}

void ComfyUIRemoteDock::applyStyleIdToPresetCombo(QComboBox *cb, const QString &styleId)
{
    if (!cb)
        return;
    const QString id = styleId.trimmed();
    if (id.isEmpty() || id == QLatin1String("none")) {
        cb->setCurrentIndex(0);
        return;
    }
    if (id.startsWith(QLatin1String("custom:"))) {
        const QString name = id.mid(7);
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toString() == id) {
                cb->setCurrentIndex(i);
                return;
            }
        }
        const int fi = cb->findText(name);
        if (fi >= 0)
            cb->setCurrentIndex(fi);
        return;
    }
    for (int i = 0; i < cb->count(); ++i) {
        if (cb->itemData(i).toString() == id) {
            cb->setCurrentIndex(i);
            return;
        }
    }
    const ComfyStyleEntry *legacy = ComfyStyleCollection::instance().findByStyleId(id);
    if (legacy) {
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toString() == legacy->styleId) {
                cb->setCurrentIndex(i);
                return;
            }
        }
    }
}

QString ComfyUIRemoteDock::checkpointNameForUpscaleRefinementPreset() const
{
    if (!m_d->comboUpscaleRefinementModel)
        return QString();
    const int idx = m_d->comboUpscaleRefinementModel->currentIndex();
    if (idx <= 0)
        return QString();
    const int fc = firstCustomPresetIndex();
    if (idx < fc)
        return m_d->comboCheckpoint ? m_d->comboCheckpoint->currentText().trimmed() : QString();
    const QString name = m_d->comboUpscaleRefinementModel->itemText(idx);
    return KSharedConfig::openConfig()
        ->group(QStringLiteral("ComfyUIRemote_Preset_") + name)
        .readEntry(QStringLiteral("Checkpoint"), QString())
        .trimmed();
}

void ComfyUIRemoteDock::readUpscaleRefinementSampling(int *outSteps, double *outCfg, QString *outSampler, QString *outScheduler) const
{
    Q_ASSERT(outSteps && outCfg && outSampler && outScheduler);
    *outSteps = m_d->spinSteps ? m_d->spinSteps->value() : 20;
    *outCfg = m_d->spinCfg ? m_d->spinCfg->value() : 8.0;
    *outSampler = m_d->comboSampler ? m_d->comboSampler->currentText().trimmed() : QStringLiteral("euler");
    *outScheduler = m_d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->ksamplerScheduler;
    if (!m_d->comboUpscaleRefinementModel)
        return;
    const int idx = m_d->comboUpscaleRefinementModel->currentIndex();
    const int fc = firstCustomPresetIndex();
    if (idx < fc || idx <= 0)
        return;
    const QString name = m_d->comboUpscaleRefinementModel->itemText(idx);
    KConfigGroup cfgG = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote_Preset_") + name);
    *outSteps = cfgG.readEntry(QStringLiteral("Steps"), *outSteps);
    *outCfg = cfgG.readEntry(QStringLiteral("Cfg"), *outCfg);
    const QString ps = cfgG.readEntry(QStringLiteral("Sampler"), QString());
    if (!ps.isEmpty())
        *outSampler = ps;
    const QString sch = cfgG.readEntry(QStringLiteral("Scheduler"), QString());
    if (!sch.isEmpty())
        *outScheduler = sch;
}

void ComfyUIRemoteDock::persistDocumentDefaultsToSettings()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QJsonObject dd;
    dd.insert(QStringLiteral("style"), encodeStyleIdForDocumentDefaults());
    dd.insert(QStringLiteral("batch_count"), m_d->spinBatchCount ? m_d->spinBatchCount->value() : 1);
    const QString pt = s.value(QStringLiteral("prompt_translation")).toString();
    const bool transEn = !pt.isEmpty() && pt != QLatin1String("disabled");
    dd.insert(QStringLiteral("translation_enabled"), transEn);
    dd.insert(QStringLiteral("prompt_translation"), transEn ? pt : QStringLiteral("disabled"));
    if (m_d->comboInpaintMode)
        dd.insert(QStringLiteral("inpaint_mode"), m_d->comboInpaintMode->currentData().toString());
    if (m_d->comboFillMode)
        dd.insert(QStringLiteral("inpaint_fill"), m_d->comboFillMode->currentData().toString());
    dd.insert(QStringLiteral("inpaint_use_model"), m_d->inpaintPersistUseModel);
    dd.insert(QStringLiteral("inpaint_use_prompt_focus"), m_d->inpaintPersistUsePromptFocus);
    if (m_d->comboInpaintContext)
        dd.insert(QStringLiteral("inpaint_context"), m_d->comboInpaintContext->currentData().toString());
    if (m_d->comboCheckpoint)
        dd.insert(QStringLiteral("checkpoint"), m_d->comboCheckpoint->currentText().trimmed());
    dd.insert(QStringLiteral("upscale_model"), QStringLiteral("default"));
    s.insert(QStringLiteral("document_defaults"), dd);
    ComfyUIUtils::saveSettingsJson(s);
}

void ComfyUIRemoteDock::tryApplyDocumentDefaultsForNewDocument(KisImageSP image)
{
    if (!image)
        return;
    const QString key = ComfyUIUtils::documentIdAnnotationKey();
    KisAnnotationSP idAnn = image->annotation(key);
    const QString docId = idAnn ? QString::fromUtf8(idAnn->annotation()).trimmed() : QString();
    if (docId.isEmpty())
        return;
    if (m_d->documentDefaultsAppliedDocIds.contains(docId))
        return;
    if (ComfyUIUtils::documentHasStoredUiJsonPayload(image)) {
        m_d->documentDefaultsAppliedDocIds.insert(docId);
        return;
    }
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QJsonObject def = ComfyUIUtils::documentDefaultsFromSettingsRoot(s);
    m_d->documentDefaultsAppliedDocIds.insert(docId);
    if (def.isEmpty())
        return;

    applyStyleIdFromDocumentDefaults(def.value(QStringLiteral("style")).toString());
    if (m_d->spinBatchCount && def.contains(QStringLiteral("batch_count"))) {
        const int bc = def.value(QStringLiteral("batch_count")).toInt(1);
        m_d->spinBatchCount->setValue(qBound(m_d->spinBatchCount->minimum(), bc, m_d->spinBatchCount->maximum()));
    }
    const QString ck = def.value(QStringLiteral("checkpoint")).toString().trimmed();
    if (!ck.isEmpty() && m_d->comboCheckpoint) {
        const int ci = m_d->comboCheckpoint->findText(ck);
        if (ci >= 0)
            m_d->comboCheckpoint->setCurrentIndex(ci);
        else
            m_d->comboCheckpoint->setCurrentText(ck);
    }
    if (def.contains(QStringLiteral("translation_enabled"))) {
        const bool tEn = def.value(QStringLiteral("translation_enabled")).toBool(false);
        QString ptx = def.value(QStringLiteral("prompt_translation")).toString();
        if (!tEn || ptx == QLatin1String("disabled"))
            ptx = QStringLiteral("disabled");
        if (ptx.isEmpty() && tEn)
            ptx = QStringLiteral("en");
        s.insert(QStringLiteral("prompt_translation"), ptx);
        ComfyUIUtils::saveSettingsJson(s);
        applyInterfaceAppearanceSettings();
    }
    if (m_d->comboInpaintMode) {
        const QString im = def.value(QStringLiteral("inpaint_mode")).toString();
        if (!im.isEmpty()) {
            QSignalBlocker b(m_d->comboInpaintMode);
            setComboCurrentItemData(m_d->comboInpaintMode, im, 0);
        }
    }
    if (m_d->comboFillMode) {
        const QString fl = def.value(QStringLiteral("inpaint_fill")).toString();
        if (!fl.isEmpty()) {
            QSignalBlocker b(m_d->comboFillMode);
            setComboCurrentItemData(m_d->comboFillMode, fl, 2);
        }
    }
    if (def.contains(QStringLiteral("inpaint_use_model")))
        m_d->inpaintPersistUseModel = def.value(QStringLiteral("inpaint_use_model")).toBool(true);
    if (def.contains(QStringLiteral("inpaint_use_prompt_focus")))
        m_d->inpaintPersistUsePromptFocus = def.value(QStringLiteral("inpaint_use_prompt_focus")).toBool(false);
    if (m_d->checkInpaintUseModel) {
        QSignalBlocker b(m_d->checkInpaintUseModel);
        m_d->checkInpaintUseModel->setChecked(m_d->inpaintPersistUseModel);
    }
    if (m_d->checkInpaintUsePromptFocus) {
        QSignalBlocker b(m_d->checkInpaintUsePromptFocus);
        m_d->checkInpaintUsePromptFocus->setChecked(m_d->inpaintPersistUsePromptFocus);
    }
    if (m_d->comboInpaintContext) {
        QString ctx = def.value(QStringLiteral("inpaint_context")).toString();
        ctx = ComfyUIUtils::inpaintContextForFreshDocumentDefaults(ctx);
        QSignalBlocker b(m_d->comboInpaintContext);
        const int cix = m_d->comboInpaintContext->findData(ctx);
        if (cix >= 0)
            m_d->comboInpaintContext->setCurrentIndex(cix);
    }
    updateNegativePromptAlertVisibility();
    persistDocumentDefaultsToSettings();
}

ComfyUIRemoteDock::~ComfyUIRemoteDock()
{
    if (m_d->pluginUpdateDownloadReply) {
        m_d->pluginUpdateDownloadReply->disconnect(this);
        m_d->pluginUpdateDownloadReply->abort();
        m_d->pluginUpdateDownloadReply.clear();
    }
    m_d->pluginUpdateSaveFile.reset();
    endWebWorkflowSwitch();
}

namespace {

bool tagCompletionTokenChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char(':') || ch == QLatin1Char('-');
}

QString tagCompletionPrefixAtCursor(QPlainTextEdit *e)
{
    if (!e)
        return {};
    QTextCursor c = e->textCursor();
    const QString t = e->toPlainText();
    int pos = c.position();
    int start = pos;
    while (start > 0 && tagCompletionTokenChar(t.at(start - 1)))
        --start;
    return t.mid(start, pos - start);
}

} // namespace

void ComfyUIRemoteDock::refreshPromptTagCompleter()
{
    if (!m_d->tagKeywordModel)
        return;
    m_d->tagKeywordModel->setStringList(ComfyUIUtils::tagKeywordsForAutocomplete());
}

void ComfyUIRemoteDock::refreshQueueResolutionRowVisibility()
{
    if (!m_d->queueResolutionRow)
        return;
    QString preset = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("performance_preset")).toString();
    if (preset.isEmpty())
        preset = QStringLiteral("auto");
    m_d->queueResolutionRow->setVisible(preset == QLatin1String("custom"));
}

void ComfyUIRemoteDock::persistSeedToConfig()
{
    if (!m_d->checkFixedSeed || !m_d->spinSeed)
        return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("FixedSeed", m_d->checkFixedSeed->isChecked());
    cfg.writeEntry("SeedValue", static_cast<qint64>(m_d->spinSeed->value()));
}

void ComfyUIRemoteDock::syncQueueSeedWidgetsFromMain()
{
    if (!m_d->queueSpinSeed || !m_d->queueCheckFixedSeed || !m_d->spinSeed || !m_d->checkFixedSeed)
        return;
    m_d->queueSpinSeed->blockSignals(true);
    m_d->queueCheckFixedSeed->blockSignals(true);
    m_d->queueSpinSeed->setValue(m_d->spinSeed->value());
    m_d->queueCheckFixedSeed->setChecked(m_d->checkFixedSeed->isChecked());
    m_d->queueSpinSeed->blockSignals(false);
    m_d->queueCheckFixedSeed->blockSignals(false);
}

void ComfyUIRemoteDock::refreshQueuePopupSupportsBatch()
{
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : 0;
    const bool supportsBatch = (ws == 0);
    if (m_d->queueBatchOptionsRow)
        m_d->queueBatchOptionsRow->setVisible(supportsBatch);
    if (m_d->queueEnqueueModeRow)
        m_d->queueEnqueueModeRow->setVisible(supportsBatch);
}

void ComfyUIRemoteDock::showPromptTagCompletion(QPlainTextEdit *editor)
{
    if (!editor)
        return;
    QCompleter *comp = nullptr;
    if (editor == m_d->editPrompt)
        comp = m_d->promptTagCompleter;
    else if (editor == m_d->editNegative)
        comp = m_d->negativePromptTagCompleter;
    if (!comp || !m_d->tagKeywordModel)
        return;
    if (m_d->tagKeywordModel->stringList().isEmpty()) {
        setStatusMessage(ComfyTr::tr("No tags loaded. Add CSV files (e.g. Danbooru.csv) to the tag folder and enable stems under Settings → Interface."),
                         false,
                         true);
        return;
    }
    const QString prefix = tagCompletionPrefixAtCursor(editor);
    comp->setCompletionPrefix(prefix);
    QRect cr = editor->cursorRect();
    cr.setWidth(qMax(cr.width(), 220));
    comp->complete(cr);
}

void ComfyUIRemoteDock::insertPromptTagCompletion(QPlainTextEdit *editor, const QString &completion)
{
    if (!editor || completion.isEmpty())
        return;
    const QString prefix = tagCompletionPrefixAtCursor(editor);
    QTextCursor tc = editor->textCursor();
    const int n = prefix.length();
    if (n > 0)
        tc.setPosition(tc.position() - n, QTextCursor::KeepAnchor);
    tc.removeSelectedText();
    // §13.138: literal parentheses in tag names must not break attention syntax
    QString escaped = completion;
    escaped.replace(QLatin1Char('('), QStringLiteral("\\("));
    escaped.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    tc.insertText(escaped);
    editor->setTextCursor(tc);
}

bool ComfyUIRemoteDock::eventFilter(QObject *obj, QEvent *event)
{
    // §13.196: Ctrl+Backspace — accept ShortcutOverride so the editor receives a normal Key_Backspace (word delete)
    if (event->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((obj == m_d->editPrompt || obj == m_d->editNegative) && ke->key() == Qt::Key_Backspace
            && (ke->modifiers() & Qt::ControlModifier)) {
            ke->accept();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if ((obj == m_d->editPrompt || obj == m_d->editNegative) && (ke->modifiers() & Qt::ControlModifier)
            && ke->key() == Qt::Key_Space) {
            showPromptTagCompletion(static_cast<QPlainTextEdit *>(obj));
            return true;
        }
    }
    // §13.196: Shift+Enter in main prompt → same as clicking Generate
    if (obj == m_d->editPrompt && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ShiftModifier)) {
            slotGenerate();
            return true;
        }
    }
    // §8.5 / §13.35 / §13.201: Ctrl+Up / Ctrl+Down — attention weight in positive and negative prompt fields
    if ((obj == m_d->editPrompt || obj == m_d->editNegative) && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) && (ke->modifiers() & Qt::ControlModifier)) {
            auto *edit = static_cast<QPlainTextEdit *>(obj);
            QString text = edit->toPlainText();
            int cursorPos = edit->textCursor().position();
            auto range = ComfyUIUtils::attentionSegmentRange(text, cursorPos);
            if (range.first >= 0 && range.second > 0) {
                QString segment = text.mid(range.first, range.second);
                double delta = (ke->key() == Qt::Key_Up) ? 0.1 : -0.1;
                QString newSegment = ComfyUIUtils::editAttentionWeight(segment, delta);
                if (newSegment != segment) {
                    QTextCursor cur = edit->textCursor();
                    cur.setPosition(range.first);
                    cur.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
                    cur.insertText(newSegment);
                }
                return true;
            }
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

void ComfyUIRemoteDock::setViewManager(KisViewManager *viewManager)
{
    m_d->connections.clear();
    m_d->viewManager = viewManager;
    if (m_d->regionPromptWidget)
        m_d->regionPromptWidget->setViewManager(viewManager);
    if (!viewManager) {
        if (m_d->documentSyncPoller)
            m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        return;
    }
    // §10.1 / §13.151: action IDs and display strings match Python ai_diffusion.action (activation flags in XML)
    KisActionManager *am = viewManager->actionManager();
    auto reg = [this, am](const QString &id, void (ComfyUIRemoteDock::*slot)()) {
        KisAction *a = am->createAction(id);
        m_d->connections.addConnection(a, &KisAction::triggered, this, slot);
    };
    reg(QStringLiteral("ai_diffusion_settings"), &ComfyUIRemoteDock::slotConfigureHelp);
    reg(QStringLiteral("ai_diffusion_generate"), &ComfyUIRemoteDock::slotAiDiffusionGenerateAction);
    reg(QStringLiteral("ai_diffusion_cancel"), &ComfyUIRemoteDock::slotAiDiffusionCancelCurrent);
    reg(QStringLiteral("ai_diffusion_cancel_queued"), &ComfyUIRemoteDock::slotAiDiffusionCancelQueued);
    reg(QStringLiteral("ai_diffusion_cancel_all"), &ComfyUIRemoteDock::slotAiDiffusionCancelAll);
    reg(QStringLiteral("ai_diffusion_toggle_preview"), &ComfyUIRemoteDock::slotAiDiffusionTogglePreview);
    reg(QStringLiteral("ai_diffusion_apply"), &ComfyUIRemoteDock::slotAiDiffusionApply);
    reg(QStringLiteral("ai_diffusion_apply_alternative"), &ComfyUIRemoteDock::slotAiDiffusionApplyAlternative);
    reg(QStringLiteral("ai_diffusion_create_region"), &ComfyUIRemoteDock::slotAiDiffusionCreateRegion);
    reg(QStringLiteral("ai_diffusion_switch_workspace_generation"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceGeneration);
    reg(QStringLiteral("ai_diffusion_switch_workspace_upscaling"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceUpscaling);
    reg(QStringLiteral("ai_diffusion_switch_workspace_live"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceLive);
    reg(QStringLiteral("ai_diffusion_switch_workspace_graph"), &ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceGraph);
    reg(QStringLiteral("ai_diffusion_toggle_workspace"), &ComfyUIRemoteDock::slotAiDiffusionToggleWorkspace);
    reg(QStringLiteral("ai_diffusion_toggle_edit_mode"), &ComfyUIRemoteDock::slotAiDiffusionToggleEditMode);
}

void ComfyUIRemoteDock::slotAiDiffusionToggleWorkspace()
{
    if (!m_d->comboWorkspace) {
        return;
    }
    const int n = m_d->comboWorkspace->count();
    if (n <= 0) {
        return;
    }
    const int next = (m_d->comboWorkspace->currentIndex() + 1) % n;
    m_d->comboWorkspace->setCurrentIndex(next);
}

void ComfyUIRemoteDock::slotAiDiffusionToggleEditMode()
{
    if (!m_d->checkEditMode) {
        return;
    }
    m_d->checkEditMode->setChecked(!m_d->checkEditMode->isChecked());
}

void ComfyUIRemoteDock::slotAiDiffusionGenerateAction()
{
    if (!m_d->comboWorkspace) {
        slotGenerate();
        return;
    }
    switch (m_d->comboWorkspace->currentIndex()) {
    case 0:
        slotGenerate();
        break;
    case 1:
        slotUpscale();
        break;
    case 2:
        if (m_d->checkLiveMode) {
            if (!m_d->checkLiveMode->isChecked())
                m_d->checkLiveMode->setChecked(true);
            slotLiveTick();
        }
        break;
    case 3:
        slotGenerateAnimation();
        break;
    case 4:
        slotGenerate();
        break;
    default:
        break;
    }
}

void ComfyUIRemoteDock::slotAiDiffusionCancelCurrent()
{
    if (m_d->isFullAnimationBatch || !m_d->animationBatchPromptIdToIndex.isEmpty() || m_d->batchNeedsPerFrameReference) {
        slotCancelQueue();
        return;
    }
    if (cancelCurrentGenerateJob()) {
        setStatusMessage(ComfyTr::tr("Cancelled current job."));
        return;
    }
    if (!m_d->inpaintPromptId.isEmpty()) {
        m_d->inpaintPollTimer->stop();
        m_d->inpaintPromptId.clear();
        if (m_d->btnInpaint)
            m_d->btnInpaint->setEnabled(true);
        m_d->progressBar->setValue(0);
        QString urlStr = m_d->editServerUrl->text().trimmed();
        if (!urlStr.isEmpty()) {
            QUrl interruptUrl(urlStr);
            QString ip = interruptUrl.path();
            if (ip.isEmpty() || ip == "/")
                interruptUrl.setPath("/interrupt");
            else if (!ip.endsWith('/'))
                interruptUrl.setPath(ip + "/interrupt");
            else
                interruptUrl.setPath(ip + "interrupt");
            QNetworkRequest reqInt(interruptUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
            reqInt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            m_d->nam->post(reqInt, QByteArray("{}"));
        }
        setStatusMessage(ComfyTr::tr("Cancelled current job."));
        return;
    }
    if (!m_d->upscalePromptId.isEmpty()) {
        m_d->upscalePollTimer->stop();
        m_d->upscalePromptId.clear();
        if (m_d->btnUpscale)
            m_d->btnUpscale->setEnabled(true);
        m_d->progressBar->setValue(0);
        QString urlStr = m_d->editServerUrl->text().trimmed();
        if (!urlStr.isEmpty()) {
            QUrl interruptUrl(urlStr);
            QString ip = interruptUrl.path();
            if (ip.isEmpty() || ip == "/")
                interruptUrl.setPath("/interrupt");
            else if (!ip.endsWith('/'))
                interruptUrl.setPath(ip + "/interrupt");
            else
                interruptUrl.setPath(ip + "interrupt");
            QNetworkRequest reqInt(interruptUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
            reqInt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            m_d->nam->post(reqInt, QByteArray("{}"));
        }
        setStatusMessage(ComfyTr::tr("Cancelled current job."));
        return;
    }
    if (!m_d->livePromptId.isEmpty()) {
        m_d->livePollTimer->stop();
        m_d->livePromptId.clear();
        stopLiveSpinner();
        QString urlStr = m_d->editServerUrl->text().trimmed();
        if (!urlStr.isEmpty()) {
            QUrl interruptUrl(urlStr);
            QString ip = interruptUrl.path();
            if (ip.isEmpty() || ip == "/")
                interruptUrl.setPath("/interrupt");
            else if (!ip.endsWith('/'))
                interruptUrl.setPath(ip + "/interrupt");
            else
                interruptUrl.setPath(ip + "interrupt");
            QNetworkRequest reqInt(interruptUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqInt);
            reqInt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            m_d->nam->post(reqInt, QByteArray("{}"));
        }
        setStatusMessage(ComfyTr::tr("Cancelled current job."));
    }
}

void ComfyUIRemoteDock::slotAiDiffusionCancelQueued()
{
    if (m_d->jobQueue.isEmpty())
        return;
    cancelQueuedGenerateJobs();
    setStatusMessage(ComfyTr::tr("Cancelled queued jobs."));
}

void ComfyUIRemoteDock::slotAiDiffusionCancelAll()
{
    slotCancelQueue();
}

void ComfyUIRemoteDock::slotAiDiffusionTogglePreview()
{
    if (m_d->previewLayerId.trimmed().isEmpty()) {
        setStatusMessage(ComfyTr::tr("No preview layer is set for this document."), false, true);
        return;
    }
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    KisNodeSP root = image->rootLayer();
    if (!root) {
        setStatusMessage(ComfyTr::tr("Could not toggle preview layer."), true);
        return;
    }
    const QUuid uid = comfyParseLayerUuidString(m_d->previewLayerId);
    if (uid.isNull()) {
        setStatusMessage(ComfyTr::tr("Invalid preview layer id."), true);
        return;
    }
    KisNodeSP node = KisLayerUtils::findNodeByUuid(root, uid);
    if (!node) {
        setStatusMessage(ComfyTr::tr("Preview layer was not found."), true);
        return;
    }
    KisImageBarrierLock lock(image);
    lock.unlock();
    KisLayerPropertiesIcons::setNodePropertyAutoUndo(node, KisLayerPropertiesIcons::visible, !node->visible(), image);
    if (m_d->canvas)
        m_d->canvas->updateCanvas();
}

void ComfyUIRemoteDock::slotAiDiffusionApply()
{
    if (!m_d->comboWorkspace)
        return;
    const int ws = m_d->comboWorkspace->currentIndex();
    if (ws != 0 && ws != 2)
        return;
    if (ws == 0) {
        slotHistoryApply();
        return;
    }
    // §10.1 Live: apply current live result (same behavior as finished live frame path)
    if (!m_d->lastLiveResultImagePath.isEmpty() && QFile::exists(m_d->lastLiveResultImagePath)) {
        QJsonObject ls = ComfyUIUtils::loadSettingsJson();
        QString liveBeh = ls.value(QStringLiteral("apply_behavior_live")).toString();
        if (liveBeh.isEmpty())
            liveBeh = QStringLiteral("replace");
        if (applyResultFileWithBehavior(m_d->lastLiveResultImagePath, liveBeh)
            && ls.value(QStringLiteral("new_seed_after_apply")).toBool(false) && m_d->spinSeed) {
            m_d->spinSeed->setValue(
                static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
        }
        if (m_d->canvas)
            m_d->canvas->updateCanvas();
        return;
    }
    slotHistoryApply();
}

void ComfyUIRemoteDock::slotAiDiffusionApplyAlternative()
{
    if (!m_d->comboWorkspace || m_d->comboWorkspace->currentIndex() != 2)
        return;
    if (m_d->lastLiveResultImagePath.isEmpty() || !QFile::exists(m_d->lastLiveResultImagePath)) {
        setStatusMessage(ComfyTr::tr("No live result to apply yet."), false, true);
        return;
    }
    if (!applyResultFileWithBehavior(m_d->lastLiveResultImagePath, QStringLiteral("layer")))
        setStatusMessage(ComfyTr::tr("Could not import image."), true);
    else if (m_d->canvas)
        m_d->canvas->updateCanvas();
}

void ComfyUIRemoteDock::slotAiDiffusionCreateRegion()
{
    slotAddRegion();
}

void ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceGeneration()
{
    if (m_d->comboWorkspace)
        m_d->comboWorkspace->setCurrentIndex(0);
}

void ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceUpscaling()
{
    if (m_d->comboWorkspace)
        m_d->comboWorkspace->setCurrentIndex(1);
}

void ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceLive()
{
    if (m_d->comboWorkspace)
        m_d->comboWorkspace->setCurrentIndex(2);
}

void ComfyUIRemoteDock::slotAiDiffusionSwitchWorkspaceGraph()
{
    if (m_d->comboWorkspace)
        m_d->comboWorkspace->setCurrentIndex(4);
}

// §13.15 / §13.52: document_id annotation; when missing, create; when duplicate across open docs, assign new ID to current (copy handling).
static void ensureDocumentId(KisImageSP image, KisCanvas2 *canvas)
{
    if (!image) return;
    const QString key = ComfyUIUtils::documentIdAnnotationKey();
    KisDocument *currentDoc = (canvas && canvas->imageView()) ? canvas->imageView()->document() : nullptr;

    KisAnnotationSP ann = image->annotation(key);
    if (ann) {
        QString existingId = QString::fromUtf8(ann->annotation());
        if (!existingId.isEmpty() && currentDoc) {
            const QList<QPointer<KisDocument> > docs = KisPart::instance()->documents();
            for (const QPointer<KisDocument> &doc : docs) {
                if (!doc || doc == currentDoc) continue;
                KisImageSP otherImg = doc->image().toStrongRef();
                if (!otherImg) continue;
                KisAnnotationSP otherAnn = otherImg->annotation(key);
                if (otherAnn && QString::fromUtf8(otherAnn->annotation()) == existingId) {
                    QString newUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    image->removeAnnotation(key);
                    image->addAnnotation(KisAnnotationSP(new KisAnnotation(key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("document_id")), newUuid.toUtf8())));
                    return;
                }
            }
        }
        return;
    }

    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    image->addAnnotation(KisAnnotationSP(new KisAnnotation(key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("document_id")), uuid.toUtf8())));
}

void ComfyUIRemoteDock::setCanvas(KoCanvasBase *canvas)
{
    KisCanvas2 *c = dynamic_cast<KisCanvas2 *>(canvas);
    m_d->canvas = c;
    setEnabled(canvas != nullptr);
    if (c) {
        KisImageSP img = c->image().toStrongRef();
        if (img) {
            m_d->documentPollInitialized = false;
            if (m_d->documentSyncPoller)
                m_d->documentSyncPoller->start();
            ensureDocumentId(img, c);
            QString docIdWarn;
            if (KisAnnotationSP idAnn = img->annotation(ComfyUIUtils::documentIdAnnotationKey())) {
                docIdWarn = QString::fromUtf8(idAnn->annotation()).trimmed();
            }
            QString docPathLabel;
            if (KisDocument *doc = c->imageView() ? c->imageView()->document() : nullptr)
                docPathLabel = doc->path();
            if (docPathLabel.isEmpty())
                docPathLabel = ComfyTr::tr("(unsaved document)");
            const ComfyUIUtils::DocumentUiJsonLoadOutcome uiMeta = ComfyUIUtils::loadDocumentUiJsonWithMeta(img);
            // §13.140: Persistence load failure — warning; continue with empty/default embedded state
            const QString parseFailDedupeKey =
                !docIdWarn.isEmpty() ? docIdWarn
                                     : (QStringLiteral("img:") + QString::number(reinterpret_cast<quintptr>(img.data())));
            if (uiMeta.parseFailed && !m_d->warnedUiJsonParseFailDocIds.contains(parseFailDedupeKey)) {
                m_d->warnedUiJsonParseFailDocIds.insert(parseFailDedupeKey);
                QMessageBox::warning(this, ComfyTr::tr("AI Diffusion Plugin"),
                                     ComfyTr::tr("Failed to load state from %1: %2", docPathLabel, uiMeta.parseError));
            }
            // §13.199: future ui.json version → defaults; warn once per document
            if (uiMeta.resetToDefaultsDueToFutureVersion && !docIdWarn.isEmpty()
                && !m_d->warnedFutureUiJsonVersionDocIds.contains(docIdWarn)) {
                m_d->warnedFutureUiJsonVersionDocIds.insert(docIdWarn);
                QMessageBox::warning(
                    this,
                    ComfyTr::trc("@title:window", "AI Image Generation data"),
                    ComfyTr::tr(
                        "This document's embedded AI data (format version %1) is newer than this Krita build supports (version %2). "
                        "Embedded plugin state was reset to defaults; saving the document may discard fields this build does not understand.",
                        uiMeta.rawVersionFromFile,
                        ComfyUIUtils::persistenceFormatVersion));
            }
            tryApplyDocumentDefaultsForNewDocument(img);
            loadRegionsPersistedForDocument(img);
            applyModelFieldsFromUiJson(uiMeta.object);
            // §13.44 / §13.189: preview_layer from annotation or ui.json
            const QString previewKey = ComfyUIUtils::previewLayerAnnotationKey();
            KisAnnotationSP previewAnn = img->annotation(previewKey);
            m_d->previewLayerId = previewAnn ? QString::fromUtf8(previewAnn->annotation()).trimmed() : QString();
            if (m_d->previewLayerId.isEmpty()) {
                m_d->previewLayerId = uiMeta.object.value(QStringLiteral("preview_layer")).toString().trimmed();
            }
            // §13.149 / §13.189: Live strength from annotation or ui.json
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2 && m_d->spinStrength) {
                const QString liveKey = ComfyUIUtils::liveWorkspaceAnnotationKey();
                QJsonObject liveObj;
                if (KisAnnotationSP liveAnn = img->annotation(liveKey)) {
                    if (!liveAnn->annotation().isEmpty())
                        liveObj = QJsonDocument::fromJson(liveAnn->annotation()).object();
                }
                if (liveObj.isEmpty()) {
                    liveObj = uiMeta.object.value(QStringLiteral("live")).toObject();
                }
                if (!liveObj.isEmpty()) {
                    const double s = liveObj.value(QStringLiteral("strength")).toDouble(0.75);
                    m_d->spinStrength->setValue(qBound(1, qRound(s * 100.0), 100));
                }
            }
            if (m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 0) {
                loadInpaintWorkspaceFromDocument();
                loadEmbeddedCustomWorkflowFromDocument();
            }
            loadDocumentHistoryFromAnnotations();
            loadAnimationWorkspaceFromDocument();
        } else {
            if (m_d->documentSyncPoller)
                m_d->documentSyncPoller->stop();
            m_d->documentPollInitialized = false;
            m_d->previewLayerId.clear();
            m_d->historyEntries.clear();
            if (m_d->listHistory)
                m_d->listHistory->clear();
            updateAnimationResultPreview(QString());
        }
        // §13.4: Prompt import from image file — when document is opened from PNG/JPG/WebP and prompts are empty, fill from A1111 parameters
        KisDocument *doc = c->imageView() ? c->imageView()->document() : nullptr;
        if (doc && m_d->editPrompt && m_d->editNegative) {
            QString path = doc->path();
            if (!path.isEmpty()) {
                const QString lower = path.toLower();
                if (lower.endsWith(QLatin1String(".png")) || lower.endsWith(QLatin1String(".jpg")) || lower.endsWith(QLatin1String(".jpeg")) || lower.endsWith(QLatin1String(".webp"))) {
                    if (m_d->editPrompt->toPlainText().trimmed().isEmpty() && m_d->editNegative->toPlainText().trimmed().isEmpty()) {
                        QPair<QString, QString> prompts = ComfyUIUtils::readPromptFromImageFile(path);
                        if (!prompts.first.isEmpty() || !prompts.second.isEmpty()) {
                            m_d->editPrompt->setPlainText(prompts.first);
                            m_d->editNegative->setPlainText(prompts.second);
                        }
                    }
                }
            }
        }
    } else {
        if (m_d->documentSyncPoller)
            m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        m_d->previewLayerId.clear();
        m_d->historyEntries.clear();
        if (m_d->listHistory)
            m_d->listHistory->clear();
        updateAnimationResultPreview(QString());
    }
    updateWelcomeVisibility();
    updateHistoryUsageLabel();
}

void ComfyUIRemoteDock::unsetCanvas()
{
    setCanvas(nullptr);
}

namespace {
QString comfyNormalizeSha256Hex(QString s)
{
    s = s.trimmed().toLower();
    if (s.startsWith(QLatin1String("0x")))
        s = s.mid(2);
    return s;
}
bool welcomePanelShowsAutoUpdate(const ComfyUIRemoteDock::Private *d)
{
    if (!ComfyUIUtils::loadSettingsJson().value(QStringLiteral("auto_update")).toBool(true))
        return false;
    using PS = ComfyUIRemoteDock::Private::PluginUpdateState;
    // §13.37: visible when auto_update and state is not latest, failed_check, or checking (includes unknown).
    switch (d->pluginUpdateState) {
    case PS::Latest:
    case PS::FailedCheck:
    case PS::Checking:
        return false;
    default:
        return true;
    }
}
} // namespace

void ComfyUIRemoteDock::updateWelcomeVisibility()
{
    if (!m_d->mainStack || m_d->mainStack->count() < 2) return;
    const bool showWelcome = !m_d->canvas || !m_d->isConnected;
    m_d->mainStack->setCurrentIndex(showWelcome ? 0 : 1);
    if (showWelcome && !m_d->updateCheckRequested)
        QTimer::singleShot(200, this, [this]() { startUpdateCheck(false); });
    if (showWelcome)
        QTimer::singleShot(500, this, &ComfyUIRemoteDock::startNewsFetch);
    // §13.190 / §13.37: AutoUpdateWidget when auto_update and state ∉ {latest, failed_check, checking}
    if (m_d->welcomeUpdateWidget && m_d->welcomeNewsWidget && m_d->welcomeConnectionWidget) {
        const bool showAuto = welcomePanelShowsAutoUpdate(m_d.data());
        m_d->welcomeUpdateWidget->setVisible(showAuto);
        m_d->welcomeNewsWidget->setVisible(!showAuto && m_d->hasUnseenNews);
        m_d->welcomeConnectionWidget->setVisible(!showAuto && !m_d->hasUnseenNews);
    }
    if (showWelcome)
        refreshWelcomeAutoUpdatePanel();
    if (m_d->welcomeStatusLabel) {
        if (m_d->updateCheckInProgress) {
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Checking for updates..."));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else if (m_d->isConnecting) {
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Connecting to server..."));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else if (m_d->isConnected && !m_d->editServerUrl->text().trimmed().isEmpty()) {
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Connected to server at %1. Create a new document or open an existing image to start!", m_d->editServerUrl->text().trimmed()));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else {
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Not connected to server."));
            if (m_d->welcomeErrorLabel) {
                if (m_d->connectionErrorOccurred) {
                    m_d->welcomeErrorLabel->setText(ComfyTr::tr("Connection attempt failed! Click below to configure and reconnect."));
                    m_d->welcomeErrorLabel->show();
                } else {
                    m_d->welcomeErrorLabel->clear();
                    m_d->welcomeErrorLabel->hide();
                }
            }
        }
    }
    if (!showWelcome && m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 1) {
        updateUpscaleTargetSize();
    }
}

void ComfyUIRemoteDock::slotCheckForUpdates()
{
    startUpdateCheck(true);
}

void ComfyUIRemoteDock::syncPluginUpdateUi()
{
    refreshWelcomeAutoUpdatePanel();
    refreshPluginInformationTabUpdateUi();
    updateWelcomeVisibility();
}

void ComfyUIRemoteDock::refreshWelcomeAutoUpdatePanel()
{
    if (!m_d->welcomeUpdateTitleLabel || !m_d->welcomeUpdateButton || !m_d->welcomeUpdateProgressBar
        || !m_d->welcomeUpdateVersionLabel || !m_d->welcomeCheckAutoUpdate)
        return;
    QString title;
    QString btnText = ComfyTr::tr("Download and Install");
    bool btnEnabled = false;
    bool showProgress = false;
    const QString verShow =
        m_d->updateRemoteVersion.isEmpty() ? m_d->lastReportedLatestPluginVersion : m_d->updateRemoteVersion;
    switch (m_d->pluginUpdateState) {
    case Private::PluginUpdateState::Available:
        // §5.2: headline + version line (not one combined string)
        title = ComfyTr::tr("A new plugin version is available!");
        if (m_d->welcomeUpdateVersionLabel) {
            m_d->welcomeUpdateVersionLabel->setText(verShow.isEmpty() ? QStringLiteral("—") : verShow);
            m_d->welcomeUpdateVersionLabel->show();
        }
        btnEnabled = !m_d->pluginUpdateDownloadReply && !m_d->updateDownloadUrl.isEmpty();
        break;
    case Private::PluginUpdateState::Downloading:
        title = ComfyTr::tr("Downloading update…");
        showProgress = true;
        break;
    case Private::PluginUpdateState::Installing:
        title = ComfyTr::tr("Installing update…");
        showProgress = true;
        break;
    case Private::PluginUpdateState::RestartRequired:
        title = m_d->updateExtractPath.isEmpty()
            ? ComfyTr::tr("Update is ready. Please restart Krita.")
            : ComfyTr::tr("Update saved to:\n%1\nFollow the release notes if needed, then restart Krita.", m_d->updateExtractPath);
        btnText = ComfyTr::tr("Open Folder");
        btnEnabled = !m_d->updateExtractPath.isEmpty() && QFileInfo::exists(m_d->updateExtractPath);
        break;
    case Private::PluginUpdateState::FailedUpdate:
        title = ComfyTr::tr("Update failed (network error or checksum mismatch).");
        btnText = ComfyTr::tr("Retry");
        btnEnabled = !m_d->pluginUpdateDownloadReply && !m_d->updateDownloadUrl.isEmpty();
        break;
    case Private::PluginUpdateState::Unknown:
        title = ComfyTr::tr("Looking for plugin updates…");
        btnEnabled = false;
        break;
    default:
        title.clear();
        break;
    }
    if (m_d->pluginUpdateState != Private::PluginUpdateState::Available && m_d->welcomeUpdateVersionLabel)
        m_d->welcomeUpdateVersionLabel->hide();
    {
        QSignalBlocker b(m_d->welcomeCheckAutoUpdate);
        m_d->welcomeCheckAutoUpdate->setChecked(
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("auto_update")).toBool(true));
    }
    m_d->welcomeUpdateTitleLabel->setText(title);
    m_d->welcomeUpdateProgressBar->setVisible(showProgress);
    m_d->welcomeUpdateButton->setText(btnText);
    m_d->welcomeUpdateButton->setEnabled(btnEnabled);
}

void ComfyUIRemoteDock::startPluginUpdateDownload()
{
    if (!m_d->nam || m_d->pluginUpdateDownloadReply)
        return;
    if (m_d->pluginUpdateState != Private::PluginUpdateState::Available
        && m_d->pluginUpdateState != Private::PluginUpdateState::FailedUpdate)
        return;
    if (m_d->updateDownloadUrl.isEmpty())
        return;
    const QUrl u(m_d->updateDownloadUrl);
    if (!u.isValid())
        return;

    m_d->pluginUpdateSaveFile.reset(new QTemporaryFile());
    m_d->pluginUpdateSaveFile->setFileTemplate(QDir::tempPath() + QStringLiteral("/cui_plugin_update_XXXXXX.zip"));
    if (!m_d->pluginUpdateSaveFile->open()) {
        m_d->pluginUpdateSaveFile.reset();
        m_d->pluginUpdateState = Private::PluginUpdateState::FailedUpdate;
        syncPluginUpdateUi();
        return;
    }

    m_d->pluginUpdateState = Private::PluginUpdateState::Downloading;
    m_d->updateExtractPath.clear();

    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Krita-ComfyUIRemote/%1").arg(ComfyUIUtils::pluginVersion()));
    QNetworkReply *reply = m_d->nam->get(req);
    m_d->pluginUpdateDownloadReply = reply;

    auto shaCtx = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    connect(reply, &QNetworkReply::readyRead, this, [this, shaCtx]() {
        if (!m_d->pluginUpdateDownloadReply || !m_d->pluginUpdateSaveFile)
            return;
        const QByteArray chunk = m_d->pluginUpdateDownloadReply->readAll();
        shaCtx->addData(chunk);
        m_d->pluginUpdateSaveFile->write(chunk);
    });
    connect(reply, &QNetworkReply::finished, this, [this, shaCtx, reply]() {
        reply->deleteLater();
        m_d->pluginUpdateDownloadReply.clear();

        auto fail = [this]() {
            m_d->pluginUpdateSaveFile.reset();
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedUpdate;
            syncPluginUpdateUi();
        };

        if (reply->error() != QNetworkReply::NoError) {
            fail();
            return;
        }
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            shaCtx->addData(rest);
            if (m_d->pluginUpdateSaveFile)
                m_d->pluginUpdateSaveFile->write(rest);
        }
        if (!m_d->pluginUpdateSaveFile) {
            fail();
            return;
        }
        m_d->pluginUpdateSaveFile->flush();
        const QString gotHex = QString::fromLatin1(shaCtx->result().toHex());
        const QString expHex = comfyNormalizeSha256Hex(m_d->updatePackageSha256);
        if (expHex.size() != 64 || gotHex != expHex) {
            fail();
            return;
        }

        m_d->pluginUpdateState = Private::PluginUpdateState::Installing;
        syncPluginUpdateUi();

        const QString zipPath = m_d->pluginUpdateSaveFile->fileName();
        const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/comfyui_remote");
        QDir().mkpath(baseDir);
        const QString staging = baseDir + QStringLiteral("/update_unpack");
        QDir(staging).removeRecursively();
        QDir().mkpath(staging);

        QString extractErr;
        const bool extracted = ComfyUIUtils::extractZipToDirectory(zipPath, staging, &extractErr);
        if (extracted) {
            m_d->updateExtractPath = QDir(staging).absolutePath();
        } else {
            const QString zipOut = baseDir + QStringLiteral("/update_package.zip");
            QFile::remove(zipOut);
            if (QFile::copy(zipPath, zipOut))
                m_d->updateExtractPath = zipOut;
            else
                m_d->updateExtractPath = zipPath;
        }
        m_d->pluginUpdateSaveFile.reset();
        m_d->pluginUpdateState = Private::PluginUpdateState::RestartRequired;
        syncPluginUpdateUi();
    });

    syncPluginUpdateUi();
}

void ComfyUIRemoteDock::refreshPluginInformationTabUpdateUi()
{
    if (m_d->pluginTabLatestVersionLabel) {
        using PS = Private::PluginUpdateState;
        const QString cur = ComfyUIUtils::pluginVersion();
        if (m_d->updateCheckInProgress || m_d->pluginUpdateState == PS::Checking) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Latest version: %1", ComfyTr::tr("Checking...")));
        } else if (m_d->pluginUpdateState == PS::FailedCheck || m_d->pluginUpdateCheckHadFailure) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Latest version: %1", ComfyTr::tr("Update check failed")));
        } else if (m_d->pluginUpdateState == PS::Unknown && m_d->lastReportedLatestPluginVersion.isEmpty()) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Latest version: %1", ComfyTr::tr("Not checked")));
        } else if (m_d->pluginUpdateState == PS::Available) {
            m_d->pluginTabLatestVersionLabel->setText(
                ComfyTr::tr("Update available: %1 (current: %2)", m_d->updateRemoteVersion, cur));
        } else if (m_d->pluginUpdateState == PS::Downloading) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Downloading update…"));
        } else if (m_d->pluginUpdateState == PS::Installing) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Installing update…"));
        } else if (m_d->pluginUpdateState == PS::RestartRequired) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Update installed. Restart Krita to finish."));
        } else if (m_d->pluginUpdateState == PS::FailedUpdate) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Update download or verification failed."));
        } else {
            m_d->pluginTabLatestVersionLabel->setText(
                ComfyTr::tr("Latest version: %1", m_d->lastReportedLatestPluginVersion.isEmpty() ? cur : m_d->lastReportedLatestPluginVersion));
        }
    }
    if (m_d->pluginTabDownloadInstallButton) {
        const bool busy = static_cast<bool>(m_d->pluginUpdateDownloadReply);
        const bool canStart =
            !busy && (m_d->pluginUpdateState == Private::PluginUpdateState::Available
                      || m_d->pluginUpdateState == Private::PluginUpdateState::FailedUpdate)
            && !m_d->updateDownloadUrl.isEmpty();
        m_d->pluginTabDownloadInstallButton->setEnabled(canStart);
    }
}

void ComfyUIRemoteDock::startUpdateCheck(bool manualRequest)
{
    // §13.37 / §13.160: GET plugin/latest?version=current; newer version requires url + sha256
    if (!m_d->nam || m_d->updateCheckInProgress)
        return;
    if (!manualRequest) {
        if (m_d->updateCheckRequested)
            return;
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        if (!settings.value(QStringLiteral("auto_update")).toBool(true)) {
            m_d->updateCheckRequested = true;
            return;
        }
        m_d->updateCheckRequested = true;
    }

    if (m_d->pluginUpdateDownloadReply) {
        m_d->pluginUpdateDownloadReply->disconnect(this);
        m_d->pluginUpdateDownloadReply->abort();
        m_d->pluginUpdateDownloadReply.clear();
    }
    m_d->pluginUpdateSaveFile.reset();
    m_d->updateDownloadUrl.clear();
    m_d->updatePackageSha256.clear();
    m_d->updateRemoteVersion.clear();
    m_d->updateExtractPath.clear();

    m_d->pluginUpdateState = Private::PluginUpdateState::Checking;
    m_d->updateCheckInProgress = true;
    m_d->pluginUpdateCheckHadFailure = false;
    syncPluginUpdateUi();

    const QString currentVer = ComfyUIUtils::pluginVersion();
    QString urlStr = ComfyUIUtils::intersticeApiBaseUrl();
    while (urlStr.endsWith(QLatin1Char('/')))
        urlStr.chop(1);
    urlStr += QStringLiteral("/plugin/latest");
    QUrl url(urlStr);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("version"), currentVer);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentVer]() {
        reply->deleteLater();
        m_d->updateCheckInProgress = false;
        if (reply->error() != QNetworkReply::NoError) {
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedCheck;
            m_d->pluginUpdateCheckHadFailure = true;
            syncPluginUpdateUi();
            return;
        }
        const QByteArray data = reply->readAll();
        QJsonParseError err;
        const QJsonObject obj = QJsonDocument::fromJson(data, &err).object();
        if (err.error != QJsonParseError::NoError || obj.isEmpty()) {
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedCheck;
            m_d->pluginUpdateCheckHadFailure = true;
            syncPluginUpdateUi();
            return;
        }
        QString latestVer = obj.value(QStringLiteral("version")).toString();
        if (latestVer.isEmpty())
            latestVer = currentVer;
        m_d->lastReportedLatestPluginVersion = latestVer;
        m_d->pluginUpdateCheckHadFailure = false;
        if (latestVer == currentVer) {
            m_d->pluginUpdateState = Private::PluginUpdateState::Latest;
            m_d->updateDownloadUrl.clear();
            m_d->updatePackageSha256.clear();
            m_d->updateRemoteVersion.clear();
            syncPluginUpdateUi();
            return;
        }
        const QString urlStr = obj.value(QStringLiteral("url")).toString().trimmed();
        const QString sha256 = obj.value(QStringLiteral("sha256")).toString().trimmed();
        if (urlStr.isEmpty() || sha256.isEmpty()) {
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedCheck;
            m_d->pluginUpdateCheckHadFailure = true;
            m_d->updateDownloadUrl.clear();
            m_d->updatePackageSha256.clear();
            m_d->updateRemoteVersion.clear();
            syncPluginUpdateUi();
            return;
        }
        m_d->pluginUpdateState = Private::PluginUpdateState::Available;
        m_d->updateDownloadUrl = urlStr;
        m_d->updatePackageSha256 = sha256;
        m_d->updateRemoteVersion = latestVer;
        syncPluginUpdateUi();
    });
}

void ComfyUIRemoteDock::startNewsFetch()
{
    // §13.38: GET plugin news from API; digest = first 16 chars of SHA256(text); show NewsWidget when digest != last_news
    if (!m_d->nam || !m_d->welcomeNewsLabel) return;
    QString urlStr = ComfyUIUtils::intersticeApiBaseUrl();
    while (urlStr.endsWith(QLatin1Char('/')))
        urlStr.chop(1);
    urlStr += QStringLiteral("/plugin/news");
    QUrl url(urlStr);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QByteArray data = reply->readAll();
        QJsonParseError err;
        QJsonObject obj = QJsonDocument::fromJson(data, &err).object();
        if (err.error != QJsonParseError::NoError || obj.isEmpty()) return;
        QString text = obj.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) return;
        QString digest = obj.value(QStringLiteral("digest")).toString();
        if (digest.isEmpty()) {
            QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
            digest = QString::fromLatin1(hash.toHex().left(16));
        }
        QString lastNews = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("last_news")).toString();
        if (digest == lastNews) return;
        m_d->hasUnseenNews = true;
        m_d->lastNewsDigest = digest;
        m_d->welcomeNewsLabel->setText(text);
        updateWelcomeVisibility();
    });
}

void ComfyUIRemoteDock::updateUpscaleTargetSize()
{
    if (!m_d->labelUpscaleTargetSize) return;
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->labelUpscaleTargetSize->setText(ComfyTr::tr("Target size: — × —"));
        return;
    }
    QSize size = m_d->viewManager->image()->size();
    int w = qRound(size.width() * m_d->upscaleFactor);
    int h = qRound(size.height() * m_d->upscaleFactor);
    const int overlapPx = m_d->tileOverlapMode == 1 ? m_d->tileOverlap : -1;
    int stylePreferredResolution = 0;
    QString styleArch;
    QString ckptHint = m_d->comboCheckpoint ? m_d->comboCheckpoint->currentText().trimmed() : QString();
    if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
        const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
            styleArch = st->architecture;
            stylePreferredResolution = st->preferredResolution;
            if (!st->checkpoints.isEmpty())
                ckptHint = st->checkpoints.first();
        }
    }
    const ComfyResources::Arch arch = ComfyWorkflowEngine::resolveArch(ckptHint, styleArch);
    const int strengthPct = m_d->sliderUpscaleRefineStrength ? m_d->sliderUpscaleRefineStrength->value() : 30;
    const double denoise = qBound(0.05, strengthPct / 100.0, 1.0);
    const ComfyUIUtils::UpscaleTiledLayoutSpec tileLayout =
        ComfyUIUtils::computeUpscaleTiledLayoutSpec(w, h, arch, stylePreferredResolution, denoise, overlapPx);
    const int tiles = tileLayout.totalTiles;
    if (tiles > 1)
        m_d->labelUpscaleTargetSize->setText(ComfyTr::tr("Target size: %1 × %2 · ~%3 tiles (estimate)", w, h, tiles));
    else
        m_d->labelUpscaleTargetSize->setText(ComfyTr::tr("Target size: %1 × %2", w, h));
}

void ComfyUIRemoteDock::slotDocumentSyncPoll()
{
    if (!m_d->documentSyncPoller)
        return;
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        return;
    }
    KisDocument *doc = m_d->canvas && m_d->canvas->imageView() ? m_d->canvas->imageView()->document() : nullptr;
    if (doc) {
        bool open = false;
        const QList<QPointer<KisDocument>> docs = KisPart::instance()->documents();
        for (const QPointer<KisDocument> &d : docs) {
            if (d.data() == doc) {
                open = true;
                break;
            }
        }
        if (!open) {
            m_d->documentSyncPoller->stop();
            m_d->documentPollInitialized = false;
            return;
        }
    }

    KisImageSP image = m_d->viewManager->image();
    if (!image) {
        m_d->documentSyncPoller->stop();
        m_d->documentPollInitialized = false;
        return;
    }

    bool curHasSel = false;
    QRect curSelRect;
    if (KisSelectionSP sel = m_d->viewManager->selection()) {
        if (sel->pixelSelection()) {
            curSelRect = sel->pixelSelection()->selectedExactRect();
            curHasSel = !curSelRect.isEmpty();
        }
    }

    int curTime = (std::numeric_limits<int>::min)();
    if (image->animationInterface() && image->animationInterface()->hasAnimation())
        curTime = image->animationInterface()->currentTime();

    if (!m_d->documentPollInitialized) {
        m_d->lastPolledHadSelection = curHasSel;
        m_d->lastPolledSelectionBounds = curSelRect;
        m_d->lastPolledCurrentTime = curTime;
        m_d->documentPollInitialized = true;
        return;
    }

    const bool selChanged =
        (curHasSel != m_d->lastPolledHadSelection) || (curHasSel && curSelRect != m_d->lastPolledSelectionBounds);
    if (selChanged) {
        m_d->lastPolledHadSelection = curHasSel;
        m_d->lastPolledSelectionBounds = curSelRect;
        // Reserved for selection-driven UX (e.g. live bounds); inpaint/generate read selection at action time.
    }

    if (curTime != m_d->lastPolledCurrentTime) {
        m_d->lastPolledCurrentTime = curTime;
        const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
        if (ws == 3 && m_d->radioSingleFrame && m_d->radioSingleFrame->isChecked() && m_d->animationPreviewRow
            && m_d->animationPreviewRow->isVisible() && m_d->animationPreviewDebounce) {
            m_d->animationPreviewDebounce->stop();
            m_d->animationPreviewDebounce->start();
        }
    }

    if (m_d->regionPromptWidget)
        m_d->regionPromptWidget->onActiveLayerChanged();
}

void ComfyUIRemoteDock::slotDebouncedAnimationTargetPreview()
{
    refreshAnimationTargetLayerLivePreview();
}

void ComfyUIRemoteDock::refreshAnimationTargetLayerLivePreview()
{
    if (!m_d->labelAnimationPreview)
        return;
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
    if (ws != 3 || !m_d->radioSingleFrame || !m_d->radioSingleFrame->isChecked())
        return;
    KisImageSP image = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!image || !image->rootLayer()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    const QString uuid =
        m_d->comboAnimationTargetLayer ? m_d->comboAnimationTargetLayer->currentData().toString().trimmed() : QString();
    if (uuid.isEmpty()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    KisPaintLayer *pl = findPaintLayerByUuidInTree(image->rootLayer(), uuid);
    if (!pl || !pl->projection()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    QRect bounds = pl->exactBounds() & image->bounds();
    if (bounds.isEmpty()) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    QImage img = pl->projection()->convertToQImage(profile, bounds,
                                                  KoColorConversionTransformation::internalRenderingIntent(),
                                                  KoColorConversionTransformation::internalConversionFlags());
    if (img.isNull()) {
        m_d->labelAnimationPreview->clear();
        return;
    }
    QPixmap pm = QPixmap::fromImage(img);
    const int maxW = 280;
    if (pm.width() > maxW)
        pm = pm.scaledToWidth(maxW, Qt::SmoothTransformation);
    m_d->labelAnimationPreview->setPixmap(pm);
}

void ComfyUIRemoteDock::updateAnimationButtonLabel()
{
    if (!m_d->btnGenerateAnimation) return;
    const bool fullAnimation = m_d->radioFullAnimation && m_d->radioFullAnimation->isChecked();
    if (fullAnimation) {
        m_d->btnGenerateAnimation->setText(ComfyTr::tr("Generate Animation"));
        m_d->btnGenerateAnimation->setToolTip(ComfyTr::tr("Generate multiple frames with sequential seeds as new layers."));
    } else {
        m_d->btnGenerateAnimation->setText(ComfyTr::tr("Generate Frame"));
        m_d->btnGenerateAnimation->setToolTip(ComfyTr::tr("Generate a single frame at current time."));
    }
}

QJsonObject ComfyUIRemoteDock::animationWorkspaceToJson() const
{
    QJsonObject o;
    if (m_d->radioFullAnimation)
        o.insert(QStringLiteral("batch_mode"), m_d->radioFullAnimation->isChecked());
    if (m_d->comboQuality) {
        const QString sq = (m_d->comboQuality->currentIndex() == 0) ? QStringLiteral("fast") : QStringLiteral("quality");
        o.insert(QStringLiteral("sampling_quality"), sq);
    }
    if (m_d->comboAnimationTargetLayer) {
        const QString id = m_d->comboAnimationTargetLayer->currentData().toString();
        if (!id.isEmpty())
            o.insert(QStringLiteral("target_layer"), id);
    }
    return o;
}

namespace {

ComfyRegionUiStateEntry comfyRegionEntryToUi(const ComfyUIRemoteDock::Private::RegionEntry &e)
{
    ComfyRegionUiStateEntry u;
    u.name = e.name;
    u.positive = e.prompt;
    u.maskSource = e.maskSource;
    u.layerIds = e.layerIds;
    u.controlLayers = e.controlLayers;
    return u;
}

ComfyUIRemoteDock::Private::RegionEntry comfyRegionEntryFromUi(const ComfyRegionUiStateEntry &u)
{
    ComfyUIRemoteDock::Private::RegionEntry e;
    e.name = u.name;
    e.prompt = u.positive;
    e.maskSource = u.maskSource;
    e.layerIds = u.layerIds;
    e.controlLayers = u.controlLayers;
    return e;
}

QList<ComfyRegionUiStateEntry> comfyRegionEntriesToUiList(const QList<ComfyUIRemoteDock::Private::RegionEntry> &entries)
{
    QList<ComfyRegionUiStateEntry> out;
    for (const ComfyUIRemoteDock::Private::RegionEntry &e : entries)
        out.append(comfyRegionEntryToUi(e));
    return out;
}

static int comfyWorkspaceIndexFromUiJson(const QString &w)
{
    if (w == QLatin1String("upscaling"))
        return 1;
    if (w == QLatin1String("live"))
        return 2;
    if (w == QLatin1String("animation"))
        return 3;
    if (w == QLatin1String("custom"))
        return 4;
    return 0;
}

} // namespace

void ComfyUIRemoteDock::mergeDocumentModelIntoUiJson(QJsonObject *ui, KisImageSP img) const
{
    if (!ui)
        return;

    QJsonObject upscale;
    upscale.insert(QStringLiteral("upscaler"), QStringLiteral("default"));
    upscale.insert(QStringLiteral("factor"), m_d->upscaleFactor);
    const bool useDiffusion = m_d->checkUpscaleRefine && m_d->checkUpscaleRefine->isChecked();
    upscale.insert(QStringLiteral("use_diffusion"), useDiffusion);
    const int strengthPct = m_d->sliderUpscaleRefineStrength ? m_d->sliderUpscaleRefineStrength->value() : 0;
    const int guidancePct = m_d->sliderUpscaleRefineGuidance ? m_d->sliderUpscaleRefineGuidance->value() : 0;
    upscale.insert(QStringLiteral("strength"), strengthPct / 100.0);
    upscale.insert(QStringLiteral("unblur_strength"), guidancePct / 100.0);
    upscale.insert(QStringLiteral("tile_overlap_mode"), m_d->tileOverlapMode);
    upscale.insert(QStringLiteral("tile_overlap"), m_d->tileOverlap);
    upscale.insert(QStringLiteral("use_prompt"), m_d->checkUpscaleUsePrompt && m_d->checkUpscaleUsePrompt->isChecked());
    upscale.insert(QStringLiteral("refinement_style"), encodeStyleIdFromPresetCombo(m_d->comboUpscaleRefinementModel));
    ui->insert(QStringLiteral("upscale"), upscale);

    QJsonObject custom;
    if (img) {
        if (KisAnnotationSP cw = img->annotation(ComfyUIUtils::customWorkflowAnnotationKey())) {
            if (!cw->annotation().isEmpty()) {
                const QByteArray raw = cw->annotation();
                QJsonParseError err{};
                const QJsonDocument wd = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(raw), &err);
                if (err.error == QJsonParseError::NoError && wd.isObject())
                    custom.insert(QStringLiteral("workflow"), wd.object());
                else
                    custom.insert(QStringLiteral("workflow_text"), QString::fromUtf8(raw));
            }
        }
    }
    if (!m_d->customWorkflowParamOverrides.isEmpty()) {
        QJsonObject pparams;
        for (auto it = m_d->customWorkflowParamOverrides.constBegin(); it != m_d->customWorkflowParamOverrides.constEnd(); ++it)
            pparams.insert(it.key(), QJsonValue::fromVariant(it.value()));
        custom.insert(QStringLiteral("params"), pparams);
    }
    if (!custom.isEmpty())
        ui->insert(QStringLiteral("custom"), custom);

    const QString rootPositive = m_d->editPrompt ? m_d->editPrompt->toPlainText() : QString();
    const QString rootNegative = m_d->editNegative ? m_d->editNegative->toPlainText() : QString();
    const QList<ComfyRegionUiStateEntry> rootUi = comfyRegionEntriesToUiList(m_d->regionEntries);
    ui->insert(QStringLiteral("root"), rootRegionUiWrapToJson(rootPositive, rootNegative, rootUi));
    ui->insert(QStringLiteral("edit"),
               rootRegionUiWrapToJson(rootPositive, rootNegative, comfyRegionEntriesToUiList(m_d->editRegionEntries)));
    ui->insert(QStringLiteral("regions"), regionUiStateEntriesToJsonArray(rootUi));
    ui->insert(QStringLiteral("control"), ComfyControlLayer::toJsonArray(m_d->rootControlLayers));

    static const QStringList wsIds = { QStringLiteral("generation"), QStringLiteral("upscaling"), QStringLiteral("live"),
                                       QStringLiteral("animation"), QStringLiteral("custom") };
    const int wix = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : 0;
    if (wix >= 0 && wix < wsIds.size())
        ui->insert(QStringLiteral("workspace"), wsIds.at(wix));
    ui->insert(QStringLiteral("style"), encodeStyleIdForDocumentDefaults());
    if (m_d->spinStrength)
        ui->insert(QStringLiteral("strength"), m_d->spinStrength->value());
    if (m_d->checkRegionOnly)
        ui->insert(QStringLiteral("region_only"), m_d->checkRegionOnly->isChecked());
    if (m_d->checkEditMode)
        ui->insert(QStringLiteral("edit_mode"), m_d->checkEditMode->isChecked());
    if (m_d->spinBatchCount)
        ui->insert(QStringLiteral("batch_count"), m_d->spinBatchCount->value());
    if (m_d->spinSeed)
        ui->insert(QStringLiteral("seed"), static_cast<double>(m_d->spinSeed->value()));
    if (m_d->checkFixedSeed)
        ui->insert(QStringLiteral("fixed_seed"), m_d->checkFixedSeed->isChecked());
    ui->insert(QStringLiteral("resolution_multiplier"), m_d->resolutionMultiplier);
    if (m_d->comboQueueMode) {
        const int qix = m_d->comboQueueMode->currentIndex();
        QString qm = QStringLiteral("back");
        if (qix == 1)
            qm = QStringLiteral("front");
        else if (qix == 2)
            qm = QStringLiteral("replace");
        ui->insert(QStringLiteral("queue_mode"), qm);
    }
    {
        QJsonObject sset = ComfyUIUtils::loadSettingsJson();
        const QString pt = sset.value(QStringLiteral("prompt_translation")).toString();
        const bool transEn = !pt.isEmpty() && pt != QLatin1String("disabled");
        ui->insert(QStringLiteral("translation_enabled"), transEn);
    }
    if (m_d->spinLayerCount)
        ui->insert(QStringLiteral("layer_count"), m_d->spinLayerCount->value());
}

void ComfyUIRemoteDock::loadRegionsPersistedForDocument(KisImageSP img)
{
    if (!img)
        return;
    const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);

    auto loadRootFromKConfig = [this]() {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int n = cfg.readEntry("RegionsCount", 0);
        m_d->regionEntries.clear();
        for (int i = 0; i < n; i++) {
            Private::RegionEntry e;
            e.name = cfg.readEntry(QStringLiteral("Region_%1_Name").arg(i), QString());
            e.prompt = cfg.readEntry(QStringLiteral("Region_%1_Prompt").arg(i), QString());
            e.maskSource = cfg.readEntry(QStringLiteral("Region_%1_MaskSource").arg(i), QStringLiteral("selection"));
            if (!e.name.isEmpty())
                m_d->regionEntries.append(e);
        }
    };
    auto loadEditFromKConfig = [this]() {
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        const int ne = cfg.readEntry("EditRegionsCount", 0);
        m_d->editRegionEntries.clear();
        for (int i = 0; i < ne; i++) {
            Private::RegionEntry e;
            e.name = cfg.readEntry(QStringLiteral("EditRegion_%1_Name").arg(i), QString());
            e.prompt = cfg.readEntry(QStringLiteral("EditRegion_%1_Prompt").arg(i), QString());
            e.maskSource = cfg.readEntry(QStringLiteral("EditRegion_%1_MaskSource").arg(i), QStringLiteral("selection"));
            if (!e.name.isEmpty())
                m_d->editRegionEntries.append(e);
        }
    };

    bool rootFromDoc = false;
    const QJsonArray ra = ComfyUIUtils::readRegionUiArrayFromDocumentUi(ui, &rootFromDoc);
    if (rootFromDoc) {
        m_d->regionEntries.clear();
        for (const ComfyRegionUiStateEntry &u : regionUiStateEntriesFromJsonArray(ra))
            m_d->regionEntries.append(comfyRegionEntryFromUi(u));
    } else {
        loadRootFromKConfig();
    }

    QString rootPos;
    QString rootNeg;
    if (rootRegionUiWrapFromJson(ui.value(QStringLiteral("root")).toObject(), &rootPos, &rootNeg, nullptr)) {
        if (m_d->editPrompt && !rootPos.isEmpty())
            m_d->editPrompt->setPlainText(rootPos);
        if (m_d->editNegative && !rootNeg.isEmpty())
            m_d->editNegative->setPlainText(rootNeg);
    }

    const QJsonObject editObj = ui.value(QStringLiteral("edit")).toObject();
    QList<ComfyRegionUiStateEntry> editRegs;
    const bool editFromDoc =
        rootRegionUiWrapFromJson(editObj, nullptr, nullptr, &editRegs) && editObj.contains(QStringLiteral("regions"));
    if (editFromDoc) {
        m_d->editRegionEntries.clear();
        for (const ComfyRegionUiStateEntry &u : editRegs)
            m_d->editRegionEntries.append(comfyRegionEntryFromUi(u));
    } else {
        loadEditFromKConfig();
    }

    m_d->rootControlLayers = ComfyControlLayer::fromJsonArray(ui.value(QStringLiteral("control")).toArray());
    refreshRootControlLayersList();

    refreshRegionsList();
}

void ComfyUIRemoteDock::applyModelFieldsFromUiJson(const QJsonObject &ui)
{
    if (ui.contains(QStringLiteral("workspace")) && m_d->comboWorkspace) {
        const int ix = comfyWorkspaceIndexFromUiJson(ui.value(QStringLiteral("workspace")).toString());
        if (ix >= 0 && ix < m_d->comboWorkspace->count())
            m_d->comboWorkspace->setCurrentIndex(ix);
    }
    if (ui.contains(QStringLiteral("style")))
        applyStyleIdFromDocumentDefaults(ui.value(QStringLiteral("style")).toString());
    if (ui.contains(QStringLiteral("strength")) && m_d->spinStrength) {
        const QJsonValue sv = ui.value(QStringLiteral("strength"));
        int pct = 100;
        if (sv.isDouble()) {
            const double d = sv.toDouble();
            pct = (d <= 1.0001) ? qBound(1, qRound(d * 100.0), 100) : qBound(1, qRound(d), 100);
        } else {
            pct = qBound(1, sv.toInt(100), 100);
        }
        m_d->spinStrength->setValue(pct);
    }
    if (ui.contains(QStringLiteral("region_only")) && m_d->checkRegionOnly) {
        QSignalBlocker b(m_d->checkRegionOnly);
        m_d->checkRegionOnly->setChecked(ui.value(QStringLiteral("region_only")).toBool());
    }
    if (ui.contains(QStringLiteral("edit_mode")) && m_d->checkEditMode) {
        QSignalBlocker b(m_d->checkEditMode);
        m_d->checkEditMode->setChecked(ui.value(QStringLiteral("edit_mode")).toBool());
    }
    if (ui.contains(QStringLiteral("batch_count")) && m_d->spinBatchCount) {
        m_d->spinBatchCount->setValue(qBound(m_d->spinBatchCount->minimum(),
                                            ui.value(QStringLiteral("batch_count")).toInt(1),
                                            m_d->spinBatchCount->maximum()));
    }
    if (ui.contains(QStringLiteral("seed")) && m_d->spinSeed) {
        const qint64 s = static_cast<qint64>(ui.value(QStringLiteral("seed")).toDouble());
        m_d->spinSeed->setValue(int(qBound<qint64>(0, s, 2147483647)));
    }
    if (ui.contains(QStringLiteral("fixed_seed")) && m_d->checkFixedSeed) {
        QSignalBlocker b(m_d->checkFixedSeed);
        m_d->checkFixedSeed->setChecked(ui.value(QStringLiteral("fixed_seed")).toBool());
    }
    if (ui.contains(QStringLiteral("resolution_multiplier"))) {
        double m = ui.value(QStringLiteral("resolution_multiplier")).toDouble(1.0);
        if (m <= 0.0)
            m = 1.0;
        m_d->resolutionMultiplier = m;
        if (m_d->sliderResolutionMultiplier && m_d->labelResolutionMultiplier) {
            int sliderValue = qRound(m * 10.0);
            sliderValue = qBound(3, sliderValue, 15);
            QSignalBlocker bs(m_d->sliderResolutionMultiplier);
            m_d->sliderResolutionMultiplier->setValue(sliderValue);
            m_d->labelResolutionMultiplier->setText(QString::number(m_d->resolutionMultiplier, 'f', 1) + QStringLiteral("×"));
        }
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
        cfg.writeEntry("ResolutionMultiplier", m_d->resolutionMultiplier);
    }
    if (ui.contains(QStringLiteral("queue_mode")) && m_d->comboQueueMode) {
        const QString qm = ui.value(QStringLiteral("queue_mode")).toString();
        int ix = 0;
        if (qm == QStringLiteral("front"))
            ix = 1;
        else if (qm == QStringLiteral("replace"))
            ix = 2;
        QSignalBlocker b(m_d->comboQueueMode);
        if (ix >= 0 && ix < m_d->comboQueueMode->count())
            m_d->comboQueueMode->setCurrentIndex(ix);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("QueueMode", ix);
    }
    if (ui.contains(QStringLiteral("upscale"))) {
        const QJsonObject us = ui.value(QStringLiteral("upscale")).toObject();
        syncUpscaleRefinementModelFromPresetCombo();
        auto readStrengthPct = [](const QJsonValue &v, int defVal) {
            if (v.isDouble()) {
                const double d = v.toDouble();
                return (d <= 1.0001) ? qBound(0, qRound(d * 100.0), 100) : qBound(0, qRound(d), 100);
            }
            return qBound(0, v.toInt(defVal), 100);
        };
        if (us.contains(QStringLiteral("factor"))) {
            double f = us.value(QStringLiteral("factor")).toDouble(m_d->upscaleFactor);
            f = qBound(1.0, f, 4.0);
            m_d->upscaleFactor = f;
            if (m_d->sliderUpscaleFactor) {
                QSignalBlocker bs(m_d->sliderUpscaleFactor);
                m_d->sliderUpscaleFactor->setValue(qRound(f * 10.0));
            }
            if (m_d->spinUpscaleFactor) {
                QSignalBlocker bsp(m_d->spinUpscaleFactor);
                m_d->spinUpscaleFactor->setValue(f);
            }
        }
        if (us.contains(QStringLiteral("use_diffusion")) && m_d->checkUpscaleRefine) {
            QSignalBlocker b(m_d->checkUpscaleRefine);
            m_d->checkUpscaleRefine->setChecked(us.value(QStringLiteral("use_diffusion")).toBool());
            if (m_d->upscaleRefineDetails)
                m_d->upscaleRefineDetails->setVisible(m_d->checkUpscaleRefine->isChecked());
        }
        if (us.contains(QStringLiteral("strength")) && m_d->sliderUpscaleRefineStrength) {
            const int pct = readStrengthPct(us.value(QStringLiteral("strength")), 30);
            QSignalBlocker b(m_d->sliderUpscaleRefineStrength);
            m_d->sliderUpscaleRefineStrength->setValue(pct);
            if (m_d->labelUpscaleRefineStrength)
                m_d->labelUpscaleRefineStrength->setText(QString::number(pct) + QLatin1Char('%'));
        }
        if (us.contains(QStringLiteral("unblur_strength")) && m_d->sliderUpscaleRefineGuidance) {
            const int pct = readStrengthPct(us.value(QStringLiteral("unblur_strength")), 50);
            QSignalBlocker b(m_d->sliderUpscaleRefineGuidance);
            m_d->sliderUpscaleRefineGuidance->setValue(pct);
            if (m_d->labelUpscaleRefineGuidance)
                m_d->labelUpscaleRefineGuidance->setText(QString::number(pct) + QLatin1Char('%'));
        }
        if (us.contains(QStringLiteral("tile_overlap_mode")) && m_d->comboTileOverlapMode) {
            const int tom = qBound(0, us.value(QStringLiteral("tile_overlap_mode")).toInt(0), m_d->comboTileOverlapMode->count() - 1);
            m_d->tileOverlapMode = tom;
            QSignalBlocker b(m_d->comboTileOverlapMode);
            m_d->comboTileOverlapMode->setCurrentIndex(tom);
            if (m_d->spinTileOverlap)
                m_d->spinTileOverlap->setVisible(tom == 1);
        }
        if (us.contains(QStringLiteral("tile_overlap")) && m_d->spinTileOverlap) {
            m_d->tileOverlap = us.value(QStringLiteral("tile_overlap")).toInt(m_d->tileOverlap);
            QSignalBlocker b(m_d->spinTileOverlap);
            m_d->spinTileOverlap->setValue(m_d->tileOverlap);
        }
        if (us.contains(QStringLiteral("use_prompt")) && m_d->checkUpscaleUsePrompt) {
            QSignalBlocker b(m_d->checkUpscaleUsePrompt);
            m_d->checkUpscaleUsePrompt->setChecked(us.value(QStringLiteral("use_prompt")).toBool());
        }
        if (us.contains(QStringLiteral("refinement_style")) && m_d->comboUpscaleRefinementModel)
            applyStyleIdToPresetCombo(m_d->comboUpscaleRefinementModel, us.value(QStringLiteral("refinement_style")).toString());
        KConfigGroup ucfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
        if (m_d->checkUpscaleRefine)
            ucfg.writeEntry(QStringLiteral("UpscaleRefineEnabled"), m_d->checkUpscaleRefine->isChecked());
        if (m_d->sliderUpscaleRefineStrength)
            ucfg.writeEntry(QStringLiteral("UpscaleRefineStrength"), m_d->sliderUpscaleRefineStrength->value());
        if (m_d->sliderUpscaleRefineGuidance)
            ucfg.writeEntry(QStringLiteral("UpscaleRefineGuidance"), m_d->sliderUpscaleRefineGuidance->value());
        if (m_d->checkUpscaleUsePrompt)
            ucfg.writeEntry(QStringLiteral("UpscaleUsePrompt"), m_d->checkUpscaleUsePrompt->isChecked());
        ucfg.writeEntry(QStringLiteral("TileOverlapMode"), m_d->tileOverlapMode);
        ucfg.writeEntry(QStringLiteral("TileOverlap"), m_d->tileOverlap);
        if (m_d->comboUpscaleRefinementModel)
            ucfg.writeEntry(QStringLiteral("UpscaleRefinementModelIndex"), m_d->comboUpscaleRefinementModel->currentIndex());
    }
    if (ui.contains(QStringLiteral("translation_enabled"))) {
        QJsonObject s = ComfyUIUtils::loadSettingsJson();
        const bool tEn = ui.value(QStringLiteral("translation_enabled")).toBool(false);
        QString ptx = s.value(QStringLiteral("prompt_translation")).toString();
        if (!tEn)
            ptx = QStringLiteral("disabled");
        else if (ptx.isEmpty() || ptx == QLatin1String("disabled"))
            ptx = QStringLiteral("en");
        s.insert(QStringLiteral("prompt_translation"), ptx);
        ComfyUIUtils::saveSettingsJson(s);
        applyInterfaceAppearanceSettings();
    }
    if (ui.contains(QStringLiteral("layer_count")) && m_d->spinLayerCount) {
        const int lc = qBound(m_d->spinLayerCount->minimum(),
                              ui.value(QStringLiteral("layer_count")).toInt(1),
                              m_d->spinLayerCount->maximum());
        m_d->spinLayerCount->setValue(lc);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("LayerCount", lc);
    }
    if (ui.contains(QStringLiteral("seed")) || ui.contains(QStringLiteral("fixed_seed")))
        persistSeedToConfig();
    if (ui.contains(QStringLiteral("edit_mode")) || ui.contains(QStringLiteral("workspace")))
        refreshRegionsList();
    updateUpscaleTargetSize();
}

void ComfyUIRemoteDock::refreshAnimationTargetLayerCombo()
{
    if (!m_d->comboAnimationTargetLayer)
        return;
    const QString prev = m_d->comboAnimationTargetLayer->currentData().toString();
    m_d->comboAnimationTargetLayer->blockSignals(true);
    m_d->comboAnimationTargetLayer->clear();
    m_d->comboAnimationTargetLayer->addItem(ComfyTr::tr("(None)"), QString());
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (img) {
        QVector<QPair<QString, QString>> items;
        KisNodeSP root = img->rootLayer();
        if (root)
            collectPaintLayerNodes(root, &items);
        std::sort(items.begin(), items.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
            return QString::localeAwareCompare(a.second, b.second) < 0;
        });
        int selectAt = 0;
        for (int i = 0; i < items.size(); ++i) {
            m_d->comboAnimationTargetLayer->addItem(ComfyTr::tr("Target layer: %1", items.at(i).second), items.at(i).first);
            if (items.at(i).first == prev)
                selectAt = i + 1;
        }
        m_d->comboAnimationTargetLayer->setCurrentIndex(selectAt);
    } else {
        m_d->comboAnimationTargetLayer->setCurrentIndex(0);
    }
    m_d->comboAnimationTargetLayer->blockSignals(false);
}

void ComfyUIRemoteDock::updateAnimationTargetLayerRowVisibility()
{
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : -1;
    const bool animWorkspace = (ws == 3);
    const bool singleFrame = m_d->radioSingleFrame && m_d->radioSingleFrame->isChecked();
    const bool show = animWorkspace && singleFrame;
    if (m_d->animationTargetRow)
        m_d->animationTargetRow->setVisible(show);
    if (m_d->animationPreviewRow) {
        m_d->animationPreviewRow->setVisible(show);
        if (!show)
            updateAnimationResultPreview(QString());
        else if (m_d->animationPreviewDebounce) {
            m_d->animationPreviewDebounce->stop();
            m_d->animationPreviewDebounce->start();
        }
    }
}

void ComfyUIRemoteDock::updateAnimationResultPreview(const QString &imagePath)
{
    if (!m_d->labelAnimationPreview)
        return;
    if (imagePath.isEmpty() || !QFile::exists(imagePath)) {
        m_d->labelAnimationPreview->clear();
        m_d->labelAnimationPreview->setPixmap(QPixmap());
        return;
    }
    QPixmap pm;
    if (!pm.load(imagePath)) {
        m_d->labelAnimationPreview->clear();
        return;
    }
    const int maxW = 280;
    if (pm.width() > maxW)
        pm = pm.scaledToWidth(maxW, Qt::SmoothTransformation);
    m_d->labelAnimationPreview->setPixmap(pm);
}

void ComfyUIRemoteDock::loadAnimationWorkspaceFromDocument()
{
    KisImageSP img = m_d->canvas ? m_d->canvas->image().toStrongRef() : KisImageSP();
    if (!img) {
        refreshAnimationTargetLayerCombo();
        updateAnimationTargetLayerRowVisibility();
        return;
    }
    const QJsonObject ui = ComfyUIUtils::loadDocumentUiJsonObject(img);
    const QJsonObject anim = ui.value(QStringLiteral("animation")).toObject();
    if (anim.isEmpty()) {
        refreshAnimationTargetLayerCombo();
        updateAnimationTargetLayerRowVisibility();
        return;
    }
    QSignalBlocker bFull(m_d->radioFullAnimation);
    QSignalBlocker bSingle(m_d->radioSingleFrame);
    QSignalBlocker bQual(m_d->comboQuality);
    QSignalBlocker bTarget(m_d->comboAnimationTargetLayer);

    if (anim.contains(QStringLiteral("batch_mode"))) {
        const bool full = anim.value(QStringLiteral("batch_mode")).toBool();
        if (m_d->radioFullAnimation)
            m_d->radioFullAnimation->setChecked(full);
        if (m_d->radioSingleFrame)
            m_d->radioSingleFrame->setChecked(!full);
        KSharedConfig::openConfig()->group("ComfyUIRemote").writeEntry("FullAnimation", full);
    }
    const QString sq = anim.value(QStringLiteral("sampling_quality")).toString();
    if (!sq.isEmpty() && m_d->comboQuality) {
        const int idx = (sq == QStringLiteral("fast")) ? 0 : 1;
        m_d->comboQuality->setCurrentIndex(idx);
    }
    refreshAnimationTargetLayerCombo();
    const QString tl = anim.value(QStringLiteral("target_layer")).toString();
    if (m_d->comboAnimationTargetLayer && !tl.isEmpty()) {
        const int ix = m_d->comboAnimationTargetLayer->findData(tl);
        if (ix >= 0)
            m_d->comboAnimationTargetLayer->setCurrentIndex(ix);
    }
    updateAnimationButtonLabel();
    updateAnimationTargetLayerRowVisibility();
}

void ComfyUIRemoteDock::setProgressBarKind(bool isUpload)
{
    if (!m_d->progressBar) return;
    // §13.18: upload = theme.progress_alt (amber); generation = default highlight
    if (isUpload) {
        m_d->progressBar->setStyleSheet(QStringLiteral(
            "QProgressBar::chunk { background: #ca8a04; border-radius: 2px; }"
        ));
    } else {
        m_d->progressBar->setStyleSheet(QString());
    }
}

void ComfyUIRemoteDock::setLiveProgress(int percent)
{
    auto *w = static_cast<LiveSpinnerWidget *>(m_d->liveSpinner);
    if (w) w->setProgress(percent);
}

void ComfyUIRemoteDock::startLiveSpinner()
{
    auto *w = static_cast<LiveSpinnerWidget *>(m_d->liveSpinner);
    if (w) w->startAnimation();
}

void ComfyUIRemoteDock::stopLiveSpinner()
{
    auto *w = static_cast<LiveSpinnerWidget *>(m_d->liveSpinner);
    if (w) w->stopAnimation();
}

void ComfyUIRemoteDock::setStatusMessage(const QString &msg, bool isError, bool isWarning)
{
    if (!m_d->labelStatus) return;
    m_d->labelStatus->setText(msg);
    // §13.27: theme colors — red for error, yellow for warning
    if (isError) {
        m_d->labelStatus->setStyleSheet(QStringLiteral("color: #dc2626;"));
    } else if (isWarning) {
        m_d->labelStatus->setStyleSheet(QStringLiteral("color: #b58900;"));
    } else {
        m_d->labelStatus->setStyleSheet(QString());
    }
}

void ComfyUIRemoteDock::startPolling()
{
    setProgressBarKind(false);  // §13.18: generation progress
    setStatusMessage(ComfyTr::tr("Generating… %1", m_d->pollCount));
    m_d->pollTimer->start(1000);
}

void ComfyUIRemoteDock::updateQueueStatus()
{
    int running = m_d->currentPromptId.isEmpty() ? 0 : 1;
    int queued = m_d->jobQueue.size();
    m_d->labelQueueCount->setText(ComfyTr::tr("Queue: %1", running + queued));
    if (m_d->btnQueuePopup) {
        const int total = running + queued;
        const bool animWorkspace = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 3;
        if (total > 0) {
            const QString tip = queued > 0
                ? ComfyTr::tr("Generating image. %1 jobs queued. Click to adjust queue or cancel.", queued)
                : ComfyTr::tr("Generating image. Click to adjust queue or cancel.");
            m_d->btnQueuePopup->setDisplayState(ComfyQueueButton::DisplayState::Active, total, tip);
        } else {
            const QString tip = animWorkspace
                ? ComfyTr::tr("Idle. Click to adjust seed or cancel jobs (Animation has no batch enqueue options).")
                : ComfyTr::tr("Idle. Click to adjust batch, seed, enqueue mode, or cancel jobs.");
            m_d->btnQueuePopup->setDisplayState(ComfyQueueButton::DisplayState::Inactive, 0, tip);
        }
    }
    if (running + queued > 0) {
        if (queued > 0) {
            setStatusMessage(ComfyTr::tr("Queue: 1 running, %1 queued.", queued));
        } else {
            setStatusMessage(ComfyTr::tr("Generating… %1", m_d->pollCount));
        }
    } else {
        setStatusMessage(ComfyTr::tr("Ready."));
    }
    m_d->btnCancelQueue->setEnabled(running + queued > 0);
}

QString ComfyUIRemoteDock::pathForCurrentHistoryRow(int *outEntryIndex, int *outImageIndex) const
{
    int row = m_d->listHistory->currentRow();
    if (row < 0) return QString();
    QListWidgetItem *item = m_d->listHistory->item(row);
    if (!item) return QString();
    QString jobId = item->data(Qt::UserRole).toString();
    int imageIndex = item->data(Qt::UserRole + 1).toInt();
    for (int i = 0; i < m_d->historyEntries.size(); i++) {
        const Private::HistoryEntry &e = m_d->historyEntries.at(i);
        if (e.jobId != jobId) continue;
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (imageIndex >= 0 && imageIndex < paths.size()) {
            if (outEntryIndex) *outEntryIndex = i;
            if (outImageIndex) *outImageIndex = imageIndex;
            return paths.at(imageIndex);
        }
        return QString();
    }
    return QString();
}

void ComfyUIRemoteDock::refreshHistoryList()
{
    m_d->listHistory->clear();
    const QSize iconSize = m_d->listHistory->iconSize();
    const int thumbW = iconSize.width();
    const int thumbH = iconSize.height();
    // §13.28a: applied overlay at (thumb.extent.width - 28, 4), 24×24 star
    const int starX = thumbW - 28;
    const int starY = 4;
    const int starSize = 24;
    QIcon starIcon = KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("star")));  // §13.153 / §13.28a
    QPixmap starPix = starIcon.pixmap(starSize, starSize);
    // §13.131: One row per image (job_id + index); multi-image entries show multiple rows
    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        if (paths.isEmpty()) continue;
        QString snippet = ComfyUIUtils::sanitizePrompt(e.prompt);  // §13.132: safe label for history
        if (e.prompt.size() > 40) snippet += "…";
        for (int imageIndex = 0; imageIndex < paths.size(); imageIndex++) {
            QString path = paths.at(imageIndex);
            QString tip = QString("%1 (%2×%3)\nSeed: %4").arg(snippet).arg(e.width).arg(e.height).arg(e.seed);
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
            if (item->icon().isNull())
                item->setText(paths.size() > 1 ? snippet + QStringLiteral(" [%1/%2]").arg(imageIndex + 1).arg(paths.size()) : snippet);
            item->setToolTip(tip);
            item->setData(Qt::UserRole, e.jobId);
            item->setData(Qt::UserRole + 1, imageIndex);
            m_d->listHistory->addItem(item);
        }
    }
}

void ComfyUIRemoteDock::applyInterfaceAppearanceSettings()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const bool liveWs = m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 2;
    const int lines = qBound(
        1,
        liveWs ? s.value(QStringLiteral("prompt_line_count_live")).toInt(2)
               : s.value(QStringLiteral("prompt_line_count")).toInt(2),
        10);
    if (m_d->editPrompt) {
        const QFontMetrics fm(m_d->editPrompt->font());
        const int h = qBound(40, fm.lineSpacing() * lines + fm.height() / 2, 800);
        m_d->editPrompt->setFixedHeight(h);
    }
    if (m_d->editNegative) {
        const QFontMetrics fn(m_d->editNegative->font());
        const int negLines = qBound(1, s.value(QStringLiteral("negative_prompt_line_count")).toInt(2), 10);
        const int nh = qBound(28, fn.lineSpacing() * negLines + fn.height() / 2, 400);
        m_d->editNegative->setFixedHeight(nh);
    }
    const bool showNeg = s.value(QStringLiteral("show_negative_prompt")).toBool(true);
    if (m_d->negativePromptBlock)
        m_d->negativePromptBlock->setVisible(showNeg);
    const bool showResizeHandle = s.value(QStringLiteral("prompt_resize_handle")).toBool(true);
    if (m_d->promptResizeHandle)
        m_d->promptResizeHandle->setVisible(showResizeHandle);
    if (m_d->negativeResizeHandle)
        m_d->negativeResizeHandle->setVisible(showNeg && showResizeHandle);
    const bool showSt = s.value(QStringLiteral("show_steps")).toBool(true);
    if (m_d->stepsParametersWidget)
        m_d->stepsParametersWidget->setVisible(showSt);
}

void ComfyUIRemoteDock::updateNegativePromptAlertVisibility()
{
    if (!m_d->labelNegativePromptAlert || !m_d->comboPreset) return;
    const int index = m_d->comboPreset->currentIndex();
    const int firstCustom = firstCustomPresetIndex();
    bool showAlert = false;
    if (index == 0) {
        showAlert = true;  // "None" — no style selected, may not use negative prompt
    } else if (index < firstCustom) {
        const bool showBuiltin =
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
        const QList<const ComfyStyleEntry *> styles = ComfyStyleCollection::instance().filtered(showBuiltin);
        const int styleIdx = index - 1;
        if (styleIdx >= 0 && styleIdx < styles.size())
            showAlert = !styles.at(styleIdx)->usesNegativePrompt();
    } else if (index >= firstCustom) {
        QString name = m_d->comboPreset->itemText(index);
        KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote_Preset_" + name);
        showAlert = !cfg.readEntry("UsesNegativePrompt", true);
    }
    m_d->labelNegativePromptAlert->setVisible(showAlert);
}

int ComfyUIRemoteDock::legacyKConfigPresetCount() const
{
    return KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote")).readEntry(QStringLiteral("PresetNames"), QStringList()).size();
}

int ComfyUIRemoteDock::firstCustomPresetIndex() const
{
    const bool showBuiltin = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
    return 1 + ComfyStyleCollection::instance().filtered(showBuiltin).size();
}

void ComfyUIRemoteDock::applyComfyStyleEntry(const ComfyStyleEntry &style)
{
    if (!m_d->editPrompt || !m_d->editNegative)
        return;
    if (!style.stylePrompt.contains(QStringLiteral("{prompt}")))
        m_d->editPrompt->setPlainText(style.stylePrompt);
    m_d->editNegative->setPlainText(style.negativePrompt);

    if (style.preferredResolution > 0) {
        const int side = style.preferredResolution;
        m_d->spinWidth->setValue(side);
        m_d->spinHeight->setValue(side);
    }

    if (m_d->comboCheckpoint && !style.checkpoints.isEmpty()) {
        QStringList available;
        for (int i = 0; i < m_d->comboCheckpoint->count(); ++i)
            available.append(m_d->comboCheckpoint->itemText(i));
        QString ckpt = ComfyFileLibrary::preferredCheckpoint(style.checkpoints, available);
        if (ckpt == QLatin1String("not-found"))
            ckpt = style.checkpoints.first();
        int ix = m_d->comboCheckpoint->findText(ckpt);
        if (ix >= 0)
            m_d->comboCheckpoint->setCurrentIndex(ix);
        else
            m_d->comboCheckpoint->setCurrentText(ckpt);
    }

    const QJsonObject samplerRoot = ComfyUIUtils::builtinSamplerPresetsRoot();
    QString sam, sch;
    int steps = style.samplerSteps;
    int minSteps = 1;
    double cfg = style.cfgScale;
    if (!style.samplerPresetName.isEmpty()
        && ComfyUIUtils::samplerPresetLookup(samplerRoot, style.samplerPresetName, &sam, &sch, &steps, &minSteps, &cfg)) {
        m_d->spinSteps->setValue(qMax(steps, minSteps));
        m_d->spinCfg->setValue(cfg);
        if (m_d->comboSampler) {
            const int si = m_d->comboSampler->findText(sam);
            if (si >= 0)
                m_d->comboSampler->setCurrentIndex(si);
            else
                m_d->comboSampler->setCurrentText(sam);
        }
        m_d->ksamplerScheduler = sch;
    } else {
        m_d->spinSteps->setValue(style.samplerSteps);
        m_d->spinCfg->setValue(style.cfgScale);
    }

    if (m_d->layerCountRow) {
        const QString arch = style.architecture.toLower();
        const bool qwenLayered = arch.contains(QLatin1String("qwen")) && arch.contains(QLatin1String("layered"));
        m_d->layerCountRow->setVisible(qwenLayered);
    }

    m_d->generateStyleVae = style.vae;
    m_d->generateStyleClipSkip = style.clipSkip;
    const QString ckpt = style.checkpoints.isEmpty() ? QString() : style.checkpoints.first();
    m_d->generateStyleArch =
        ComfyResources::archFromKey(style.architecture);
    if (m_d->generateStyleArch == ComfyResources::Arch::Unknown && !ckpt.isEmpty())
        m_d->generateStyleArch = ComfyResources::archFromCheckpointName(ckpt);
}

void ComfyUIRemoteDock::rebuildPresetComboItems()
{
    if (!m_d->comboPreset) return;
    ComfyUIUtils::ensureBundledPluginDataInstalled();
    ComfyStyleCollection::instance().reload();
    const QString prevStyleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
    m_d->comboPreset->blockSignals(true);
    m_d->comboPreset->clear();
    m_d->comboPreset->addItem(ComfyTr::tr("None"));
    m_d->comboPreset->setItemData(0, QStringLiteral("none"));
    m_d->comboPreset->setItemIcon(0, ComfyTheme::icon(QStringLiteral("file-json")));
    const bool showBuiltin = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
    const QList<const ComfyStyleEntry *> styles = ComfyStyleCollection::instance().filtered(showBuiltin);
    for (const ComfyStyleEntry *s : styles) {
        const int idx = m_d->comboPreset->addItem(s->name);
        m_d->comboPreset->setItemData(idx, s->styleId);
        const QString ckpt = s->checkpoints.isEmpty() ? QString() : s->checkpoints.first();
        m_d->comboPreset->setItemIcon(idx, ComfyTheme::checkpointIconForArchitectureKey(s->architecture, ckpt));
    }
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    const QStringList customNames = cfg.readEntry("PresetNames", QStringList());
    for (const QString &name : customNames) {
        if (name.isEmpty())
            continue;
        const int idx = m_d->comboPreset->addItem(name);
        m_d->comboPreset->setItemData(idx, QStringLiteral("custom:") + name);
    }
    int restoreIdx = 0;
    if (!prevStyleId.isEmpty() && prevStyleId != QLatin1String("none"))
        applyStyleIdToPresetCombo(m_d->comboPreset, prevStyleId);
    restoreIdx = m_d->comboPreset->currentIndex();
    if (restoreIdx < 0)
        restoreIdx = 0;
    m_d->comboPreset->setCurrentIndex(restoreIdx);
    m_d->comboPreset->blockSignals(false);
    ComfyTheme::applyFlatComboStyle(m_d->comboPreset);
    if (m_d->btnDeletePreset)
        m_d->btnDeletePreset->setEnabled(m_d->comboPreset->currentIndex() >= firstCustomPresetIndex());
    updateNegativePromptAlertVisibility();
    syncUpscaleRefinementModelFromPresetCombo();
    persistDocumentDefaultsToSettings();  // §13.194
}

void ComfyUIRemoteDock::syncUpscaleRefinementModelFromPresetCombo()
{
    if (!m_d->comboUpscaleRefinementModel || !m_d->comboPreset)
        return;
    const QString prevText = m_d->comboUpscaleRefinementModel->currentText();
    m_d->comboUpscaleRefinementModel->blockSignals(true);
    m_d->comboUpscaleRefinementModel->clear();
    for (int i = 0; i < m_d->comboPreset->count(); ++i)
        m_d->comboUpscaleRefinementModel->addItem(m_d->comboPreset->itemText(i));
    KConfigGroup ucfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const int restore = ucfg.readEntry(QStringLiteral("UpscaleRefinementModelIndex"), -1);
    if (restore >= 0 && restore < m_d->comboUpscaleRefinementModel->count())
        m_d->comboUpscaleRefinementModel->setCurrentIndex(restore);
    else if (!prevText.isEmpty()) {
        const int fi = m_d->comboUpscaleRefinementModel->findText(prevText);
        if (fi >= 0)
            m_d->comboUpscaleRefinementModel->setCurrentIndex(fi);
        else
            m_d->comboUpscaleRefinementModel->setCurrentIndex(0);
    } else {
        m_d->comboUpscaleRefinementModel->setCurrentIndex(0);
    }
    m_d->comboUpscaleRefinementModel->blockSignals(false);
}

bool ComfyUIRemoteDock::renameCustomPreset(const QString &oldName, const QString &newName)
{
    const QString o = oldName.trimmed();
    const QString n = newName.trimmed();
    if (o.isEmpty() || n.isEmpty() || o == n || !m_d->comboPreset)
        return false;

    KSharedConfig::Ptr cfgPtr = KSharedConfig::openConfig();
    KConfigGroup mainGrp(cfgPtr, QStringLiteral("ComfyUIRemote"));
    QStringList names = mainGrp.readEntry(QStringLiteral("PresetNames"), QStringList());
    const int nameIdx = names.indexOf(o);
    if (nameIdx < 0 || names.contains(n))
        return false;

    KConfigGroup fromGrp(cfgPtr, QStringLiteral("ComfyUIRemote_Preset_") + o);
    if (!fromGrp.exists())
        return false;
    KConfigGroup toGrp(cfgPtr, QStringLiteral("ComfyUIRemote_Preset_") + n);
    if (toGrp.exists())
        return false;

    const QMap<QString, QString> em = fromGrp.entryMap();
    for (auto it = em.constBegin(); it != em.constEnd(); ++it)
        toGrp.writeEntry(it.key(), it.value());

    fromGrp.deleteGroup();
    names[nameIdx] = n;
    mainGrp.writeEntry(QStringLiteral("PresetNames"), names);
    cfgPtr->sync();

    rebuildPresetComboItems();
    const int ni = m_d->comboPreset->findText(n);
    if (ni >= 0)
        m_d->comboPreset->setCurrentIndex(ni);
    return true;
}

void ComfyUIRemoteDock::applyQualitySamplerPresetFromSettings()
{
    const QString key =
        ComfyUIUtils::loadSettingsJson().value(QStringLiteral("quality_sampler_preset")).toString().trimmed();
    applyQualitySamplerPresetKey(key);
}

void ComfyUIRemoteDock::applyQualitySamplerPresetKey(const QString &presetName)
{
    const QString key = presetName.trimmed();
    if (key.isEmpty()) {
        m_d->ksamplerScheduler = QStringLiteral("normal");
        return;
    }
    QString sampler, scheduler;
    int steps = 20;
    int minSteps = 1;
    double cfg = 8.0;
    const QJsonObject root = ComfyUIUtils::builtinSamplerPresetsRoot();
    if (!ComfyUIUtils::samplerPresetLookup(root, key, &sampler, &scheduler, &steps, &minSteps, &cfg)) {
        m_d->ksamplerScheduler = QStringLiteral("normal");
        return;
    }
    if (m_d->comboSampler)
        m_d->comboSampler->setCurrentText(sampler);
    if (m_d->spinSteps)
        m_d->spinSteps->setValue(qMax(steps, minSteps));
    if (m_d->spinCfg)
        m_d->spinCfg->setValue(cfg);
    m_d->ksamplerScheduler = scheduler;
}

void ComfyUIRemoteDock::refreshStylesTabLoraWarning()
{
    QLabel *w = m_d->stylesTabLoraWarningLabel.data();
    QListWidget *list = m_d->stylesTabLoraListWidget.data();
    if (!w || !list) {
        return;
    }
    QListWidgetItem *it = list->currentItem();
    if (!it) {
        w->hide();
        return;
    }
    const QString fn = it->data(Qt::UserRole).toString().trimmed();
    if (fn.isEmpty() || !m_d->isConnected || m_d->comfyServerLoraFilenames.isEmpty()) {
        w->hide();
        return;
    }
    const bool onServer = ComfyUIUtils::loraFilenameKnownOnServer(fn, m_d->comfyServerLoraFilenames);
    if (fn.startsWith(QStringLiteral("lora-"), Qt::CaseInsensitive)) {
        w->setText(ComfyTr::tr("This LoRA is a reserved or server-managed resource."));
        w->setStyleSheet(QStringLiteral("color: #b8860b;"));
        w->setWordWrap(true);
        w->show();
    } else if (!onServer) {
        w->setText(ComfyTr::tr("The LoRA file is not installed on the server."));
        w->setStyleSheet(QStringLiteral("color: #b8860b;"));
        w->setWordWrap(true);
        w->show();
    } else {
        w->hide();
    }
}

void ComfyUIRemoteDock::applyStylesTabLoraListFilter()
{
    QListWidget *list = m_d->stylesTabLoraListWidget.data();
    if (!list) {
        return;
    }
    const int mode = m_d->stylesTabLoraFilterMode;
    const bool haveServer = m_d->isConnected && !m_d->comfyServerLoraFilenames.isEmpty();
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *it = list->item(i);
        if (!it) {
            continue;
        }
        const QString fn = it->data(Qt::UserRole).toString();
        const bool onServer = ComfyUIUtils::loraFilenameKnownOnServer(fn, m_d->comfyServerLoraFilenames);
        bool hide = false;
        if (mode == 1 && haveServer && !onServer)
            hide = true;
        else if (mode == 2 && haveServer && onServer)
            hide = true;
        list->setRowHidden(i, hide);
    }
}

qint64 ComfyUIRemoteDock::historyResultStorageBytes() const
{
    qint64 total = 0;
    for (const Private::HistoryEntry &e : m_d->historyEntries) {
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty()) {
                QFileInfo info(p);
                if (info.exists()) total += info.size();
            }
        }
    }
    return total;
}

void ComfyUIRemoteDock::pruneHistoryToStorageLimit()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    // §4.8: Active history size (RAM cache of thumbnails) — prefer history_active_mb, else legacy history_storage
    int limitMb = s.value(QStringLiteral("history_active_mb")).toInt(0);
    if (limitMb <= 0)
        limitMb = s.value(QStringLiteral("history_storage")).toInt(20);
    limitMb = qBound(5, limitMb, 20000);
    const qint64 limitBytes = static_cast<qint64>(limitMb) * 1024 * 1024;
    while (m_d->historyEntries.size() > 0 && historyResultStorageBytes() > limitBytes) {
        Private::HistoryEntry e = m_d->historyEntries.takeLast();
        evictDocumentEmbeddedSlotIfAny(e.documentSlot);
        QStringList paths = e.resultImagePaths;
        if (paths.isEmpty() && !e.resultImagePath.isEmpty())
            paths << e.resultImagePath;
        for (const QString &p : paths) {
            if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p);
        }
    }
    refreshHistoryList();
    updateHistoryUsageLabel();
}

void ComfyUIRemoteDock::updateHistoryUsageLabel()
{
    if (m_d->labelHistoryUsageMb) {
        const qint64 bytes = historyResultStorageBytes();
        const double mb = bytes / (1024.0 * 1024.0);
        m_d->labelHistoryUsageMb->setText(ComfyTr::tr("Currently using %1 MB", QString::number(mb, 'f', 1)));
    }
    if (m_d->labelStoredHistoryMb) {
        KisImageSP img = m_d->viewManager ? m_d->viewManager->image().toStrongRef() : KisImageSP();
        const qint64 docBytes = ComfyUIUtils::documentEmbeddedHistoryStorageBytes(img);
        const double docMb = docBytes / (1024.0 * 1024.0);
        m_d->labelStoredHistoryMb->setText(ComfyTr::tr("Currently using %1 MB", QString::number(docMb, 'f', 1)));
    }
}

void ComfyUIRemoteDock::syncPerformanceFromAutoPreset()
{
    QJsonObject s = ComfyUIUtils::loadSettingsJson();
    if (s.value(QStringLiteral("performance_preset")).toString() != QLatin1String("auto"))
        return;
    int b = m_d->spinBatchCount ? m_d->spinBatchCount->value() : 1;
    double m = m_d->resolutionMultiplier <= 0.0 ? 1.0 : m_d->resolutionMultiplier;
    ComfyUIUtils::generationPerformanceBatchResolution(s, m_d->lastComfySystemStats, b, m, &b, &m);
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(s, &m);
    b = qBound(1, b, 16);
    m = qMax(0.3, qMin(m <= 0.0 ? 1.0 : m, 3.0));
    if (m_d->spinBatchCount)
        m_d->spinBatchCount->setValue(qBound(m_d->spinBatchCount->minimum(), b, m_d->spinBatchCount->maximum()));
    m_d->resolutionMultiplier = m;
    if (m_d->sliderResolutionMultiplier) {
        const int sv = qRound(m * 10.0);
        m_d->sliderResolutionMultiplier->setValue(
            qBound(m_d->sliderResolutionMultiplier->minimum(), sv, m_d->sliderResolutionMultiplier->maximum()));
    }
    if (m_d->labelResolutionMultiplier)
        m_d->labelResolutionMultiplier->setText(QString::number(m, 'f', 1) + QLatin1String("×"));
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("BatchCount", m_d->spinBatchCount ? m_d->spinBatchCount->value() : b);
    cfg.writeEntry("ResolutionMultiplier", m_d->resolutionMultiplier);
    refreshQueueResolutionRowVisibility();
}

void ComfyUIRemoteDock::applyPromptHeader()
{
    if (m_d->regionPromptWidget)
        m_d->regionPromptWidget->setPromptHeaderMode(qBound(0, m_d->promptHeaderMode, 2));
    if (!m_d->regionsGroupBox || !m_d->regionHeaderLabel) return;
    const int mode = qBound(0, m_d->promptHeaderMode, 2);
    if (mode == 0) {
        m_d->regionsGroupBox->setTitle(ComfyTr::tr("Regions"));
        m_d->regionHeaderLabel->setPixmap(QPixmap());
        m_d->regionHeaderLabel->setText(ComfyTr::tr("Different prompt per area (layer or selection):"));
        m_d->regionHeaderLabel->show();
    } else if (mode == 1) {
        m_d->regionsGroupBox->setTitle(QString());
        m_d->regionHeaderLabel->setText(QString());
        m_d->regionHeaderLabel->setPixmap(
            KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("region-prompt"))).pixmap(16, 16));
        m_d->regionHeaderLabel->show();
    } else {
        m_d->regionsGroupBox->setTitle(QString());
        m_d->regionHeaderLabel->setText(QString());
        m_d->regionHeaderLabel->setPixmap(QPixmap());
        m_d->regionHeaderLabel->hide();
    }
}

void ComfyUIRemoteDock::refreshRegionsList()
{
    if (m_d->regionPromptWidget) {
        m_d->regionPromptWidget->bind(&comfyActiveRegionEntries(m_d.data()), &m_d->activeRegionIndex);
        m_d->regionPromptWidget->refresh();
    }
    refreshRegionControlLayersList();
}

void ComfyUIRemoteDock::loadRegionsFromConfig()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    int n = cfg.readEntry("RegionsCount", 0);
    m_d->regionEntries.clear();
    for (int i = 0; i < n; i++) {
        Private::RegionEntry e;
        e.name = cfg.readEntry(QString("Region_%1_Name").arg(i), QString());
        e.prompt = cfg.readEntry(QString("Region_%1_Prompt").arg(i), QString());
        e.maskSource = cfg.readEntry(QString("Region_%1_MaskSource").arg(i), "selection");
        e.layerIds = cfg.readEntry(QString("Region_%1_LayerIds").arg(i), QString());
        if (!e.name.isEmpty())
            m_d->regionEntries.append(e);
    }
    int ne = cfg.readEntry("EditRegionsCount", 0);
    m_d->editRegionEntries.clear();
    for (int i = 0; i < ne; i++) {
        Private::RegionEntry e;
        e.name = cfg.readEntry(QString("EditRegion_%1_Name").arg(i), QString());
        e.prompt = cfg.readEntry(QString("EditRegion_%1_Prompt").arg(i), QString());
        e.maskSource = cfg.readEntry(QString("EditRegion_%1_MaskSource").arg(i), QStringLiteral("selection"));
        e.layerIds = cfg.readEntry(QString("EditRegion_%1_LayerIds").arg(i), QString());
        if (!e.name.isEmpty())
            m_d->editRegionEntries.append(e);
    }
}

void ComfyUIRemoteDock::saveRegionsToConfig()
{
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", m_d->regionEntries.size());
    for (int i = 0; i < m_d->regionEntries.size(); i++) {
        const Private::RegionEntry &e = m_d->regionEntries.at(i);
        cfg.writeEntry(QString("Region_%1_Name").arg(i), e.name);
        cfg.writeEntry(QString("Region_%1_Prompt").arg(i), e.prompt);
        cfg.writeEntry(QString("Region_%1_MaskSource").arg(i), e.maskSource);
        cfg.writeEntry(QString("Region_%1_LayerIds").arg(i), e.layerIds);
    }
    cfg.writeEntry("EditRegionsCount", m_d->editRegionEntries.size());
    for (int i = 0; i < m_d->editRegionEntries.size(); i++) {
        const Private::RegionEntry &e = m_d->editRegionEntries.at(i);
        cfg.writeEntry(QString("EditRegion_%1_Name").arg(i), e.name);
        cfg.writeEntry(QString("EditRegion_%1_Prompt").arg(i), e.prompt);
        cfg.writeEntry(QString("EditRegion_%1_MaskSource").arg(i), e.maskSource);
        cfg.writeEntry(QString("EditRegion_%1_LayerIds").arg(i), e.layerIds);
    }
    cfg.config()->sync();
    scheduleDocumentUiJsonSave();
}

void ComfyUIRemoteDock::savePreviewLayerIdToDocument(const QString &layerId)
{
    if (!m_d->canvas) return;
    KisImageSP img = m_d->canvas->image().toStrongRef();
    if (!img) return;
    const QString key = ComfyUIUtils::previewLayerAnnotationKey();
    if (layerId.isEmpty()) {
        img->removeAnnotation(key);
    } else {
        img->removeAnnotation(key);
        img->addAnnotation(KisAnnotationSP(new KisAnnotation(key, ComfyUIUtils::documentAnnotationDescription(QStringLiteral("preview_layer")), layerId.toUtf8())));
    }
    m_d->previewLayerId = layerId;
}

void ComfyUIRemoteDock::slotAddPoseGuideToVectorLayer()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisLayerSP al = m_d->viewManager->activeLayer();
    KisShapeLayerSP sl(qobject_cast<KisShapeLayer *>(al.data()));
    if (!sl) {
        setStatusMessage(ComfyTr::tr("Select a vector layer, then add a pose guide."), true);
        return;
    }
    KisDocument *doc = m_d->canvas && m_d->canvas->imageView() ? m_d->canvas->imageView()->document() : nullptr;
    if (!doc) {
        setStatusMessage(ComfyTr::tr("Could not access the document to edit vector shapes."), true);
        return;
    }
    const int people = m_d->spinPoseGuidePeopleCount ? m_d->spinPoseGuidePeopleCount->value() : 1;
    if (ComfyUIPoseLayers::instance().addPoseCharacter(m_d->viewManager->image(), sl, doc, people))
        setStatusMessage(ComfyTr::tr("Pose guide added. Pose SVG is refreshed every 500 ms while the layer exists."), false);
    else
        setStatusMessage(ComfyTr::tr("Could not add pose guide (check that the layer is a vector layer)."), true);
}

