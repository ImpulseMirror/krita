/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyConnectionInternal.h"
#include "ComfyLiveRunner.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QJsonObject>
#include <QJsonValue>
#include <QSignalBlocker>

#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
#include <QWebSocket>
#endif

using ComfyConnectionInternal::syncDetectedModelsLabel;

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

void ComfyUIRemoteDock::refreshConnectionTabUi()
{
    refreshConnectionActionButton();
    if (!m_d->labelConnectionStatus && !m_d->labelDetectedModels)
        return;

    if (m_d->isConnected) {
        if (m_d->labelConnectionStatus) {
            m_d->labelConnectionStatus->setText(ComfyTr::tr("Connected"));
            ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Success);
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
            ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
        }
        return;
    }

    if (m_d->labelConnectionStatus) {
        m_d->labelConnectionStatus->setText(ComfyTr::tr("Disconnected"));
        ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
    }
    syncDetectedModelsLabel(m_d.data());
}

void ComfyUIRemoteDock::slotDisconnect()
{
    ++m_d->connectionSessionId;
    cancelConnectionAutostartRetry();
    m_d->connectionAutostartActive = false;
    m_d->connectionAutostartRetryAttempt = 0;
    if (m_d->pollTimer)
        m_d->pollTimer->stop();
    if (m_d->inpaintRt.inpaintPollTimer)
        m_d->inpaintRt.inpaintPollTimer->stop();
    if (m_d->upscaleRt.upscalePollTimer)
        m_d->upscaleRt.upscalePollTimer->stop();
    ComfyLiveRunner::stopLivePollLoop(this);
    if (m_d->liveRt.livePollTimer)
        m_d->liveRt.livePollTimer->stop();
    if (m_d->generateRt.controlPreviewPollTimer)
        m_d->generateRt.controlPreviewPollTimer->stop();
    if (m_d->generateRt.controlLayerJobPollTimer)
        m_d->generateRt.controlLayerJobPollTimer->stop();
#if defined(COMFYUI_HAVE_QT_WEBSOCKETS)
    if (m_d->generateRt.controlLayerJobWebSocket) {
        m_d->generateRt.controlLayerJobWebSocket->close();
        m_d->generateRt.controlLayerJobWebSocket->deleteLater();
        m_d->generateRt.controlLayerJobWebSocket = nullptr;
    }
#endif
    m_d->generateRt.controlLayerJobOpenPoseJson = QJsonValue();

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
        ComfyUiStyle::styleStatusLabel(m_d->labelConnectionStatus, ComfyUiStyle::StatusTone::Neutral);
    }
    if (m_d->labelDetectedModels) {
        m_d->labelDetectedModels->setText(ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        ComfyUiStyle::styleCaption(m_d->labelDetectedModels);
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
