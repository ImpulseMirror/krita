/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyFileLibrary.h"

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
#include <QTimer>

#include <kis_icon_utils.h>

void ComfyUIRemoteDock::slotRefreshSamplers()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    QString path = url.path();
    // §13.123: ComfyObjectInfo — GET object_info; we parse node input.required for sampler_name / ckpt_name options
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    setStatusMessage(ComfyTr::tr("Loading samplers…"));
    m_d->btnRefreshSamplers->setEnabled(false);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->btnRefreshSamplers->setEnabled(true);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatusMessage(ComfyTr::tr("Failed to load samplers: %1", reply->errorString()), true);
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
            setStatusMessage(ComfyTr::tr("Loaded %1 samplers.", names.size()));
        } else {
            setStatusMessage(ComfyTr::tr("No sampler list in server response."), true);
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
        setStatusMessage(ComfyTr::tr("Enter a server URL first."), true);
        return;
    }
    QUrl url(urlStr);
    if (!url.isValid()) {
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        return;
    }
    QString path = url.path();
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    setStatusMessage(ComfyTr::tr("Loading checkpoints…"));
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
            setStatusMessage(ComfyTr::tr("Failed to load checkpoints: %1", reply->errorString()), true);
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        const QStringList names = ComfyUIUtils::parseCheckpointNamesFromObjectInfoRoot(root);
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
            ComfyFileLibrary::instance().init();
            ComfyFileLibrary::instance().updateRemoteLoras(m_d->comfyServerLoraFilenames);
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
        + ComfyTr::tr("ComfyUI core / extensions").toHtmlEscaped() + QLatin1String("</a></li>");
}

// §13.71: Build list-format HTML for missing custom nodes (heading, <ul>, sentences)
static QString buildMissingNodesListFormat(const QStringList &missingNodes)
{
    QString html;
    html += QLatin1String("<p><b>") + ComfyTr::tr("The following ComfyUI custom nodes are missing or too old") + QLatin1String("</b></p><ul>");
    for (const QString &name : missingNodes)
        html += missingCustomNodeListItemHtml(name);
    html += QLatin1String("</ul><p>") + ComfyTr::tr("Please install or update the custom node package (e.g. ComfyUI Manager or the node's repository).") + QLatin1String("</p>");
    html += QLatin1String("<p>") + ComfyTr::tr("If nodes are still missing, check the ComfyUI output at startup for errors.") + QLatin1String("</p>");
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
        html += QLatin1String("<p><b>") + ComfyTr::tr("Missing common models (required):") + QLatin1String("</b></p><ul>");
        html += QLatin1String("<li>") + ComfyTr::tr("Checkpoints: server returned no ckpt_name entries in object_info (install models or fix CheckpointLoaderSimple).").toHtmlEscaped()
            + QLatin1String("</li>");
        html += QLatin1String("</ul>");
    }
    html += QLatin1String("<p><b>") + ComfyTr::tr("Detected base models:") + QLatin1String("</b></p><ul>");
    for (const auto &row : rows) {
        const QString key = QString::fromLatin1(row.classifierKey);
        const QString label = QString::fromLatin1(row.displayArch);
        const bool ok = present.contains(key);
        html += QLatin1String("<li><b>") + label.toHtmlEscaped() + QLatin1String("</b>: ");
        if (ok)
            html += ComfyTr::tr("supported").toHtmlEscaped();
        else
            html += ComfyTr::tr("missing %1", QString::fromLatin1(row.displayArch)).toHtmlEscaped();
        html += QLatin1String("</li>");
    }
    html += QLatin1String("</ul>");
    html += QLatin1String("<p>")
        + ComfyTr::tr("Install the required custom nodes and models on your ComfyUI server. Check the client.log file for more details.")
        + QLatin1String("</p>");
    return html;
}

void ComfyUIRemoteDock::refreshConnectionActionButton()
{
    if (!m_d->btnTest)
        return;
    if (m_d->isConnecting) {
        m_d->btnTest->setEnabled(false);
        m_d->btnTest->setText(ComfyTr::tr("Connecting…"));
        m_d->btnTest->setIcon(ComfyTheme::icon(QStringLiteral("web-connection")));
        return;
    }
    m_d->btnTest->setEnabled(true);
    if (m_d->isConnected) {
        m_d->btnTest->setText(ComfyTr::tr("Disconnect"));
        m_d->btnTest->setIcon(ComfyTheme::icon(QStringLiteral("cancel")));
    } else {
        m_d->btnTest->setText(ComfyTr::tr("Connect"));
        m_d->btnTest->setIcon(ComfyTheme::icon(QStringLiteral("web-connection")));
    }
}

static void syncDetectedModelsLabel(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->labelDetectedModels)
        return;
    if (!d->isConnected || d->lastObjectInfoRoot.isEmpty()) {
        d->labelDetectedModels->setText(
            ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        d->labelDetectedModels->setTextFormat(Qt::PlainText);
        return;
    }
    QStringList missing;
    for (const QString &key : requiredObjectInfoNodes()) {
        if (!d->lastObjectInfoRoot.contains(key))
            missing << key;
    }
    if (!missing.isEmpty()) {
        d->labelDetectedModels->setTextFormat(Qt::RichText);
        d->labelDetectedModels->setText(buildMissingNodesListFormat(missing));
        d->labelDetectedModels->setStyleSheet(QString());
        d->labelDetectedModels->setOpenExternalLinks(true);
        d->labelDetectedModels->setTextInteractionFlags(Qt::TextBrowserInteraction);
        return;
    }
    const QStringList ckptNames = ComfyUIUtils::parseCheckpointNamesFromObjectInfoRoot(d->lastObjectInfoRoot);
    d->labelDetectedModels->setTextFormat(Qt::RichText);
    d->labelDetectedModels->setText(buildMissingResourcesDictFormatHtml(ckptNames));
    d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
    d->labelDetectedModels->setOpenExternalLinks(true);
    d->labelDetectedModels->setTextInteractionFlags(Qt::TextBrowserInteraction);
}

void ComfyUIRemoteDock::refreshConnectionTabUi()
{
    refreshConnectionActionButton();
    if (!m_d->labelConnectionStatus && !m_d->labelDetectedModels)
        return;

    if (m_d->isConnected) {
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Connected"));
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: green;"));
        }
        syncDetectedModelsLabel(m_d.data());
        return;
    }

    if (m_d->isConnecting) {
        if (m_d->labelConnectionStatus) {
            QString statusText;
            if (m_d->connectionAutostartActive && m_d->connectionAutostartRetryAttempt > 0) {
                statusText = ComfyTr::tr("Retrying (%1/%2)",
                                         m_d->connectionAutostartRetryAttempt,
                                         m_d->connectionAutostartMaxRetries);
            } else if (!m_d->lastComfySystemStats.isEmpty() && m_d->lastObjectInfoRoot.isEmpty()) {
                statusText = ComfyTr::tr("Loading server capabilities…");
            } else {
                statusText = ComfyTr::tr("Connecting");
            }
            m_d->labelConnectionStatus->setText(statusText);
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        }
        return;
    }

    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
    }
    syncDetectedModelsLabel(m_d.data());
}

// §13.89: User disconnect — cancel timers and clear Comfy client state (mirrors Python Connection.disconnect() for local URL client).
void ComfyUIRemoteDock::slotDisconnect()
{
    ++m_d->connectionSessionId;
    cancelConnectionAutostartRetry();
    m_d->connectionAutostartActive = false;
    m_d->connectionAutostartRetryAttempt = 0;
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
    if (m_d->controlLayerJobPollTimer)
        m_d->controlLayerJobPollTimer->stop();
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (m_d->controlLayerJobWebSocket) {
        m_d->controlLayerJobWebSocket->close();
        m_d->controlLayerJobWebSocket->deleteLater();
        m_d->controlLayerJobWebSocket = nullptr;
    }
#endif
    m_d->controlLayerJobOpenPoseJson = QJsonValue();

    m_d->isConnected = false;
    m_d->isConnecting = false;
    m_d->connectionErrorOccurred = false;
    m_d->connectionErrorKind.clear();
    m_d->comfyDeviceSummary.clear();
    m_d->lastComfySystemStats = QJsonObject();
    if (m_d->labelPerfDevice)
        m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
    }
    if (m_d->labelDetectedModels) {
        m_d->labelDetectedModels->setText(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
    }
    clearObjectInfoDerivedServerCaches();
    refreshStylesTabLoraWarning();
    applyStylesTabLoraListFilter();
    setStatusMessage(ComfyTr::tr("Disconnected from server."));
    updateWelcomeVisibility();
    refreshConnectionActionButton();
    refreshInterfacePromptTranslationCombo();
}

void ComfyUIRemoteDock::cancelConnectionAutostartRetry()
{
    if (m_d->connectionRetryTimer)
        m_d->connectionRetryTimer->stop();
}

void ComfyUIRemoteDock::scheduleConnectionAutostartRetry(const QString &reason)
{
    if (!m_d->connectionAutostartActive)
        return;

    if (m_d->connectionAutostartRetryAttempt >= m_d->connectionAutostartMaxRetries) {
        qWarning("ComfyUI Remote: autostart connect gave up after %d retries (%s).",
                 m_d->connectionAutostartMaxRetries,
                 qPrintable(reason));
        m_d->connectionAutostartActive = false;
        m_d->connectionAutostartRetryAttempt = 0;
        m_d->isConnecting = false;
        m_d->isConnected = false;
        m_d->connectionErrorOccurred = true;
        setStatusMessage(ComfyTr::tr("Could not connect to ComfyUI after %1 attempts. Use Connect to retry.",
                                    m_d->connectionAutostartMaxRetries),
                         true);
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        }
        refreshConnectionActionButton();
        updateWelcomeVisibility();
        return;
    }

    const int attempt = m_d->connectionAutostartRetryAttempt + 1;
    const int delayMs = qMin(8000, 400 * (1 << m_d->connectionAutostartRetryAttempt));
    m_d->connectionAutostartRetryAttempt = attempt;

    qWarning("ComfyUI Remote: autostart connect retry %d/%d in %d ms (%s).",
             attempt,
             m_d->connectionAutostartMaxRetries,
             delayMs,
             qPrintable(reason));

    if (!m_d->connectionRetryTimer) {
        m_d->connectionRetryTimer = new QTimer(this);
        m_d->connectionRetryTimer->setSingleShot(true);
        connect(m_d->connectionRetryTimer, &QTimer::timeout, this, [this]() {
            if (m_d->connectionAutostartActive)
                slotTestConnection();
        });
    }

    m_d->isConnecting = true;
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(
            ComfyTr::tr("Retrying (%1/%2)", attempt, m_d->connectionAutostartMaxRetries));
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
    }
    setStatusMessage(ComfyTr::tr("Server not ready, retrying connection (%1/%2)…",
                                 attempt,
                                 m_d->connectionAutostartMaxRetries));
    refreshConnectionActionButton();
    updateWelcomeVisibility();
    m_d->connectionRetryTimer->start(delayMs);
}

void ComfyUIRemoteDock::handleConnectionProbeFailure(uint session,
                                                     const QString &retryReason,
                                                     const QString &userMessage,
                                                     bool isError,
                                                     bool allowAutostartRetry)
{
    if (session != m_d->connectionSessionId)
        return;

    m_d->comfyDeviceSummary.clear();
    m_d->lastComfySystemStats = QJsonObject();
    clearObjectInfoDerivedServerCaches();
    refreshStylesTabLoraWarning();
    applyStylesTabLoraListFilter();
    if (m_d->labelPerfDevice)
        m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
    m_d->isConnected = false;
    m_d->connectionErrorOccurred = true;

    if (m_d->connectionAutostartActive && allowAutostartRetry) {
        scheduleConnectionAutostartRetry(retryReason);
        if (m_d->connectionAutostartActive)
            return;
    } else if (m_d->connectionAutostartActive) {
        m_d->connectionAutostartActive = false;
        m_d->connectionAutostartRetryAttempt = 0;
    }

    m_d->isConnecting = false;
    m_d->connectionErrorKind = QStringLiteral("network");
    setStatusMessage(userMessage, isError);
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Error: %1", userMessage));
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: red;"));
    }
    if (m_d->labelDetectedModels) {
        m_d->labelDetectedModels->setText(
            ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        m_d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        m_d->labelDetectedModels->setTextFormat(Qt::PlainText);
    }
    updateWelcomeVisibility();
    refreshConnectionActionButton();
}

void ComfyUIRemoteDock::handleConnectionEstablished(uint session,
                                                      const QJsonObject &objectInfoRoot,
                                                      const QString &base)
{
    if (session != m_d->connectionSessionId)
        return;

    cancelConnectionAutostartRetry();
    m_d->connectionAutostartActive = false;
    m_d->connectionAutostartRetryAttempt = 0;
    m_d->isConnecting = false;
    m_d->isConnected = true;
    m_d->connectionErrorOccurred = false;
    m_d->connectionErrorKind.clear();

    syncFromObjectInfoRoot(objectInfoRoot);
    fetchComfyModelsLorasMergeAndRefreshStylesTab();
    {
        const QStringList ckptNames = ComfyUIUtils::parseCheckpointNamesFromObjectInfoRoot(objectInfoRoot);
        const QString bu = m_d->editServerUrl->text().trimmed();
        if (!ckptNames.isEmpty() && !bu.isEmpty())
            fetchFilteredCheckpointListAndApply(ckptNames, bu, true);
    }

    {
        QStringList missing;
        for (const QString &key : requiredObjectInfoNodes()) {
            if (!objectInfoRoot.contains(key))
                missing << key;
        }
        if (!missing.isEmpty())
            m_d->connectionErrorKind = QStringLiteral("missing_resources");
    }

    setStatusMessage(ComfyTr::tr("Connected to ComfyUI."));
    updateWelcomeVisibility();
    refreshConnectionTabUi();
    refreshInterfacePromptTranslationCombo();
    Q_UNUSED(base);
}

// §13.198: Connect — system_stats then object_info; connected only after object_info succeeds.
void ComfyUIRemoteDock::slotTestConnection()
{
    QString urlStr = m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        cancelConnectionAutostartRetry();
        m_d->connectionAutostartActive = false;
        m_d->connectionAutostartRetryAttempt = 0;
        setStatusMessage(ComfyTr::tr("Enter a server URL."), true);
        m_d->isConnected = false;
        m_d->isConnecting = false;
        m_d->connectionErrorOccurred = false;
        m_d->connectionErrorKind.clear();  // §7.4a
        m_d->comfyDeviceSummary.clear();
        m_d->lastComfySystemStats = QJsonObject();
        if (m_d->labelPerfDevice)
            m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
        }
        if (m_d->labelDetectedModels) {
            m_d->labelDetectedModels->setText(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
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
    const QString base = ComfyUIUtils::normalizeComfyServerBaseUrl(urlStr);
    QUrl url = ComfyUIUtils::comfyResolveApiUrl(base, QStringLiteral("system_stats"));
    if (!url.isValid() || url.host().isEmpty()) {
        setStatusMessage(ComfyTr::tr("Invalid URL."), true);
        m_d->isConnected = false;
        m_d->isConnecting = false;
        m_d->connectionErrorOccurred = true;
        m_d->comfyDeviceSummary.clear();
        m_d->lastComfySystemStats = QJsonObject();
        clearObjectInfoDerivedServerCaches();
        refreshStylesTabLoraWarning();
        applyStylesTabLoraListFilter();
        if (m_d->labelPerfDevice)
            m_d->labelPerfDevice->setText(ComfyTr::tr("Device: (connect to server)"));
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Error: Invalid URL"));
            m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: red;"));
        }
        if (m_d->labelDetectedModels) {
            m_d->labelDetectedModels->setText(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        }
        updateWelcomeVisibility();
        refreshConnectionActionButton();
        return;
    }
    cancelConnectionAutostartRetry();
    const uint session = ++m_d->connectionSessionId;
    m_d->isConnected = false;
    setStatusMessage(ComfyTr::tr("Connecting…"));
    m_d->isConnecting = true;
    m_d->connectionErrorOccurred = false;
    updateWelcomeVisibility();
    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Connecting"));
        m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
    }
    refreshConnectionActionButton();
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, base, session]() {
        if (session != m_d->connectionSessionId) {
            reply->deleteLater();
            return;
        }
        const QByteArray responseBody = reply->readAll();
        reply->deleteLater();
        const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && httpCode == 200) {
            const QJsonObject sysRoot = QJsonDocument::fromJson(responseBody).object();
            m_d->lastComfySystemStats = sysRoot;
            m_d->comfyDeviceSummary = ComfyUIUtils::formatComfySystemStatsDeviceLine(sysRoot);
            if (m_d->labelPerfDevice)
                m_d->labelPerfDevice->setText(m_d->comfyDeviceSummary);
            syncPerformanceFromAutoPreset();
            setStatusMessage(ComfyTr::tr("Loading server capabilities…"));
            if (m_d->labelConnectionStatus) {
                m_d->labelConnectionStatus->setText(ComfyTr::tr("Loading server capabilities…"));
                m_d->labelConnectionStatus->setStyleSheet(QStringLiteral("color: gray;"));
            }

            QUrl objUrl = ComfyUIUtils::comfyResolveApiUrl(base, QStringLiteral("object_info"));
            QNetworkRequest reqObj(objUrl);
            ComfyUIUtils::setComfyUIRequestHeaders(reqObj);
            QNetworkReply *replyObj = m_d->nam->get(reqObj);
            connect(replyObj, &QNetworkReply::finished, this, [this, replyObj, base, session]() {
                if (session != m_d->connectionSessionId) {
                    replyObj->deleteLater();
                    return;
                }
                replyObj->deleteLater();
                if (replyObj->error() != QNetworkReply::NoError) {
                    qWarning("ComfyUI Remote: object_info failed during connect: %s",
                             qPrintable(replyObj->errorString()));
                    handleConnectionProbeFailure(
                        session,
                        replyObj->errorString(),
                        ComfyTr::tr("Connected to server but could not load capabilities: %1",
                                    replyObj->errorString()));
                    return;
                }
                const QJsonObject root = QJsonDocument::fromJson(replyObj->readAll()).object();
                if (root.isEmpty()) {
                    qWarning("ComfyUI Remote: object_info returned empty JSON during connect.");
                    handleConnectionProbeFailure(
                        session,
                        QStringLiteral("empty object_info"),
                        ComfyTr::tr("Connected to server but received an empty capability list."));
                    return;
                }
                handleConnectionEstablished(session, root, base);
            });
            return;
        }

        QString userMessage;
        QString retryReason = reply->errorString();
        if (httpCode == 401) {
            m_d->connectionErrorKind = QStringLiteral("network");
            userMessage = ComfyTr::tr("Unauthorized (401). Check server authentication.");
            retryReason = QStringLiteral("HTTP 401");
            handleConnectionProbeFailure(session, retryReason, userMessage, true, false);
            return;
        }

        const bool networkError = (reply->error() != QNetworkReply::NoError
                                 && reply->error() != QNetworkReply::ContentNotFoundError);
        m_d->connectionErrorKind = networkError ? QStringLiteral("network") : QStringLiteral("unknown");
        userMessage = ComfyTr::tr("Connection failed: %1", reply->errorString());
        qWarning("ComfyUI Remote: system_stats failed during connect (http=%d): %s",
                 httpCode,
                 qPrintable(retryReason));
        handleConnectionProbeFailure(session, retryReason, userMessage);
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
    ComfyFileLibrary::instance().init();
    ComfyFileLibrary::instance().updateRemoteLoras(m_d->comfyServerLoraFilenames);
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
            setStatusMessage(ComfyTr::tr("No checkpoint list in server response (use custom name)."), true);
        return;
    }

    m_d->comboCheckpoint->addItems(filteredNames);
    ComfyFileLibrary::instance().init();
    ComfyFileLibrary::instance().updateRemoteCheckpoints(filteredNames);
    const int ix = m_d->comboCheckpoint->findText(prevCkpt);
    if (ix >= 0) {
        m_d->comboCheckpoint->setCurrentIndex(ix);
        setStatusMessage(ComfyTr::tr("Loaded %1 checkpoints.", filteredNames.size()));
        return;
    }

    m_d->comboCheckpoint->setCurrentIndex(0);
    setStatusMessage(ComfyTr::tr("Loaded %1 checkpoints.", filteredNames.size()));

    if (prevPresetIdx >= firstCustom && m_d->comboPreset) {
        const QString presetName = m_d->comboPreset->itemText(prevPresetIdx);
        KConfigGroup presetCfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote_Preset_") + presetName);
        const QString savedCkpt = presetCfg.readEntry(QStringLiteral("Checkpoint"), QString()).trimmed();
        if (!savedCkpt.isEmpty() && m_d->comboCheckpoint->findText(savedCkpt) < 0) {
            QSignalBlocker b(m_d->comboPreset);
            m_d->comboPreset->setCurrentIndex(0);
            slotPresetChanged(0);
            setStatusMessage(ComfyTr::tr("The checkpoint saved with style \"%1\" is not on this server. Style reset to None.",
                                  presetName),
                             false,
                             true);
        }
    }
}

// §13.81 / §13.198: When a server URL is saved, auto-connect once on startup.
void ComfyUIRemoteDock::tryAutostartServerFallback()
{
    if (!m_d->editServerUrl || !m_d->nam || m_d->autostartServerProbeDone)
        return;

    m_d->autostartServerProbeDone = true;

    if (m_d->isConnected || m_d->isConnecting)
        return;

    const QString savedUrl = ComfyUIUtils::savedServerUrl();
    if (savedUrl.isEmpty())
        return;

    if (m_d->editServerUrl->text().trimmed().isEmpty()) {
        QSignalBlocker b(m_d->editServerUrl);
        m_d->editServerUrl->setText(savedUrl);
    }

    KConfigGroup modeCfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    const QString mode = modeCfg.readEntry(QStringLiteral("ServerMode"), QStringLiteral("undefined"));
    if (mode != QLatin1String("external")) {
        modeCfg.writeEntry(QStringLiteral("ServerMode"), QStringLiteral("external"));
        KSharedConfig::openConfig()->sync();
    }

    m_d->connectionAutostartActive = true;
    m_d->connectionAutostartRetryAttempt = 0;
    slotTestConnection();
}

void ComfyUIRemoteDock::refreshInterfacePromptTranslationCombo()
{
    if (!m_d->settingsPromptTranslationCombo)
        return;

    QComboBox *combo = m_d->settingsPromptTranslationCombo;
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QString saved = s.value(QStringLiteral("prompt_translation")).toString().trimmed();
    if (saved.isEmpty() || saved == QLatin1String("disabled"))
        saved.clear();

    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem(ComfyTr::tr("Disabled"), QString());
    if (m_d->isConnected) {
        combo->setEnabled(true);
        for (const ComfyLanguageInfo &lang : ComfyLocalization::instance().availableLanguages())
            combo->addItem(lang.name, lang.id);
    } else {
        combo->setEnabled(false);
    }
    const int idx = saved.isEmpty() ? 0 : combo->findData(saved);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}
