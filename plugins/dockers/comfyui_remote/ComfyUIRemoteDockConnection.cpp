/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <klocalizedstring.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <QSignalBlocker>

namespace {
QStringList parseCheckpointNamesFromObjectInfoRoot(const QJsonObject &root)
{
    const QJsonObject nodeInfo = root.value(QStringLiteral("CheckpointLoaderSimple")).toObject();
    const QJsonObject input = nodeInfo.value(QStringLiteral("input")).toObject();
    const QJsonObject required = input.value(QStringLiteral("required")).toObject();
    const QJsonValue ckptVal = required.value(QStringLiteral("ckpt_name"));
    QStringList names;
    if (ckptVal.isArray()) {
        const QJsonArray arr = ckptVal.toArray();
        if (!arr.isEmpty() && arr.at(0).isArray()) {
            for (const QJsonValue &v : arr.at(0).toArray())
                names << v.toString();
        }
    }
    return names;
}
} // namespace

void ComfyUIRemoteDock::slotRefreshSamplers()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(i18n("Enter a server URL first."), true);
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        setStatusMessage(i18n("Invalid URL."), true);
        return;
    }
    QString path = url.path();
    // §13.123: ComfyObjectInfo — GET object_info; we parse node input.required for sampler_name / ckpt_name options
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    setStatusMessage(i18n("Loading samplers…"));
    m_d->btnRefreshSamplers->setEnabled(false);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->btnRefreshSamplers->setEnabled(true);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(i18n("Failed to load samplers: %1", reply->errorString()), true);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        syncFromObjectInfoRoot(root);
        fetchComfyModelsLorasMergeAndRefreshStylesTab();
        QJsonObject nodeInfo = root.value("KSampler").toObject();
        QJsonObject input = nodeInfo.value("input").toObject();
        QJsonObject required = input.value("required").toObject();
        QJsonValue samplerVal = required.value("sampler_name");
        QStringList names;
        if (samplerVal.isArray()) {
            QJsonArray arr = samplerVal.toArray();
            if (!arr.isEmpty() && arr.at(0).isArray()) {
                for (const QJsonValue &v : arr.at(0).toArray())
                    names << v.toString();
            }
        }
        if (!names.isEmpty()) {
            QString current = m_d->comboSampler->currentText();
            m_d->comboSampler->clear();
            m_d->comboSampler->addItems(names);
            int idx = m_d->comboSampler->findText(current);
            if (idx >= 0) m_d->comboSampler->setCurrentIndex(idx);
            else m_d->comboSampler->setCurrentIndex(0);
            setStatusMessage(i18n("Loaded %1 samplers.", names.size()));
        } else {
            setStatusMessage(i18n("No sampler list in server response."), true);
        }
    });
}

void ComfyUIRemoteDock::slotRefreshCheckpoints()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        setStatusMessage(i18n("Enter a server URL first."), true);
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        setStatusMessage(i18n("Invalid URL."), true);
        return;
    }
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    setStatusMessage(i18n("Loading checkpoints…"));
    m_d->btnRefreshCheckpoints->setEnabled(false);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    const QString baseUrlStr = urlStr;
    connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrlStr]() {
        m_d->btnRefreshCheckpoints->setEnabled(true);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            clearObjectInfoDerivedServerCaches();
            refreshStylesTabLoraWarning();
            applyStylesTabLoraListFilter();
            setStatusMessage(i18n("Failed to load checkpoints: %1", reply->errorString()), true);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        const QStringList names = parseCheckpointNamesFromObjectInfoRoot(root);
        syncFromObjectInfoRoot(root);
        fetchComfyModelsLorasMergeAndRefreshStylesTab();
        fetchFilteredCheckpointListAndApply(names, baseUrlStr, false);
    });
}

void ComfyUIRemoteDock::fetchComfyModelsLorasMergeAndRefreshStylesTab()
{
    const QString baseUrlStr = m_d->editServerUrl->text().trimmed();
    if (baseUrlStr.isEmpty() || !m_d->nam)
        return;
    QUrl lorasUrl(baseUrlStr);
    const QString lp = lorasUrl.path();
    if (lp.isEmpty() || lp == QLatin1String("/"))
        lorasUrl.setPath(QStringLiteral("/models/loras"));
    else if (!lp.endsWith(QLatin1Char('/')))
        lorasUrl.setPath(lp + QStringLiteral("/models/loras"));
    else
        lorasUrl.setPath(lp + QStringLiteral("models/loras"));
    QNetworkRequest reqL(lorasUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(reqL);
    QNetworkReply *replyL = m_d->nam->get(reqL);
    connect(replyL, &QNetworkReply::finished, this, [this, replyL]() {
        replyL->deleteLater();
        if (replyL->error() == QNetworkReply::NoError) {
            const QJsonDocument d = QJsonDocument::fromJson(replyL->readAll());
            QSet<QString> seen(m_d->comfyServerLoraFilenames.begin(), m_d->comfyServerLoraFilenames.end());
            auto addOne = [&seen](const QString &s) {
                const QString t = s.trimmed();
                if (!t.isEmpty())
                    seen.insert(t);
            };
            if (d.isArray()) {
                for (const QJsonValue &v : d.array()) {
                    if (v.isString())
                        addOne(v.toString());
                    else if (v.isObject()) {
                        const QJsonObject o = v.toObject();
                        addOne(o.value(QStringLiteral("name")).toString());
                        addOne(o.value(QStringLiteral("path")).toString());
                    }
                }
            }
            m_d->comfyServerLoraFilenames = QStringList(seen.begin(), seen.end());
            m_d->comfyServerLoraFilenames.sort(Qt::CaseInsensitive);
        }
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
    });
}

// §13.71: Missing resources display (Connection tab). (a) List format: missing custom nodes from object_info.
// (b) Dict format (missing models per arch) not yet implemented — would require model discovery per architecture.
static const QStringList &requiredObjectInfoNodes()
{
    static const QStringList list = {
        QStringLiteral("CheckpointLoaderSimple"),
        QStringLiteral("KSampler"),
        QStringLiteral("VAEDecode"),
        QStringLiteral("VAEEncodeForInpaint"),
    };
    return list;
}

// §13.71: Build list-format HTML for missing custom nodes (heading, <ul>, sentences, help link)
static QString buildMissingNodesListFormat(const QStringList &missingNodes)
{
    QString html;
    html += QLatin1String("<p><b>") + i18n("The following ComfyUI custom nodes are missing or too old") + QLatin1String("</b></p><ul>");
    for (const QString &name : missingNodes)
        html += QLatin1String("<li>") + name.toHtmlEscaped() + QLatin1String("</li>");
    html += QLatin1String("</ul><p>") + i18n("Please install or update the custom node package (e.g. ComfyUI Manager or the node's repository).") + QLatin1String("</p>");
    html += QLatin1String("<p>") + i18n("If nodes are still missing, check the ComfyUI output at startup for errors.") + QLatin1String("</p>");
    html += QLatin1String("<p>") + i18n("See <a href=\"https://docs.interstice.cloud\">Custom ComfyUI Setup</a> for required models. Check the client.log file for more details.") + QLatin1String("</p>");
    return html;
}

// §13.198: User-initiated Connect; no automatic periodic retry — reconnection is on button click
void ComfyUIRemoteDock::slotTestConnection()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(i18n("Enter a server URL."), true);
        m_d->isConnected = false;
        m_d->isConnecting = false;
        m_d->connectionErrorOccurred = false;
        m_d->connectionErrorKind.clear();  // §7.4a
        m_d->comfyDeviceSummary.clear();
        m_d->lastComfySystemStats = QJsonObject();
        if (m_d->labelPerfDevice)
            m_d->labelPerfDevice->setText(i18n("Device: (connect to server)"));
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(i18n("Disconnected"));
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        }
        if (m_d->labelDetectedModels) {
            m_d->labelDetectedModels->setText(i18n("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
            m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
            m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
        }
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        updateWelcomeVisibility();
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        setStatusMessage(i18n("Invalid URL."), true);
        m_d->isConnected = false;
        m_d->isConnecting = false;
        m_d->connectionErrorOccurred = true;
        m_d->comfyDeviceSummary.clear();
        m_d->lastComfySystemStats = QJsonObject();
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        if (m_d->labelPerfDevice)
            m_d->labelPerfDevice->setText(i18n("Device: (connect to server)"));
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(i18n("Error: Invalid URL"));
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: red;"));
        }
        if (m_d->labelDetectedModels) {
            m_d->labelDetectedModels->setText(i18n("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        }
        updateWelcomeVisibility();
        return;
    }
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/system_stats");
    else if (!path.endsWith('/')) url.setPath(path + "/system_stats");
    else url.setPath(path + "system_stats");
    setStatusMessage(i18n("Connecting…"));
    m_d->isConnecting = true;
    m_d->connectionErrorOccurred = false;
    updateWelcomeVisibility();
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(i18n("Connecting"));
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
    }
    m_d->btnTest->setEnabled(false);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->btnTest->setEnabled(true);
        m_d->isConnecting = false;
        const QByteArray responseBody = reply->readAll();
        reply->deleteLater();
        int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && httpCode == 200) {
            m_d->isConnected = true;
            m_d->connectionErrorOccurred = false;
            m_d->connectionErrorKind.clear();
            {
                const QJsonObject sysRoot = QJsonDocument::fromJson(responseBody).object();
                m_d->lastComfySystemStats = sysRoot;
                m_d->comfyDeviceSummary = ComfyUIUtils::formatComfySystemStatsDeviceLine(sysRoot);
                if (m_d->labelPerfDevice)
                    m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary);
            }
            syncPerformanceFromAutoPreset();
            setStatusMessage(i18n("Connected to ComfyUI."));
            if (m_d->labelConnectionStatus) {
                m_d->labelConnectionStatus->setText(i18n("Connected"));
                m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: green;"));
            }
            // §13.71: Fetch object_info to detect missing nodes; then update labelDetectedModels
            QUrl objUrl(m_d->editServerUrl->text().trimmed());
            QString p = objUrl.path();
            if (p.isEmpty() || p == "/") objUrl.setPath("/object_info");
            else if (!p.endsWith('/')) objUrl.setPath(p + "/object_info");
            else objUrl.setPath(p + "object_info");
            QNetworkRequest reqObj(objUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqObj);
            QNetworkReply *replyObj = m_d->nam->get(reqObj);
            connect(replyObj, &QNetworkReply::finished, this, [this, replyObj]() {
                replyObj->deleteLater();
                if (!m_d->labelDetectedModels) { updateWelcomeVisibility(); return; }
                if (replyObj->error() != QNetworkReply::NoError) {
                    clearObjectInfoDerivedServerCaches();
                    refreshStylesTabLoraWarning();
                    applyStylesTabLoraListFilter();
                    m_d->labelDetectedModels->setText(i18n("Connected. Could not load node list: %1", replyObj->errorString()));
                    m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
                    m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
                    updateWelcomeVisibility();
                    return;
                }
                QJsonObject root = QJsonDocument::fromJson(replyObj->readAll()).object();
                syncFromObjectInfoRoot(root);
                fetchComfyModelsLorasMergeAndRefreshStylesTab();
                {
                    const QStringList ckptNames = parseCheckpointNamesFromObjectInfoRoot(root);
                    const QString bu = m_d->editServerUrl->text().trimmed();
                    if (!ckptNames.isEmpty() && !bu.isEmpty())
                        fetchFilteredCheckpointListAndApply(ckptNames, bu, true);
                }
                QStringList missing;
                for (const QString &key : requiredObjectInfoNodes()) {
                    if (!root.contains(key))
                        missing << key;
                }
                if (!missing.isEmpty()) {
                    // §13.71 (a): List format — grey when connected, default when error; we show error content so use default
                    m_d->connectionErrorKind = QStringLiteral("missing_resources");
                    m_d->labelDetectedModels->setTextFormat(Qt::RichText);
                    m_d->labelDetectedModels->setText(buildMissingNodesListFormat(missing));
                    m_d->labelDetectedModels->setStyleSheet(QString());  // default color when showing missing
                    m_d->labelDetectedModels->setOpenExternalLinks(true);
                    m_d->labelDetectedModels->setTextInteractionFlags(Qt::TextBrowserInteraction);
                } else {
                    m_d->labelDetectedModels->setText(i18n("Connected. No missing nodes detected. Architecture detection (SD 1.5, SD XL, Flux, etc.) not yet implemented in this port."));
                    m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
                    m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
                }
                updateWelcomeVisibility();
            });
        } else {
            m_d->comfyDeviceSummary.clear();
            m_d->lastComfySystemStats = QJsonObject();
            clearObjectInfoDerivedServerCaches();
            refreshStylesTabLoraWarning();
            applyStylesTabLoraListFilter();
            if (m_d->labelPerfDevice)
                m_d->labelPerfDevice->setText(i18n("Device: (connect to server)"));
            m_d->isConnected = false;
            m_d->connectionErrorOccurred = true;
            if (httpCode == 401) {
                m_d->connectionErrorKind = QStringLiteral("network");
                setStatusMessage(i18n("Unauthorized (401). Check server authentication."), true);
                if (m_d->labelConnectionStatus) {
                    m_d->labelConnectionStatus->setText(i18n("Error: Unauthorized (401)"));
                    m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: red;"));
                }
            } else {
                bool networkError = (reply->error() != QNetworkReply::NoError
                    && reply->error() != QNetworkReply::ContentNotFoundError);
                m_d->connectionErrorKind = networkError ? QStringLiteral("network") : QStringLiteral("unknown");
                setStatusMessage(i18n("Connection failed: %1", reply->errorString()), true);
                if (m_d->labelConnectionStatus) {
                    m_d->labelConnectionStatus->setText(i18n("Error: %1", reply->errorString()));
                    m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: red;"));
                }
            }
            if (m_d->labelDetectedModels) {
                m_d->labelDetectedModels->setText(i18n("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
                m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
                m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
            }
        }
        updateWelcomeVisibility();
    });
}

void ComfyUIRemoteDock::clearObjectInfoDerivedServerCaches()
{
    m_d->comfyServerLoraFilenames.clear();
    m_d->objectInfoSpec58NodesPresent.clear();
    m_d->objectInfoSpec58LastLoggedSignature.clear();
}

void ComfyUIRemoteDock::syncFromObjectInfoRoot(const QJsonObject &objectInfoRoot)
{
    ComfyUIUtils::extractLoraFilenamesFromObjectInfo(objectInfoRoot, &m_d->comfyServerLoraFilenames);
    m_d->objectInfoSpec58NodesPresent = ComfyUIUtils::specSection58NodesPresentInObjectInfo(objectInfoRoot);
    const QString sig = m_d->objectInfoSpec58NodesPresent.join(QLatin1Char('\x1e'));
    if (sig != m_d->objectInfoSpec58LastLoggedSignature) {
        m_d->objectInfoSpec58LastLoggedSignature = sig;
        qWarning("ComfyUI Remote: Spec 13.58 — %d of %d documented node types present in object_info.",
                 static_cast<int>(m_d->objectInfoSpec58NodesPresent.size()),
                 static_cast<int>(ComfyUIUtils::comfyUiSpecSection58NodeClassTypes().size()));
    }
}

void ComfyUIRemoteDock::fetchFilteredCheckpointListAndApply(const QStringList &namesFromObjectInfo,
                                                            const QString &baseUrlStr,
                                                            bool noOpWhenNamesEmpty)
{
    if (namesFromObjectInfo.isEmpty()) {
        if (!noOpWhenNamesEmpty)
            applyServerCheckpointList(QStringList(), true);
        return;
    }
    const QUrl modelInfoUrl =
        ComfyUIUtils::comfyResolveApiUrl(baseUrlStr, QStringLiteral("api/etn/model_info/checkpoints?limit=2000"));
    QNetworkRequest reqMk(modelInfoUrl);
    ComfyUIUtils::setComfyUIRequestHeaders(reqMk);
    QNetworkReply *replyMk = m_d->nam->get(reqMk);
    connect(replyMk, &QNetworkReply::finished, this, [this, replyMk, namesFromObjectInfo]() {
        replyMk->deleteLater();
        QStringList finalList = namesFromObjectInfo;
        if (replyMk->error() == QNetworkReply::NoError) {
            const QJsonDocument md = QJsonDocument::fromJson(replyMk->readAll());
            finalList = ComfyUIUtils::filterCheckpointNamesWithEtnModelInfo(namesFromObjectInfo, md);
        }
        applyServerCheckpointList(finalList, false);
    });
}

void ComfyUIRemoteDock::applyServerCheckpointList(const QStringList &filteredNames, bool emptyServerList)
{
    if (!m_d->comboCheckpoint)
        return;
    const QString prevCkpt = m_d->comboCheckpoint->currentText().trimmed();
    const int prevPresetIdx = m_d->comboPreset ? m_d->comboPreset->currentIndex() : 0;
    const int firstCustom = firstCustomPresetIndex();

    m_d->comboCheckpoint->clear();
    if (filteredNames.isEmpty()) {
        m_d->comboCheckpoint->addItem(QStringLiteral("v1-5-pruned-emaonly.safetensors"));
        m_d->comboCheckpoint->setCurrentIndex(0);
        if (emptyServerList)
            setStatusMessage(i18n("No checkpoint list in server response (use custom name)."), true);
        return;
    }

    m_d->comboCheckpoint->addItems(filteredNames);
    const int ix = m_d->comboCheckpoint->findText(prevCkpt);
    if (ix >= 0) {
        m_d->comboCheckpoint->setCurrentIndex(ix);
        setStatusMessage(i18n("Loaded %1 checkpoints.", filteredNames.size()));
        return;
    }

    m_d->comboCheckpoint->setCurrentIndex(0);
    setStatusMessage(i18n("Loaded %1 checkpoints.", filteredNames.size()));

    if (prevPresetIdx >= firstCustom && m_d->comboPreset) {
        const QString presetName = m_d->comboPreset->itemText(prevPresetIdx);
        KConfigGroup presetCfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote_Preset_") + presetName);
        const QString savedCkpt = presetCfg.readEntry(QStringLiteral("Checkpoint"), QString()).trimmed();
        if (!savedCkpt.isEmpty() && m_d->comboCheckpoint->findText(savedCkpt) < 0) {
            QSignalBlocker b(m_d->comboPreset);
            m_d->comboPreset->setCurrentIndex(0);
            slotPresetChanged(0);
            setStatusMessage(i18n("The checkpoint saved with style \"%1\" is not on this server. Style reset to None.",
                                  presetName),
                             false,
                             true);
        }
    }
}
