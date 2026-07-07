/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyUiStyle.h"

#include "ComfyDockUiBuilder.h"

#include <QTimer>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QUrlQuery>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

namespace {

QString comfyNormalizeSha256Hex(QString s)
{
    s = s.trimmed().toLower();
    if (s.startsWith(QLatin1String("0x")))
        s = s.mid(2);
    return s;
}
bool welcomePanelShowsAutoUpdate(const ComfyUIRemoteDock::Private *d)
{
    if (!ComfyUIUtils::loadSettingsJson().value(QStringLiteral("auto_update")).toBool(true))
        return false;
    using PS = ComfyUIRemoteDock::Private::PluginUpdateState;
    // §13.37: visible when auto_update and state is not latest, failed_check, or checking (includes unknown).
    switch (d->pluginUpdateState) {
    case PS::Latest:
    case PS::FailedCheck:
    case PS::Checking:
        return false;
    default:
        return true;
    }
}
} // namespace


void ComfyUIRemoteDock::updateWelcomeVisibility()
{
    if (!m_d->mainStack || m_d->mainStack->count() < 2) return;
    const bool showWelcome = !m_d->canvas || !m_d->isConnected;
    m_d->mainStack->setCurrentIndex(showWelcome ? 0 : 1);
    if (showWelcome && !m_d->updateCheckRequested)
        QTimer::singleShot(200, this, [this]() { startUpdateCheck(false); });
    if (showWelcome)
        QTimer::singleShot(500, this, &ComfyUIRemoteDock::startNewsFetch);
    // §13.190 / §13.37: AutoUpdateWidget when auto_update and state ∉ {latest, failed_check, checking}
    if (m_d->welcomeUpdateWidget && m_d->welcomeNewsWidget && m_d->welcomeConnectionWidget) {
        const bool showAuto = welcomePanelShowsAutoUpdate(m_d.data());
        m_d->welcomeUpdateWidget->setVisible(showAuto);
        m_d->welcomeNewsWidget->setVisible(!showAuto && m_d->hasUnseenNews);
        m_d->welcomeConnectionWidget->setVisible(!showAuto && !m_d->hasUnseenNews);
    }
    if (showWelcome)
        refreshWelcomeAutoUpdatePanel();
    if (m_d->welcomeStatusLabel) {
        const auto resetWelcomeStatusStyle = [](QLabel *label) {
            if (label)
                ComfyUiStyle::resetLabelStyle(label);
        };
        if (m_d->updateCheckInProgress) {
            resetWelcomeStatusStyle(m_d->welcomeStatusLabel);
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Checking for updates..."));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else if (m_d->isConnecting) {
            resetWelcomeStatusStyle(m_d->welcomeStatusLabel);
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Connecting to server..."));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else if (m_d->isConnected && !m_d->editServerUrl->text().trimmed().isEmpty()) {
            resetWelcomeStatusStyle(m_d->welcomeStatusLabel);
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Connected to server at %1. Create a new document or open an existing image to start!", m_d->editServerUrl->text().trimmed()));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else if (m_d->connectionErrorOccurred) {
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Connection attempt failed! Click below to configure and reconnect."));
            ComfyUiStyle::styleStatusLabel(m_d->welcomeStatusLabel, ComfyUiStyle::StatusTone::Warning);
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        } else {
            resetWelcomeStatusStyle(m_d->welcomeStatusLabel);
            m_d->welcomeStatusLabel->setText(ComfyTr::tr("Not connected to server."));
            if (m_d->welcomeErrorLabel) {
                m_d->welcomeErrorLabel->clear();
                m_d->welcomeErrorLabel->hide();
            }
        }
    }
    if (!showWelcome && m_d->comboWorkspace && m_d->comboWorkspace->currentIndex() == 1) {
        updateUpscaleTargetSize();
    }
}
void ComfyUIRemoteDock::slotCheckForUpdates()
{
    startUpdateCheck(true);
}
void ComfyUIRemoteDock::syncPluginUpdateUi()
{
    refreshWelcomeAutoUpdatePanel();
    refreshPluginInformationTabUpdateUi();
    updateWelcomeVisibility();
}
void ComfyUIRemoteDock::refreshWelcomeAutoUpdatePanel()
{
    if (!m_d->welcomeUpdateTitleLabel || !m_d->welcomeUpdateButton || !m_d->welcomeUpdateProgressBar
        || !m_d->welcomeUpdateVersionLabel || !m_d->welcomeCheckAutoUpdate)
        return;
    QString title;
    QString btnText = ComfyTr::tr("Download and Install");
    bool btnEnabled = false;
    bool showProgress = false;
    const QString verShow =
        m_d->updateRemoteVersion.isEmpty() ? m_d->lastReportedLatestPluginVersion : m_d->updateRemoteVersion;
    switch (m_d->pluginUpdateState) {
    case Private::PluginUpdateState::Available:
        // §5.2: headline + version line (not one combined string)
        title = ComfyTr::tr("A new plugin version is available!");
        if (m_d->welcomeUpdateVersionLabel) {
            m_d->welcomeUpdateVersionLabel->setText(verShow.isEmpty() ? QStringLiteral("—") : verShow);
            m_d->welcomeUpdateVersionLabel->show();
        }
        btnEnabled = !m_d->pluginUpdateDownloadReply && !m_d->updateDownloadUrl.isEmpty();
        break;
    case Private::PluginUpdateState::Downloading:
        title = ComfyTr::tr("Downloading update…");
        showProgress = true;
        break;
    case Private::PluginUpdateState::Installing:
        title = ComfyTr::tr("Installing update…");
        showProgress = true;
        break;
    case Private::PluginUpdateState::RestartRequired:
        title = m_d->updateExtractPath.isEmpty()
            ? ComfyTr::tr("Update is ready. Please restart Krita.")
            : ComfyTr::tr("Update saved to:\n%1\nFollow the release notes if needed, then restart Krita.", m_d->updateExtractPath);
        btnText = ComfyTr::tr("Open Folder");
        btnEnabled = !m_d->updateExtractPath.isEmpty() && QFileInfo::exists(m_d->updateExtractPath);
        break;
    case Private::PluginUpdateState::FailedUpdate:
        title = ComfyTr::tr("Update failed (network error or checksum mismatch).");
        btnText = ComfyTr::tr("Retry");
        btnEnabled = !m_d->pluginUpdateDownloadReply && !m_d->updateDownloadUrl.isEmpty();
        break;
    case Private::PluginUpdateState::Unknown:
        title = ComfyTr::tr("Looking for plugin updates…");
        btnEnabled = false;
        break;
    default:
        title.clear();
        break;
    }
    if (m_d->pluginUpdateState != Private::PluginUpdateState::Available && m_d->welcomeUpdateVersionLabel)
        m_d->welcomeUpdateVersionLabel->hide();
    {
        QSignalBlocker b(m_d->welcomeCheckAutoUpdate);
        m_d->welcomeCheckAutoUpdate->setChecked(
            ComfyUIUtils::loadSettingsJson().value(QStringLiteral("auto_update")).toBool(true));
    }
    m_d->welcomeUpdateTitleLabel->setText(title);
    m_d->welcomeUpdateProgressBar->setVisible(showProgress);
    m_d->welcomeUpdateButton->setText(btnText);
    m_d->welcomeUpdateButton->setEnabled(btnEnabled);
}
void ComfyUIRemoteDock::startPluginUpdateDownload()
{
    if (!m_d->nam || m_d->pluginUpdateDownloadReply)
        return;
    if (m_d->pluginUpdateState != Private::PluginUpdateState::Available
        && m_d->pluginUpdateState != Private::PluginUpdateState::FailedUpdate)
        return;
    if (m_d->updateDownloadUrl.isEmpty())
        return;
    const QUrl u(m_d->updateDownloadUrl);
    if (!u.isValid())
        return;

    m_d->pluginUpdateSaveFile.reset(new QTemporaryFile());
    m_d->pluginUpdateSaveFile->setFileTemplate(QDir::tempPath() + QStringLiteral("/cui_plugin_update_XXXXXX.zip"));
    if (!m_d->pluginUpdateSaveFile->open()) {
        m_d->pluginUpdateSaveFile.reset();
        m_d->pluginUpdateState = Private::PluginUpdateState::FailedUpdate;
        syncPluginUpdateUi();
        return;
    }

    m_d->pluginUpdateState = Private::PluginUpdateState::Downloading;
    m_d->updateExtractPath.clear();

    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Krita-ComfyUIRemote/%1").arg(ComfyUIUtils::pluginVersion()));
    QNetworkReply *reply = m_d->nam->get(req);
    m_d->pluginUpdateDownloadReply = reply;

    auto shaCtx = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    connect(reply, &QNetworkReply::readyRead, this, [this, shaCtx]() {
        if (!m_d->pluginUpdateDownloadReply || !m_d->pluginUpdateSaveFile)
            return;
        const QByteArray chunk = m_d->pluginUpdateDownloadReply->readAll();
        shaCtx->addData(chunk);
        m_d->pluginUpdateSaveFile->write(chunk);
    });
    connect(reply, &QNetworkReply::finished, this, [this, shaCtx, reply]() {
        reply->deleteLater();
        m_d->pluginUpdateDownloadReply.clear();

        auto fail = [this]() {
            m_d->pluginUpdateSaveFile.reset();
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedUpdate;
            syncPluginUpdateUi();
        };

        if (reply->error() != QNetworkReply::NoError) {
            fail();
            return;
        }
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            shaCtx->addData(rest);
            if (m_d->pluginUpdateSaveFile)
                m_d->pluginUpdateSaveFile->write(rest);
        }
        if (!m_d->pluginUpdateSaveFile) {
            fail();
            return;
        }
        m_d->pluginUpdateSaveFile->flush();
        const QString gotHex = QString::fromLatin1(shaCtx->result().toHex());
        const QString expHex = comfyNormalizeSha256Hex(m_d->updatePackageSha256);
        if (expHex.size() != 64 || gotHex != expHex) {
            fail();
            return;
        }

        m_d->pluginUpdateState = Private::PluginUpdateState::Installing;
        syncPluginUpdateUi();

        const QString zipPath = m_d->pluginUpdateSaveFile->fileName();
        const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/comfyui_remote");
        QDir().mkpath(baseDir);
        const QString staging = baseDir + QStringLiteral("/update_unpack");
        QDir(staging).removeRecursively();
        QDir().mkpath(staging);

        QString extractErr;
        const bool extracted = ComfyUIUtils::extractZipToDirectory(zipPath, staging, &extractErr);
        if (extracted) {
            m_d->updateExtractPath = QDir(staging).absolutePath();
        } else {
            const QString zipOut = baseDir + QStringLiteral("/update_package.zip");
            QFile::remove(zipOut);
            if (QFile::copy(zipPath, zipOut))
                m_d->updateExtractPath = zipOut;
            else
                m_d->updateExtractPath = zipPath;
        }
        m_d->pluginUpdateSaveFile.reset();
        m_d->pluginUpdateState = Private::PluginUpdateState::RestartRequired;
        syncPluginUpdateUi();
    });

    syncPluginUpdateUi();
}
void ComfyUIRemoteDock::refreshPluginInformationTabUpdateUi()
{
    if (m_d->pluginTabLatestVersionLabel) {
        using PS = Private::PluginUpdateState;
        const QString cur = ComfyUIUtils::pluginVersion();
        if (m_d->updateCheckInProgress || m_d->pluginUpdateState == PS::Checking) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Latest version: %1", ComfyTr::tr("Checking...")));
        } else if (m_d->pluginUpdateState == PS::FailedCheck || m_d->pluginUpdateCheckHadFailure) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Latest version: %1", ComfyTr::tr("Update check failed")));
        } else if (m_d->pluginUpdateState == PS::Unknown && m_d->lastReportedLatestPluginVersion.isEmpty()) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Latest version: %1", ComfyTr::tr("Not checked")));
        } else if (m_d->pluginUpdateState == PS::Available) {
            m_d->pluginTabLatestVersionLabel->setText(
                ComfyTr::tr("Update available: %1 (current: %2)", m_d->updateRemoteVersion, cur));
        } else if (m_d->pluginUpdateState == PS::Downloading) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Downloading update…"));
        } else if (m_d->pluginUpdateState == PS::Installing) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Installing update…"));
        } else if (m_d->pluginUpdateState == PS::RestartRequired) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Update installed. Restart Krita to finish."));
        } else if (m_d->pluginUpdateState == PS::FailedUpdate) {
            m_d->pluginTabLatestVersionLabel->setText(ComfyTr::tr("Update download or verification failed."));
        } else {
            m_d->pluginTabLatestVersionLabel->setText(
                ComfyTr::tr("Latest version: %1", m_d->lastReportedLatestPluginVersion.isEmpty() ? cur : m_d->lastReportedLatestPluginVersion));
        }
    }
    if (m_d->pluginTabDownloadInstallButton) {
        const bool busy = static_cast<bool>(m_d->pluginUpdateDownloadReply);
        const bool canStart =
            !busy && (m_d->pluginUpdateState == Private::PluginUpdateState::Available
                      || m_d->pluginUpdateState == Private::PluginUpdateState::FailedUpdate)
            && !m_d->updateDownloadUrl.isEmpty();
        m_d->pluginTabDownloadInstallButton->setEnabled(canStart);
    }
}
void ComfyUIRemoteDock::startUpdateCheck(bool manualRequest)
{
    // §13.37 / §13.160: GET plugin/latest?version=current; newer version requires url + sha256
    if (!m_d->nam || m_d->updateCheckInProgress)
        return;
    if (!manualRequest) {
        if (m_d->updateCheckRequested)
            return;
        QJsonObject settings = ComfyUIUtils::loadSettingsJson();
        if (!settings.value(QStringLiteral("auto_update")).toBool(true)) {
            m_d->updateCheckRequested = true;
            return;
        }
        m_d->updateCheckRequested = true;
    }

    if (m_d->pluginUpdateDownloadReply) {
        m_d->pluginUpdateDownloadReply->disconnect(this);
        m_d->pluginUpdateDownloadReply->abort();
        m_d->pluginUpdateDownloadReply.clear();
    }
    m_d->pluginUpdateSaveFile.reset();
    m_d->updateDownloadUrl.clear();
    m_d->updatePackageSha256.clear();
    m_d->updateRemoteVersion.clear();
    m_d->updateExtractPath.clear();

    m_d->pluginUpdateState = Private::PluginUpdateState::Checking;
    m_d->updateCheckInProgress = true;
    m_d->pluginUpdateCheckHadFailure = false;
    syncPluginUpdateUi();

    const QString currentVer = ComfyUIUtils::pluginVersion();
    QString urlStr = ComfyUIUtils::intersticeApiBaseUrl();
    while (urlStr.endsWith(QLatin1Char('/')))
        urlStr.chop(1);
    urlStr += QStringLiteral("/plugin/latest");
    QUrl url(urlStr);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("version"), currentVer);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentVer]() {
        reply->deleteLater();
        m_d->updateCheckInProgress = false;
        if (reply->error() != QNetworkReply::NoError) {
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedCheck;
            m_d->pluginUpdateCheckHadFailure = true;
            syncPluginUpdateUi();
            return;
        }
        const QByteArray data = reply->readAll();
        QJsonParseError err;
        const QJsonObject obj = QJsonDocument::fromJson(data, &err).object();
        if (err.error != QJsonParseError::NoError || obj.isEmpty()) {
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedCheck;
            m_d->pluginUpdateCheckHadFailure = true;
            syncPluginUpdateUi();
            return;
        }
        QString latestVer = obj.value(QStringLiteral("version")).toString();
        if (latestVer.isEmpty())
            latestVer = currentVer;
        m_d->lastReportedLatestPluginVersion = latestVer;
        m_d->pluginUpdateCheckHadFailure = false;
        if (latestVer == currentVer) {
            m_d->pluginUpdateState = Private::PluginUpdateState::Latest;
            m_d->updateDownloadUrl.clear();
            m_d->updatePackageSha256.clear();
            m_d->updateRemoteVersion.clear();
            syncPluginUpdateUi();
            return;
        }
        const QString urlStr = obj.value(QStringLiteral("url")).toString().trimmed();
        const QString sha256 = obj.value(QStringLiteral("sha256")).toString().trimmed();
        if (urlStr.isEmpty() || sha256.isEmpty()) {
            m_d->pluginUpdateState = Private::PluginUpdateState::FailedCheck;
            m_d->pluginUpdateCheckHadFailure = true;
            m_d->updateDownloadUrl.clear();
            m_d->updatePackageSha256.clear();
            m_d->updateRemoteVersion.clear();
            syncPluginUpdateUi();
            return;
        }
        m_d->pluginUpdateState = Private::PluginUpdateState::Available;
        m_d->updateDownloadUrl = urlStr;
        m_d->updatePackageSha256 = sha256;
        m_d->updateRemoteVersion = latestVer;
        syncPluginUpdateUi();
    });
}
void ComfyUIRemoteDock::startNewsFetch()
{
    // §13.38: GET plugin news from API; digest = first 16 chars of SHA256(text); show NewsWidget when digest != last_news
    if (!m_d->nam || !m_d->welcomeNewsLabel) return;
    QString urlStr = ComfyUIUtils::intersticeApiBaseUrl();
    while (urlStr.endsWith(QLatin1Char('/')))
        urlStr.chop(1);
    urlStr += QStringLiteral("/plugin/news");
    QUrl url(urlStr);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_d->nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QByteArray data = reply->readAll();
        QJsonParseError err;
        QJsonObject obj = QJsonDocument::fromJson(data, &err).object();
        if (err.error != QJsonParseError::NoError || obj.isEmpty()) return;
        QString text = obj.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) return;
        QString digest = obj.value(QStringLiteral("digest")).toString();
        if (digest.isEmpty()) {
            QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
            digest = QString::fromLatin1(hash.toHex().left(16));
        }
        QString lastNews = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("last_news")).toString();
        if (digest == lastNews) return;
        m_d->hasUnseenNews = true;
        m_d->lastNewsDigest = digest;
        m_d->welcomeNewsLabel->setText(text);
        updateWelcomeVisibility();
    });
}
