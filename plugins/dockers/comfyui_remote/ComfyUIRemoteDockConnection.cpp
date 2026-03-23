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

#include <kis_icon_utils.h>

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

// §13.71: Missing resources display (Connection tab). (a) List format: missing nodes from object_info + link per row.
// (b) Dict format: optional "Missing common models" when ckpt list empty; then per-arch from classifyCheckpointArch (skip Arch.all/illu_v in table).
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

// §13.71 (a): Each list entry shows node name + link (parity with CustomNode name/URL when no per-node URL is known).
static QString missingCustomNodeListItemHtml(const QString &classType)
{
    const QString href = QStringLiteral("https://github.com/comfyanonymous/ComfyUI");
    return QLatin1String("<li>") + classType.toHtmlEscaped() + QLatin1String(" — <a href=\"") + href + QLatin1String("\">")
        + i18n("ComfyUI core / extensions").toHtmlEscaped() + QLatin1String("</a></li>");
}

// §13.71: Build list-format HTML for missing custom nodes (heading, <ul>, sentences)
static QString buildMissingNodesListFormat(const QStringList &missingNodes)
{
    QString html;
    html += QLatin1String("<p><b>") + i18n("The following ComfyUI custom nodes are missing or too old") + QLatin1String("</b></p><ul>");
    for (const QString &name : missingNodes)
        html += missingCustomNodeListItemHtml(name);
    html += QLatin1String("</ul><p>") + i18n("Please install or update the custom node package (e.g. ComfyUI Manager or the node's repository).") + QLatin1String("</p>");
    html += QLatin1String("<p>") + i18n("If nodes are still missing, check the ComfyUI output at startup for errors.") + QLatin1String("</p>");
    return html;
}

// §13.71 (b): Dict-style HTML — "Detected base models:" per arch (skip Arch.all, Arch.illu_v); footer link only on this format.
static QString buildMissingResourcesDictFormatHtml(const QStringList &checkpointNames)
{
    QSet<QString> present;
    for (const QString &n : checkpointNames) {
        const QString a = ComfyUIUtils::classifyCheckpointArch(n);
        if (a != QLatin1String("unknown"))
            present.insert(a);
    }
    static const struct {
        const char *classifierKey;
        const char *displayArch;
    } rows[] = {
        {"sd15", "sd15"},
        {"sdxl", "sdxl"},
        {"flux", "flux"},
        {"flux_k", "flux_k"},
        {"flux2_4b", "flux2_4b"},
        {"qwen_e", "qwen"},
    };
    QString html;
    // §13.71 (b): When the server exposes no checkpoint filenames, treat as missing common (Arch.all) model inventory.
    if (checkpointNames.isEmpty()) {
        html += QLatin1String("<p><b>") + i18n("Missing common models (required):") + QLatin1String("</b></p><ul>");
        html += QLatin1String("<li>") + i18n("Checkpoints: server returned no ckpt_name entries in object_info (install models or fix CheckpointLoaderSimple).").toHtmlEscaped()
            + QLatin1String("</li>");
        html += QLatin1String("</ul>");
    }
    html += QLatin1String("<p><b>") + i18n("Detected base models:") + QLatin1String("</b></p><ul>");
    for (const auto &row : rows) {
        const QString key = QString::fromLatin1(row.classifierKey);
        const QString label = QString::fromLatin1(row.displayArch);
        const bool ok = present.contains(key);
        html += QLatin1String("<li><b>") + label.toHtmlEscaped() + QLatin1String("</b>: ");
        if (ok)
            html += i18n("supported").toHtmlEscaped();
        else
            html += i18n("missing %1", QString::fromLatin1(row.displayArch)).toHtmlEscaped();
        html += QLatin1String("</li>");
    }
    html += QLatin1String("</ul>");
    html += QLatin1String("<p>") + i18n("See <a href=\"https://docs.interstice.cloud\">Custom ComfyUI Setup</a> for required models. Check the client.log file for more details.") + QLatin1String("</p>");
    return html;
}

void ComfyUIRemoteDock::refreshCloudAuthStatusLabel()
{
    if (!m_d->labelCloudAuthStatus)
        return;
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    const bool tokenEmpty = s.value(QStringLiteral("access_token")).toString().trimmed().isEmpty();
    if (tokenEmpty) {
        // §13.89: Cloud + empty token → auth_missing (no CloudClient in this build)
        m_d->labelCloudAuthStatus->setText(
            i18n("Authentication required: Online Service sign-in is not available in this build. Use Custom ComfyUI or the Python plugin."));
        m_d->labelCloudAuthStatus->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    } else {
        m_d->labelCloudAuthStatus->setText(
            i18n("An access token is stored, but the Online Service API is not available in this build. Use Custom ComfyUI to connect to your own server."));
        m_d->labelCloudAuthStatus->setStyleSheet(QStringLiteral("color: palette(mid);"));
    }
    m_d->labelCloudAuthStatus->setWordWrap(true);
}

void ComfyUIRemoteDock::refreshConnectionActionButton()
{
    if (!m_d->btnTest)
        return;
    if (m_d->isConnecting) {
        m_d->btnTest->setEnabled(false);
        m_d->btnTest->setText(i18n("Connecting…"));
        m_d->btnTest->setIcon(KisIconUtils::loadIcon("network-connect"));
        return;
    }
    m_d->btnTest->setEnabled(true);
    if (m_d->isConnected) {
        m_d->btnTest->setText(i18n("Disconnect"));
        m_d->btnTest->setIcon(KisIconUtils::loadIcon("dialog-cancel"));
    } else {
        m_d->btnTest->setText(i18n("Connect"));
        m_d->btnTest->setIcon(KisIconUtils::loadIcon("network-connect"));
    }
}

// §13.89: User disconnect — cancel timers and clear Comfy client state (mirrors Python Connection.disconnect() for local URL client).
void ComfyUIRemoteDock::slotDisconnect()
{
    if (m_d->pollTimer)
        m_d->pollTimer->stop();
    if (m_d->inpaintPollTimer)
        m_d->inpaintPollTimer->stop();
    if (m_d->upscalePollTimer)
        m_d->upscalePollTimer->stop();
    if (m_d->liveTimer)
        m_d->liveTimer->stop();
    if (m_d->livePollTimer)
        m_d->livePollTimer->stop();
    if (m_d->controlPreviewPollTimer)
        m_d->controlPreviewPollTimer->stop();

    m_d->isConnected = false;
    m_d->isConnecting = false;
    m_d->connectionErrorOccurred = false;
    m_d->connectionErrorKind.clear();
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
    setStatusMessage(i18n("Disconnected from server."));
    updateWelcomeVisibility();
    refreshConnectionActionButton();
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
        refreshConnectionActionButton();
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
        refreshConnectionActionButton();
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
    refreshConnectionActionButton();
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
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
                if (!m_d->labelDetectedModels) {
                    updateWelcomeVisibility();
                    refreshConnectionActionButton();
                    return;
                }
                if (replyObj->error() != QNetworkReply::NoError) {
                    clearObjectInfoDerivedServerCaches();
                    refreshStylesTabLoraWarning();
                    applyStylesTabLoraListFilter();
                    m_d->labelDetectedModels->setText(i18n("Connected. Could not load node list: %1", replyObj->errorString()));
                    m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
                    m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
                    updateWelcomeVisibility();
                    refreshConnectionActionButton();
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
                    const QStringList ckptNames = parseCheckpointNamesFromObjectInfoRoot(root);
                    m_d->labelDetectedModels->setTextFormat(Qt::RichText);
                    m_d->labelDetectedModels->setText(buildMissingResourcesDictFormatHtml(ckptNames));
                    m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
                    m_d->labelDetectedModels->setOpenExternalLinks(true);
                    m_d->labelDetectedModels->setTextInteractionFlags(Qt::TextBrowserInteraction);
                }
                updateWelcomeVisibility();
                refreshConnectionActionButton();
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
        refreshConnectionActionButton();
    });
}

void ComfyUIRemoteDock::clearObjectInfoDerivedServerCaches()
{
    m_d->comfyServerLoraFilenames.clear();
    m_d->objectInfoSpec58NodesPresent.clear();
    m_d->objectInfoSpec58LastLoggedSignature.clear();
    m_d->lastObjectInfoRoot = QJsonObject();
}

void ComfyUIRemoteDock::syncFromObjectInfoRoot(const QJsonObject &objectInfoRoot)
{
    m_d->lastObjectInfoRoot = objectInfoRoot;
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

namespace {
QString comfyBaseWithHttpScheme(const QString &hostOrUrl)
{
    QString base = hostOrUrl.trimmed();
    if (base.isEmpty())
        return QString();
    if (!base.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        base = QStringLiteral("http://") + base;
    }
    return base;
}

QString hostPortKey(const QString &hostOrUrl)
{
    QString b = hostOrUrl.trimmed();
    if (b.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
        b = b.mid(7);
    else if (b.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        b = b.mid(8);
    return b;
}
} // namespace

// §13.81: When server_mode is undefined, try settings.server_url then 127.0.0.1:8000; on success set external + URL;
// on total failure set cloud and clear connection error (Online Service path).
void ComfyUIRemoteDock::tryAutostartServerFallback()
{
    if (!m_d->editServerUrl || !m_d->nam || m_d->autostartServerProbeDone)
        return;

    KConfigGroup modeCfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    if (modeCfg.readEntry(QStringLiteral("ServerMode"), QStringLiteral("undefined")) != QLatin1String("undefined"))
        return;

    m_d->autostartServerProbeDone = true;

    // §13.81: First URL is settings.server_url (JSON), then 127.0.0.1:8000 — not unsaved line-edit text.
    const QJsonObject sJson = ComfyUIUtils::loadSettingsJson();
    QString primary = sJson.value(QStringLiteral("server_url")).toString().trimmed();
    if (primary.isEmpty()) {
        KConfigGroup urlCfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
        primary = urlCfg.readEntry(QStringLiteral("ServerUrl"), QString()).trimmed();
    }
    if (primary.isEmpty())
        primary = QStringLiteral("127.0.0.1:8188");
    const QString fallback = QStringLiteral("127.0.0.1:8000");

    auto finishCloudFallback = [this]() {
        KConfigGroup cg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
        cg.writeEntry(QStringLiteral("ServerMode"), QStringLiteral("cloud"));
        KSharedConfig::openConfig()->sync();
        m_d->isConnected = false;
        m_d->connectionErrorOccurred = false;
        m_d->connectionErrorKind.clear();
        updateWelcomeVisibility();
    };

    auto applyExternalAndConnect = [this](const QString &hostPort) {
        {
            QSignalBlocker b(m_d->editServerUrl);
            m_d->editServerUrl->setText(hostPort);
        }
        KConfigGroup cg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
        cg.writeEntry(QStringLiteral("ServerUrl"), hostPort);
        cg.writeEntry(QStringLiteral("ServerMode"), QStringLiteral("external"));
        QJsonObject st = ComfyUIUtils::loadSettingsJson();
        st.insert(QStringLiteral("server_url"), hostPort);
        ComfyUIUtils::saveSettingsJson(st);
        KSharedConfig::openConfig()->sync();
        slotTestConnection();
    };

    auto probeSystemStats = [this](const QString &candidate) -> QUrl {
        const QString base = comfyBaseWithHttpScheme(candidate);
        if (base.isEmpty())
            return QUrl();
        QUrl url = ComfyUIUtils::comfyResolveApiUrl(base, QStringLiteral("system_stats"));
        return url.isValid() ? url : QUrl();
    };

    const QUrl primaryStats = probeSystemStats(primary);
    if (!primaryStats.isValid()) {
        finishCloudFallback();
        return;
    }

    QNetworkRequest req1(primaryStats);
    ComfyUIUtils::setComfyUIRequestHeaders(req1);
    QNetworkReply *reply1 = m_d->nam->get(req1);
    connect(reply1, &QNetworkReply::finished, this, [this, reply1, primary, fallback, applyExternalAndConnect,
                                                      finishCloudFallback]() {
        reply1->deleteLater();
        const int http1 = reply1->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok1 = reply1->error() == QNetworkReply::NoError && http1 == 200;
        if (ok1) {
            applyExternalAndConnect(primary);
            return;
        }
        if (hostPortKey(primary) == hostPortKey(fallback)) {
            finishCloudFallback();
            return;
        }
        const QUrl fbStats = probeSystemStats(fallback);
        if (!fbStats.isValid()) {
            finishCloudFallback();
            return;
        }
        QNetworkRequest req2(fbStats);
        ComfyUIUtils::setComfyUIRequestHeaders(req2);
        QNetworkReply *reply2 = m_d->nam->get(req2);
        connect(reply2, &QNetworkReply::finished, this, [this, reply2, fallback, applyExternalAndConnect, finishCloudFallback]() {
            reply2->deleteLater();
            const int http2 = reply2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok2 = reply2->error() == QNetworkReply::NoError && http2 == 200;
            if (ok2)
                applyExternalAndConnect(fallback);
            else
                finishCloudFallback();
        });
    });
}
