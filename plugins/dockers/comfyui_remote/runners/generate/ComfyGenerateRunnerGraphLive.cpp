/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyGenerateRunner.h"
#include "ComfyGenerateRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMap>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRandomGenerator>
#include <QSharedPointer>
#include <QTimer>
#include <QUuid>
#include <QTemporaryFile>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>

#include <algorithm>

#include <KSharedConfig>

#include <KoUpdater.h>
#include <kis_animation_importer.h>
#include <kis_image.h>
#include <kis_image_animation_interface.h>
#include <kis_node.h>
#include <kis_selection.h>
#include <kis_time_span.h>
#include <commands/KisNodeRenameCommand.h>
#include <KisDocument.h>
#include <KisViewManager.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyGenerateRunner {

using namespace ComfyGenerateRunnerInternal;

void maybeContinueCustomGraphLive(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->customGraphLiveActive)
        return;
    if (dock->m_d->checkCustomGraphLive && !dock->m_d->checkCustomGraphLive->isChecked()) {
        dock->m_d->customGraphLiveActive = false;
        return;
    }
    if (!dock->m_d->comboWorkspace || dock->m_d->comboWorkspace->currentIndex() != 4)
        return;
    if (!dock->m_d->editCustomWorkflow || dock->m_d->editCustomWorkflow->toPlainText().trimmed().isEmpty())
        return;
    if (!dock->m_d->currentPromptId.isEmpty() || !dock->m_d->jobQueue.isEmpty())
        return;
    if (!dock->m_d->customGraphLiveTimer) {
        dock->m_d->customGraphLiveTimer = new QTimer(dock);
        dock->m_d->customGraphLiveTimer->setSingleShot(true);
        QObject::connect(dock->m_d->customGraphLiveTimer, &QTimer::timeout, dock, &ComfyUIRemoteDock::slotCustomGraphLiveResubmit);
    }
    dock->m_d->customGraphLiveTimer->start(100);

}
void onCustomGraphLiveResubmit(ComfyUIRemoteDock *dock)
{

    if (!dock->m_d->customGraphLiveActive) {
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }
    if (dock->m_d->checkCustomGraphLive && !dock->m_d->checkCustomGraphLive->isChecked()) {
        dock->m_d->customGraphLiveActive = false;
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }
    if (!dock->m_d->viewManager || !dock->m_d->viewManager->image()) {
        dock->m_d->customGraphLiveActive = false;
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }
    const QString customJson = dock->m_d->editCustomWorkflow ? dock->m_d->editCustomWorkflow->toPlainText().trimmed() : QString();
    if (customJson.isEmpty())
        return;
    QJsonParseError err;
    const QByteArray jsonBytes = ComfyUIUtils::stripJsonLineComments(customJson.toUtf8());
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    QJsonObject workflow = doc.object();
    if (!dock->tryResolveCustomWorkflowInPlace(&workflow)) {
        dock->m_d->customGraphLiveActive = false;
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }
    const auto validation = ComfyUIUtils::validateCustomWorkflowStyleAndPromptNodes(workflow);
    if (!validation.first) {
        dock->setStatusMessage(validation.second, true);
        dock->m_d->customGraphLiveActive = false;
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }
    if (!validateCustomWorkflowGraphOrShowError(dock, dock->m_d.data(), workflow)) {
        dock->m_d->customGraphLiveActive = false;
        if (dock->m_d->generate.btnGenerate)
            dock->m_d->generate.btnGenerate->setEnabled(true);
        return;
    }
    KisImageSP wfImage = dock->m_d->viewManager->image().toStrongRef();
    ComfyUIUtils::applyCustomWorkflowParameterValues(workflow, dock->m_d->customWorkflowParamOverrides, wfImage);
    dock->m_d->batchUseCustomWorkflow = true;
    dock->m_d->batchCustomWorkflow = workflow;
    dock->m_d->batchNeedsPerFrameReference = false;
    dock->m_d->batchSubmitIndex = 0;
    dock->m_d->batchCountTarget = 1;
    if (!dock->m_d->generate.checkFixedSeed || !dock->m_d->generate.checkFixedSeed->isChecked()) {
        const qint64 newSeed = static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
        dock->m_d->batchBaseSeed = newSeed;
        if (dock->m_d->generate.spinSeed)
            dock->m_d->generate.spinSeed->setValue(static_cast<int>(newSeed));
    }
    if (dock->m_d->generate.btnGenerate)
        dock->m_d->generate.btnGenerate->setEnabled(false);
    dispatchBatchPromptRequest(dock, workflow, 0);

}

} // namespace ComfyGenerateRunner
