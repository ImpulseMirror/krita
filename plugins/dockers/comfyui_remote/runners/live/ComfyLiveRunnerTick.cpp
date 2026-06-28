/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLiveRunner.h"
#include "ComfyLiveRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyPollRunnerCommon.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyPromptClient.h"
#include "ComfyRegionProcess.h"
#include "ComfyResources.h"
#include "ComfyStyleCollection.h"
#include "ComfyUploadPipeline.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRandomGenerator>
#include <QRect>
#include <QTemporaryFile>
#include <QUrl>
#include <QUuid>

#include <kis_image.h>
#include <kis_image_manager.h>


using namespace ComfyLiveRunnerInternal;

namespace ComfyLiveRunner {

void onTick(ComfyUIRemoteDock *dock)
{

    if (dock->m_d->liveRt.liveAwaitingLoraUploads || !dock->m_d->live.checkLiveMode->isChecked() || !dock->m_d->viewManager
        || !dock->m_d->viewManager->image())
        return;
    KisImageSP image = dock->m_d->viewManager->image();
    // §13.42: Block generation if document color mode is not RGBA 8-bit
    auto colorCheck = ComfyUIUtils::checkColorMode(image);
    if (!colorCheck.first) {
        dock->setStatusMessage(colorCheck.second, true);
        dock->m_d->liveRt.liveTimer->start(30000);
        return;
    }
    QString urlStr = dock->m_d->editServerUrl->text().trimmed();
    if (urlStr.isEmpty()) {
        dock->m_d->liveRt.liveTimer->start(30000);
        return;
    }
    beginUploadPipeline(dock);

}

} // namespace ComfyLiveRunner
