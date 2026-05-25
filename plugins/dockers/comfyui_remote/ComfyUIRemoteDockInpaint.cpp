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

#include <klocalizedstring.h>
#include <KisViewManager.h>
#include <kis_image_manager.h>
#include <kis_selection.h>

void ComfyUIRemoteDock::slotInpaint()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(ComfyTr::tr("Open a document first."), true);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        return;
    }
    KisSelectionSP sel = m_d->viewManager->selection();
    if (!sel || !sel->pixelSelection()) {
        setStatusMessage(ComfyTr::tr("Make a selection to inpaint."), true);
        return;
    }
    QRect rect = sel->pixelSelection()->selectedExactRect();
    if (rect.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Selection is empty. Draw a selection first."), true);
        return;
    }
    const int extentW = image->width();
    const int extentH = image->height();
    // §13.102: SelectionModifiers.square — force bounds to square for workflow
    if (ComfyUIUtils::getSelectionModifiersSquare())
        rect = ComfyUIUtils::makeRectSquare(rect, extentW, extentH);
    // §13.154: Full-document selection → run full-image generation (no mask)
    if (ComfyUIUtils::isSelectionEntireDocument(image, m_d->viewManager)) {
        slotGenerate();
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    m_d->inpaintCurrentImage = ComfyUIUtils::getCanvasAsQImage(image);
    if (m_d->inpaintCurrentImage.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not export canvas."), true);
        return;
    }
    m_d->inpaintCurrentImage = m_d->inpaintCurrentImage.convertToFormat(QImage::Format_ARGB32);
    // §13.102: SelectionModifiers.invert — invert selection before creating mask
    QImage maskImg = ComfyUIUtils::getMaskAsQImage(image, m_d->viewManager, QString("selection"), ComfyUIUtils::getSelectionModifiersInvert());
    if (maskImg.isNull()) {
        setStatusMessage(ComfyTr::tr("Could not get selection mask."), true);
        return;
    }
    // Mask for ComfyUI VAEEncodeForInpaint: white = area to inpaint
    QImage maskPng(maskImg.size(), QImage::Format_ARGB32);
    for (int y = 0; y < maskImg.height(); y++)
        for (int x = 0; x < maskImg.width(); x++) {
            int g = qGray(maskImg.pixel(x, y));
            maskPng.setPixel(x, y, qRgba(255, 255, 255, 255 - g));
        }
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!m_d->inpaintCurrentImage.save(tmpImage->fileName())) {
        setStatusMessage(ComfyTr::tr("Could not save temp image."), true);
        return;
    }
    QTemporaryFile *tmpMask = new QTemporaryFile(this);
    tmpMask->setFileTemplate(tmpMask->fileTemplate() + ".png");
    tmpMask->open();
    tmpMask->close();
    if (!maskPng.save(tmpMask->fileName())) {
        setStatusMessage(ComfyTr::tr("Could not save temp mask."), true);
        return;
    }
    m_d->btnInpaint->setEnabled(false);
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
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(ComfyTr::tr("Uploading image…"));
    setProgressBarKind(true);  // §13.18
    connect(reply, &QNetworkReply::finished, this, [this, reply, tmpMask, baseUrl, extentW, extentH, areaX, areaY, areaW, areaH]() {
        reply->deleteLater();
        setProgressBarKind(false);  // §13.18: image upload finished
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Upload error: %1", reply->errorString()), true);
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->inpaintUploadedImageName = obj.value("name").toString();
        m_d->inpaintUploadedImageSubfolder = obj.value("subfolder").toString();
        if (m_d->inpaintUploadedImageName.isEmpty()) {
            setStatusMessage(ComfyTr::tr("Server did not return image name."), true);
            m_d->btnInpaint->setEnabled(true);
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
        QNetworkReply *replyMask = m_d->nam->post(reqMask, maskPart);
        maskPart->setParent(replyMask);
        m_d->labelStatus->setText(ComfyTr::tr("Uploading mask…"));
        setProgressBarKind(true);  // §13.18: mask upload
        connect(replyMask, &QNetworkReply::finished, this, [this, replyMask, extentW, extentH, areaX, areaY, areaW, areaH]() {
            replyMask->deleteLater();
            setProgressBarKind(false);  // §13.18: upload finished
            if (replyMask->error() != QNetworkReply::NoError) {
                setStatusMessage(ComfyTr::tr("Mask upload error: %1", replyMask->errorString()), true);
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyMask->readAll()).object();
            m_d->inpaintUploadedMaskName = obj.value("name").toString();
            m_d->inpaintUploadedMaskSubfolder = obj.value("subfolder").toString();
            if (m_d->inpaintUploadedMaskName.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Server did not return mask name."), true);
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            // §13.205: Automatic inpaint mode — expand if selection touches/exceeds doc bounds, else fill.
            // §13.206: detect_inpaint() — InpaintParams from mode, arch, strength, conditioning
            const QString ckptName = m_d->comboCheckpoint->currentText().trimmed().isEmpty()
                ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
            const QString arch = ComfyUIUtils::classifyCheckpointArch(ckptName);
            // §13.206: When InpaintMode is not automatic, use explicit Fill or Expand; otherwise heuristic
            QString effectiveMode;
            if (m_d->comboInpaintMode && m_d->comboInpaintMode->currentData().toString() != QLatin1String("automatic"))
                effectiveMode = m_d->comboInpaintMode->currentData().toString();
            else
                effectiveMode = ComfyUIUtils::detectInpaintMode(extentW, extentH, areaX, areaY, areaW, areaH);
            const double strength0to1 = (m_d->spinStrength ? m_d->spinStrength->value() : 100) / 100.0;
            QString posPromptRaw = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
            const bool positiveEmpty = posPromptRaw.isEmpty();
            const QList<ComfyControlLayerEntry> jobControls =
                mergedJobControlLayers(m_d->rootControlLayers, comfyActiveRegionEntries(m_d.data()));
            const bool hasStructuralControl = ComfyControlLayer::hasStructuralControlAmong(jobControls);
            ComfyUIUtils::InpaintParams inpaintParams = ComfyUIUtils::detectInpaintParams(
                effectiveMode, arch, strength0to1, positiveEmpty, hasStructuralControl, false);
            // §13.169: CustomInpaint.use_inpaint — user can disable dedicated inpaint model path
            if (!m_d->inpaintPersistUseModel)
                inpaintParams.useInpaintModel = false;
            // §13.169: use_prompt_focus → use_condition_mask (combined with §13.206 SD1.5 add_object rule)
            inpaintParams.useConditionMask = inpaintParams.useConditionMask || m_d->inpaintPersistUsePromptFocus;
            // §13.188: Use fill combo when set, else derived fillKind (five options: none, neutral, blur, border, inpaint)
            QString useFillKind = inpaintParams.fillKind;
            if (m_d->comboFillMode && m_d->comboFillMode->currentData().isValid()
                && effectiveMode != QLatin1String("replace_background"))
                useFillKind = m_d->comboFillMode->currentData().toString();
            int selFeather = 50;
            double selMinTransition = 0.0;
            int selGrowOffset = 0;
            ComfyUIUtils::getSelectionModifierSettings(&selFeather, &selMinTransition, &selGrowOffset);
            const int baseGrow = ComfyUIUtils::calcSelectionPreProcessGrow(extentW, extentH, areaW, areaH, strength0to1, selFeather, selMinTransition, selGrowOffset);
            // §13.206 / §13.188: border → more grow, else (blur/none/neutral/inpaint) → less; edit mode uses base grow
            const int growMaskBy = inpaintParams.isEditMode ? baseGrow
                : (useFillKind == QLatin1String("border"))
                    ? qMax(12, baseGrow) : qMax(6, baseGrow);
            quint32 inpaintSeed = static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            QString posPrompt = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
            posPrompt = ComfyUIUtils::evalWildcards(posPrompt, inpaintSeed);
            ComfyUIUtils::extractLayerPlaceholders(posPrompt);  // §13.35: <layer:name> → "Picture {n}"
            posPrompt = ComfyUIUtils::prependInpaintPromptInstructions(posPrompt, effectiveMode, arch);
            posPrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(posPrompt);
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
            bp.growMaskBy = ComfyUIUtils::clampInpaintGrowFeather(growMaskBy);
            bp.arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
            bp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
            QJsonObject workflow = ComfyWorkflowEngine::buildInpaint(bp);
            if (workflow.isEmpty()) {
                setStatusMessage(ComfyTr::tr("Inpainting workflow error."), true);
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            m_d->inpaintPendingWorkflow = workflow;
            m_d->inpaintPendingArch = bp.arch;
            m_d->inpaintControlLayersActive = jobControls;
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
    QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt, expectedPromptId]() {
        replyPrompt->deleteLater();
        const QByteArray body = replyPrompt->readAll();
        if (replyPrompt->error() != QNetworkReply::NoError) {
            QString serverMsg = ComfyUIUtils::extractServerErrorFromBody(body);
            if (serverMsg.isEmpty())
                serverMsg = replyPrompt->errorString();
            setStatusMessage(ComfyTr::tr("Submit error: %1", serverMsg), true);
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (obj.contains(QStringLiteral("error"))) {
            QString serverMsg = ComfyUIUtils::extractServerErrorFromBody(body);
            if (serverMsg.isEmpty())
                serverMsg = ComfyTr::tr("(empty error body)");
            setStatusMessage(ComfyTr::tr("Submit error: %1", serverMsg), true);
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const QString promptId = obj.value(QStringLiteral("prompt_id")).toString();
        if (promptId.isEmpty()) {
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        if (promptId != expectedPromptId) {
            setStatusMessage(ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
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
            KisImageSP image = m_d->viewManager->image();
            QImage maskImg = ComfyUIUtils::getMaskAsQImage(image, m_d->viewManager, QString("selection"), ComfyUIUtils::getSelectionModifiersInvert());
            if (!result.isNull() && result.size() == m_d->inpaintCurrentImage.size() && !maskImg.isNull()) {
                ComfyUIUtils::compositeWithMask(m_d->inpaintCurrentImage, result.convertToFormat(QImage::Format_ARGB32), maskImg);
            } else if (!result.isNull()) {
                m_d->inpaintCurrentImage = result.convertToFormat(QImage::Format_ARGB32);
            }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (!tmp.open() || !m_d->inpaintCurrentImage.save(tmp.fileName())) {
                m_d->inpaintPromptId.clear();
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            tmp.close();
            if (m_d->viewManager->imageManager()) {
                m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                if (m_d->canvas) m_d->canvas->updateCanvas();
            }
            m_d->labelStatus->setText(ComfyTr::tr("Inpaint done. Result added as new layer."));
            m_d->progressBar->setValue(100);
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
        });
    });
}
