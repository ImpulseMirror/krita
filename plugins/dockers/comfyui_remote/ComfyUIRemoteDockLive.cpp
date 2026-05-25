/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyStyleCollection.h"

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
#include <KisDocument.h>

void ComfyUIRemoteDock::slotLiveTick()
{
    if (m_d->liveAwaitingLoraUploads || !m_d->checkLiveMode->isChecked() || !m_d->viewManager
        || !m_d->viewManager->image())
        return;
    KisImageSP image = m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        setStatusMessage(colorCheck.second, true);
        m_d->liveTimer->start(30000);
        return;
    }
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->liveTimer->start(30000);
        return;
    }
    beginLiveUploadPipeline();
}

void ComfyUIRemoteDock::beginLiveUploadPipeline()
{
    m_d->liveLoraUploadPaths.clear();
    if (m_d->isConnected && m_d->nam) {
        ComfyFileLibrary::instance().init();
        for (const ComfyFileRecord *rec :
             ComfyFileLibrary::instance().localLorasMissingOnServer(m_d->comfyServerLoraFilenames)) {
            if (rec && !rec->path.isEmpty())
                m_d->liveLoraUploadPaths.append(rec->path);
        }
    }
    if (!m_d->liveLoraUploadPaths.isEmpty()) {
        m_d->liveAwaitingLoraUploads = true;
        m_d->liveLoraUploadIndex = 0;
        uploadNextLiveLoraFile();
        return;
    }
    uploadLiveCanvasAndPrompt();
}

void ComfyUIRemoteDock::uploadNextLiveLoraFile()
{
    if (!m_d->liveAwaitingLoraUploads || !m_d->nam || !m_d->checkLiveMode->isChecked()) {
        m_d->liveAwaitingLoraUploads = false;
        m_d->liveTimer->start(30000);
        return;
    }
    const QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->liveAwaitingLoraUploads = false;
        m_d->liveTimer->start(30000);
        return;
    }
    while (m_d->liveLoraUploadIndex < m_d->liveLoraUploadPaths.size()) {
        const QString path = m_d->liveLoraUploadPaths.at(m_d->liveLoraUploadIndex++);
        const QString baseName = QFileInfo(path).fileName();
        if (baseName.isEmpty() || !QFile::exists(path))
            continue;

        m_d->labelStatus->setText(ComfyTr::tr("Live: uploading LoRA %1…", baseName));
        setProgressBarKind(true);
        QNetworkReply *reply = ComfyUIUtils::tryUploadLoraFileViaEtnApi(m_d->nam, urlStr, path, this);
        if (!reply) {
            setProgressBarKind(false);
            setStatusMessage(ComfyTr::tr("Could not read LoRA file %1 for upload.", baseName), true);
            m_d->liveAwaitingLoraUploads = false;
            m_d->liveTimer->start(30000);
            return;
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, baseName]() {
            reply->deleteLater();
            setProgressBarKind(false);
            if (!m_d->checkLiveMode->isChecked()) {
                m_d->liveAwaitingLoraUploads = false;
                return;
            }
            const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code = codeVar.isValid() ? codeVar.toInt() : 0;
            const bool ok =
                (reply->error() == QNetworkReply::NoError && (code == 200 || code == 201 || code == 204));
            if (!ok) {
                const QString codeStr = code > 0 ? QString::number(code) : QStringLiteral("—");
                setStatusMessage(
                    ComfyTr::tr("Live: LoRA upload failed for %1 (HTTP %2).", baseName, codeStr),
                    true);
                m_d->liveAwaitingLoraUploads = false;
                m_d->liveTimer->start(30000);
                return;
            }
            if (!m_d->comfyServerLoraFilenames.contains(baseName, Qt::CaseInsensitive)) {
                m_d->comfyServerLoraFilenames.append(baseName);
                m_d->comfyServerLoraFilenames.sort(Qt::CaseInsensitive);
                ComfyFileLibrary::instance().init();
                ComfyFileLibrary::instance().updateRemoteLoras(m_d->comfyServerLoraFilenames);
            }
            uploadNextLiveLoraFile();
        });
        return;
    }
    continueLiveAfterLoraUploads();
}

void ComfyUIRemoteDock::continueLiveAfterLoraUploads()
{
    m_d->liveAwaitingLoraUploads = false;
    if (!m_d->checkLiveMode->isChecked()) {
        m_d->liveTimer->start(30000);
        return;
    }
    uploadLiveCanvasAndPrompt();
}

void ComfyUIRemoteDock::uploadLiveCanvasAndPrompt()
{
    if (!m_d->checkLiveMode->isChecked() || !m_d->viewManager || !m_d->viewManager->image()) {
        m_d->liveTimer->start(30000);
        return;
    }
    KisImageSP image = m_d->viewManager->image();
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        m_d->liveTimer->start(30000);
        return;
    }
    QImage canvasImg = ComfyUIUtils::getCanvasAsQImage(image);
    if (canvasImg.isNull()) {
        m_d->liveTimer->start(30000);
        return;
    }
    QTemporaryFile *tmp = new QTemporaryFile(this);
    tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
    tmp->open();
    tmp->close();
    if (!canvasImg.save(tmp->fileName())) {
        m_d->liveTimer->start(30000);
        return;
    }
    QUrl uploadUrl(m_d->editServerUrl->text().trimmed());
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
                   QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"krita_live.png\"")));
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
            if (m_d->checkLiveMode->isChecked())
                m_d->liveTimer->start(30000);
            return;
        }
        m_d->liveUploadedImageName =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("name")).toString();
        if (m_d->liveUploadedImageName.isEmpty()) {
            m_d->liveTimer->start(30000);
            return;
        }
        const QString ckptName = m_d->comboCheckpoint->currentText().trimmed().isEmpty()
            ? QStringLiteral("v1-5-pruned-emaonly.safetensors")
            : m_d->comboCheckpoint->currentText().trimmed();
        QString styleArch;
        if (m_d->comboPreset && m_d->comboPreset->currentIndex() > 0) {
            const QString styleId = encodeStyleIdFromPresetCombo(m_d->comboPreset);
            if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId))
                styleArch = st->architecture;
        }
        const quint32 liveSeed =
            static_cast<quint32>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        QString livePos = ComfyUIUtils::stripPromptComments(m_d->editPrompt->toPlainText()).trimmed();
        livePos = ComfyUIUtils::evalWildcards(livePos, liveSeed);
        ComfyUIUtils::extractLayerPlaceholders(livePos);
        livePos = ComfyUIUtils::mergeLibraryLoraTagsIntoPositivePrompt(livePos);
        const ComfyUIUtils::ResolvedSamplerInputs si = ComfyUIUtils::resolveSamplerForLive(
            ComfyUIUtils::loadSettingsJson(),
            m_d->comboSampler ? m_d->comboSampler->currentText() : QString(),
            m_d->spinSteps ? m_d->spinSteps->value() : 20,
            m_d->spinCfg ? m_d->spinCfg->value() : 8.0);
        ComfyWorkflowEngine::LiveParams lp;
        lp.checkpoint = ckptName;
        lp.imageName = m_d->liveUploadedImageName;
        lp.arch = ComfyWorkflowEngine::resolveArch(ckptName, styleArch);
        lp.seed = static_cast<qint64>(liveSeed);
        lp.positivePrompt = livePos;
        lp.negativePrompt =
            ComfyUIUtils::evalWildcards(ComfyUIUtils::stripPromptComments(m_d->editNegative->toPlainText()).trimmed(),
                                       liveSeed);
        lp.denoise = (m_d->spinStrength ? m_d->spinStrength->value() : 75) / 100.0;
        lp.sampler = si.sampler;
        lp.scheduler = si.scheduler;
        lp.steps = si.steps;
        lp.cfg = si.cfg;
        lp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
        QJsonObject workflow = ComfyWorkflowEngine::buildLive(lp);
        if (workflow.isEmpty()) {
            m_d->liveTimer->start(30000);
            return;
        }
        ComfyWorkflowEngine::applyCheckpointStyleOptions(
            &workflow, m_d->generateStyleVae, m_d->generateStyleClipSkip, m_d->generateStyleArch);
        ComfyUIUtils::applyPerformancePreferencesToWorkflow(workflow);
        if (m_d->clientId.isEmpty())
            m_d->clientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString expectedPromptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject payload;
        payload[QStringLiteral("prompt")] = workflow;
        payload[QStringLiteral("client_id")] = m_d->clientId;
        payload[QStringLiteral("prompt_id")] = expectedPromptId;
        QUrl promptUrl(m_d->editServerUrl->text().trimmed());
        QString p = promptUrl.path();
        if (p.isEmpty() || p == QLatin1Char('/'))
            promptUrl.setPath(QStringLiteral("/prompt"));
        else if (!p.endsWith(QLatin1Char('/')))
            promptUrl.setPath(p + QStringLiteral("/prompt"));
        else
            promptUrl.setPath(p + QStringLiteral("prompt"));
        QNetworkRequest reqP(promptUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqP);
        reqP.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        QNetworkReply *replyP = m_d->nam->post(reqP, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(replyP, &QNetworkReply::finished, this, [this, replyP, expectedPromptId]() {
            replyP->deleteLater();
            if (!m_d->checkLiveMode->isChecked() || replyP->error() != QNetworkReply::NoError) {
                if (m_d->checkLiveMode->isChecked())
                    m_d->liveTimer->start(30000);
                return;
            }
            QString promptId =
                QJsonDocument::fromJson(replyP->readAll()).object().value(QStringLiteral("prompt_id")).toString();
            if (promptId.isEmpty()) {
                m_d->liveTimer->start(30000);
                return;
            }
            if (promptId != expectedPromptId) {
                setStatusMessage(
                    ComfyTr::tr("Prompt ID mismatch - Please update ComfyUI to 0.3.45 or later!"), true);
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
