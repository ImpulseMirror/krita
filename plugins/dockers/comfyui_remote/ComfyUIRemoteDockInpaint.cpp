/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyControlLayer.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyFileLibrary.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyRegionProcess.h"

#include <QUrl>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QRandomGenerator>
#include <QFileInfo>
#include <QLoggingCategory>

#include <cmath>

#include <klocalizedstring.h>
#include <KisViewManager.h>
#include <kis_image_manager.h>
#include <kis_selection.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace {

static QImage cropContextResultToTarget(const QImage &image, const QRect &contextBounds, const QRect &targetBounds)
{
    if (image.isNull() || contextBounds.isEmpty() || targetBounds.isEmpty())
        return image;

    const QSize contextSize(contextBounds.width(), contextBounds.height());
    const QRect targetLocal = targetBounds.translated(-contextBounds.topLeft());

    // Server already returned the masked target patch (buildInpaint targetBoundsRelative path).
    if (image.size() == targetBounds.size())
        return image;

    // Full context canvas — copy the target sub-rectangle in context coordinates.
    if (image.size() == contextSize) {
        if (targetLocal.isEmpty())
            return image;
        QRect local = targetLocal & QRect(QPoint(0, 0), image.size());
        if (local.isEmpty())
            return image;
        QImage cropped = image.copy(local);
        if (cropped.size() != targetBounds.size())
            cropped = cropped.scaled(targetBounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return cropped;
    }

    // Near-target output (rounding) — scale to expected patch size.
    if (qAbs(image.width() - targetBounds.width()) <= 8 && qAbs(image.height() - targetBounds.height()) <= 8)
        return image.scaled(targetBounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    return image;
}

} // namespace

void ComfyUIRemoteDock::slotInpaint()
{
    qCWarning(KIS_COMFYUI_REMOTE)
        << "slotInpaint ENTER url=" << (m_d->editServerUrl ? m_d->editServerUrl->text() : QStringLiteral("<no-url-widget>"))
        << "hasView=" << (m_d->viewManager != nullptr)
        << "hasImage=" << (m_d->viewManager && m_d->viewManager->image())
        << "hasNam=" << (m_d->nam != nullptr)
        << "btnInpaint=" << (m_d->btnInpaint ? m_d->btnInpaint->isEnabled() : false)
        << "isConnected=" << m_d->isConnected;
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: no view/image, aborting";
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    if (!m_d->nam) {
        // FAITHFUL_PORT/BUG: previously this function happily dereferenced
        // m_d->nam without checking it. If we landed here from
        // tryStartRefineFromGenerate before isConnected ever flipped true the
        // post() call would have crashed silently on some configurations.
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: m_d->nam is null, aborting";
        setStatusMessage(ComfyTr::tr("Not connected to ComfyUI server. Open Settings and connect first."), true);
        return;
    }
    commitPromptEditorsFromUi();
    KisImageSP image = m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: colorCheck failed:" << colorCheck.second;
        setStatusMessage(colorCheck.second, true);
        return;
    }
    KisSelectionSP sel = m_d->viewManager->selection();
    QRect rect;
    QImage maskImg;
    m_d->inpaintFromRegionLayer = false;
    const int extentW = image->width();
    const int extentH = image->height();
    const bool hasPartialSelection =
        sel && sel->pixelSelection() && !sel->pixelSelection()->selectedExactRect().isEmpty()
        && !ComfyUIUtils::isSelectionEntireDocument(image, m_d->viewManager);
    if (hasPartialSelection) {
        rect = sel->pixelSelection()->selectedExactRect();
        maskImg = ComfyUIUtils::getMaskAsQImage(image, m_d->viewManager, QStringLiteral("selection"),
                                                ComfyUIUtils::getSelectionModifiersInvert());
    } else {
        const bool regionOnly = m_d->checkRegionOnly && m_d->checkRegionOnly->isChecked();
        const QList<Private::RegionEntry> regs = comfyActiveRegionEntries(m_d.data());
        const int row = comfyActiveRegionRow(m_d.data());
        if (regionOnly && row >= 0 && row < regs.size()) {
            const ComfyRegionProcess::RegionInpaintMask rim =
                ComfyRegionProcess::getRegionInpaintMask(image, m_d->viewManager, regs.at(row));
            if (rim.valid) {
                maskImg = rim.maskGray;
                rect = rim.bounds;
                m_d->inpaintFromRegionLayer = true;
            }
        }
    }
    if (rect.isEmpty() || maskImg.isNull()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: no selection or region mask";
        setStatusMessage(ComfyTr::tr("Make a selection or enable region-only mode on an active region."), true);
        return;
    }
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask rect=" << rect << "fromRegion=" << m_d->inpaintFromRegionLayer
                                  << "image=" << QSize(extentW, extentH);
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: server URL empty";
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: invalid URL=" << urlStr;
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    m_d->inpaintCurrentImage = ComfyUIUtils::getCanvasAsQImage(image);
    if (m_d->inpaintCurrentImage.isNull()) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: getCanvasAsQImage returned null";
        setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
        return;
    }
    m_d->inpaintCurrentImage = m_d->inpaintCurrentImage.convertToFormat(QImage::Format_ARGB32);
    const QRect docBounds = image->bounds();
    const double strength0to1Early = (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0;
    int selFeather = 10;
    double selMinTransition = 0.0;
    int selGrowOffset = 0;
    ComfyUIUtils::getSelectionModifierSettings(&selFeather, &selMinTransition, &selGrowOffset);
    QString effectiveModeEarly;
    if (m_d->inpaintFromRegionLayer)
        effectiveModeEarly = QStringLiteral("add_object");
    else if (m_d->comboInpaintMode && m_d->comboInpaintMode->currentData().toString() != QLatin1String("automatic"))
        effectiveModeEarly = m_d->comboInpaintMode->currentData().toString();
    else
        effectiveModeEarly =
            ComfyUIUtils::detectInpaintMode(extentW, extentH, rect.x(), rect.y(), rect.width(), rect.height());
    m_d->inpaintTargetBounds = m_d->inpaintFromRegionLayer
        ? rect
        : ComfyUIUtils::computePaddedSelectionBounds(
              rect, docBounds, strength0to1Early, selFeather, selMinTransition, selGrowOffset,
              ComfyUIUtils::getSelectionPaddingPercent(), effectiveModeEarly,
              ComfyUIUtils::getSelectionModifiersSquare());
    QString contextKey = QStringLiteral("automatic");
    if (effectiveModeEarly == QLatin1String("custom") && m_d->comboInpaintContext)
        contextKey = m_d->comboInpaintContext->currentData().toString();
    const bool fullStrengthInpaint = strength0to1Early >= 1.0 && effectiveModeEarly != QLatin1String("custom");
    if (m_d->inpaintFromRegionLayer) {
        m_d->inpaintContextBounds = rect;
    } else if (fullStrengthInpaint) {
        // Upstream compute_bounds(): 100% inpaint sends a larger square context so
        // local details (for example eye shape) inform the generated replacement.
        const QRect target = m_d->inpaintTargetBounds.isEmpty() ? rect : m_d->inpaintTargetBounds;
        const int avgSide = (target.width() + target.height()) / 2;
        const int pad = qMax(qMax(extentW, extentH) / 16, avgSide / 2);
        QRect ctx = target.adjusted(-pad, -pad, pad, pad);
        const int side = qMax(512, qMax(ctx.width(), ctx.height()));
        ctx = QRect(ctx.center().x() - side / 2, ctx.center().y() - side / 2, side, side);
        ctx = ctx.intersected(docBounds);
        m_d->inpaintContextBounds = ctx;
    } else if (effectiveModeEarly == QLatin1String("custom")) {
        // Custom inpaint context (mask_bounds / layer_bounds / entire_image) from settings.
        m_d->inpaintContextBounds =
            ComfyUIUtils::computeInpaintContextBounds(image, m_d->viewManager, m_d->inpaintTargetBounds, contextKey);
    } else {
        // Upstream compute_bounds(): strength < 100% uses padded mask bounds only (no extra context pad).
        m_d->inpaintContextBounds = m_d->inpaintTargetBounds;
    }
    m_d->inpaintFullCanvasImage = m_d->inpaintCurrentImage;
    m_d->inpaintCompositingMaskCropped =
        ComfyUIUtils::cropImageToDocumentRect(maskImg, m_d->inpaintContextBounds, docBounds);
    m_d->inpaintCurrentImage =
        ComfyUIUtils::cropImageToDocumentRect(m_d->inpaintFullCanvasImage, m_d->inpaintContextBounds, docBounds);
    if (m_d->inpaintCurrentImage.isNull() || m_d->inpaintCompositingMaskCropped.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not crop inpaint context."), true);
        return;
    }
    // §13.205–206: resolve mode/fill before upload so fill pre-process can run on the context crop
    const QString ckptNameEarly = checkpointForGenerate();
    const QString archEarly = ComfyUIUtils::classifyCheckpointArch(ckptNameEarly);
    const QString posPromptRawEarly = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
    const bool positiveEmptyEarly = posPromptRawEarly.isEmpty();
    const QList<ComfyControlLayerEntry> jobControlsEarly =
        mergedJobControlLayers(m_d->rootControlLayers, comfyActiveRegionEntries(m_d.data()));
    const bool hasStructuralControlEarly = ComfyControlLayer::hasStructuralControlAmong(jobControlsEarly);
    ComfyUIUtils::InpaintParams inpaintParamsEarly = ComfyUIUtils::detectInpaintParams(
        effectiveModeEarly, archEarly, strength0to1Early, positiveEmptyEarly, hasStructuralControlEarly, false);
    if (!m_d->inpaintPersistUseModel)
        inpaintParamsEarly.useInpaintModel = false;
    inpaintParamsEarly.useConditionMask = inpaintParamsEarly.useConditionMask || m_d->inpaintPersistUsePromptFocus;
    QString useFillKindEarly = inpaintParamsEarly.fillKind;
    if (effectiveModeEarly == QLatin1String("custom")
        && m_d->comboFillMode && m_d->comboFillMode->currentData().isValid()
        && effectiveModeEarly != QLatin1String("replace_background"))
        useFillKindEarly = m_d->comboFillMode->currentData().toString();
    const ComfyUIUtils::SelectionPreProcess preprocess = ComfyUIUtils::calcSelectionPreProcess(
        extentW, extentH, rect.width(), rect.height(), strength0to1Early, selFeather, selMinTransition, selGrowOffset,
        ComfyUIUtils::getSelectionBlendPixels(), ComfyUIUtils::getSelectionModifiersInvert());
    m_d->inpaintPreprocessGrow = preprocess.grow;
    m_d->inpaintPreprocessFeather = preprocess.feather;
    m_d->inpaintPreprocessBlend = preprocess.blend;
    if (ComfyRegionProcess::maskAverage(m_d->inpaintCompositingMaskCropped) < 0.001) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: compositing mask empty after crop";
        setStatusMessage(ComfyTr::tr("Selection mask is empty. Re-select the area and try again."), true);
        return;
    }
    QString styleArchEarly;
    if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
        const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
            styleArchEarly = st->architecture;
    }
    m_d->inpaintNativeContextImage = m_d->inpaintCurrentImage;
    m_d->inpaintNativeCompositingMask = m_d->inpaintCompositingMaskCropped;
    m_d->inpaintNativeContextSize = m_d->inpaintNativeContextImage.size();
    m_d->inpaintUseRefineRegionWorkflow = !inpaintParamsEarly.useInpaintModel;
    const ComfyResources::Arch archForScale = ComfyWorkflowEngine::resolveArch(ckptNameEarly, styleArchEarly);
    m_d->inpaintDiffusionExtent = m_d->inpaintNativeContextSize;
    if (m_d->inpaintUseRefineRegionWorkflow) {
        const ComfyUIUtils::DiffusionPreparedExtent prep =
            ComfyUIUtils::prepareDiffusionInputExtent(m_d->inpaintNativeContextSize, archForScale);
        m_d->inpaintDiffusionExtent = prep.initial;
        if (prep.initial.isValid() && prep.initial != m_d->inpaintNativeContextSize) {
            m_d->inpaintCurrentImage = m_d->inpaintCurrentImage.scaled(
                prep.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_d->inpaintCompositingMaskCropped = m_d->inpaintCompositingMaskCropped.scaled(
                prep.initial, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
    }
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: contextBounds=" << m_d->inpaintContextBounds
                                  << "croppedCanvas=" << m_d->inpaintCurrentImage.size()
                                  << "nativeContext=" << m_d->inpaintNativeContextSize
                                  << "diffusionExtent=" << m_d->inpaintDiffusionExtent
                                  << "contextKey=" << contextKey << "effectiveMode=" << effectiveModeEarly;
    // Upstream sends mask as Grayscale8: white = area to inpaint.
    QImage maskPng = m_d->inpaintCompositingMaskCropped.format() == QImage::Format_Grayscale8
                         ? m_d->inpaintCompositingMaskCropped
                         : m_d->inpaintCompositingMaskCropped.convertToFormat(QImage::Format_Grayscale8);
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!m_d->inpaintCurrentImage.save(tmpImage->fileName())) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: failed to save temp image to" << tmpImage->fileName();
        setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }
    QTemporaryFile *tmpMask = new QTemporaryFile(this);
    tmpMask->setFileTemplate(tmpMask->fileTemplate() + ".png");
    tmpMask->open();
    tmpMask->close();
    if (!maskPng.save(tmpMask->fileName())) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: failed to save temp mask to" << tmpMask->fileName();
        setStatusMessage(ComfyTr::tr("Could not save temp mask."), true);
        return;
    }
    qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: temp image=" << tmpImage->fileName()
                                  << "temp mask=" << tmpMask->fileName()
                                  << "imageSize=" << QFileInfo(tmpImage->fileName()).size()
                                  << "maskSize=" << QFileInfo(tmpMask->fileName()).size();
    if (m_d->btnInpaint)
        m_d->btnInpaint->setEnabled(false);
    if (m_d->btnGenerate)
        m_d->btnGenerate->setEnabled(false);
    m_d->progressBar->setValue(0);
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
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    if (!reply) {
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: nam->post returned null reply!";
        setStatusMessage(ComfyTr::tr("Network error: could not start upload."), true);
        if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
        return;
    }
    multiPart->setParent(reply);
    m_d->labelStatus->setText(ComfyTr::tr("Uploading image…"));
    setProgressBarKind(true);  // §13.18
    connect(reply, &QNetworkReply::finished, this, [this, reply, tmpMask, baseUrl, extentW, extentH, areaX, areaY, areaW, areaH,
                                                  effectiveModeEarly, useFillKindEarly, inpaintParamsEarly, jobControlsEarly]() {
        reply->deleteLater();
        setProgressBarKind(false);  // §13.18: image upload finished
        const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int code = codeVar.isValid() ? codeVar.toInt() : 0;
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: image upload REPLY httpStatus=" << code
                                      << "err=" << reply->error() << "errStr=" << reply->errorString();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->inpaintUploadedImageName = obj.value("name").toString();
        m_d->inpaintUploadedImageSubfolder = obj.value("subfolder").toString();
        qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: image uploaded name=" << m_d->inpaintUploadedImageName
                                      << "subfolder=" << m_d->inpaintUploadedImageSubfolder;
        if (m_d->inpaintUploadedImageName.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QUrl uploadUrl(m_d->editServerUrl->text().trimmed());
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
        QNetworkReply *replyMask = m_d->nam->post(reqMask, maskPart);
        if (!replyMask) {
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask nam->post returned null!";
            setStatusMessage(ComfyTr::tr("Network error: could not start mask upload."), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            return;
        }
        maskPart->setParent(replyMask);
        m_d->labelStatus->setText(ComfyTr::tr("Uploading mask…"));
        setProgressBarKind(true);  // §13.18: mask upload
        connect(replyMask, &QNetworkReply::finished, this, [this, replyMask, extentW, extentH, areaX, areaY, areaW, areaH,
                                                            effectiveModeEarly, useFillKindEarly, inpaintParamsEarly,
                                                            jobControlsEarly]() {
            replyMask->deleteLater();
            setProgressBarKind(false);  // §13.18: upload finished
            const QVariant codeVar2 = replyMask->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code2 = codeVar2.isValid() ? codeVar2.toInt() : 0;
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask upload REPLY httpStatus=" << code2
                                          << "err=" << replyMask->error() << "errStr=" << replyMask->errorString();
            if (replyMask->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Mask upload error: %1", replyMask->errorString()), true);
                if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyMask->readAll()).object();
            m_d->inpaintUploadedMaskName = obj.value("name").toString();
            m_d->inpaintUploadedMaskSubfolder = obj.value("subfolder").toString();
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: mask uploaded name=" << m_d->inpaintUploadedMaskName
                                          << "subfolder=" << m_d->inpaintUploadedMaskSubfolder;
            if (m_d->inpaintUploadedMaskName.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return mask name."), true);
                if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            // §13.205–206: mode/arch/strength already resolved before context crop + fill pre-process
            const QString ckptName = checkpointForGenerate();
            const QString arch = ComfyUIUtils::classifyCheckpointArch(ckptName);
            const QString effectiveMode = effectiveModeEarly;
            const double strength0to1 = (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0;
            ComfyUIUtils::InpaintParams inpaintParams = inpaintParamsEarly;
            const QString useFillKind = useFillKindEarly;
            const QList<ComfyControlLayerEntry> jobControls = jobControlsEarly;
            const ComfyUIUtils::SelectionPreProcess preprocess{m_d->inpaintPreprocessGrow, m_d->inpaintPreprocessFeather,
                                                             m_d->inpaintPreprocessBlend};
            quint32 inpaintSeed = static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            QString posPrompt = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
            posPrompt = ComfyUIUtils::evalWildcards(posPrompt, inpaintSeed);
            ComfyUIUtils::extractLayerPlaceholders(posPrompt);  // §13.35: <layer:name> → "Picture {n}"
            posPrompt = ComfyUIUtils::prependInpaintPromptInstructions(posPrompt, effectiveMode, arch);
            posPrompt = ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(posPrompt, currentStyleLoras());
            QString styleArch;
            if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
                const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
                if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                    styleArch = st->architecture;
            }
            ComfyWorkflowEngine::InpaintBuildParams bp;
            bp.imageName = m_d->inpaintUploadedImageName;
            bp.maskImageName = m_d->inpaintUploadedMaskName;
            bp.checkpoint = ckptName;
            bp.styleLoras = currentStyleLoras();
            bp.positivePrompt = posPrompt;
            bp.negativePrompt =
                ComfyUIUtils::evalWildcards(ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed(),
                                           inpaintSeed);
            bp.seed = static_cast<qint64>(inpaintSeed);
            bp.denoise = inpaintParams.useInpaintModel ? 1.0 : strength0to1;
            bp.steps = m_d->spinSteps->value();
            bp.cfg = m_d->spinCfg->value();
            bp.sampler = m_d->comboSampler->currentText().trimmed().isEmpty() ? QStringLiteral("euler")
                                                                              : m_d->comboSampler->currentText().trimmed();
            bp.scheduler = m_d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->ksamplerScheduler;
            bp.growMaskBy = preprocess.grow;
            bp.featherMaskBy = preprocess.feather;
            bp.blendMaskBy = preprocess.blend;
            bp.targetBoundsRelative = m_d->inpaintTargetBounds.translated(-m_d->inpaintContextBounds.topLeft());
            bp.fillKind = useFillKind;
            bp.useConditionMask = inpaintParams.useConditionMask;
            bp.backgroundPrompt =
                ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(QString(), currentStyleLoras());
            bp.arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
            bp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
            QJsonObject workflow;
            if (inpaintParams.useInpaintModel) {
                workflow = ComfyWorkflowEngine::buildInpaint(bp);
            } else {
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
                rrp.growMaskBy = preprocess.grow;
                rrp.featherMaskBy = preprocess.feather;
                rrp.colorMatch = ComfyUIUtils::settingsColorMatchEnabled();
                rrp.extentWidth = qMax(64, m_d->inpaintDiffusionExtent.width());
                rrp.extentHeight = qMax(64, m_d->inpaintDiffusionExtent.height());
                workflow = ComfyWorkflowEngine::buildRefineRegion(rrp);
            }
            qCWarning(KIS_COMFYUI_REMOTE)
                << "slotInpaint: built workflow nodeCount=" << workflow.size()
                << "kind=" << (inpaintParams.useInpaintModel ? "inpaint" : "refine_region")
                << "ckpt=" << bp.checkpoint << "arch=" << static_cast<int>(bp.arch)
                << "denoise=" << bp.denoise << "growMaskBy=" << bp.growMaskBy << "featherMaskBy=" << bp.featherMaskBy
                << "effectiveMode=" << effectiveMode
                << "useFillKind=" << useFillKind
                << "useInpaintModel=" << inpaintParams.useInpaintModel
                << "posLen=" << bp.positivePrompt.size()
                << "negLen=" << bp.negativePrompt.size()
                << "steps=" << bp.steps << "cfg=" << bp.cfg;
            if (workflow.isEmpty()) {
                qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaint: workflow empty after build!";
                setStatusMessage(ComfyTr::tr("Inpainting workflow error."), true);
                if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            m_d->inpaintPendingWorkflow = workflow;
            m_d->inpaintPendingArch = bp.arch;
            m_d->inpaintControlLayersActive = jobControls;
            // Stash params so slotInpaintPoll can build a HistoryEntry on completion.
            Private::HistoryEntry pending;
            pending.prompt = m_d->editPrompt ? m_d->editPrompt->toPlainText() : QString();
            pending.negative = m_d->editNegative ? m_d->editNegative->toPlainText() : QString();
            pending.checkpoint = bp.checkpoint;
            pending.styleName = (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0)
                ? m_d->comboPreset->currentText() : QString();
            pending.width = m_d->inpaintCurrentImage.width();
            pending.height = m_d->inpaintCurrentImage.height();
            pending.steps = bp.steps;
            pending.cfg = bp.cfg;
            pending.strength = m_d->spinStrength ? m_d->spinStrength->value() : 100;
            pending.samplerName = bp.sampler;
            pending.seed = bp.seed;
            pending.hasMask = true;
            pending.inpaintMode = effectiveMode;
            pending.contextBounds = m_d->inpaintTargetBounds;
            m_d->inpaintPendingEntry = pending;
            qCWarning(KIS_COMFYUI_REMOTE)
                << "slotInpaint: dispatching beginInpaintUploadPipeline() pendingNodeCount="
                << m_d->inpaintPendingWorkflow.size()
                << "controlLayers=" << m_d->inpaintControlLayersActive.size();
            beginInpaintUploadPipeline();
        });
    });
}

void ComfyUIRemoteDock::beginInpaintUploadPipeline()
{
    m_d->inpaintLoraUploadPaths.clear();
    if (m_d->isConnected && m_d->nam) {
        ComfyFileLibrary::instance().init();
        for (const ComfyFileRecord *rec :
             ComfyFileLibrary::instance().localLorasMissingOnServer(m_d->comfyServerLoraFilenames)) {
            if (rec && !rec->path.isEmpty())
                m_d->inpaintLoraUploadPaths.append(rec->path);
        }
    }
    if (!m_d->inpaintLoraUploadPaths.isEmpty()) {
        m_d->inpaintAwaitingLoraUploads = true;
        m_d->inpaintLoraUploadIndex = 0;
        uploadNextInpaintLoraFile();
        return;
    }
    if (ComfyControlLayer::anyNeedsGenerateUpload(m_d->inpaintControlLayersActive)) {
        m_d->inpaintControlUploadIndex = 0;
        m_d->inpaintControlUploadedNames.clear();
        uploadNextInpaintControlImage();
        return;
    }
    submitInpaintWorkflow(m_d->inpaintPendingWorkflow);
}

void ComfyUIRemoteDock::uploadNextInpaintLoraFile()
{
    if (!m_d->inpaintAwaitingLoraUploads || !m_d->nam) {
        m_d->btnInpaint->setEnabled(true);
        return;
    }
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        m_d->inpaintAwaitingLoraUploads = false;
        m_d->btnInpaint->setEnabled(true);
        return;
    }
    while (m_d->inpaintLoraUploadIndex < m_d->inpaintLoraUploadPaths.size()) {
        const QString path = m_d->inpaintLoraUploadPaths.at(m_d->inpaintLoraUploadIndex++);
        const QString baseName = QFileInfo(path).fileName();
        if (baseName.isEmpty() || !QFile::exists(path))
            continue;

        m_d->labelStatus->setText(ComfyTr::tr("Uploading LoRA %1…", baseName));
        setProgressBarKind(true);
        QNetworkReply *reply = ComfyUIUtils::tryUploadLoraFileViaEtnApi(m_d->nam, urlStr, path, this);
        if (!reply) {
            setProgressBarKind(false);
            setStatusMessage(ComfyTr::tr("Could not read LoRA file %1 for upload.", baseName), true);
            m_d->inpaintAwaitingLoraUploads = false;
            m_d->btnInpaint->setEnabled(true);
            return;
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, baseName]() {
            reply->deleteLater();
            setProgressBarKind(false);
            const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code = codeVar.isValid() ? codeVar.toInt() : 0;
            const bool ok =
                (reply->error() == QNetworkReply::NoError && (code == 200 || code == 201 || code == 204));
            if (!ok) {
                const QString codeStr = code > 0 ? QString::number(code) : QStringLiteral("—");
                setStatusMessage(
                    ComfyTr::tr("LoRA upload failed for %1 (HTTP %2). Install the file on the server or use Styles → Upload.",
                                baseName,
                                codeStr),
                    true);
                m_d->inpaintAwaitingLoraUploads = false;
                m_d->btnInpaint->setEnabled(true);
                return;
            }
            if (!m_d->comfyServerLoraFilenames.contains(baseName, Qt::CaseInsensitive)) {
                m_d->comfyServerLoraFilenames.append(baseName);
                m_d->comfyServerLoraFilenames.sort(Qt::CaseInsensitive);
                ComfyFileLibrary::instance().init();
                ComfyFileLibrary::instance().updateRemoteLoras(m_d->comfyServerLoraFilenames);
            }
            uploadNextInpaintLoraFile();
        });
        return;
    }
    continueInpaintAfterLoraUploads();
}

void ComfyUIRemoteDock::continueInpaintAfterLoraUploads()
{
    m_d->inpaintAwaitingLoraUploads = false;
    if (ComfyControlLayer::anyNeedsGenerateUpload(m_d->inpaintControlLayersActive)) {
        m_d->inpaintControlUploadIndex = 0;
        m_d->inpaintControlUploadedNames.clear();
        uploadNextInpaintControlImage();
        return;
    }
    submitInpaintWorkflow(m_d->inpaintPendingWorkflow);
}

void ComfyUIRemoteDock::uploadNextInpaintControlImage()
{
    if (!m_d->viewManager) {
        m_d->btnInpaint->setEnabled(true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid() || !image) {
        setStatusMessage(ComfyTr::tr("Invalid server or document."), true);
        m_d->btnInpaint->setEnabled(true);
        return;
    }

    while (m_d->inpaintControlUploadIndex < m_d->inpaintControlLayersActive.size()) {
        const ComfyControlLayerEntry ce = m_d->inpaintControlLayersActive.at(m_d->inpaintControlUploadIndex);
        m_d->inpaintControlUploadIndex++;
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;

        QImage img = ComfyUIUtils::getLayerProjectionAsQImage(image, ce.layerName);
        if (img.isNull()) {
            setStatusMessage(ComfyTr::tr("Could not export control layer \"%1\".", ce.layerName), true);
            m_d->btnInpaint->setEnabled(true);
            return;
        }
        if (ComfyUIUtils::isControlModeLines(ce.mode)) {
            img = img.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < img.height(); y++) {
                for (int x = 0; x < img.width(); x++) {
                    const QRgb px = img.pixel(x, y);
                    img.setPixel(x, y, qAlpha(px) > 0 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
                }
            }
        }

        QTemporaryFile *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!img.save(tmp->fileName())) {
            setStatusMessage(ComfyTr::tr("Could not save control layer image."), true);
            m_d->btnInpaint->setEnabled(true);
            return;
        }

        QUrl uploadUrl(urlStr);
        QString up = uploadUrl.path();
        if (up.isEmpty() || up == QLatin1Char('/'))
            uploadUrl.setPath(QStringLiteral("/upload/image"));
        else if (!up.endsWith(QLatin1Char('/')))
            uploadUrl.setPath(up + QStringLiteral("/upload/image"));
        else
            uploadUrl.setPath(up + QStringLiteral("upload/image"));

        tmp->open();
        QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"inpaint_control_%1.png\"")
                                    .arg(m_d->inpaintControlUploadedNames.size())));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);
        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = m_d->nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);
        m_d->labelStatus->setText(ComfyTr::tr("Uploading control layer %1…", ce.layerName));
        setProgressBarKind(true);
        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp]() {
            replyUp->deleteLater();
            setProgressBarKind(false);
            if (replyUp->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Control upload error: %1", replyUp->errorString()), true);
                m_d->btnInpaint->setEnabled(true);
                return;
            }
            const QString name =
                QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return control image name."), true);
                m_d->btnInpaint->setEnabled(true);
                return;
            }
            m_d->inpaintControlUploadedNames.append(name);
            uploadNextInpaintControlImage();
        });
        return;
    }

    submitInpaintWorkflow(m_d->inpaintPendingWorkflow);
}

void ComfyUIRemoteDock::submitInpaintWorkflow(QJsonObject workflow)
{
    qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow ENTER nodeCount=" << workflow.size()
                                  << "hasNam=" << (m_d->nam != nullptr);
    ComfyWorkflowEngine::applyCheckpointStyleOptions(
        &workflow, m_d->generateStyleVae, m_d->generateStyleClipSkip, m_d->generateStyleArch);

    QList<ComfyWorkflowEngine::IpAdapterLayerInput> ipInputs;
    QList<ComfyWorkflowEngine::ControlNetLayerInput> cnInputs;
    int uploadIdx = 0;
    for (const ComfyControlLayerEntry &ce : m_d->inpaintControlLayersActive) {
        if (!ComfyControlLayer::needsGenerateUpload(ce))
            continue;
        if (uploadIdx >= m_d->inpaintControlUploadedNames.size())
            break;
        const QString imageName = m_d->inpaintControlUploadedNames.at(uploadIdx++);
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
    ComfyWorkflowEngine::applyIpAdapterLayers(&workflow, ipInputs, m_d->inpaintPendingArch);
    ComfyWorkflowEngine::applyControlNetLayers(&workflow, cnInputs, m_d->inpaintPendingArch);
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
    m_d->inpaintControlLayersActive.clear();
    m_d->inpaintControlUploadedNames.clear();

    if (m_d->clientId.isEmpty())
        m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject payload;
    payload.insert(QStringLiteral("prompt"), workflow);
    payload.insert(QStringLiteral("client_id"), m_d->clientId);
    payload.insert(QStringLiteral("prompt_id"), expectedPromptId);
    QUrl promptUrl(m_d->editServerUrl->text().trimmed());
    QString p = promptUrl.path();
    if (p.isEmpty() || p == QLatin1Char('/'))
        promptUrl.setPath(QStringLiteral("/prompt"));
    else if (!p.endsWith(QLatin1Char('/')))
        promptUrl.setPath(p + QStringLiteral("/prompt"));
    else
        promptUrl.setPath(p + QStringLiteral("prompt"));
    QNetworkRequest reqPrompt(promptUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(reqPrompt);
    reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray promptBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow POST url=" << promptUrl.toString()
                                  << "bodyBytes=" << promptBody.size()
                                  << "expectedPromptId=" << expectedPromptId;
    if (!m_d->nam) {
        qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow: m_d->nam is null!";
        setStatusMessage(ComfyTr::tr("Not connected to ComfyUI server."), true);
        if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
        return;
    }
    QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, promptBody);
    if (!replyPrompt) {
        qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow: nam->post returned null!";
        setStatusMessage(ComfyTr::tr("Network error: could not submit prompt."), true);
        if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
        return;
    }
    connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt, expectedPromptId]() {
        replyPrompt->deleteLater();
        const QByteArray body = replyPrompt->readAll();
        const QVariant codeVar = replyPrompt->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int code = codeVar.isValid() ? codeVar.toInt() : 0;
        qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow REPLY httpStatus=" << code
                                      << "err=" << replyPrompt->error()
                                      << "errStr=" << replyPrompt->errorString()
                                      << "bodyBytes=" << body.size();
        if (replyPrompt->error() != QNetworkReply::NoError) {
            qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow body preview (utf8, truncated to 800B):"
                                          << QString::fromUtf8(body.left(800));
            QString serverMsg = ComfyUIUtils::extractServerErrorFromBody(body);
            if (serverMsg.isEmpty())
                serverMsg = replyPrompt->errorString();
            setStatusMessage(ComfyTr::tr("Submit error: %1", serverMsg), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.contains(QStringLiteral("error"))) {
            qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow body preview (utf8, truncated to 800B):"
                                          << QString::fromUtf8(body.left(800));
            QString serverMsg = ComfyUIUtils::extractServerErrorFromBody(body);
            if (serverMsg.isEmpty())
                serverMsg = ComfyTr::tr("(empty error body)");
            setStatusMessage(ComfyTr::tr("Submit error: %1", serverMsg), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const QString promptId = obj.value(QStringLiteral("prompt_id")).toString();
        if (promptId.isEmpty()) {
            qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow: server returned no prompt_id";
            setStatusMessage(ComfyTr::tr("Server returned no prompt_id."), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        if (promptId != expectedPromptId) {
            qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow: prompt_id mismatch expected="
                                          << expectedPromptId << "got=" << promptId;
            setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        qCWarning(KIS_COMFYUI_REMOTE) << "submitInpaintWorkflow: accepted, polling promptId=" << promptId;
        m_d->inpaintPromptId = promptId;
        m_d->inpaintPollCount = 0;
        m_d->labelStatus->setText(ComfyTr::tr("Inpainting…"));
        m_d->inpaintPollTimer->start(1000);
    });
}

void ComfyUIRemoteDock::slotInpaintPoll()
{
    if (m_d->inpaintPromptId.isEmpty()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->inpaintPromptId.clear();
        m_d->btnInpaint->setEnabled(true);
        m_d->progressBar->setValue(0);
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->inpaintPromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->inpaintPromptId);
    else baseUrl.setPath(path + "history/" + m_d->inpaintPromptId);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("History error: %1", reply->errorString()), true);
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->inpaintPromptId).toObject();
        if (const QString execErr = ComfyUIUtils::comfyHistoryExecutionError(hist); !execErr.isEmpty()) {
            setStatusMessage(execErr, true);
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->inpaintPollCount++;
            if (m_d->inpaintPollCount >= Private::inpaintMaxPollCount) {
                setStatusMessage(ComfyTr::tr("Inpaint timed out."), true);
                m_d->inpaintPromptId.clear();
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->inpaintPollTimer->start(1000);
            return;
        }
        QString filename, subfolder;
        for (const QString &nodeId : outputs.keys()) {
            QJsonArray images = outputs.value(nodeId).toObject().value("images").toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images.at(0).toObject();
                filename = img.value("filename").toString();
                subfolder = img.value("subfolder").toString();
                break;
            }
        }
        if (filename.isEmpty()) {
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        vp += "view";
        viewUrl.setPath(vp);
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqView(viewUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqView);
        QNetworkReply *replyView = m_d->nam->get(reqView);
        connect(replyView, &QNetworkReply::finished, this, [this, replyView]() {
            replyView->deleteLater();
            if (replyView->error() != QNetworkReply::NoError) {
                m_d->inpaintPromptId.clear();
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QImage result;
            result.loadFromData(replyView->readAll());
            const QSize nativeContextSize = m_d->inpaintNativeContextImage.isNull()
                                                ? m_d->inpaintCurrentImage.size()
                                                : m_d->inpaintNativeContextImage.size();
            const QImage contextImage = m_d->inpaintNativeContextImage.isNull()
                                            ? m_d->inpaintCurrentImage
                                            : m_d->inpaintNativeContextImage;
            const QImage compositingMask = m_d->inpaintNativeCompositingMask.isNull()
                                               ? m_d->inpaintCompositingMaskCropped
                                               : m_d->inpaintNativeCompositingMask;
            QImage serverResult = result;
            if (!serverResult.isNull() && nativeContextSize.isValid()
                && serverResult.size() != nativeContextSize) {
                serverResult = serverResult.scaled(nativeContextSize, Qt::IgnoreAspectRatio,
                                                   Qt::SmoothTransformation);
            }
            qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaintPoll: rawResult=" << result.size()
                                          << "scaledResult=" << serverResult.size()
                                          << "nativeContext=" << nativeContextSize
                                          << "diffusionExtent=" << m_d->inpaintDiffusionExtent
                                          << "targetBounds=" << m_d->inpaintTargetBounds;
            QImage outputImage = contextImage;
            const QSize contextSize = nativeContextSize;
            const QSize targetSize = m_d->inpaintTargetBounds.size();
            if (!serverResult.isNull() && serverResult.size() == contextSize && !compositingMask.isNull()) {
                QImage patch = contextImage;
                const QImage compositeMask = ComfyUIUtils::denoiseToCompositingMask(
                    compositingMask, m_d->inpaintPreprocessGrow, m_d->inpaintPreprocessFeather,
                    m_d->inpaintPreprocessBlend);
                ComfyUIUtils::compositeWithMask(patch, serverResult.convertToFormat(QImage::Format_RGB32),
                                                compositeMask);
                outputImage = patch;
            } else if (!serverResult.isNull() && serverResult.size() == targetSize
                       && !compositingMask.isNull()) {
                const QRect targetLocal =
                    m_d->inpaintTargetBounds.translated(-m_d->inpaintContextBounds.topLeft());
                outputImage = contextImage;
                QImage patch = outputImage.copy(targetLocal);
                const QImage compositeMask = ComfyUIUtils::denoiseToCompositingMask(
                    compositingMask, m_d->inpaintPreprocessGrow, m_d->inpaintPreprocessFeather,
                    m_d->inpaintPreprocessBlend);
                QImage maskRegion = compositeMask.copy(targetLocal);
                if (result.size() != patch.size())
                    patch = patch.scaled(result.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                if (maskRegion.size() != patch.size())
                    maskRegion = maskRegion.scaled(patch.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                ComfyUIUtils::compositeWithMask(patch, result.convertToFormat(QImage::Format_RGB32), maskRegion);
                ComfyUIUtils::blitImageInto(outputImage, patch, targetLocal.topLeft());
            } else if (!serverResult.isNull()) {
                qCWarning(KIS_COMFYUI_REMOTE) << "slotInpaintPoll: composite skipped, using scaled server result";
                outputImage = serverResult.convertToFormat(QImage::Format_ARGB32);
            }
            outputImage = cropContextResultToTarget(outputImage, m_d->inpaintContextBounds, m_d->inpaintTargetBounds);
            const QString promptId = m_d->inpaintPromptId;
            const QString cachePath = ComfyUIUtils::historyCacheDir() + QStringLiteral("/")
                + (promptId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : promptId)
                + QStringLiteral(".png");
            if (QFile::exists(cachePath)) QFile::remove(cachePath);
            if (!outputImage.save(cachePath)) {
                m_d->inpaintPromptId.clear();
                m_d->inpaintPendingEntry = Private::HistoryEntry();
                if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
                if (m_d->btnGenerate) m_d->btnGenerate->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            rememberHistoryPreviewImage(cachePath, outputImage);
            // §13.131: record completed inpaint in history; preview/apply via shared generation_finished path
            Private::HistoryEntry entry = m_d->inpaintPendingEntry;
            entry.jobId = promptId;
            entry.resultImagePath = cachePath;
            entry.resultImagePaths = QStringList() << cachePath;
            entry.width = outputImage.width();
            entry.height = outputImage.height();
            m_d->historyEntries.prepend(entry);
            while (m_d->historyEntries.size() > Private::maxHistoryEntries) {
                Private::HistoryEntry old = m_d->historyEntries.takeLast();
                evictDocumentEmbeddedSlotIfAny(old.documentSlot);
                QStringList paths = old.resultImagePaths;
                if (paths.isEmpty() && !old.resultImagePath.isEmpty()) paths << old.resultImagePath;
                for (const QString &p : paths) { if (!p.isEmpty() && QFile::exists(p)) QFile::remove(p); }
            }
            pruneHistoryToStorageLimit();
            persistTopHistoryEntryToDocument(false);
            handleGenerationFinished(cachePath, false);
            m_d->inpaintPendingEntry = Private::HistoryEntry();
            m_d->inpaintFullCanvasImage = QImage();
            m_d->inpaintCompositingMaskCropped = QImage();
            m_d->inpaintNativeContextImage = QImage();
            m_d->inpaintNativeCompositingMask = QImage();
            m_d->inpaintNativeContextSize = QSize();
            m_d->inpaintDiffusionExtent = QSize();
            m_d->inpaintUseRefineRegionWorkflow = false;
            m_d->inpaintContextBounds = QRect();
            m_d->inpaintTargetBounds = QRect();
            m_d->inpaintPreprocessGrow = 0;
            m_d->inpaintPreprocessFeather = 0;
            m_d->inpaintPreprocessBlend = 0;
            m_d->inpaintFromRegionLayer = false;
            m_d->labelStatus->setText(ComfyTr::tr("Inpaint done."));
            m_d->progressBar->setValue(100);
            m_d->inpaintPromptId.clear();
            if (m_d->btnInpaint) m_d->btnInpaint->setEnabled(true);
            if (m_d->btnGenerate) m_d->btnGenerate->setEnabled(true);
        });
    });
}
