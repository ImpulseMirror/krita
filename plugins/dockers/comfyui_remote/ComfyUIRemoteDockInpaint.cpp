/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyUIWorkflows.h"

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

#include <klocalizedstring.h>
#include <KisViewManager.h>
#include <kis_selection.h>

void ComfyUIRemoteDock::slotInpaint()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(i18n("Open a document first."), true);
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
        setStatusMessage(i18n("Make a selection to inpaint."), true);
        return;
    }
    QRect rect = sel->pixelSelection()->selectedExactRect();
    if (rect.isEmpty()) {
        setStatusMessage(i18n("Selection is empty. Draw a selection first."), true);
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
        setStatusMessage(i18n("Enter a server URL first."), true);
        return;
    }
    QUrl baseUrl(urlStr);
    if (!baseUrl.isValid()) {
        setStatusMessage(i18n("Invalid URL."), true);
        return;
    }
    m_d->inpaintCurrentImage = ComfyUIUtils::getCanvasAsQImage(image);
    if (m_d->inpaintCurrentImage.isNull()) {
        setStatusMessage(i18n("Could not export canvas."), true);
        return;
    }
    m_d->inpaintCurrentImage = m_d->inpaintCurrentImage.convertToFormat(QImage::Format_ARGB32);
    // §13.102: SelectionModifiers.invert — invert selection before creating mask
    QImage maskImg = ComfyUIUtils::getMaskAsQImage(image, m_d->viewManager, QString("selection"), ComfyUIUtils::getSelectionModifiersInvert());
    if (maskImg.isNull()) {
        setStatusMessage(i18n("Could not get selection mask."), true);
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
        setStatusMessage(i18n("Could not save temp image."), true);
        return;
    }
    QTemporaryFile *tmpMask = new QTemporaryFile(this);
    tmpMask->setFileTemplate(tmpMask->fileTemplate() + ".png");
    tmpMask->open();
    tmpMask->close();
    if (!maskPng.save(tmpMask->fileName())) {
        setStatusMessage(i18n("Could not save temp mask."), true);
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
    m_d->labelStatus->setText(i18n("Uploading image…"));
    setProgressBarKind(true);  // §13.18
    connect(reply, &QNetworkReply::finished, this, [this, reply, tmpMask, baseUrl, extentW, extentH, areaX, areaY, areaW, areaH]() {
        reply->deleteLater();
        setProgressBarKind(false);  // §13.18: image upload finished
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Upload error: %1", reply->errorString()), true);
            m_d->btnInpaint->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->inpaintUploadedImageName = obj.value("name").toString();
        m_d->inpaintUploadedImageSubfolder = obj.value("subfolder").toString();
        if (m_d->inpaintUploadedImageName.isEmpty()) {
            setStatusMessage(i18n("Server did not return image name."), true);
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
        m_d->labelStatus->setText(i18n("Uploading mask…"));
        setProgressBarKind(true);  // §13.18: mask upload
        connect(replyMask, &QNetworkReply::finished, this, [this, replyMask, extentW, extentH, areaX, areaY, areaW, areaH]() {
            replyMask->deleteLater();
            setProgressBarKind(false);  // §13.18: upload finished
            if (replyMask->error() != QNetworkReply::NoError) {
                setStatusMessage(i18n("Mask upload error: %1", replyMask->errorString()), true);
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(replyMask->readAll()).object();
            m_d->inpaintUploadedMaskName = obj.value("name").toString();
            m_d->inpaintUploadedMaskSubfolder = obj.value("subfolder").toString();
            if (m_d->inpaintUploadedMaskName.isEmpty()) {
                setStatusMessage(i18n("Server did not return mask name."), true);
                m_d->btnInpaint->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonParseError err;
            QJsonObject workflow = QJsonDocument::fromJson(QByteArray(inpaintingWorkflowTemplate), &err).object();
            if (err.error != QJsonParseError::NoError) {
                setStatusMessage(i18n("Inpainting workflow error."), true);
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
            const bool hasStructuralControl = false;  // no control layers in this flow
            const ComfyUIUtils::InpaintParams inpaintParams = ComfyUIUtils::detectInpaintParams(
                effectiveMode, arch, strength0to1, positiveEmpty, hasStructuralControl, false);
            // §13.188: Use fill combo when set, else derived fillKind (five options: none, neutral, blur, border, inpaint)
            QString useFillKind = inpaintParams.fillKind;
            if (m_d->comboFillMode && m_d->comboFillMode->currentData().isValid())
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
            QJsonObject n1 = workflow["1"].toObject();
            QJsonObject i1 = n1["inputs"].toObject();
            i1["image"] = m_d->inpaintUploadedImageName;
            n1["inputs"] = i1;
            workflow["1"] = n1;
            QJsonObject n2 = workflow["2"].toObject();
            QJsonObject i2 = n2["inputs"].toObject();
            i2["image"] = m_d->inpaintUploadedMaskName;
            n2["inputs"] = i2;
            workflow["2"] = n2;
            // §13.127: grow_mask_by (and feather) must be in 0–499 when passing inpaint params to workflow
            QJsonObject n7 = workflow["7"].toObject();
            QJsonObject i7 = n7["inputs"].toObject();
            i7["grow_mask_by"] = ComfyUIUtils::clampInpaintGrowFeather(growMaskBy);
            n7["inputs"] = i7;
            workflow["7"] = n7;
            QJsonObject n4 = workflow["4"].toObject();
            QJsonObject i4 = n4["inputs"].toObject();
            i4["ckpt_name"] = ckptName;
            n4["inputs"] = i4;
            workflow["4"] = n4;
            QJsonObject n5 = workflow["5"].toObject();
            QJsonObject i5 = n5["inputs"].toObject();
            quint32 inpaintSeed = static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            QString posPrompt = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
            posPrompt = ComfyUIUtils::evalWildcards(posPrompt, inpaintSeed);
            ComfyUIUtils::extractLayerPlaceholders(posPrompt);  // §13.35: <layer:name> → "Picture {n}"
            posPrompt = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(posPrompt);
            i5["text"] = posPrompt.isEmpty() ? QString("a beautiful painting") : posPrompt;
            n5["inputs"] = i5;
            workflow["5"] = n5;
            QJsonObject n6 = workflow["6"].toObject();
            QJsonObject i6 = n6["inputs"].toObject();
            i6["text"] = ComfyUIUtils::evalWildcards(ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed(), inpaintSeed);
            n6["inputs"] = i6;
            workflow["6"] = n6;
            QJsonObject n8 = workflow["8"].toObject();
            QJsonObject i8 = n8["inputs"].toObject();
            i8["seed"] = static_cast<double>(inpaintSeed);
            // §13.206: use_inpaint_model by arch — when true use full denoise 1.0, else strength
            i8["denoise"] = inpaintParams.useInpaintModel ? 1.0 : strength0to1;
            i8["steps"] = m_d->spinSteps->value();
            i8["cfg"] = m_d->spinCfg->value();
            i8["sampler_name"] = m_d->comboSampler->currentText().trimmed().isEmpty() ? QString("euler") : m_d->comboSampler->currentText().trimmed();
            i8["scheduler"] = m_d->ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : m_d->ksamplerScheduler;
            n8["inputs"] = i8;
            workflow["8"] = n8;
            ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
            if (m_d->clientId.isEmpty())
                m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QJsonObject payload;
            payload["prompt"] = workflow;
            payload["client_id"] = m_d->clientId;
            payload["prompt_id"] = expectedPromptId;
            QUrl promptUrl(m_d->editServerUrl->text().trimmed());
            QString p = promptUrl.path();
            if (p.isEmpty() || p == "/") promptUrl.setPath("/prompt");
            else if (!p.endsWith('/')) promptUrl.setPath(p + "/prompt");
            else promptUrl.setPath(p + "prompt");
            QNetworkRequest reqPrompt(promptUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqPrompt);
            reqPrompt.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            QNetworkReply *replyPrompt = m_d->nam->post(reqPrompt, QJsonDocument(payload).toJson(QJsonDocument::Compact));
            connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt, expectedPromptId]() {
                replyPrompt->deleteLater();
                QByteArray body = replyPrompt->readAll();
                if (replyPrompt->error() != QNetworkReply::NoError) {
                    QJsonObject obj = QJsonDocument::fromJson(body).object();
                    if (obj.contains("error"))
                        setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
                    else
                        setStatusMessage(i18n("Submit error: %1", replyPrompt->errorString()), true);
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                QJsonObject obj = QJsonDocument::fromJson(body).object();
                if (obj.contains("error")) {
                    setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                QString promptId = obj["prompt_id"].toString();
                if (promptId.isEmpty()) {
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                if (promptId != expectedPromptId) {
                    setStatusMessage(i18n("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                    m_d->btnInpaint->setEnabled(true);
                    m_d->progressBar->setValue(0);
                    return;
                }
                m_d->inpaintPromptId = promptId;
                m_d->inpaintPollCount = 0;
                m_d->labelStatus->setText(i18n("Inpainting…"));
                m_d->inpaintPollTimer->start(1000);
            });
        });
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
            setStatusMessage(i18n("History error: %1", reply->errorString()), true);
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
                setStatusMessage(i18n("Inpaint timed out."), true);
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
            m_d->labelStatus->setText(i18n("Inpaint done. Result added as new layer."));
            m_d->progressBar->setValue(100);
            m_d->inpaintPromptId.clear();
            m_d->btnInpaint->setEnabled(true);
        });
    });
}
