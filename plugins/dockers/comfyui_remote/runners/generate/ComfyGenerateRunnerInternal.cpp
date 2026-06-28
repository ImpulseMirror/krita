/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyGenerateRunnerInternal.h"

#include "ComfyControlLayer.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyRegionProcess.h"
#include "ComfyStyleCollection.h"
#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyWorkflowEngine.h"

#include <QComboBox>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QVariant>

#include <KSharedConfig>

#include <kis_types.h>

namespace ComfyGenerateRunnerInternal {

QString encodeStyleIdFromCombo(const QComboBox *cb)
{
    if (!cb || cb->currentIndex() <= 0)
        return QStringLiteral("none");
    const QVariant data = cb->itemData(cb->currentIndex());
    if (data.isValid() && !data.toString().isEmpty())
        return data.toString();
    return QStringLiteral("custom:") + cb->currentText();
}

QString checkpointFromPrivate(const ComfyUIRemoteDock::Private *d)
{
    QString ckpt;
    if (d && d->generate.comboPreset && d->generate.comboPreset->currentIndex() > 0) {
        const int idx = d->generate.comboPreset->currentIndex();
        const QString styleId = encodeStyleIdFromCombo(d->generate.comboPreset);
        if (styleId.startsWith(QLatin1String("custom:"))) {
            const QString name = d->generate.comboPreset->itemText(idx);
            ckpt = KSharedConfig::openConfig()
                       ->group(QStringLiteral("ComfyUIRemote_Preset_") + name)
                       .readEntry(QStringLiteral("Checkpoint"), QString())
                       .trimmed();
        } else if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
            if (!st->checkpoints.isEmpty()) {
                QStringList available;
                if (d->generate.comboCheckpoint) {
                    for (int i = 0; i < d->generate.comboCheckpoint->count(); ++i)
                        available.append(d->generate.comboCheckpoint->itemText(i));
                }
                ckpt = ComfyFileLibrary::preferredCheckpoint(st->checkpoints, available);
                if (ckpt == QLatin1String("not-found"))
                    ckpt = st->checkpoints.first();
            }
        }
    }
    if (ckpt.isEmpty() && d && d->generate.comboCheckpoint)
        ckpt = d->generate.comboCheckpoint->currentText().trimmed();
    if (ckpt.isEmpty())
        ckpt = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    return ckpt;
}

QJsonArray styleLorasFromPrivate(const ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->generate.comboPreset || d->generate.comboPreset->currentIndex() <= 0)
        return {};
    if (const ComfyStyleEntry *st =
            ComfyStyleCollection::instance().findByStyleId(encodeStyleIdFromCombo(d->generate.comboPreset)))
        return st->loras;
    return {};
}

QStringList layerNamesFromRegionEntries(const QList<ComfyUIRemoteDock::Private::RegionEntry> &entries)
{
    QStringList names;
    for (const ComfyUIRemoteDock::Private::RegionEntry &e : entries) {
        if (e.maskSource.startsWith(QLatin1String("layer:")))
            names.append(e.maskSource.mid(6));
    }
    return names;
}

void clearBatchCaptureStash(ComfyUIRemoteDock::Private *d)
{
    if (!d)
        return;
    d->batchStashedRegionLayerNames.clear();
    d->batchStashedContextBounds = QRect();
    d->batchStashedHasMask = false;
}

void stashBatchCaptureMetadata(ComfyUIRemoteDock::Private *d)
{
    if (!d)
        return;
    const QStringList regionNames = layerNamesFromRegionEntries(comfyActiveRegionEntries(d));
    if (!regionNames.isEmpty())
        d->batchStashedRegionLayerNames = regionNames;
    else
        d->batchStashedRegionLayerNames.clear();
    if (d->generateRt.generateRefinePrepared.ok) {
        d->batchStashedContextBounds = d->generateRt.generateRefinePrepared.contextBounds;
        d->batchStashedHasMask = d->generateRt.generateRefinePrepared.hasMask;
    }
}

void applyHistoryCaptureStashToEntry(const ComfyUIRemoteDock::Private *d, ComfyUIRemoteDock::Private::HistoryEntry *entry)
{
    if (!d || !entry)
        return;
    if (!d->batchStashedRegionLayerNames.isEmpty())
        entry->regionLayerNames = d->batchStashedRegionLayerNames;
    if (d->batchStashedHasMask || d->batchStashedContextBounds.isValid()) {
        entry->contextBounds = d->batchStashedContextBounds;
        entry->hasMask = d->batchStashedHasMask;
    }
}

bool validateCustomWorkflowGraphOrShowError(ComfyUIRemoteDock *dock,
                                            ComfyUIRemoteDock::Private *d,
                                            const QJsonObject &workflow)
{
    const auto apiCheck = ComfyUIUtils::validateCustomWorkflowApiGraph(workflow, d->lastObjectInfoRoot);
    if (apiCheck.first)
        return true;
    dock->setStatusMessage(apiCheck.second, true);
    return false;
}

bool expandCustomKritaInjectionWorkflow(ComfyUIRemoteDock *dock,
                                        ComfyUIRemoteDock::Private *d,
                                        QJsonObject *workflow,
                                        QString *errorOut,
                                        ComfyUIUtils::CustomWorkflowExpandState *expandStateOut)
{
    if (!workflow || !ComfyUIUtils::workflowNeedsCustomKritaExpansion(*workflow))
        return true;

    const bool customGenerationModeLive =
        d->customGraphLiveActive || (d->checkCustomGraphLive && d->checkCustomGraphLive->isChecked());
    const bool excludeInternal = ComfyUIUtils::customWorkflowCaptureExcludesInternal(customGenerationModeLive);
    KisImageSP image = d->viewManager ? d->viewManager->image().toStrongRef() : KisImageSP();
    if (!image) {
        if (errorOut)
            *errorOut = ComfyTr::tr("Open a document first.");
        return false;
    }
    const QRect docBounds = image->bounds();
    const double strength = (d->generate.spinStrength ? d->generate.spinStrength->value() : 100) / 100.0;

    const bool needsCanvasCapture = ComfyUIUtils::workflowContainsKritaInjectionNodes(*workflow);
    ComfyUIUtils::CustomWorkflowKritaCapture capture;
    if (needsCanvasCapture) {
        capture = ComfyUIUtils::captureCustomWorkflowKritaInput(
            image, d->viewManager, *workflow, strength, excludeInternal, d->rootControlLayers, d->previewLayerId);
        if (!capture.ok) {
            if (errorOut)
                *errorOut = capture.errorMessage;
            return false;
        }
    } else {
        capture.captureBounds = docBounds;
        capture.ok = true;
    }

    const qint64 seed = d->generate.checkFixedSeed && d->generate.checkFixedSeed->isChecked()
        ? static_cast<qint64>(d->generate.spinSeed->value())
        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint32>(1u << 31)));
    if (!d->generate.checkFixedSeed || !d->generate.checkFixedSeed->isChecked())
        d->generate.spinSeed->setValue(static_cast<int>(seed));

    const QString baseUrl = d->editServerUrl ? d->editServerUrl->text().trimmed() : QString();
    QString uploadErr;

    QString canvasName;
    if (!ComfyUIUtils::findFirstWorkflowNodeIdByClassType(*workflow, QStringLiteral("ETN_KritaCanvas")).isEmpty()) {
        canvasName = ComfyUIUtils::uploadImageToComfySync(d->nam, baseUrl, capture.canvasImage,
                                                          QStringLiteral("krita_custom_canvas.png"), &uploadErr);
        if (canvasName.isEmpty()) {
            if (errorOut)
                *errorOut = uploadErr.isEmpty() ? ComfyTr::tr("Could not upload canvas for custom workflow.") : uploadErr;
            return false;
        }
    }

    QString maskName;
    if (capture.hasSelectionMask && !capture.maskImage.isNull()) {
        const QImage maskPng = ComfyUIUtils::maskPngForComfyUpload(capture.maskImage);
        maskName = ComfyUIUtils::uploadImageToComfySync(d->nam, baseUrl, maskPng,
                                                        QStringLiteral("krita_custom_mask.png"), &uploadErr);
        if (maskName.isEmpty()) {
            if (errorOut)
                *errorOut = uploadErr.isEmpty() ? ComfyTr::tr("Could not upload mask for custom workflow.") : uploadErr;
            return false;
        }
    }

    QHash<QString, QString> layerUploads;
    const QRect &exportBounds = capture.captureBounds;
    for (auto it = workflow->constBegin(); it != workflow->constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject node = it.value().toObject();
        const QString classType = node.value(QStringLiteral("class_type")).toString();
        if (classType != QLatin1String("ETN_KritaImageLayer") && classType != QLatin1String("ETN_KritaMaskLayer"))
            continue;
        const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        QString paramName = inputs.value(QStringLiteral("name")).toString();
        if (paramName.isEmpty()) {
            paramName = classType == QLatin1String("ETN_KritaMaskLayer") ? QStringLiteral("Mask")
                                                                         : QStringLiteral("Image");
        }
        const bool maskLayerNode = classType == QLatin1String("ETN_KritaMaskLayer");
        const QString layerName = ComfyUIUtils::resolveCustomWorkflowLayerName(
            image, it.key(), paramName, inputs.value(QStringLiteral("name")).toString(),
            d->customWorkflowParamOverrides, maskLayerNode);
        if (layerName.isEmpty()) {
            if (errorOut)
                *errorOut = ComfyTr::tr("Custom workflow layer node \"%1\" could not be resolved.").arg(paramName);
            return false;
        }
        const QImage layerImg = ComfyUIUtils::exportCustomWorkflowLayerImage(
            image, d->viewManager, layerName, exportBounds, maskLayerNode);
        if (layerImg.isNull()) {
            if (errorOut)
                *errorOut = ComfyTr::tr("Could not export layer \"%1\" for custom workflow.").arg(layerName);
            return false;
        }
        const QString uploaded = ComfyUIUtils::uploadImageToComfySync(
            d->nam, baseUrl, layerImg, QStringLiteral("krita_custom_layer_%1.png").arg(it.key()), &uploadErr);
        if (uploaded.isEmpty()) {
            if (errorOut)
                *errorOut = uploadErr.isEmpty()
                    ? ComfyTr::tr("Could not upload layer \"%1\" for custom workflow.").arg(layerName)
                    : uploadErr;
            return false;
        }
        layerUploads.insert(it.key(), uploaded);
    }

    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams exp;
    exp.workflow = *workflow;
    exp.canvasImageName = canvasName;
    exp.maskImageName = maskName;
    exp.captureBounds = capture.captureBounds;
    exp.hasSelectionMask = capture.hasSelectionMask;
    exp.seed = seed;
    exp.layerUploadByNodeId = layerUploads;

    const QJsonObject settingsRoot = ComfyUIUtils::loadSettingsJson();
    const QString dockSampler = d->generate.comboSampler && !d->generate.comboSampler->currentText().trimmed().isEmpty()
        ? d->generate.comboSampler->currentText().trimmed()
        : QStringLiteral("euler");
    const int dockSteps = d->generate.spinSteps ? d->generate.spinSteps->value() : 20;
    const double dockCfg = d->generate.spinCfg ? d->generate.spinCfg->value() : 7.0;

    const ComfyStyleEntry *dockStyle = nullptr;
    QString dockStyleId;
    if (d->generate.comboPreset && d->generate.comboPreset->currentIndex() > 0) {
        dockStyleId = encodeStyleIdFromCombo(d->generate.comboPreset);
        dockStyle = ComfyStyleCollection::instance().findByStyleId(dockStyleId);
    }

    auto resolveStyleIdForNode = [&](const QString &paramName) -> QString {
        const QVariant overrideVal = d->customWorkflowParamOverrides.value(paramName);
        const QString overrideStr = overrideVal.toString().trimmed();
        if (!overrideStr.isEmpty() && overrideStr != QLatin1String("auto") && overrideStr != QLatin1String("regular")
            && overrideStr != QLatin1String("live")
            && ComfyStyleCollection::instance().findByStyleId(overrideStr))
            return overrideStr;
        return dockStyleId;
    };

    for (auto it = workflow->constBegin(); it != workflow->constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject node = it.value().toObject();
        const QString classType = node.value(QStringLiteral("class_type")).toString();
        const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        if (classType == QLatin1String("ETN_KritaStyle")) {
            QString paramName = inputs.value(QStringLiteral("name")).toString();
            if (paramName.isEmpty())
                paramName = QStringLiteral("Style");
            const QString styleId = resolveStyleIdForNode(paramName);
            const ComfyStyleEntry *styleEntry = ComfyStyleCollection::instance().findByStyleId(styleId);
            const QString samplerPreset = inputs.value(QStringLiteral("sampler_preset")).toString(QStringLiteral("auto"));
            const ComfyUIUtils::CustomWorkflowStyleBundle bundle = ComfyUIUtils::resolveCustomWorkflowStyleBundle(
                styleEntry, settingsRoot, dockSampler, dockSteps, dockCfg, samplerPreset, customGenerationModeLive);
            if (!bundle.ok) {
                if (errorOut)
                    *errorOut = bundle.errorMessage;
                return false;
            }
            ComfyWorkflowEngine::CustomWorkflowStyleExpandInput styleIn;
            styleIn.checkpoint = bundle.checkpoint;
            styleIn.loras = ComfyWorkflowEngine::checkpointLorasFromStyle(bundle.styleLoras);
            styleIn.positivePrompt = bundle.positivePrompt;
            styleIn.negativePrompt = bundle.negativePrompt;
            styleIn.sampler = bundle.sampler;
            styleIn.scheduler = bundle.scheduler;
            styleIn.steps = bundle.steps;
            styleIn.cfg = bundle.cfg;
            exp.kritaStyleByNodeId.insert(it.key(), styleIn);
        }
    }

    const QString sapNodeId =
        ComfyUIUtils::findFirstWorkflowNodeIdByClassType(*workflow, QStringLiteral("ETN_KritaStyleAndPrompt"));
    if (!sapNodeId.isEmpty() && !dockStyle) {
        if (errorOut)
            *errorOut = ComfyTr::tr("Custom workflow requires a style when using Krita Style & Prompt.");
        return false;
    }
    if (!sapNodeId.isEmpty()) {
        const QJsonObject sapInputs =
            workflow->value(sapNodeId).toObject().value(QStringLiteral("inputs")).toObject();
        const QString samplerPreset = sapInputs.value(QStringLiteral("sampler_preset")).toString(QStringLiteral("auto"));
        const ComfyUIUtils::CustomWorkflowStyleBundle bundle = ComfyUIUtils::resolveCustomWorkflowStyleBundle(
            dockStyle, settingsRoot, dockSampler, dockSteps, dockCfg, samplerPreset, customGenerationModeLive);
        if (!bundle.ok) {
            if (errorOut)
                *errorOut = bundle.errorMessage;
            return false;
        }
        exp.checkpoint = bundle.checkpoint;
        exp.arch = ComfyWorkflowEngine::resolveArch(bundle.checkpoint, dockStyle ? dockStyle->architecture : QString());
        exp.loras = ComfyWorkflowEngine::checkpointLorasFromStyle(bundle.styleLoras);
        exp.sampler = bundle.sampler;
        exp.scheduler = bundle.scheduler;
        exp.steps = bundle.steps;
        exp.cfg = bundle.cfg;
    } else {
        const QString ckpt = checkpointFromPrivate(d);
        QString styleArch;
        if (dockStyle)
            styleArch = dockStyle->architecture;
        exp.checkpoint = ckpt;
        exp.arch = ComfyWorkflowEngine::resolveArch(ckpt, dockStyle ? dockStyle->architecture : QString());
        exp.loras = ComfyWorkflowEngine::checkpointLorasFromStyle(styleLorasFromPrivate(d));
        exp.sampler = dockSampler;
        exp.scheduler = d->generateRt.ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : d->generateRt.ksamplerScheduler;
        exp.steps = dockSteps;
        exp.cfg = dockCfg;
    }

    QString userPos = ComfyUIUtils::stripPromptComments(d->generate.editPrompt->toPlainText()).trimmed();
    QString userNeg = ComfyUIUtils::stripPromptComments(d->generate.editNegative->toPlainText()).trimmed();
    ComfyUIUtils::CustomWorkflowEvaluatedPrompts evaluated;
    bool hasEvaluatedPrompts = false;
    if (!sapNodeId.isEmpty() && dockStyle) {
        evaluated = ComfyUIUtils::prepareCustomWorkflowStyleAndPrompts(
            userPos, userNeg, dockStyle, seed, exp.cfg, ComfyUIUtils::activePromptTranslationLanguage(), exp.arch);
        hasEvaluatedPrompts = true;
        if (!evaluated.ok) {
            if (errorOut)
                *errorOut = evaluated.errorMessage;
            return false;
        }
        exp.positivePrompt = evaluated.positiveFinal;
        exp.negativePrompt = evaluated.negativeFinal;
        QList<ComfyWorkflowEngine::CheckpointLoraWeight> promptLoras;
        promptLoras.reserve(evaluated.promptLoras.size());
        for (const ComfyUIUtils::ExtractedPromptLora &pl : evaluated.promptLoras) {
            ComfyWorkflowEngine::CheckpointLoraWeight w;
            w.name = pl.name;
            w.strengthModel = pl.strength;
            w.strengthClip = pl.strength;
            promptLoras.append(w);
        }
        exp.loras = ComfyWorkflowEngine::mergeCheckpointLorasUnique(exp.loras, promptLoras);
    } else {
        QString pos = userPos;
        pos = ComfyUIUtils::evalWildcards(pos, static_cast<quint32>(seed & 0xFFFFFFFFu));
        exp.positivePrompt =
            ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(pos, styleLorasFromPrivate(d));
        exp.negativePrompt = ComfyUIUtils::evalWildcards(userNeg, static_cast<quint32>(seed & 0xFFFFFFFFu));
    }

    *workflow = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(exp);

    if (expandStateOut) {
        QJsonArray loraMeta;
        for (const ComfyWorkflowEngine::CheckpointLoraWeight &lw : exp.loras) {
            loraMeta.append(QJsonObject{{QStringLiteral("name"), lw.name},
                                        {QStringLiteral("weight"), lw.strengthModel}});
        }
        expandStateOut->captureBounds = capture.captureBounds;
        expandStateOut->hasSelectionMask = capture.hasSelectionMask;
        expandStateOut->promptMetadata = hasEvaluatedPrompts ? evaluated.metadata : QJsonObject();
        if (!loraMeta.isEmpty())
            expandStateOut->promptMetadata.insert(QStringLiteral("loras"), loraMeta);
        expandStateOut->inputFingerprint = ComfyUIUtils::computeCustomWorkflowInputFingerprint(
            exp.workflow, capture, seed, exp.positivePrompt, exp.negativePrompt, loraMeta,
            d->customWorkflowParamOverrides);
    }
    return true;
}

QList<ComfyUIRemoteDock::Private::RegionEntry> regionsForGenerate(const ComfyUIRemoteDock::Private *d)
{
    QList<ComfyUIRemoteDock::Private::RegionEntry> regs = comfyActiveRegionEntries(d);
    if (d->generate.checkRegionOnly && d->generate.checkRegionOnly->isChecked()) {
        const int row = comfyActiveRegionRow(d);
        if (row >= 0 && row < regs.size())
            return {regs.at(row)};
    }
    return regs;
}

QList<ComfyControlLayerEntry> controlLayersForGenerate(const ComfyUIRemoteDock::Private *d)
{
    return mergedJobControlLayers(d->rootControlLayers, regionsForGenerate(d));
}

int takeGenerateQueueMode(ComfyUIRemoteDock::Private *d)
{
    if (d->generateRt.generateOneShotQueueMode >= 0) {
        const int mode = d->generateRt.generateOneShotQueueMode;
        d->generateRt.generateOneShotQueueMode = -1;
        return mode;
    }
    return d->generate.comboQueueMode ? d->generate.comboQueueMode->currentData().toInt() : 0;
}

QImage maskPngForComfyUpload(const QImage &maskGray)
{
    return ComfyUIUtils::maskPngForComfyUpload(maskGray);
}

QString samplerModelNodeId(const QJsonObject &workflow)
{
    const QJsonArray refineLatent = workflow.value(QStringLiteral("6"))
                                        .toObject()
                                        .value(QStringLiteral("inputs"))
                                        .toObject()
                                        .value(QStringLiteral("latent_image"))
                                        .toArray();
    if (refineLatent.size() >= 1 && refineLatent.at(0).toString() == QLatin1String("2"))
        return QStringLiteral("3");

    const QJsonArray model = workflow.value(QStringLiteral("3"))
                               .toObject()
                               .value(QStringLiteral("inputs"))
                               .toObject()
                               .value(QStringLiteral("model"))
                               .toArray();
    if (model.size() >= 1)
        return model.at(0).toString();
    return QStringLiteral("4");
}

QString samplerPositiveNodeId(const QJsonObject &workflow)
{
    const QJsonArray pos = workflow.value(QStringLiteral("3"))
                               .toObject()
                               .value(QStringLiteral("inputs"))
                               .toObject()
                               .value(QStringLiteral("positive"))
                               .toArray();
    if (pos.size() >= 1)
        return pos.at(0).toString();
    return QStringLiteral("6");
}

ComfyWorkflowEngine::AnimationFrameParams animationFrameParamsFromDock(const ComfyUIRemoteDock::Private *d,
                                                                       const QString &checkpoint,
                                                                       int frameIndex,
                                                                       qint64 batchBaseSeed,
                                                                       int batchSeedStep,
                                                                       const QString &styleArch,
                                                                       const QJsonArray &styleLoras)
{
    ComfyWorkflowEngine::AnimationFrameParams af;
    const QString ckpt = checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors")
                                                        : checkpoint.trimmed();
    af.base.arch = ComfyWorkflowEngine::resolveArch(ckpt, styleArch);
    af.base.checkpoint = ckpt;
    const qint64 seed = ComfyWorkflowEngine::animationFrameSeed(batchBaseSeed, frameIndex, batchSeedStep);
    QString promptText = ComfyUIUtils::stripPromptComments(d->generate.editPrompt->toPlainText()).trimmed();
    promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
    ComfyUIUtils::extractLayerPlaceholders(promptText);
    af.base.positivePrompt =
        ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(promptText, styleLoras);
    af.base.styleLoras = styleLoras;
    af.base.negativePrompt = ComfyUIUtils::evalWildcards(
        ComfyUIUtils::stripPromptComments(d->generate.editNegative->toPlainText()).trimmed(), static_cast<quint32>(seed & 0xFFFFFFFFu));
    af.base.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
    af.base.denoise = (d->generate.spinStrength ? d->generate.spinStrength->value() : 100) / 100.0;
    af.base.sampler = d->generate.comboSampler->currentText().trimmed().isEmpty() ? QStringLiteral("euler")
                                                                         : d->generate.comboSampler->currentText().trimmed();
    af.base.scheduler = d->generateRt.ksamplerScheduler.isEmpty() ? QStringLiteral("normal") : d->generateRt.ksamplerScheduler;
    af.base.steps = d->generate.spinSteps->value();
    af.base.cfg = d->generate.spinCfg->value();
    const bool animationFastSampling =
        d->comboWorkspace && d->comboWorkspace->currentIndex() == 3 && d->generate.comboQuality
        && d->generate.comboQuality->currentIndex() == 0;
    if (animationFastSampling) {
        QString styleId;
        if (d->generate.comboPreset && d->generate.comboPreset->currentIndex() > 0)
            styleId = d->generate.comboPreset->itemData(d->generate.comboPreset->currentIndex()).toString();
        const ComfyStyleEntry *styleEntry = ComfyStyleCollection::instance().findByStyleId(styleId);
        const ComfyUIUtils::ResolvedSamplerInputs si = ComfyUIUtils::resolveSamplerForLive(
            styleEntry,
            ComfyUIUtils::loadSettingsJson(),
            d->generate.comboSampler ? d->generate.comboSampler->currentText() : QString(),
            d->generate.spinSteps ? d->generate.spinSteps->value() : 20,
            d->generate.spinCfg ? d->generate.spinCfg->value() : 8.0);
        af.base.sampler = si.sampler;
        af.base.scheduler = si.scheduler;
        af.base.steps = si.steps;
        af.base.cfg = si.cfg;
    }
    int w = d->generate.spinWidth->value();
    int h = d->generate.spinHeight->value();
    int genBatchTmp = d->generate.spinBatchCount ? d->generate.spinBatchCount->value() : 1;
    double genMul2 = d->generate.resolutionMultiplier <= 0.0 ? 1.0 : d->generate.resolutionMultiplier;
    const QJsonObject settingsRootBatch = ComfyUIUtils::loadSettingsJson();
    ComfyUIUtils::generationPerformanceBatchResolution(settingsRootBatch, d->lastComfySystemStats, genBatchTmp, genMul2,
                                                       &genBatchTmp, &genMul2);
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(settingsRootBatch, &genMul2);
    const double bmul = qMax(0.3, qMin(genMul2 <= 0.0 ? 1.0 : genMul2, 3.0));
    w = qBound(64, static_cast<int>(w * bmul), 8192);
    h = qBound(64, static_cast<int>(h * bmul), 8192);
    ComfyUIUtils::clampExtentToMaxMegapixels(&w, &h);
    af.base.width = w;
    af.base.height = h;
    af.base.batchSize = 1;
    af.batchBaseSeed = batchBaseSeed;
    af.frameIndex = frameIndex;
    af.batchSeedStep = batchSeedStep;
    return af;
}

bool animationRequiresCanvasImage(const ComfyUIRemoteDock::Private *d)
{
    if (!d)
        return false;
    const int strengthPct = d->generate.spinStrength ? d->generate.spinStrength->value() : 100;
    const bool editMode = d->generate.checkEditMode && d->generate.checkEditMode->isChecked();
    return strengthPct < 100 || editMode;
}

ComfyWorkflowEngine::RefineParams animationRefineParamsFromDock(const ComfyUIRemoteDock::Private *d,
                                                                const QString &checkpoint,
                                                                int frameIndex,
                                                                qint64 batchBaseSeed,
                                                                int batchSeedStep,
                                                                const QString &styleArch,
                                                                const QJsonArray &styleLoras,
                                                                const QString &imageName)
{
    ComfyWorkflowEngine::RefineParams rp;
    const bool editMode = d->generate.checkEditMode && d->generate.checkEditMode->isChecked();
    const double strength0to1 = (d->generate.spinStrength ? d->generate.spinStrength->value() : 100) / 100.0;
    QString linkedEditStyleId;
    const ComfyStyleEntry *styleEntry = nullptr;
    if (d->generate.comboPreset && d->generate.comboPreset->currentIndex() > 0) {
        const QString styleId = d->generate.comboPreset->itemData(d->generate.comboPreset->currentIndex()).toString();
        if (const ComfyStyleEntry *st = ComfyStyleCollection::instance().findByStyleId(styleId)) {
            styleEntry = st;
            linkedEditStyleId = st->linkedEditStyle;
        }
    }
    const ComfyUIUtils::LinkedEditStyleOverride link = ComfyUIUtils::linkedEditStyleOverride(
        editMode,
        linkedEditStyleId,
        checkpoint.trimmed().isEmpty() ? QStringLiteral("v1-5-pruned-emaonly.safetensors") : checkpoint.trimmed(),
        d->generate.spinSteps->value(),
        d->generate.spinCfg->value(),
        strength0to1,
        d->generate.comboSampler->currentText().trimmed(),
        d->generateRt.ksamplerScheduler);

    rp.checkpoint = link.checkpoint;
    rp.imageName = imageName;
    rp.arch = ComfyWorkflowEngine::resolveArch(link.checkpoint, styleArch);
    const qint64 seed = ComfyWorkflowEngine::animationFrameSeed(batchBaseSeed, frameIndex, batchSeedStep);
    rp.seed = seed;
    rp.steps = link.steps;
    rp.cfg = link.cfg;
    rp.denoise = link.denoise;
    rp.sampler = link.sampler.isEmpty() ? QStringLiteral("euler") : link.sampler;
    rp.scheduler = link.scheduler.isEmpty() ? QStringLiteral("normal") : link.scheduler;

    const bool animationFastSampling =
        d->comboWorkspace && d->comboWorkspace->currentIndex() == 3 && d->generate.comboQuality
        && d->generate.comboQuality->currentIndex() == 0;
    if (animationFastSampling) {
        const ComfyUIUtils::ResolvedSamplerInputs si = ComfyUIUtils::resolveSamplerForLive(
            styleEntry,
            ComfyUIUtils::loadSettingsJson(),
            d->generate.comboSampler ? d->generate.comboSampler->currentText() : QString(),
            d->generate.spinSteps ? d->generate.spinSteps->value() : 20,
            d->generate.spinCfg ? d->generate.spinCfg->value() : 8.0);
        rp.sampler = si.sampler;
        rp.scheduler = si.scheduler;
        rp.steps = si.steps;
        rp.cfg = si.cfg;
    }

    ComfyUIUtils::applyStrengthResolvedSamplingToRefine(
        &rp,
        styleEntry,
        ComfyUIUtils::loadSettingsJson(),
        rp.sampler,
        rp.steps,
        rp.cfg,
        strength0to1);

    QString userPos = ComfyUIUtils::stripPromptComments(d->generate.editPrompt->toPlainText()).trimmed();
    QString promptText = link.active
        ? ComfyUIUtils::mergeStylePromptWithInstruction(link.stylePositiveTemplate, userPos).trimmed()
        : userPos;
    promptText = ComfyUIUtils::evalWildcards(promptText, static_cast<quint32>(seed & 0xFFFFFFFFu));
    ComfyUIUtils::extractLayerPlaceholders(promptText);
    rp.styleLoras = styleLoras;
    rp.positivePrompt =
        ComfyUIUtils::mergeStyleLoraTriggersIntoPositivePrompt(promptText, styleLoras);
    const QString negSrc =
        link.active ? link.styleNegative : ComfyUIUtils::stripPromptComments(d->generate.editNegative->toPlainText()).trimmed();
    rp.negativePrompt = ComfyUIUtils::evalWildcards(negSrc, static_cast<quint32>(seed & 0xFFFFFFFFu));
    rp.promptTranslationLanguage = ComfyUIUtils::activePromptTranslationLanguage();
    rp.nsfwFilterSensitivity = ComfyUIUtils::settingsNsfwFilterSensitivity();
    return rp;
}

void clearAnimationBatchState(ComfyUIRemoteDock::Private *d)
{
    if (!d)
        return;
    d->batchCountTarget = 0;
    d->isFullAnimationBatch = false;
    d->animationBatchPromptIdToIndex.clear();
    d->animationBatchSourcePathByFrame.clear();
    d->animationBatchFrameTimes.clear();
    d->animationBatchGroupId.clear();
    d->batchNeedsPerFrameReference = false;
    d->batchNeedsPerFrameAnimationRefine = false;
    clearBatchCaptureStash(d);
}

} // namespace ComfyGenerateRunnerInternal
