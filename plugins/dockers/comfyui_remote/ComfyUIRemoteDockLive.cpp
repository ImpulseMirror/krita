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
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QImage>

#include <klocalizedstring.h>

void ComfyUIRemoteDock::slotLiveTick()
{
    if (!m_d->checkLiveMode->isChecked() || !m_d->viewManager || !m_d->viewManager->image()) return;
    KisImageSP image = m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        m_d->liveTimer->start(30000);
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { m_d->liveTimer->start(30000); return; }
    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull()) { m_d->liveTimer->start(30000); return; }
    QTemporaryFile *tmp = new QTemporaryFile(this);
    tmp->setFileTemplate(tmp->fileTemplate() + ".png");
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) { m_d->liveTimer->start(30000); return; }
    QUrl uploadUrl(m_d->editServerUrl->text().trimmed());
    QString up = uploadUrl.path();
    if (up.isEmpty() || up == "/") uploadUrl.setPath("/upload/image");
    else if (!up.endsWith('/')) uploadUrl.setPath(up + "/upload/image");
    else uploadUrl.setPath(up + "upload/image");
    tmp->open();
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"image\"; filename=\"krita_live.png\""));
    part.setBodyDevice(tmp);
    tmp->setParent(multiPart);
    multiPart->append(part);
    QNetworkRequest req(uploadUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->post(req, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!m_d->checkLiveMode->isChecked() || reply->error() != QNetworkReply::NoError) {
            if (m_d->checkLiveMode->isChecked()) m_d->liveTimer->start(30000);
            return;
        }
        m_d->liveUploadedImageName = QJsonDocument::fromJson(reply->readAll()).object().value("name").toString();
        if (m_d->liveUploadedImageName.isEmpty()) { m_d->liveTimer->start(30000); return; }
        QJsonParseError err;
        QJsonObject workflow = QJsonDocument::fromJson(QByteArray(img2imgWorkflowTemplate), &err).object();
        if (err.error != QJsonParseError::NoError) { m_d->liveTimer->start(30000); return; }
        QJsonObject n1 = workflow["1"].toObject();
        QJsonObject i1 = n1["inputs"].toObject();
        i1["image"] = m_d->liveUploadedImageName;
        n1["inputs"] = i1;
        workflow["1"] = n1;
        QJsonObject n3 = workflow["3"].toObject();
        QJsonObject i3 = n3["inputs"].toObject();
        i3["ckpt_name"] = m_d->comboCheckpoint->currentText().trimmed().isEmpty() ? QString("v1-5-pruned-emaonly.safetensors") : m_d->comboCheckpoint->currentText().trimmed();
        n3["inputs"] = i3;
        workflow["3"] = n3;
        QJsonObject n4 = workflow["4"].toObject();
        QJsonObject i4 = n4["inputs"].toObject();
        quint32 liveSeed = static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        QString livePos = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
        livePos = ComfyUIUtils::evalWildcards(livePos, liveSeed);
        ComfyUIUtils::extractLayerPlaceholders(livePos);  // §13.35: <layer:name> → "Picture {n}"
        livePos = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(livePos);
        i4["text"] = livePos.isEmpty() ? QString("a beautiful painting") : livePos;
        n4["inputs"] = i4;
        workflow["4"] = n4;
        QJsonObject n5 = workflow["5"].toObject();
        QJsonObject i5 = n5["inputs"].toObject();
        i5["text"] = ComfyUIUtils::evalWildcards(ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed(), liveSeed);
        n5["inputs"] = i5;
        workflow["5"] = n5;
        QJsonObject n6 = workflow["6"].toObject();
        QJsonObject i6 = n6["inputs"].toObject();
        i6["seed"] = static_cast<double>(liveSeed);
        // §13.149: LiveWorkspace strength (denoise) — use persisted live strength
        i6["denoise"] = (m_d->spinStrength ? m_d->spinStrength->value() : 75) / 100.0;
        {
            const ComfyUIUtils::ResolvedSamplerInputs si = ComfyUIUtils::resolveSamplerForLive(
                ComfyUIUtils::loadSettingsJson(),
                m_d->comboSampler ? m_d->comboSampler->currentText() : QString(),
                m_d->spinSteps ? m_d->spinSteps->value() : 20,
                m_d->spinCfg ? m_d->spinCfg->value() : 8.0);
            i6["sampler_name"] = si.sampler;
            i6["scheduler"] = si.scheduler;
            i6["steps"] = si.steps;
            i6["cfg"] = si.cfg;
        }
        n6["inputs"] = i6;
        workflow["6"] = n6;
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
        QNetworkRequest reqP(promptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqP);
        reqP.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *replyP = m_d->nam->post(reqP, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyP, &QNetworkReply::finished, this, [this, replyP, expectedPromptId]() {
            replyP->deleteLater();
            if (!m_d->checkLiveMode->isChecked() || replyP->error() != QNetworkReply::NoError) {
                if (m_d->checkLiveMode->isChecked()) m_d->liveTimer->start(30000);
                return;
            }
            QString promptId = QJsonDocument::fromJson(replyP->readAll()).object().value("prompt_id").toString();
            if (promptId.isEmpty()) { m_d->liveTimer->start(30000); return; }
            if (promptId != expectedPromptId) {
                setStatusMessage(i18n("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
                m_d->liveTimer->start(30000);
                return;
            }
            m_d->livePromptId = promptId;
            m_d->livePollCount = 0;
            startLiveSpinner();
            setLiveProgress(0);
            m_d->livePollTimer->start(1000);
        });
    });
}

void ComfyUIRemoteDock::slotLivePoll()
{
    if (m_d->livePromptId.isEmpty() || !m_d->checkLiveMode->isChecked()) return;
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) { m_d->livePromptId.clear(); stopLiveSpinner(); m_d->liveTimer->start(30000); return; }
    QUrl baseUrl(urlStr);
    QString path = baseUrl.path();
    if (path.isEmpty() || path == "/") baseUrl.setPath("/history/" + m_d->livePromptId);
    else if (!path.endsWith('/')) baseUrl.setPath(path + "/history/" + m_d->livePromptId);
    else baseUrl.setPath(path + "history/" + m_d->livePromptId);
    QNetworkRequest req(baseUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (!m_d->checkLiveMode->isChecked()) { m_d->livePromptId.clear(); stopLiveSpinner(); return; }
        if (reply->error() != QNetworkReply::NoError) { m_d->livePromptId.clear(); stopLiveSpinner(); m_d->liveTimer->start(30000); return; }
        QJsonObject hist = QJsonDocument::fromJson(reply->readAll()).object().value(m_d->livePromptId).toObject();
        QJsonObject outputs = hist.value("outputs").toObject();
        if (outputs.isEmpty()) {
            m_d->livePollCount++;
            setLiveProgress((m_d->livePollCount * 100) / Private::liveMaxPollCount);
            if (m_d->livePollCount >= Private::liveMaxPollCount) { m_d->livePromptId.clear(); stopLiveSpinner(); m_d->liveTimer->start(30000); return; }
            m_d->livePollTimer->start(1000);
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
        if (filename.isEmpty()) { m_d->livePromptId.clear(); stopLiveSpinner(); m_d->liveTimer->start(30000); return; }
        QUrl viewUrl(m_d->editServerUrl->text().trimmed());
        QString vp = viewUrl.path();
        if (!vp.endsWith('/')) vp += '/';
        viewUrl.setPath(vp + "view");
        QUrlQuery q;
        q.addQueryItem("filename", filename);
        if (!subfolder.isEmpty()) q.addQueryItem("subfolder", subfolder);
        viewUrl.setQuery(q);
        QNetworkRequest reqV(viewUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqV);
        QNetworkReply *replyV = m_d->nam->get(reqV);
        connect(replyV, &QNetworkReply::finished, this, [this, replyV]() {
            replyV->deleteLater();
            if (!m_d->checkLiveMode->isChecked()) { m_d->livePromptId.clear(); stopLiveSpinner(); return; }
            if (replyV->error() != QNetworkReply::NoError) { m_d->livePromptId.clear(); stopLiveSpinner(); m_d->liveTimer->start(30000); return; }
            QTemporaryFile tmp;
            tmp.setFileTemplate(tmp.fileTemplate() + ".png");
            if (tmp.open()) {
                tmp.write(replyV->readAll());
                tmp.close();
                {
                    const QString cachePath =
                        QDir(ComfyUIUtils::historyCacheDir()).filePath(QStringLiteral("last_live_result.png"));
                    QFile::remove(cachePath);
                    if (QFile::copy(tmp.fileName(), cachePath))
                        m_d->lastLiveResultImagePath = cachePath;
                }
                // §13.45: When Record is on, save frame to .live-frames/frame-N.webp
                if (m_d->checkLiveRecord && m_d->checkLiveRecord->isChecked() && m_d->canvas && m_d->canvas->imageView() && m_d->canvas->imageView()->document()) {
                    QString docPath = m_d->canvas->imageView()->document()->path();
                    if (!docPath.isEmpty()) {
                        QString framePath = ComfyUIUtils::liveFramePath(docPath, m_d->liveFrameIndex);
                        QDir().mkpath(QFileInfo(framePath).absolutePath());
                        QImage img;
                        if (img.load(tmp.fileName()) && img.save(framePath, "webp"))
                            m_d->liveFrameIndex++;
                    }
                }
                if (m_d->viewManager->imageManager()) {
                    QJsonObject ls = ComfyUIUtils::loadSettingsJson();
                    QString liveBeh = ls.value(QStringLiteral("apply_behavior_live")).toString();
                    if (liveBeh.isEmpty())
                        liveBeh = QStringLiteral("replace");
                    if (applyResultFileWithBehavior(tmp.fileName(), liveBeh)
                        && ls.value(QStringLiteral("new_seed_after_apply")).toBool(false) && m_d->spinSeed) {
                        m_d->spinSeed->setValue(
                            static_cast<int>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31))));
                    }
                }
            }
            m_d->livePromptId.clear();
            stopLiveSpinner();
            if (m_d->checkLiveMode->isChecked()) m_d->liveTimer->start(30000);
        });
    });
}
