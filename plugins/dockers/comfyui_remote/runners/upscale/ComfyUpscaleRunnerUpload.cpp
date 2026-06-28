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
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#include <kis_image.h>
#include <kis_image_manager.h>


using namespace ComfyUpscaleRunnerInternal;

namespace ComfyUpscaleRunner {

void beginConditioningUploadPipeline(ComfyUIRemoteDock *dock)
{

    dock->m_d->upscaleRt.upscaleControlLayersActive = controlLayersForUpscale(dock->m_d.data());
    KisImageSP image = dock->m_d->viewManager ? dock->m_d->viewManager->image().toStrongRef() : KisImageSP();
    ComfyUploadPipeline::ControlUploadOptions controlOpts;
    controlOpts.filenamePrefix = QStringLiteral("upscale_control_");
    if (dock->m_d->upscaleRt.upscaleStashedW2 > 0 && dock->m_d->upscaleRt.upscaleStashedH2 > 0)
        controlOpts.targetSize = QSize(dock->m_d->upscaleRt.upscaleStashedW2, dock->m_d->upscaleRt.upscaleStashedH2);
    const QList<ComfyUploadPipeline::ImageItem> controlItems =
        ComfyControlLayer::anyNeedsGenerateUpload(dock->m_d->upscaleRt.upscaleControlLayersActive)
            ? ComfyUploadPipeline::buildControlUploadItems(image, dock->m_d->upscaleRt.upscaleControlLayersActive, controlOpts)
            : QList<ComfyUploadPipeline::ImageItem>();

    if (controlItems.isEmpty()) {
        finalizeWorkflowAndSubmit(dock);
        return;
    }

    dock->m_d->upscaleRt.upscaleAwaitingControlUploads = true;
    dock->m_d->upscaleRt.upscaleControlUploadedNames.clear();
    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.setProgressKind = [dock](bool isUpload) { dock->setProgressBarKind(isUpload); };
    handlers.setStatusText = [dock](const QString &text) {
        if (dock->m_d->labelStatus)
            dock->m_d->labelStatus->setText(text);
    };
    handlers.setStatusMessage = [dock](const QString &msg, bool isError) { dock->setStatusMessage(msg, isError); };
    handlers.onComplete = [dock](const ComfyUploadPipeline::Result &result) {
        dock->m_d->upscaleRt.upscaleAwaitingControlUploads = false;
        dock->m_d->upscaleRt.upscaleControlUploadedNames = result.uploadedImageNames;
        finalizeWorkflowAndSubmit(dock);
    };
    handlers.onAbort = [dock]() {
        dock->m_d->upscaleRt.upscaleAwaitingControlUploads = false;
        dock->m_d->upscale.btnUpscale->setEnabled(true);
    };
    run->start(dock->m_d->editServerUrl->text().trimmed(), {}, controlItems, std::move(handlers));

}

void uploadNextRegionMask(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->upscaleRt.upscaleAwaitingRegionMaskUploads || !dock->m_d->nam) {
        dock->m_d->upscale.btnUpscale->setEnabled(true);
        return;
    }

    const int targetW = dock->m_d->upscaleRt.upscaleStashedW2;
    const int targetH = dock->m_d->upscaleRt.upscaleStashedH2;
    QList<ComfyUploadPipeline::ImageItem> maskItems;
    for (int inputIdx = 0; inputIdx < dock->m_d->upscaleRt.upscaleProcessedRegions.size(); ++inputIdx) {
        if (inputIdx >= dock->m_d->upscaleRt.upscaleRegionalInputs.size())
            continue;

        const ComfyRegionProcess::ProcessedRegionEntry &region = dock->m_d->upscaleRt.upscaleProcessedRegions.at(inputIdx);
        if (region.maskGray.isNull()) {
            dock->setStatusMessage(ComfyTr::tr("Region mask is empty."), true);
            dock->m_d->upscaleRt.upscaleAwaitingRegionMaskUploads = false;
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            return;
        }
        const QString regionLabel =
            region.isBackground ? ComfyTr::tr("background") : QString::number(inputIdx);
        ComfyUploadPipeline::ImageItem item;
        item.statusText = ComfyTr::tr("Uploading region mask %1…", regionLabel);
        item.filenameHint = QStringLiteral("upscale_region_mask_%1.png").arg(inputIdx);
        const QImage maskGray = region.maskGray;
        item.prepareImage = [maskGray, targetW, targetH]() {
            return maskPngForComfyUpload(maskGray, targetW, targetH);
        };
        item.onUploaded = [dock, inputIdx](const QString &name) {
            if (inputIdx < dock->m_d->upscaleRt.upscaleRegionalInputs.size())
                dock->m_d->upscaleRt.upscaleRegionalInputs[inputIdx].maskImageName = name;
        };
        maskItems.append(item);
    }

    auto finishRegionMasks = [dock]() {
        dock->m_d->upscaleRt.upscaleAwaitingRegionMaskUploads = false;
        beginConditioningUploadPipeline(dock);
    };

    if (maskItems.isEmpty()) {
        finishRegionMasks();
        return;
    }

    auto *run = new ComfyUploadPipeline::Run(dock->m_d->nam, dock);
    ComfyUploadPipeline::Handlers handlers;
    handlers.setProgressKind = [dock](bool isUpload) { dock->setProgressBarKind(isUpload); };
    handlers.setStatusText = [dock](const QString &text) {
        if (dock->m_d->labelStatus)
            dock->m_d->labelStatus->setText(text);
    };
    handlers.setStatusMessage = [dock](const QString &msg, bool isError) { dock->setStatusMessage(msg, isError); };
    handlers.onComplete = [finishRegionMasks](const ComfyUploadPipeline::Result &) { finishRegionMasks(); };
    handlers.onAbort = [dock]() {
        dock->m_d->upscaleRt.upscaleAwaitingRegionMaskUploads = false;
        dock->m_d->upscale.btnUpscale->setEnabled(true);
    };
    run->start(dock->m_d->editServerUrl->text().trimmed(), {}, maskItems, std::move(handlers));

}

void finalizeWorkflowAndSubmit(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->upscaleRt.upscalePendingIsTiled) {
        dock->m_d->upscale.btnUpscale->setEnabled(true);
        return;
    }

    ComfyWorkflowEngine::UpscaleTiledParams tp = dock->m_d->upscaleRt.upscaleStashedTiledParams;
    tp.regionalPrompts = dock->m_d->upscaleRt.upscaleRegionalInputs;

    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : dock->m_d->upscaleRt.upscaleControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= dock->m_d->upscaleRt.upscaleControlUploadedNames.size())
            break;
        const QString imageName = dock->m_d->upscaleRt.upscaleControlUploadedNames.at(uploadIdx++);
        if (ComfyResources::ControlMode::isIpAdapter(ce.mode)) {
            ComfyWorkflowEngine::IpAdapterLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            tp.ipAdapterLayers.append(in);
        } else {
            ComfyWorkflowEngine::ControlNetLayerInput in;
            in.mode = ce.mode;
            in.imageName = imageName;
            in.strength = ComfyControlLayer::strengthAsFloat(ce.strength);
            in.startPercent = ce.start;
            in.endPercent = ce.end;
            tp.controlLayers.append(in);
        }
    }

    QJsonObject workflow = ComfyWorkflowEngine::buildUpscaleTiled(tp);
    if (workflow.isEmpty()) {
        dock->setStatusMessage(ComfyTr::tr("Tiled upscale workflow error."), true);
        dock->m_d->upscale.btnUpscale->setEnabled(true);
        dock->m_d->progressBar->setValue(0);
        dock->m_d->upscaleRt.upscalePendingIsTiled = false;
        dock->m_d->upscaleRt.upscaleRegionalInputs.clear();
        dock->m_d->upscaleRt.upscaleProcessedRegions.clear();
        return;
    }
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    const bool wantRefine = dock->m_d->upscaleRt.upscalePendingWantRefine;
    dock->m_d->upscaleRt.upscalePendingIsTiled = false;
    dock->m_d->upscaleRt.upscaleRegionalInputs.clear();
    dock->m_d->upscaleRt.upscaleProcessedRegions.clear();
    dock->m_d->upscaleRt.upscaleControlLayersActive.clear();
    dock->m_d->upscaleRt.upscaleControlUploadedNames.clear();
    submitWorkflow(dock, workflow, wantRefine, true);

}

void submitWorkflow(ComfyUIRemoteDock *dock, const QJsonObject &workflow, bool wantRefine, bool useTiledRefine)
{

    if (dock->m_d->clientId.isEmpty())
        dock->m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ComfyPromptClient::SubmitRequest submitReq;
    submitReq.workflow = workflow;
    submitReq.clientId = dock->m_d->clientId;
    submitReq.expectedPromptId = expectedPromptId;
    const QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    ComfyPromptClient::submitPrompt(dock->m_d->nam, urlStr, submitReq, dock,
                                    [dock, expectedPromptId, wantRefine, useTiledRefine](
                                        const ComfyPromptClient::SubmitResult &result) {
        if (!result.ok) {
            dock->setStatusMessage(ComfyTr::tr("Submit error: %1", result.errorMessage), true);
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            dock->m_d->progressBar->setValue(0);
            return;
        }
        if (result.promptId != expectedPromptId) {
            dock->setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            dock->m_d->upscale.btnUpscale->setEnabled(true);
            dock->m_d->progressBar->setValue(0);
            return;
        }
        dock->m_d->upscaleRt.upscalePromptId = result.promptId;
        dock->m_d->upscaleRt.upscalePollCount = 0;
        dock->m_d->upscaleRt.upscaleLastSubmitUsedRefine = wantRefine;
        dock->m_d->labelStatus->setText(useTiledRefine ? ComfyTr::tr("Tiled upscale…")
                                                 : (wantRefine ? ComfyTr::tr("Upscaling and refining…")
                                                               : ComfyTr::tr("Upscaling…")));
        dock->m_d->upscaleRt.upscalePollTimer->start(1000);
    });

}



} // namespace ComfyUpscaleRunner
