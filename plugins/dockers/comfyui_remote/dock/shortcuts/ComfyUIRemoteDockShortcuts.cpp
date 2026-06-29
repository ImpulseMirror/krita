/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include "ComfyGenerateRunner.h"
#include "ComfyControlRunner.h"
#include "ComfyGenerateUi.h"
#include "ComfyHistoryInternal.h"
#include "ComfyLiveRunner.h"
#include "ComfyLiveRunnerInternal.h"

#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QRandomGenerator>
#include <QThread>

#include <kis_image.h>
#include <kis_group_layer.h>
#include <kis_layer_utils.h>
#include <kis_layer_properties_icons.h>
#include <KisImageBarrierLock.h>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

namespace {

struct LiveApplyBlock {
    ComfyUIRemoteDock::Private *d = nullptr;
    KisImageSP image;

    explicit LiveApplyBlock(ComfyUIRemoteDock::Private *priv)
        : d(priv)
    {
        if (!d)
            return;
        d->liveRt.liveApplyInProgress = true;
        image = d->viewManager ? d->viewManager->image() : nullptr;
        for (int i = 0; i < 500 && ComfyLiveRunnerInternal::livePipelineBusy(d); ++i) {
            if (image)
                image->waitForDone();
            QThread::msleep(20);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        if (image)
            image->waitForDone();
    }

    ~LiveApplyBlock()
    {
        if (!d)
            return;
        if (image)
            image->waitForDone();
        d->liveRt.liveApplyInProgress = false;
    }
};

} // namespace

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
    if (!m_d->generate.checkEditMode) {
        return;
    }
    m_d->generate.checkEditMode->setChecked(!m_d->generate.checkEditMode->isChecked());
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
        if (m_d->live.checkLiveMode) {
            if (!m_d->live.checkLiveMode->isChecked())
                m_d->live.checkLiveMode->setChecked(true);
            else
                ComfyLiveRunner::startLivePollLoop(this);
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
    if (m_d->isFullAnimationBatch || !m_d->animationBatchPromptIdToIndex.isEmpty()
        || m_d->batchNeedsPerFrameReference || m_d->batchNeedsPerFrameAnimationRefine) {
        slotCancelQueue();
        return;
    }
    if (cancelCurrentGenerateJob()) {
        setStatusMessage(ComfyTr::tr("Cancelled current job."));
        return;
    }
    if (!m_d->inpaintRt.inpaintPromptId.isEmpty()) {
        m_d->inpaintRt.inpaintPollTimer->stop();
        m_d->inpaintRt.inpaintPromptId.clear();
        if (m_d->inpaint.btnInpaint)
            m_d->inpaint.btnInpaint->setEnabled(true);
        resetProgressBarToIdle();
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
    if (!m_d->upscaleRt.upscalePromptId.isEmpty()) {
        m_d->upscaleRt.upscalePollTimer->stop();
        m_d->upscaleRt.upscalePromptId.clear();
        if (m_d->upscale.btnUpscale)
            m_d->upscale.btnUpscale->setEnabled(true);
        resetProgressBarToIdle();
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
    if (!m_d->liveRt.livePromptId.isEmpty()) {
        m_d->liveRt.livePollTimer->stop();
        m_d->liveRt.livePromptId.clear();
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
    if (!m_d->liveRt.lastLiveResultImagePath.isEmpty() && QFile::exists(m_d->liveRt.lastLiveResultImagePath)) {
        const LiveApplyBlock liveApply(m_d.data());
        removeStaleLiveCanvasPreviewLayer();
        if (m_d->viewManager) {
            KisImageSP prepImage = m_d->viewManager->image();
            if (prepImage)
                ComfyHistoryInternal::removePreviewLayersFromImage(prepImage, m_d->viewManager.data(), m_d->previewLayerId);
        }
        QJsonObject ls = ComfyUIUtils::loadSettingsJson();
        QString regionBeh = ls.value(QStringLiteral("apply_region_behavior_live")).toString();
        if (regionBeh.isEmpty())
            regionBeh = QStringLiteral("replace");
        QStringList regionLayerNames;
        if (!m_d->liveRt.liveRegionalInputs.isEmpty()) {
            for (const Private::RegionEntry &re : comfyActiveRegionEntries(m_d.data())) {
                if (re.maskSource.startsWith(QLatin1String("layer:")))
                    regionLayerNames.append(re.maskSource.mid(6));
            }
        }
        bool applied = false;
        if (!regionLayerNames.isEmpty() && regionBeh != QLatin1String("none"))
            applied = applyResultToNamedRegionLayers(m_d->liveRt.lastLiveResultImagePath, regionLayerNames, regionBeh);
        if (!applied) {
            QString liveBeh = ls.value(QStringLiteral("apply_behavior_live")).toString();
            if (liveBeh.isEmpty())
                liveBeh = QStringLiteral("replace");
            const QString layerName = ComfyHistoryInternal::liveResultLayerName(
                m_d->liveRt.livePreparedPositive, static_cast<qint64>(m_d->liveRt.livePreparedSeed));
            QRect resultBounds;
            if (m_d->liveRt.livePrepared.hasMask && m_d->liveRt.livePrepared.contextBounds.isValid())
                resultBounds = m_d->liveRt.livePrepared.contextBounds;
            applied = applyResultFileWithBehavior(m_d->liveRt.lastLiveResultImagePath, liveBeh, layerName,
                                                  resultBounds);
        }
        if (applied && ls.value(QStringLiteral("new_seed_after_apply")).toBool(false) && m_d->generate.spinSeed) {
            m_d->generate.spinSeed->setValue(
                static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
        }
        if (applied && m_d->viewManager) {
            KisImageSP image = m_d->viewManager->image();
            KisLayerSP active = m_d->viewManager->activeLayer();
            if (image)
                ComfyHistoryInternal::refreshCanvasProjectionAfterApply(image, active);
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
    if (m_d->liveRt.lastLiveResultImagePath.isEmpty() || !QFile::exists(m_d->liveRt.lastLiveResultImagePath)) {
        setStatusMessage(ComfyTr::tr("No live result to apply yet."), false, true);
        return;
    }
    const LiveApplyBlock liveApply(m_d.data());
    removeStaleLiveCanvasPreviewLayer();
    const QString layerName = ComfyHistoryInternal::liveResultLayerName(
        m_d->liveRt.livePreparedPositive, static_cast<qint64>(m_d->liveRt.livePreparedSeed));
    QRect resultBounds;
    if (m_d->liveRt.livePrepared.hasMask && m_d->liveRt.livePrepared.contextBounds.isValid())
        resultBounds = m_d->liveRt.livePrepared.contextBounds;
    if (!applyResultFileWithBehavior(m_d->liveRt.lastLiveResultImagePath, QStringLiteral("layer"), layerName,
                                     resultBounds))
        setStatusMessage(ComfyTr::tr("Could not import image."), true);
    else {
        if (m_d->viewManager) {
            KisImageSP image = m_d->viewManager->image();
            KisLayerSP active = m_d->viewManager->activeLayer();
            if (image)
                ComfyHistoryInternal::refreshCanvasProjectionAfterApply(image, active);
        }
        if (m_d->canvas)
            m_d->canvas->updateCanvas();
    }
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
