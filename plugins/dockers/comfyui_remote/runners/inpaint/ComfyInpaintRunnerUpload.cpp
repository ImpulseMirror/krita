/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyInpaintRunner.h"
#include "ComfyInpaintRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionLink.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QDateTime>
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

#include <KisViewManager.h>
#include <kis_image.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)


using namespace ComfyInpaintRunnerInternal;

namespace ComfyInpaintRunner {

void beginUploadPipeline(ComfyUIRemoteDock *dock)
{

    const QStringList loraPaths =
        dock->m_d->isConnected && dock->m_d->nam
            ? ComfyUploadPipeline::collectMissingLoraUploadPaths(dock->m_d->comfyServerLoraFilenames)
            : QStringList();

    KisImageSP image = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
    ComfyUploadPipeline::ControlUploadOptions controlOpts;
    controlOpts.filenamePrefix = QStringLiteral("inpaint_control_");
    const QList<ComfyUploadPipeline::ImageItem> controlItems =
        ComfyControlLayer::anyNeedsGenerateUpload(dock->m_d->inpaintRt.inpaintControlLayersActive)
            ? ComfyUploadPipeline::buildControlUploadItems(image, dock->m_d->inpaintRt.inpaintControlLayersActive, controlOpts)
            : QList<ComfyUploadPipeline::ImageItem>();

    if (loraPaths.isEmpty() && controlItems.isEmpty()) {
        submitWorkflow(dock, dock->m_d->inpaintRt.inpaintPendingWorkflow);
        return;
    }

    dock->m_d->inpaintRt.inpaintControlUploadedNames.clear();
    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.setProgressKind = [dock](bool isUpload) { dock->setProgressBarKind(isUpload); };
    handlers.setStatusText = [dock](const QString &text) {
        if (dock->m_d->labelStatus)
            dock->m_d->labelStatus->setText(text);
    };
    handlers.setStatusMessage = [dock](const QString &msg, bool isError) { dock->setStatusMessage(msg, isError); };
    handlers.onLoraUploaded = [dock](const QString &) {
        ComfyFileLibrary::instance().init();
        ComfyFileLibrary::instance().updateRemoteLoras(dock->m_d->comfyServerLoraFilenames);
    };
    handlers.onComplete = [dock](const ComfyUploadPipeline::Result &result) {
        dock->m_d->inpaintRt.inpaintAwaitingLoraUploads = false;
        dock->m_d->inpaintRt.inpaintControlUploadedNames = result.uploadedImageNames;
        submitWorkflow(dock, dock->m_d->inpaintRt.inpaintPendingWorkflow);
    };
    handlers.onAbort = [dock]() {
        dock->m_d->inpaintRt.inpaintAwaitingLoraUploads = false;
        dock->reEnableGenerateUi();
    };
    dock->m_d->inpaintRt.inpaintAwaitingLoraUploads = !loraPaths.isEmpty();
    run->start(dock->m_d->editServerUrl->text().trimmed(),
               loraPaths,
               controlItems,
               std::move(handlers),
               &dock->m_d->comfyServerLoraFilenames);

}

void submitWorkflow(ComfyUIRemoteDock *dock, QJsonObject workflow)
{

    ComfyWorkflowEngine::applyCheckpointStyleOptions(
        &workflow, dock->m_d->generateRt.generateStyleVae, dock->m_d->generateRt.generateStyleClipSkip, dock->m_d->generateRt.generateStyleArch);

    QList<ComfyWorkflowEngine::IpAdapterLayerInput> ipInputs;
    QList<ComfyWorkflowEngine::ControlNetLayerInput> cnInputs;
    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : dock->m_d->inpaintRt.inpaintControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= dock->m_d->inpaintRt.inpaintControlUploadedNames.size())
            break;
        const QString imageName = dock->m_d->inpaintRt.inpaintControlUploadedNames.at(uploadIdx++);
        if (ComfyResources::ControlMode::isIpAdapter(ce.mode)) {
            ComfyWorkflowEngine::IpAdapterLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            ipInputs.append(in);
        } else {
            ComfyWorkflowEngine::ControlNetLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            cnInputs.append(in);
        }
    }
    ComfyWorkflowEngine::applyIpAdapterLayers(&workflow, ipInputs, dock->m_d->inpaintRt.inpaintPendingArch);
    ComfyWorkflowEngine::applyControlNetLayers(&workflow, cnInputs, dock->m_d->inpaintRt.inpaintPendingArch);
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    dock->m_d->inpaintRt.inpaintControlLayersActive.clear();
    dock->m_d->inpaintRt.inpaintControlUploadedNames.clear();

    {
        QString latentPath;
        const QString graphSummary = summarizeWorkflowGraph(workflow, &latentPath);
        InpaintDiagSnapshot diag;
        diag.event = QStringLiteral("submit");
        diag.pluginVersion = ComfyUIUtils::pluginVersion();
        diag.workflowKind =
            dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow ? QStringLiteral("refine_region")
                                                                : QStringLiteral("inpaint");
        diag.refineRegion = dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow;
        diag.latentPath = latentPath;
        diag.graphSummary = graphSummary;
        diag.archKey = dock->m_d->inpaintRt.inpaintDiagArchKey;
        diag.denoise = dock->m_d->inpaintRt.inpaintDiagDenoise;
        logInpaintDiag(diag);
    }

    if (dock->m_d->clientId.isEmpty())
        dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ComfyPromptClient::SubmitRequest submitReq;
    submitReq.workflow = workflow;
    submitReq.clientId = dock->m_d->clientId;
    submitReq.expectedPromptId = expectedPromptId;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (!dock->m_d->nam) {
        dock->setStatusMessage(ComfyTr::tr("Not connected to ComfyUI server."), true);
        dock->reEnableGenerateUi();
        return;
    }
    ComfyPromptClient::submitPrompt(dock->m_d->nam, urlStr, submitReq, dock,
                                    [dock, expectedPromptId](const ComfyPromptClient::SubmitResult &result) {
        if (!result.ok) {
            if (!result.responseBody.isEmpty()) {
            }
            dock->setStatusMessage(ComfyTr::tr("Submit error: %1",
                                           result.errorMessage.isEmpty()
                                               ? ComfyTr::tr("Network error: could not submit prompt.")
                                               : result.errorMessage),
                              true);
            dock->reEnableGenerateUi();
            dock->resetProgressBarToIdle();
            return;
        }
        if (result.promptId != expectedPromptId) {
            dock->setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            dock->reEnableGenerateUi();
            dock->resetProgressBarToIdle();
            return;
        }
        dock->m_d->inpaintRt.inpaintPromptId = result.promptId;
        dock->m_d->inpaintRt.inpaintPollCount = 0;
        dock->m_d->labelStatus->setText(ComfyTr::tr("Inpainting…"));
        dock->m_d->inpaintRt.inpaintPollTimer->start(1000);
    });

}

} // namespace ComfyInpaintRunner
