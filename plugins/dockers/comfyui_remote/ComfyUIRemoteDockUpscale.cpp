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
#include <QRandomGenerator>
#include <QUuid>

#include <klocalizedstring.h>
#include <kis_image_manager.h>

void ComfyUIRemoteDock::slotUpscale()
{
    if (!m_d->viewManager || !m_d->viewManager->image()) {
        setStatusMessage(i18n("Open a document first."), true);
        return;
    }
    auto colorCheck = ComfyUIUtils::checkColorMode(m_d->viewManager->image());
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
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
    KisImageSP image = m_d->viewManager->image();
    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull()) {
        setStatusMessage(i18n("Could not export canvas."), true);
        return;
    }
    int w = canvasImg.width();
    int h = canvasImg.height();
    int w2 = qRound(w * m_d->upscaleFactor);
    int h2 = qRound(h * m_d->upscaleFactor);
    QTemporaryFile *tmpImage = new QTemporaryFile(this);
    tmpImage->setFileTemplate(tmpImage->fileTemplate() + ".png");
    tmpImage->open();
    tmpImage->close();
    if (!canvasImg.save(tmpImage->fileName())) {
        setStatusMessage(i18n("Could not save temp image."), true);
        return;
    }
    m_d->btnUpscale->setEnabled(false);
    m_d->progressBar->setValue(0);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/upload/image");
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/upload/image");
    else baseUrl.setPath(path + "upload/image");
    tmpImage->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant("form-data; name=\"image\"; filename=\"krita_upscale.png\""));
    imagePart.setBodyDevice(tmpImage);
    tmpImage->setParent(multiPart);
    multiPart->append(imagePart);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    m_d->labelStatus->setText(i18n("Uploading for upscale…"));
    setProgressBarKind(true);  // §13.18
    connect(reply, &QNetworkReply::finished, this, [this, reply, w2, h2]() {
        reply->deleteLater();
        setProgressBarKind(false);  // §13.18: upload finished
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Upload error: %1", reply->errorString()), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_d->upscaleUploadedImageName = obj.value("name").toString();
        if (m_d->upscaleUploadedImageName.isEmpty()) {
            setStatusMessage(i18n("Server did not return image name."), true);
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        const bool wantRefine = m_d->checkUpscaleRefine && m_d->checkUpscaleRefine->isChecked();
        QJsonObject workflow;
        if (wantRefine) {
            if (!m_d->comboUpscaleRefinementModel || m_d->comboUpscaleRefinementModel->currentIndex() <= 0) {
                setStatusMessage(
                    i18n("Select a refinement model (a style preset other than \"None\"), or disable \"Refine upscaled image\"."),
                    true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonParseError errRf;
            workflow = QJsonDocument::fromJson(QByteArray(upscaleRefineWorkflowTemplate), &errRf).object();
            if (errRf.error != QJsonParseError::NoError) {
                setStatusMessage(i18n("Upscale refine workflow error."), true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QString ckpt = checkpointNameForUpscaleRefinementPreset();
            if (ckpt.isEmpty())
                ckpt = m_d->comboCheckpoint ? m_d->comboCheckpoint->currentText().trimmed() : QString();
            if (ckpt.isEmpty())
                ckpt = QStringLiteral("v1-5-pruned-emaonly.safetensors");
            const int wR = qMax(64, (w2 / 8) * 8);
            const int hR = qMax(64, (h2 / 8) * 8);
            {
                QJsonObject n1 = workflow[QStringLiteral("1")].toObject();
                QJsonObject i1 = n1[QStringLiteral("inputs")].toObject();
                i1[QStringLiteral("image")] = m_d->upscaleUploadedImageName;
                n1[QStringLiteral("inputs")] = i1;
                workflow[QStringLiteral("1")] = n1;
            }
            {
                QJsonObject n2 = workflow[QStringLiteral("2")].toObject();
                QJsonObject i2 = n2[QStringLiteral("inputs")].toObject();
                i2[QStringLiteral("width")] = wR;
                i2[QStringLiteral("height")] = hR;
                const QString sm = ComfyUIUtils::normalizeDiffusionScaleMode(
                    ComfyUIUtils::loadSettingsJson().value(QStringLiteral("diffusion_scale_mode")).toString());
                i2[QStringLiteral("upscale_method")] = ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(sm);
                n2[QStringLiteral("inputs")] = i2;
                workflow[QStringLiteral("2")] = n2;
            }
            {
                QJsonObject n4 = workflow[QStringLiteral("4")].toObject();
                QJsonObject i4 = n4[QStringLiteral("inputs")].toObject();
                i4[QStringLiteral("ckpt_name")] = ckpt;
                n4[QStringLiteral("inputs")] = i4;
                workflow[QStringLiteral("4")] = n4;
            }
            int baseSteps = 20;
            double baseCfg = 8.0;
            QString sam = QStringLiteral("euler");
            QString sch = QStringLiteral("normal");
            readUpscaleRefinementSampling(&baseSteps, &baseCfg, &sam, &sch);
            const ComfyUIUtils::ResolvedSamplerInputs rsi = ComfyUIUtils::resolveSamplerForLive(
                ComfyUIUtils::loadSettingsJson(), sam, baseSteps, baseCfg);
            const int strengthPct = m_d->sliderUpscaleRefineStrength ? m_d->sliderUpscaleRefineStrength->value() : 30;
            const int guidancePct = m_d->sliderUpscaleRefineGuidance ? m_d->sliderUpscaleRefineGuidance->value() : 50;
            const double denoise = qBound(0.05, strengthPct / 100.0, 1.0);
            const double kcfg = qBound(1.0, 1.0 + (guidancePct / 100.0) * 14.0, 20.0);
            qint64 seed = m_d->checkFixedSeed && m_d->checkFixedSeed->isChecked()
                ? static_cast<qint64>(m_d->spinSeed ? m_d->spinSeed->value() : 0)
                : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
            if (!m_d->checkFixedSeed || !m_d->checkFixedSeed->isChecked()) {
                if (m_d->spinSeed)
                    m_d->spinSeed->setValue(static_cast<int>(qBound<qint64>(0, seed, 2147483647)));
            }
            const bool usePrompt = m_d->checkUpscaleUsePrompt && m_d->checkUpscaleUsePrompt->isChecked() && m_d->editPrompt;
            QString pos;
            QString neg;
            if (usePrompt) {
                pos = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
                pos = ComfyUIUtils::evalWildcards(pos, static_cast<quint32>(seed));
                ComfyUIUtils::extractLayerPlaceholders(pos);
                pos = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(pos);
                neg = ComfyUIUtils::evalWildcards(
                    ComfyUIUtils::stripPromptComments(m_d->editNegative ? m_d->editNegative->toPlainText() : QString()).trimmed(),
                    static_cast<quint32>(seed));
            }
            if (pos.isEmpty())
                pos = QStringLiteral("high quality, detailed");
            {
                QJsonObject n5 = workflow[QStringLiteral("5")].toObject();
                QJsonObject i5 = n5[QStringLiteral("inputs")].toObject();
                i5[QStringLiteral("text")] = pos;
                n5[QStringLiteral("inputs")] = i5;
                workflow[QStringLiteral("5")] = n5;
            }
            {
                QJsonObject n6 = workflow[QStringLiteral("6")].toObject();
                QJsonObject i6 = n6[QStringLiteral("inputs")].toObject();
                i6[QStringLiteral("text")] = neg;
                n6[QStringLiteral("inputs")] = i6;
                workflow[QStringLiteral("6")] = n6;
            }
            {
                QJsonObject n7 = workflow[QStringLiteral("7")].toObject();
                QJsonObject i7 = n7[QStringLiteral("inputs")].toObject();
                i7[QStringLiteral("seed")] = static_cast<double>(seed);
                i7[QStringLiteral("steps")] = rsi.steps;
                i7[QStringLiteral("cfg")] = kcfg;
                i7[QStringLiteral("denoise")] = denoise;
                i7[QStringLiteral("sampler_name")] = rsi.sampler;
                i7[QStringLiteral("scheduler")] = rsi.scheduler;
                n7[QStringLiteral("inputs")] = i7;
                workflow[QStringLiteral("7")] = n7;
            }
        } else {
            QJsonParseError errUp;
            workflow = QJsonDocument::fromJson(QByteArray(upscaleWorkflowTemplate), &errUp).object();
            if (errUp.error != QJsonParseError::NoError) {
                setStatusMessage(i18n("Upscale workflow error."), true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject n1 = workflow[QStringLiteral("1")].toObject();
            QJsonObject i1 = n1[QStringLiteral("inputs")].toObject();
            i1[QStringLiteral("image")] = m_d->upscaleUploadedImageName;
            n1[QStringLiteral("inputs")] = i1;
            workflow[QStringLiteral("1")] = n1;
            QJsonObject n2 = workflow[QStringLiteral("2")].toObject();
            QJsonObject i2 = n2[QStringLiteral("inputs")].toObject();
            i2[QStringLiteral("width")] = w2;
            i2[QStringLiteral("height")] = h2;
            {
                const QString sm = ComfyUIUtils::normalizeDiffusionScaleMode(
                    ComfyUIUtils::loadSettingsJson().value(QStringLiteral("diffusion_scale_mode")).toString());
                i2[QStringLiteral("upscale_method")] = ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(sm);
            }
            n2[QStringLiteral("inputs")] = i2;
            workflow[QStringLiteral("2")] = n2;
        }
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (wantRefine) {
            ComfyUIUtils::applyUpscaleRefineVaedecodeTiling(
                workflow,
                QStringLiteral("8"),
                m_d->tileOverlapMode,
                m_d->tileOverlap,
                ComfyUIUtils::loadSettingsJson());
        }
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
        connect(replyPrompt, &QNetworkReply::finished, this, [this, replyPrompt, expectedPromptId, wantRefine]() {
            replyPrompt->deleteLater();
            QByteArray body = replyPrompt->readAll();
            if (replyPrompt->error() != QNetworkReply::NoError) {
                QJsonObject obj = QJsonDocument::fromJson(body).object();
                if (obj.contains("error"))
                    setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
                else
                    setStatusMessage(i18n("Submit error: %1", replyPrompt->errorString()), true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QJsonObject obj = QJsonDocument::fromJson(body).object();
            if (obj.contains("error")) {
                setStatusMessage(ComfyUIUtils::formatServerErrorMessage(obj["error"].toString()), true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QString promptId = obj["prompt_id"].toString();
            if (promptId.isEmpty()) {
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            if (promptId != expectedPromptId) {
                setStatusMessage(i18n("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->upscalePromptId = promptId;
            m_d->upscalePollCount = 0;
            m_d->upscaleLastSubmitUsedRefine = wantRefine;
            m_d->labelStatus->setText(wantRefine ? i18n("Upscaling and refining…") : i18n("Upscaling…"));
            m_d->upscalePollTimer->start(1000);
        });
    });
}

void ComfyUIRemoteDock::slotUpscalePoll()
{
    if (m_d->upscalePromptId.isEmpty()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->upscalePromptId.clear();
        m_d->btnUpscale->setEnabled(true);
        m_d->progressBar->setValue(0);
        return;
    }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->upscalePromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->upscalePromptId);
    else baseUrl.setPath(path + "history/" + m_d->upscalePromptId);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("History error: %1", reply->errorString()), true);
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
            m_d->progressBar->setValue(0);
            return;
        }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->upscalePromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->upscalePollCount++;
            if (m_d->upscalePollCount >= Private::upscaleMaxPollCount) {
                setStatusMessage(i18n("Upscale timed out."), true);
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            m_d->upscalePollTimer->start(1000);
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
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
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
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (!tmp.open()) {
                m_d->upscalePromptId.clear();
                m_d->btnUpscale->setEnabled(true);
                m_d->progressBar->setValue(0);
                return;
            }
            tmp.write(replyView->readAll());
            tmp.close();
            if (m_d->viewManager->imageManager()) {
                m_d->viewManager->imageManager()->importImage(QUrl::fromLocalFile(tmp.fileName()), "KisPaintLayer");
                if (m_d->canvas) m_d->canvas->updateCanvas();
            }
            m_d->labelStatus->setText(m_d->upscaleLastSubmitUsedRefine
                ? i18n("Upscale and refine done. Result added as new layer.")
                : i18n("Upscale done. Result added as new layer."));
            m_d->progressBar->setValue(100);
            m_d->upscalePromptId.clear();
            m_d->btnUpscale->setEnabled(true);
        });
    });
}
