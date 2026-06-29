/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLiveRunner.h"
#include "ComfyLiveRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRandomGenerator>
#include <QRect>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#include <kis_image.h>
#include <kis_image_manager.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

using namespace ComfyLiveRunnerInternal;

namespace {

ComfyUIUtils::SamplerPresetLoraResult resolveLiveSamplerLora(ComfyUIRemoteDock *dock)
{
    const ComfyStyleEntry *styleEntry = dock->currentJsonStyleEntry();
    const QString livePreset =
        ComfyUIUtils::liveSamplerPresetName(styleEntry, ComfyUIUtils::loadSettingsJson());
    const QString ckpt = dock->checkpointForGenerate();
    const ComfyResources::Arch arch =
        ComfyWorkflowEngine::resolveArch(ckpt, styleEntry ? styleEntry->architecture : QString());
    return ComfyUIUtils::resolveSamplerPresetLora(
        livePreset,
        arch,
        ComfyUIUtils::mergedServerLoraFilenames(dock->m_d->comfyServerLoraFilenames));
}

} // namespace

namespace ComfyLiveRunner {

void startLivePollLoop(ComfyUIRemoteDock *dock)
{
    if (!dock || !dock->m_d->live.checkLiveMode || !dock->m_d->live.checkLiveMode->isChecked())
        return;
    if (dock->m_d->isConnected && dock->m_d->comfyServerLoraFilenames.isEmpty())
        dock->fetchComfyModelsLorasMergeAndRefreshStylesTab();

    const ComfyUIUtils::SamplerPresetLoraResult samplerLora = resolveLiveSamplerLora(dock);
    if (!samplerLora.ok && !samplerLora.errorMessage.isEmpty())
        dock->setStatusMessage(samplerLora.errorMessage, true);

    dock->m_d->liveRt.liveSamplerLoraBlockMessage.clear();
    dock->m_d->liveRt.liveScheduler.reset();
    dock->removeStaleLiveCanvasPreviewLayer();
    if (dock->m_d->liveRt.liveTimer && !dock->m_d->liveRt.liveTimer->isActive())
        dock->m_d->liveRt.liveTimer->start();
    qCWarning(KIS_COMFYUI_REMOTE).noquote() << QStringLiteral("COMFY_LIVE poll loop started");
    onTick(dock);
}

void stopLivePollLoop(ComfyUIRemoteDock *dock)
{
    if (!dock)
        return;
    if (dock->m_d->liveRt.liveTimer)
        dock->m_d->liveRt.liveTimer->stop();
    dock->m_d->liveRt.liveScheduler.reset();
    dock->m_d->liveRt.livePipelineBusy = false;
    dock->clearLiveDockerPreview();
    qCWarning(KIS_COMFYUI_REMOTE).noquote() << QStringLiteral("COMFY_LIVE poll loop stopped");
}

void onTick(ComfyUIRemoteDock *dock)
{
    if (!dock->m_d->live.checkLiveMode || !dock->m_d->live.checkLiveMode->isChecked()) {
        stopLivePollLoop(dock);
        return;
    }
    if (livePipelineBusy(dock->m_d.data()) || dock->m_d->liveRt.liveApplyInProgress) {
        return;
    }
    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        return;
    }

    KisImageSP image = dock->m_d->viewManager->image();
    const auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        dock->setStatusMessage(colorCheck.second, true);
        return;
    }
    const QString urlStr = dock->m_d->editServerUrl ? dock->m_d->editServerUrl->text().trimmed() : QString();
    if (urlStr.isEmpty() || !dock->m_d->isConnected) {
        return;
    }

    dock->commitPromptEditorsFromUi();
    const ComfyPrepareLiveWorkflow::Result prep =
        ComfyPrepareLiveWorkflow::prepare(dock->prepareLiveWorkflowInput());
    if (!prep.ok) {
        if (!prep.errorMessage.isEmpty())
            qCWarning(KIS_COMFYUI_REMOTE).noquote()
                << QStringLiteral("COMFY_LIVE prepare not ready:") << prep.errorMessage;
        return;
    }

    const ComfyUIUtils::SamplerPresetLoraResult samplerLora = resolveLiveSamplerLora(dock);
    if (!samplerLora.ok) {
        if (samplerLora.errorMessage != dock->m_d->liveRt.liveSamplerLoraBlockMessage) {
            dock->m_d->liveRt.liveSamplerLoraBlockMessage = samplerLora.errorMessage;
            dock->setStatusMessage(samplerLora.errorMessage, true);
        }
        return;
    }
    dock->m_d->liveRt.liveSamplerLoraBlockMessage.clear();

    const int seed = dock->m_d->generate.spinSeed ? dock->m_d->generate.spinSeed->value() : 0;
    const QString positive =
        ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt ? dock->m_d->generate.editPrompt->toPlainText()
                                                                        : QString())
            .trimmed();
    const QString negative =
        ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative ? dock->m_d->generate.editNegative->toPlainText()
                                                                          : QString())
            .trimmed();
    const bool editMode = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
    const QByteArray fingerprint = computeLiveInputFingerprint(prep, positive, negative, seed, editMode, prep.contextImage);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (!dock->m_d->liveRt.liveScheduler.shouldGenerate(fingerprint, nowMs)) {
        return;
    }

    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_LIVE change detected — scheduling generation strength=")
        << prep.strength0to1 << QStringLiteral("bounds=") << prep.contextBounds;
    beginUploadPipeline(dock);
}

} // namespace ComfyLiveRunner
