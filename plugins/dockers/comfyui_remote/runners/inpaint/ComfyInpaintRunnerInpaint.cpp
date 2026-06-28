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

namespace {

QString prepWorkflowKindName(ComfyPrepareGenerateWorkflow::WorkflowKind kind)
{
    using WK = ComfyPrepareGenerateWorkflow::WorkflowKind;
    switch (kind) {
    case WK::Generate:
        return QStringLiteral("generate");
    case WK::Refine:
        return QStringLiteral("refine");
    case WK::Inpaint:
        return QStringLiteral("inpaint");
    case WK::RefineRegion:
        return QStringLiteral("refine_region");
    }
    return QStringLiteral("unknown");
}

} // namespace

void onInpaint(ComfyUIRemoteDock *dock)
{

    qCWarning(KIS_COMFYUI_REMOTE)
        << "slotInpaint ENTER url=" << (dock->m_d->editServerUrl ? dock->m_d->editServerUrl->text() : QStringLiteral("<no-url-widget>"))
        << "hasView=" << (dock->m_d->viewManager != nullptr)
        << "hasImage=" << (dock->m_d->viewManager && dock->m_d->viewManager->image())
        << "hasNam=" << (dock->m_d->nam != nullptr)
        << "btnInpaint=" << (dock->m_d->inpaint.btnInpaint ? dock->m_d->inpaint.btnInpaint->isEnabled() : false)
        << "isConnected=" << dock->m_d->isConnected;
    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: no view/image, aborting";
        dock->setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    if (!dock->m_d->nam) {
        // FAITHFUL_PORT/BUG: previously this function happily dereferenced
        // dock->m_d->nam without checking it. If we landed here from
        // tryStartRefineFromGenerate before isConnected ever flipped true the
        // post() call would have crashed silently on some configurations.
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: dock->m_d->nam is null, aborting";
        dock->setStatusMessage(ComfyTr::tr("Not connected to ComfyUI server. Open Settings and connect first."), true);
        return;
    }
    dock->commitPromptEditorsFromUi();
    KisImageSP image = dock->m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: colorCheck failed:" << colorCheck.second;
        dock->setStatusMessage(colorCheck.second, true);
        return;
    }
    const int extentW = image->width();
    const int extentH = image->height();

    ComfyPrepareGenerateWorkflow::PrepareFlags prepFlags;
    prepFlags.requireMask = true;
    prepFlags.captureImage = true;
    const ComfyPrepareGenerateWorkflow::Result prep =
        ComfyPrepareGenerateWorkflow::prepare(dock->prepareGenerateWorkflowInput(prepFlags));
    if (!prep.ok) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: prepare failed:" << prep.errorMessage;
        dock->setStatusMessage(prep.errorMessage, true);
        return;
    }
    if (!prep.hasMask
        || (prep.workflowKind != ComfyPrepareGenerateWorkflow::WorkflowKind::Inpaint
            && prep.workflowKind != ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion)) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: unsupported workflow kind"
                                      << static_cast<int>(prep.workflowKind);
        dock->setStatusMessage(ComfyTr::tr("Make a selection or enable region-only mode on an active region."), true);
        return;
    }

    dock->m_d->inpaintRt.inpaintPrepared = prep;
    dock->m_d->inpaintRt.inpaintFromRegionLayer = prep.fromRegionLayer;
    dock->m_d->inpaintRt.inpaintTargetBounds = prep.maskPaddedBounds;
    dock->m_d->inpaintRt.inpaintContextBounds = prep.contextBounds;
    dock->m_d->inpaintRt.inpaintCurrentImage = prep.contextImage;
    dock->m_d->inpaintRt.inpaintFullCanvasImage = prep.nativeContextImage.isNull() ? prep.contextImage : prep.nativeContextImage;
    dock->m_d->inpaintRt.inpaintCompositingMaskCropped = prep.compositingMaskCropped;
    dock->m_d->inpaintRt.inpaintNativeContextImage = prep.nativeContextImage.isNull() ? prep.contextImage : prep.nativeContextImage;
    dock->m_d->inpaintRt.inpaintNativeCompositingMask =
        prep.nativeCompositingMask.isNull() ? prep.compositingMaskCropped : prep.nativeCompositingMask;
    dock->m_d->inpaintRt.inpaintNativeContextSize = prep.nativeContextSize;
    dock->m_d->inpaintRt.inpaintDiffusionExtent = prep.diffusionExtent;
    dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow =
        prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
    dock->m_d->inpaintRt.inpaintServerMaskedOutput = prep.hasMask && !prep.targetBoundsRelative.isEmpty();
    dock->m_d->inpaintRt.inpaintPreprocessGrow = prep.preprocess.grow;
    dock->m_d->inpaintRt.inpaintPreprocessFeather = prep.preprocess.feather;
    dock->m_d->inpaintRt.inpaintPreprocessBlend = prep.preprocess.blend;

    const QRect rect = prep.selectionOriginalBounds.isEmpty() ? prep.maskPaddedBounds : prep.selectionOriginalBounds;
    {
        InpaintDiagSnapshot diag;
        diag.event = QStringLiteral("prepare");
        diag.pluginVersion = ComfyUIUtils::pluginVersion();
        diag.workflowKind = prepWorkflowKindName(prep.workflowKind);
        diag.archKey = ComfyResources::archToKey(prep.arch);
        diag.checkpoint = dock->checkpointForGenerate();
        diag.strength0to1 = prep.strength0to1;
        diag.useInpaintModel = prep.inpaintParams.useInpaintModel;
        diag.refineRegion = prep.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
        diag.serverPreMasked = prep.hasMask && !prep.targetBoundsRelative.isEmpty();
        diag.editMode = dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked();
        diag.effectiveMode = prep.effectiveInpaintMode;
        diag.modifierMode = prep.modifierMode;
        diag.selectionOriginal = rect;
        diag.maskPaddedBounds = prep.maskPaddedBounds;
        diag.contextBounds = prep.contextBounds;
        diag.targetBoundsRelative = prep.targetBoundsRelative;
        diag.nativeContextSize = prep.nativeContextSize;
        diag.uploadContextSize = prep.contextImage.size();
        diag.diffusionExtent = prep.diffusionExtent;
        diag.grow = prep.preprocess.grow;
        diag.feather = prep.preprocess.feather;
        diag.blend = prep.preprocess.blend;
        diag.contextPixels = describeImagePixels(prep.contextImage, QStringLiteral("prepContext"));
        diag.maskPixels = describeImagePixels(prep.compositingMaskCropped, QStringLiteral("prepMask"));
        logInpaintDiag(diag);
    }
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask rect=" << rect << "fromRegion=" << dock->m_d->inpaintRt.inpaintFromRegionLayer
                                  << "image=" << QSize(extentW, extentH)
                                  << "workflowKind=" << static_cast<int>(prep.workflowKind);
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: server URL empty";
        dock->setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: invalid URL=" << urlStr;
        dock->setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    if (dock->m_d->inpaintRt.inpaintCurrentImage.isNull() || dock->m_d->inpaintRt.inpaintCompositingMaskCropped.isNull()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: prepare returned empty context or mask";
        dock->setStatusMessage(ComfyTr::tr("Could not crop inpaint context."), true);
        return;
    }
    dock->m_d->inpaintRt.inpaintCurrentImage = dock->m_d->inpaintRt.inpaintCurrentImage.convertToFormat(QImage::Format_ARGB32);
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: contextBounds=" << dock->m_d->inpaintRt.inpaintContextBounds
                                  << "croppedCanvas=" << dock->m_d->inpaintRt.inpaintCurrentImage.size()
                                  << "nativeContext=" << dock->m_d->inpaintRt.inpaintNativeContextSize
                                  << "diffusionExtent=" << dock->m_d->inpaintRt.inpaintDiffusionExtent
                                  << "effectiveMode=" << prep.effectiveInpaintMode;
    // Upstream sends mask as Grayscale8: white = area to inpaint.
    QImage maskPng = dock->m_d->inpaintRt.inpaintCompositingMaskCropped.format() == QImage::Format_Grayscale8
                         ? dock->m_d->inpaintRt.inpaintCompositingMaskCropped
                         : dock->m_d->inpaintRt.inpaintCompositingMaskCropped.convertToFormat(QImage::Format_Grayscale8);
    {
        InpaintDiagSnapshot diag;
        diag.event = QStringLiteral("upload");
        diag.pluginVersion = ComfyUIUtils::pluginVersion();
        diag.workflowKind = prepWorkflowKindName(prep.workflowKind);
        diag.refineRegion = dock->m_d->inpaintRt.inpaintUseRefineRegionWorkflow;
        diag.nativeContextSize = dock->m_d->inpaintRt.inpaintNativeContextSize;
        diag.uploadContextSize = dock->m_d->inpaintRt.inpaintCurrentImage.size();
        diag.diffusionExtent = dock->m_d->inpaintRt.inpaintDiffusionExtent;
        diag.contextBounds = dock->m_d->inpaintRt.inpaintContextBounds;
        diag.targetBoundsRelative = prep.targetBoundsRelative;
        diag.contextPixels = describeImagePixels(dock->m_d->inpaintRt.inpaintCurrentImage, QStringLiteral("uploadContext"));
        diag.maskPixels = describeImagePixels(maskPng, QStringLiteral("uploadMask"));
        logInpaintDiag(diag);
    }
    QTemporaryFile *tmpImage = new QTemporaryFile(dock);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!dock->m_d->inpaintRt.inpaintCurrentImage.save(tmpImage->fileName())) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: failed to save temp image to" << tmpImage->fileName();
        dock->setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }
    QTemporaryFile *tmpMask = new QTemporaryFile(dock);
    tmpMask->setFileTemplate(tmpMask->fileTemplate() + ".png");
    tmpMask->open();
    tmpMask->close();
    if (!maskPng.save(tmpMask->fileName())) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: failed to save temp mask to" << tmpMask->fileName();
        dock->setStatusMessage(ComfyTr::tr("Could not save temp mask."), true);
        return;
    }
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: temp image=" << tmpImage->fileName()
                                  << "temp mask=" << tmpMask->fileName()
                                  << "imageSize=" << QFileInfo(tmpImage->fileName()).size()
                                  << "maskSize=" << QFileInfo(tmpMask->fileName()).size();
    if (dock->m_d->inpaint.btnInpaint)
        dock->m_d->inpaint.btnInpaint->setEnabled(false);
    if (dock->m_d->generate.btnGenerate)
        dock->m_d->generate.btnGenerate->setEnabled(false);
    dock->m_d->progressBar->setValue(0);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/upload/image");
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/upload/image");
    else baseUrl.setPath(path + "upload/image");
    tmpImage->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_inpaint.png\""));
    imagePart.setBodyDevice(tmpImage);
    tmpImage->setParent(multiPart);
    multiPart->append(imagePart);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    const int areaX = rect.x();
    const int areaY = rect.y();
    const int areaW = rect.width();
    const int areaH = rect.height();
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint POST image upload url=" << baseUrl.toString()
                                  << "area=" << rect;
    QNetworkReply *reply = dock->m_d->nam->post(req, multiPart);
    if (!reply) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: nam->post returned null reply!";
        dock->setStatusMessage(ComfyTr::tr("Network error: could not start upload."), true);
        dock->reEnableGenerateUi();
        return;
    }
    multiPart->setParent(reply);
    dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading image…"));
    dock->setProgressBarKind(true);  // §13.18
    QObject::connect(reply, &QNetworkReply::finished, dock, [dock, reply, tmpMask, baseUrl, extentW, extentH, areaX, areaY, areaW, areaH]() {
        reply->deleteLater();
        dock->setProgressBarKind(false);  // §13.18: image upload finished
        const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int code = codeVar.isValid() ? codeVar.toInt() : 0;
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: image upload REPLY httpStatus=" << code
                                      << "err=" << reply->error() << "errStr=" << reply->errorString();
        if (reply->error() != QNetworkReply::NoError) {
            dock->setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            dock->reEnableGenerateUi();
            dock->m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        dock->m_d->inpaintRt.inpaintUploadedImageName = obj.value("name").toString();
        dock->m_d->inpaintRt.inpaintUploadedImageSubfolder = obj.value("subfolder").toString();
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: image uploaded name=" << dock->m_d->inpaintRt.inpaintUploadedImageName
                                      << "subfolder=" << dock->m_d->inpaintRt.inpaintUploadedImageSubfolder;
        if (dock->m_d->inpaintRt.inpaintUploadedImageName.isEmpty()) {
            dock->setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            dock->reEnableGenerateUi();
            dock->m_d->progressBar->setValue(0);
            return;
        }
        QUrl uploadUrl(dock->m_d->editServerUrl->text().trimmed());
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == "/") uploadUrl.setPath("/upload/image");
        else if (!up.endsWith('/')) uploadUrl.setPath(up + "/upload/image");
        else uploadUrl.setPath(up + "upload/image");
        tmpMask->open();
        QHttpMultiPart *maskPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
            QVariant("form-data; name=\"image\"; filename=\"krita_inpaint_mask.png\""));
        part.setBodyDevice(tmpMask);
        tmpMask->setParent(maskPart);
        maskPart->append(part);
        QNetworkRequest reqMask(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqMask);
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint POST mask upload url=" << uploadUrl.toString();
        QNetworkReply *replyMask = dock->m_d->nam->post(reqMask, maskPart);
        if (!replyMask) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask nam->post returned null!";
            dock->setStatusMessage(ComfyTr::tr("Network error: could not start mask upload."), true);
            dock->reEnableGenerateUi();
            return;
        }
        maskPart->setParent(replyMask);
        dock->m_d->labelStatus->setText(ComfyTr::tr("Uploading mask…"));
        dock->setProgressBarKind(true);  // §13.18: mask upload
        QObject::connect(replyMask, &QNetworkReply::finished, dock, [dock, replyMask, extentW, extentH, areaX, areaY, areaW, areaH]() {
            replyMask->deleteLater();
            dock->setProgressBarKind(false);  // §13.18: upload finished
            const QVariant codeVar2 = replyMask->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code2 = codeVar2.isValid() ? codeVar2.toInt() : 0;
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask upload REPLY httpStatus=" << code2
                                          << "err=" << replyMask->error() << "errStr=" << replyMask->errorString();
            if (replyMask->error() != QNetworkReply::NoError) {
                dock->setStatusMessage(ComfyTr::tr("Mask upload error: %1", replyMask->errorString()), true);
                dock->reEnableGenerateUi();
                dock->m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyMask->readAll()).object();
            dock->m_d->inpaintRt.inpaintUploadedMaskName = obj.value("name").toString();
            dock->m_d->inpaintRt.inpaintUploadedMaskSubfolder = obj.value("subfolder").toString();
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask uploaded name=" << dock->m_d->inpaintRt.inpaintUploadedMaskName
                                          << "subfolder=" << dock->m_d->inpaintRt.inpaintUploadedMaskSubfolder;
            if (dock->m_d->inpaintRt.inpaintUploadedMaskName.isEmpty()) {
                dock->setStatusMessage(ComfyTr::tr("Server did not return mask name."), true);
                dock->reEnableGenerateUi();
                dock->m_d->progressBar->setValue(0);
                return;
            }
            const ComfyPrepareGenerateWorkflow::Result &prepared = dock->m_d->inpaintRt.inpaintPrepared;
            const bool useRefineRegion =
                prepared.workflowKind == ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
            QString linkedEditStyleId;
            if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
                const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    linkedEditStyleId = st->linkedEditStyle;
            }
            const ComfyUIUtils::LinkedEditStyleOverride link = ComfyUIUtils::linkedEditStyleOverride(
                dock->m_d->generate.checkEditMode && dock->m_d->generate.checkEditMode->isChecked(),
                linkedEditStyleId,
                dock->checkpointForGenerate(),
                dock->m_d->generate.spinSteps->value(),
                dock->m_d->generate.spinCfg->value(),
                prepared.strength0to1,
                dock->m_d->generate.comboSampler->currentText().trimmed(),
                dock->m_d->generateRt.ksamplerScheduler);

            const QString ckptName = link.active ? link.checkpoint : dock->checkpointForGenerate();
            const QString arch = ComfyUIUtils::classifyCheckpointArch(ckptName);
            const QString effectiveMode = prepared.effectiveInpaintMode;
            const double strength0to1 = prepared.strength0to1;
            const ComfyUIUtils::InpaintParams inpaintParams = prepared.inpaintParams;
            const QString useFillKind = prepared.fillKind;
            const QList<ComfyControlLayerEntry> jobControls =
                mergedJobControlLayers(dock->m_d->rootControlLayers, comfyActiveRegionEntries(dock->m_d.data()));
            const ComfyUIUtils::SelectionPreProcess preprocess{dock->m_d->inpaintRt.inpaintPreprocessGrow, dock->m_d->inpaintRt.inpaintPreprocessFeather,
                                                             dock->m_d->inpaintRt.inpaintPreprocessBlend};
            quint32 inpaintSeed = static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            QString posPrompt = prepared.useProcessedPositive ? prepared.effectivePositivePrompt
                                                              : ComfyUIUtils::stripPromptComments(dock->m_d->generate.editPrompt->toPlainText()).trimmed();
            if (link.active)
                posPrompt = ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, posPrompt).trimmed();
            posPrompt = ComfyUIUtils::evalWildcards(posPrompt, inpaintSeed);
            ComfyUIUtils::extractLayerPlaceholders(posPrompt);
            posPrompt = ComfyUIUtils::prependInpaintPromptInstructions(posPrompt, effectiveMode, arch);
            posPrompt = ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(posPrompt, dock->currentStyleLoras());
            QString styleArch;
            if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
                const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    styleArch = st->architecture;
            }
            ComfyWorkflowEngine::InpaintBuildParams bp;
            bp.imageName = dock->m_d->inpaintRt.inpaintUploadedImageName;
            bp.maskImageName = dock->m_d->inpaintRt.inpaintUploadedMaskName;
            bp.checkpoint = ckptName;
            bp.styleLoras = dock->currentStyleLoras();
            bp.positivePrompt = posPrompt;
            bp.negativePrompt =
                ComfyUIUtils::evalWildcards(link.active ? link.styleNegative
                                                       : ComfyUIUtils::stripPromptComments(dock->m_d->generate.editNegative->toPlainText()).trimmed(),
                                           inpaintSeed);
            bp.seed = static_cast<qint64>(inpaintSeed);
            bp.denoise = useRefineRegion ? strength0to1
                                         : (inpaintParams.useInpaintModel ? 1.0 : strength0to1);
            bp.steps = link.active ? link.steps : dock->m_d->generate.spinSteps->value();
            bp.cfg = link.active ? link.cfg : dock->m_d->generate.spinCfg->value();
            bp.sampler = link.active
                ? link.sampler
                : (dock->m_d->generate.comboSampler->currentText().trimmed().isEmpty() ? QStringLiteral("euler")
                                                                         : dock->m_d->generate.comboSampler->currentText().trimmed());
            bp.scheduler = link.active
                ? link.scheduler
                : (dock->m_d->generateRt.ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : dock->m_d->generateRt.ksamplerScheduler);
            bp.growMaskBy = preprocess.grow;
            bp.featherMaskBy = preprocess.feather;
            bp.blendMaskBy = preprocess.blend;
            bp.targetBoundsRelative = prepared.targetBoundsRelative;
            bp.nativeTargetBoundsRelative = prepared.nativeTargetBoundsRelative.isEmpty()
                                                ? prepared.targetBoundsRelative
                                                : prepared.nativeTargetBoundsRelative;
            bp.fillKind = useFillKind;
            bp.useConditionMask = inpaintParams.useConditionMask;
            bp.useInpaintModel = inpaintParams.useInpaintModel;
            bp.initialExtentWidth = qMax(64, dock->m_d->inpaintRt.inpaintDiffusionExtent.width());
            bp.initialExtentHeight = qMax(64, dock->m_d->inpaintRt.inpaintDiffusionExtent.height());
            bp.desiredExtentWidth = bp.initialExtentWidth;
            bp.desiredExtentHeight = bp.initialExtentHeight;
            bp.contextExtentWidth = qMax(64, dock->m_d->inpaintRt.inpaintNativeContextSize.width());
            bp.contextExtentHeight = qMax(64, dock->m_d->inpaintRt.inpaintNativeContextSize.height());
            bp.backgroundPrompt =
                ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(QString(), dock->currentStyleLoras());
            bp.arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
            bp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
            const ComfyUIUtils::ResolvedInpaintServerModels inpaintModels =
                ComfyUIUtils::resolveInpaintServerModels(dock->m_d->lastObjectInfoRoot,
                                                         bp.arch,
                                                         inpaintParams.useInpaintModel);
            bp.controlNetInpaintFile = inpaintModels.controlNetInpaintFile;
            bp.fooocusInpaintHead = inpaintModels.fooocusInpaintHead;
            bp.fooocusInpaintPatch = inpaintModels.fooocusInpaintPatch;
            const ComfyStyleEntry *styleEntry = nullptr;
            if (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0) {
                const QString styleId = dock->encodeStyleIdFromPresetCombo(dock->m_d->generate.comboPreset);
                styleEntry = ComfyStyleCollection::instance().findByStyleId(styleId);
            }
            QJsonObject workflow;
            if (useRefineRegion) {
                ComfyWorkflowEngine::RefineRegionParams rrp;
                rrp.refine.imageName = bp.imageName;
                rrp.maskImageName = bp.maskImageName;
                rrp.refine.checkpoint = bp.checkpoint;
                rrp.refine.positivePrompt = bp.positivePrompt;
                rrp.refine.negativePrompt = bp.negativePrompt;
                rrp.refine.promptTranslationLanguage = bp.promptTranslationLanguage;
                rrp.refine.seed = bp.seed;
                rrp.refine.steps = bp.steps;
                rrp.refine.cfg = bp.cfg;
                rrp.refine.denoise = bp.denoise;
                rrp.refine.sampler = bp.sampler;
                rrp.refine.scheduler = bp.scheduler;
                rrp.refine.arch = bp.arch;
                rrp.refine.styleLoras = bp.styleLoras;
                ComfyUIUtils::applyStrengthResolvedSamplingToRefine(
                    &rrp.refine,
                    styleEntry,
                    ComfyUIUtils::loadSettingsJson(),
                    bp.sampler,
                    bp.steps,
                    bp.cfg,
                    prepared.strength0to1);
                rrp.growMaskBy = preprocess.grow;
                rrp.featherMaskBy = preprocess.feather;
                rrp.blendMaskBy = preprocess.blend;
                rrp.targetBoundsRelative = prepared.targetBoundsRelative;
                rrp.nativeTargetBoundsRelative = bp.nativeTargetBoundsRelative;
                rrp.contextExtentWidth = bp.contextExtentWidth;
                rrp.contextExtentHeight = bp.contextExtentHeight;
                rrp.colorMatch = ComfyUIUtils::settingsColorMatchEnabled();
                rrp.useInpaintModel = inpaintParams.useInpaintModel;
                rrp.nsfwFilterSensitivity = ComfyUIUtils::settingsNsfwFilterSensitivity();
                rrp.extentWidth = qMax(64, dock->m_d->inpaintRt.inpaintDiffusionExtent.width());
                rrp.extentHeight = qMax(64, dock->m_d->inpaintRt.inpaintDiffusionExtent.height());
                rrp.controlNetInpaintFile = bp.controlNetInpaintFile;
                rrp.fooocusInpaintHead = bp.fooocusInpaintHead;
                rrp.fooocusInpaintPatch = bp.fooocusInpaintPatch;
                workflow = ComfyWorkflowEngine::buildRefineRegion(rrp);
            } else {
                workflow = ComfyWorkflowEngine::buildInpaint(bp);
            }
            qCWarning(KIS_COMFYUI_REMOTE)
                << "slotInpaint: built workflow nodeCount=" << workflow.size()
                << "kind=" << (useRefineRegion ? "refine_region" : "inpaint")
                << "ckpt=" << bp.checkpoint << "arch=" << static_cast<int>(bp.arch)
                << "denoise=" << bp.denoise << "growMaskBy=" << bp.growMaskBy << "featherMaskBy=" << bp.featherMaskBy
                << "effectiveMode=" << effectiveMode
                << "useFillKind=" << useFillKind
                << "useInpaintModel=" << inpaintParams.useInpaintModel
                << "posLen=" << bp.positivePrompt.size()
                << "negLen=" << bp.negativePrompt.size()
                << "steps=" << bp.steps << "cfg=" << bp.cfg;
            {
                QString latentPath;
                const QString graphSummary = summarizeWorkflowGraph(workflow, &latentPath);
                dock->m_d->inpaintRt.inpaintDiagLatentPath = latentPath;
                dock->m_d->inpaintRt.inpaintDiagArchKey = ComfyResources::archToKey(bp.arch);
                dock->m_d->inpaintRt.inpaintDiagDenoise = bp.denoise;
                InpaintDiagSnapshot diag;
                diag.event = QStringLiteral("build");
                diag.pluginVersion = ComfyUIUtils::pluginVersion();
                diag.workflowKind = useRefineRegion ? QStringLiteral("refine_region") : QStringLiteral("inpaint");
                diag.archKey = dock->m_d->inpaintRt.inpaintDiagArchKey;
                diag.checkpoint = ckptName;
                diag.strength0to1 = strength0to1;
                diag.denoise = bp.denoise;
                diag.useInpaintModel = inpaintParams.useInpaintModel;
                diag.refineRegion = useRefineRegion;
                diag.nativeContextSize = dock->m_d->inpaintRt.inpaintNativeContextSize;
                diag.diffusionExtent = dock->m_d->inpaintRt.inpaintDiffusionExtent;
                diag.imageUploadName = bp.imageName;
                diag.maskUploadName = bp.maskImageName;
                diag.graphSummary = graphSummary;
                diag.latentPath = latentPath;
                logInpaintDiag(diag);
                qCWarning(KIS_COMFYUI_REMOTE).nospace()
                    << "slotInpaint: workflow latentPath=" << latentPath
                    << " graph=" << graphSummary;
            }
            if (workflow.isEmpty()) {
                qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: workflow empty after build!";
                dock->setStatusMessage(ComfyTr::tr("Inpainting workflow error."), true);
                dock->reEnableGenerateUi();
                dock->m_d->progressBar->setValue(0);
                return;
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            dock->m_d->inpaintRt.inpaintPendingWorkflow = workflow;
            dock->m_d->inpaintRt.inpaintPendingArch = bp.arch;
            dock->m_d->inpaintRt.inpaintControlLayersActive = jobControls;
            // Stash params so slotInpaintPoll can build a HistoryEntry on completion.
            ComfyUIRemoteDock::Private::HistoryEntry pending;
            pending.prompt = dock->m_d->generate.editPrompt ? dock->m_d->generate.editPrompt->toPlainText() : QString();
            pending.negative = dock->m_d->generate.editNegative ? dock->m_d->generate.editNegative->toPlainText() : QString();
            pending.checkpoint = bp.checkpoint;
            pending.styleName = (dock->m_d->generate.comboPreset && dock->m_d->generate.comboPreset->currentIndex() > 0)
                ? dock->m_d->generate.comboPreset->currentText() : QString();
            pending.width = dock->m_d->inpaintRt.inpaintCurrentImage.width();
            pending.height = dock->m_d->inpaintRt.inpaintCurrentImage.height();
            pending.steps = bp.steps;
            pending.cfg = bp.cfg;
            pending.strength = dock->m_d->generate.spinStrength ? dock->m_d->generate.spinStrength->value() : 100;
            pending.samplerName = bp.sampler;
            pending.seed = bp.seed;
            pending.hasMask = true;
            pending.inpaintMode = effectiveMode;
            pending.contextBounds = dock->m_d->inpaintRt.inpaintContextBounds;
            pending.targetBounds = dock->m_d->inpaintRt.inpaintTargetBounds;
            dock->m_d->inpaintRt.inpaintPendingEntry = pending;
            qCWarning(KIS_COMFYUI_REMOTE)
                << "slotInpaint: dispatching beginUploadPipeline(dock) pendingNodeCount="
                << dock->m_d->inpaintRt.inpaintPendingWorkflow.size()
                << "controlLayers=" << dock->m_d->inpaintRt.inpaintControlLayersActive.size();
            beginUploadPipeline(dock);
        });
    });

}

} // namespace ComfyInpaintRunner
