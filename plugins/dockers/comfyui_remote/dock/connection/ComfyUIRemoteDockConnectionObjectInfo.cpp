/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyLocalization.h"
#include "ComfyStyleCollection.h"
#include "ComfyUIUtils.h"
#include "ComfyFileLibrary.h"

#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QSignalBlocker>
#include <KSharedConfig>
#include <KConfigGroup>

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
    if (path.isEmpty() || path == "/") url.setPath("/object_info");
    else if (!path.endsWith('/')) url.setPath(path + "/object_info");
    else url.setPath(path + "object_info");
    setStatusMessage(ComfyTr::tr("Loading samplers…"));
    m_d->generate.btnRefreshSamplers->setEnabled(false);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_d->generate.btnRefreshSamplers->setEnabled(true);
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
            QString current = m_d->generate.comboSampler->currentText();
            m_d->generate.comboSampler->clear();
            m_d->generate.comboSampler->addItems(names);
            int idx = m_d->generate.comboSampler->findText(current);
            if (idx >= 0) m_d->generate.comboSampler->setCurrentIndex(idx);
            else m_d->generate.comboSampler->setCurrentIndex(0);
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
    m_d->generate.btnRefreshCheckpoints->setEnabled(false);
    QNetworkRequest req(url);
    ComfyUIUtils::setComfyUIRequestHeaders(req);
    QNetworkReply *reply = m_d->nam->get(req);
    const QString baseUrlStr = urlStr;
    connect(reply, &QNetworkReply::finished, this, [this, reply, baseUrlStr]() {
        m_d->generate.btnRefreshCheckpoints->setEnabled(true);
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
    if (!m_d->generate.comboCheckpoint)
        return;
    const QString prevCkpt = m_d->generate.comboCheckpoint->currentText().trimmed();
    const int prevPresetIdx = m_d->generate.comboPreset ? m_d->generate.comboPreset->currentIndex() : 0;
    const int firstCustom = firstCustomPresetIndex();

    m_d->generate.comboCheckpoint->clear();
    if (filteredNames.isEmpty()) {
        m_d->generate.comboCheckpoint->addItem(QStringLiteral("v1-5-pruned-emaonly.safetensors"));
        m_d->generate.comboCheckpoint->setCurrentIndex(0);
        if (emptyServerList)
            setStatusMessage(ComfyTr::tr("No checkpoint list in server response (use custom name)."), true);
        return;
    }

    m_d->generate.comboCheckpoint->addItems(filteredNames);
    ComfyFileLibrary::instance().init();
    ComfyFileLibrary::instance().updateRemoteCheckpoints(filteredNames);
    const int ix = m_d->generate.comboCheckpoint->findText(prevCkpt);
    if (ix >= 0) {
        m_d->generate.comboCheckpoint->setCurrentIndex(ix);
        setStatusMessage(ComfyTr::tr("Loaded %1 checkpoints.", filteredNames.size()));
        return;
    }

    m_d->generate.comboCheckpoint->setCurrentIndex(0);
    setStatusMessage(ComfyTr::tr("Loaded %1 checkpoints.", filteredNames.size()));

    if (m_d->generate.comboPreset && m_d->generate.comboPreset->currentIndex() > 0) {
        if (prevPresetIdx > 0 && prevPresetIdx < firstCustom) {
            const bool showBuiltin =
                ComfyUIUtils::loadSettingsJson().value(QStringLiteral("show_builtin_styles")).toBool(true);
            const QList<const ComfyStyleEntry *> styles = ComfyStyleCollection::instance().filtered(showBuiltin);
            const int styleIdx = prevPresetIdx - 1;
            if (styleIdx >= 0 && styleIdx < styles.size())
                applyComfyStyleEntry(*styles.at(styleIdx));
        } else if (prevPresetIdx >= firstCustom) {
            const QString presetName = m_d->generate.comboPreset->itemText(prevPresetIdx);
            KConfigGroup presetCfg =
                KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote_Preset_") + presetName);
            const QString savedCkpt = presetCfg.readEntry(QStringLiteral("Checkpoint"), QString()).trimmed();
            if (!savedCkpt.isEmpty() && m_d->generate.comboCheckpoint->findText(savedCkpt) < 0) {
                QSignalBlocker b(m_d->generate.comboPreset);
                m_d->generate.comboPreset->setCurrentIndex(0);
                slotPresetChanged(0);
                setStatusMessage(ComfyTr::tr(
                                     "The checkpoint saved with style \"%1\" is not on this server. Style reset to None.",
                                     presetName),
                                 false,
                                 true);
            }
        }
    }
}
