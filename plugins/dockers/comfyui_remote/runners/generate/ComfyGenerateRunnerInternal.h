/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyWorkflowEngine.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class ComfyUIRemoteDock;

namespace ComfyUIUtils {
struct CustomWorkflowExpandState;
}

namespace ComfyGenerateRunnerInternal {

void clearBatchCaptureStash(ComfyUIRemoteDock::Private *d);
void stashBatchCaptureMetadata(ComfyUIRemoteDock::Private *d);
void applyHistoryCaptureStashToEntry(const ComfyUIRemoteDock::Private *d,
                                     ComfyUIRemoteDock::Private::HistoryEntry *entry);
bool validateCustomWorkflowGraphOrShowError(ComfyUIRemoteDock *dock,
                                            ComfyUIRemoteDock::Private *d,
                                            const QJsonObject &workflow);
bool expandCustomKritaInjectionWorkflow(ComfyUIRemoteDock *dock,
                                        ComfyUIRemoteDock::Private *d,
                                        QJsonObject *workflow,
                                        QString *errorOut,
                                        ComfyUIUtils::CustomWorkflowExpandState *expandStateOut = nullptr);
bool animationRequiresCanvasImage(const ComfyUIRemoteDock::Private *d);
void clearAnimationBatchState(ComfyUIRemoteDock::Private *d);
QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForGenerate(const ComfyUIRemoteDock::Private *d);
QList<ComfyControlLayerEntry> controlLayersForGenerate(const ComfyUIRemoteDock::Private *d);
int takeGenerateQueueMode(ComfyUIRemoteDock::Private *d);
ComfyWorkflowEngine::AnimationFrameParams animationFrameParamsFromDock(const ComfyUIRemoteDock::Private *d,
                                                                       const QString &checkpoint,
                                                                       int frameIndex,
                                                                       qint64 batchBaseSeed,
                                                                       int batchSeedStep,
                                                                       const QString &styleArch,
                                                                       const QJsonArray &styleLoras);
ComfyWorkflowEngine::RefineParams animationRefineParamsFromDock(const ComfyUIRemoteDock::Private *d,
                                                                const QString &checkpoint,
                                                                int frameIndex,
                                                                qint64 batchBaseSeed,
                                                                int batchSeedStep,
                                                                const QString &styleArch,
                                                                const QJsonArray &styleLoras,
                                                                const QString &imageName);

} // namespace ComfyGenerateRunnerInternal
