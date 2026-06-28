/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUpscaleRunner.h"
#include "ComfyUpscaleRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionLink.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyHistoryInternal.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QImage>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#include <kis_image.h>


using namespace ComfyUpscaleRunnerInternal;

namespace ComfyUpscaleRunner {

void onPollTimer(ComfyUIRemoteDock *dock)
{
    if (dock->m_d->upscaleRt.upscalePromptId.isEmpty())
        return;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->m_d->upscaleRt.upscalePromptId.clear();
        dock->m_d->upscale.btnUpscale->setEnabled(true);
        dock->m_d->progressBar->setValue(0);
        return;
    }
    const QString promptId = dock->m_d->upscaleRt.upscalePromptId;
    ComfyPromptClient::fetchHistory(dock->m_d->nam, urlStr, promptId, dock,
                                    [dock, urlStr](const ComfyPromptClient::HistoryFetchResult &result) {
        const auto failUpscale = [dock]() {
            dock->m_d->upscaleRt.upscalePromptId.clear();
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            dock->m_d->progressBar->setValue(0);
        };
        ComfyPollRunnerCommon::PollRunningConfig running;
        running.pollCount = &dock->m_d->upscaleRt.upscalePollCount;
        running.maxPollCount = UpscaleRuntime::upscaleMaxPollCount;
        running.pollTimer = dock->m_d->upscaleRt.upscalePollTimer;
        running.onTimeout = [dock, failUpscale]() {
            dock->setStatusMessage(ComfyTr::tr("Upscale timed out."), true);
            failUpscale();
        };
        const auto terminal = [dock, failUpscale](const ComfyPromptClient::HistoryFetchResult &r) {
            if (r.state == ComfyPromptClient::HistoryState::NetworkError)
                dock->setStatusMessage(ComfyTr::tr("History error: %1", r.errorMessage), true);
            else if (r.state == ComfyPromptClient::HistoryState::ExecutionError)
                dock->setStatusMessage(r.errorMessage, true);
            failUpscale();
        };
        if (ComfyPollRunnerCommon::handleHistoryFetch(result, running, terminal)
            == ComfyPollRunnerCommon::HistoryPollOutcome::Handled)
            return;
        ComfyPromptClient::downloadOutputImage(dock->m_d->nam, urlStr, result.images.first(), dock,
                                               [dock](const QByteArray &data, const QString &errorMessage) {
            if (!errorMessage.isEmpty()) {
                dock->m_d->upscaleRt.upscalePromptId.clear();
                dock->m_d->upscale.btnUpscale->setEnabled(true);
                dock->m_d->progressBar->setValue(0);
                return;
            }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (!tmp.open()) {
                dock->m_d->upscaleRt.upscalePromptId.clear();
                dock->m_d->upscale.btnUpscale->setEnabled(true);
                dock->m_d->progressBar->setValue(0);
                return;
            }
            tmp.write(data);
            tmp.close();
            const QImage resultImg = QImage::fromData(data);
            const int resultW = dock->m_d->upscaleRt.upscaleResultW > 0 ? dock->m_d->upscaleRt.upscaleResultW
                                                                        : resultImg.width();
            const int resultH = dock->m_d->upscaleRt.upscaleResultH > 0 ? dock->m_d->upscaleRt.upscaleResultH
                                                                        : resultImg.height();
            const QString layerName = ComfyHistoryInternal::upscaleResultLayerName(
                resultW, resultH, dock->m_d->upscaleRt.upscaleSeed);
            const QString applyBehavior = ComfyHistoryInternal::applyBehaviorFromSettings(dock->m_d.data());
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("UPSCALE_DIAG poll complete importing layer size=") << resultImg.size()
                << QStringLiteral("name=") << layerName << QStringLiteral("behavior=") << applyBehavior;
            if (!dock->applyResultFileWithBehavior(tmp.fileName(), applyBehavior, layerName)) {
                dock->setStatusMessage(ComfyTr::tr("Could not import upscale result."), true);
                dock->m_d->upscaleRt.upscalePromptId.clear();
                dock->m_d->upscale.btnUpscale->setEnabled(true);
                dock->m_d->progressBar->setValue(0);
                return;
            }
            dock->m_d->labelStatus->setText(dock->m_d->upscaleRt.upscaleLastSubmitUsedRefine
                ? ComfyTr::tr("Upscale and refine done. Result added as new layer.")
                : ComfyTr::tr("Upscale done. Result added as new layer."));
            dock->m_d->progressBar->setValue(100);
            dock->m_d->upscaleRt.upscalePromptId.clear();
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            dock->updateQueueStatus();
        });
    });
}

} // namespace ComfyUpscaleRunner
