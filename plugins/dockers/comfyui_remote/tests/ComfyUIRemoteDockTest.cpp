/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QTest>
#include <QSignalSpy>

#include <KSharedConfig>
#include <KConfigGroup>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QGuiApplication>
#include <QPalette>
#include <QFile>
#include <QTemporaryDir>
#include <QPainter>

#include "ComfyUIRemoteDock.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyControlLayer.h"
#include "ComfyOpenPose.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyStyleCollection.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyTheme.h"
#include "ComfyRegionProcess.h"
#include "ComfyUIUtils.h"
#include "ComfyPrepareGenerateWorkflow.h"
#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyRegionLink.h"

using ComfyUIUtils::ComfyRegionUiStateEntry;
using ComfyUIUtils::readRegionUiArrayFromDocumentUi;
using ComfyUIUtils::regionUiStateEntryFromJson;
using ComfyUIUtils::regionUiStateEntryToJson;
using ComfyUIUtils::rootRegionUiWrapFromJson;
using ComfyUIUtils::rootRegionUiWrapToJson;
using ComfyUIUtils::persistenceFormatVersion;

class ComfyUIRemoteDockTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testDockCreationAndObserverName();
    void testSetViewManagerAndCanvas();
    void testDockObjectName();
    void testRegionsConfigRoundtrip();
    void testRegionsConfigEmpty();
    void testRegionsConfigSingleRegion();
    void testRegionsConfigManyRegions();
    void testPresetsConfigRoundtrip();
    void testPresetsConfigEmpty();
    void testPresetCheckpointRoundtrip();
    void testPresetGroupDeleteSemantics();
    void testRegionMaskSourceDefaultValue();
    void testRegionsConfigEmptyNameStored();
    void testDefaultWorkflowJsonFormat();
    void testDefaultWorkflowNodeInputs();
    void testInpaintingWorkflowJsonFormat();
    void testInpaintingWorkflowNodeLinks();
    void testMigrateDockLayoutComfyUIRemoteToImageDiffusion();
    void testMigrateDockLayoutSkipsWhenImageDiffusionHasDockArea();
    void testDiffusionScaleModeNormalizeAndAdjust();
    void testComfyImageScaleMethodForScaleMode();
    void testUniformTileGridCount2D();
    void testDiffusionTileLayoutApi();
    void testDiffusionUpscaleTileEstimateExtentPx();
    void testApplyUpscaleRefineVaedecodeTiling();
    void testControlPresetsBuiltinDefault();
    void testControlPresetsArchKeyFallback();
    void testResolveDefaultControlLayerPreset();
    void testIntervalSliderSignalsAndBounds();
    void testBuildControlImageWorkflow();
    void testCompositeControlImageOntoExtent();
    void testTryResolveCustomWorkflowJsonApiPassthrough();
    void testConvertComfyUiWorkflowUiToApiEmptyLatent();
    void testConvertComfyUiWorkflowUiToApiThreeTupleLink();
    void testKritaIconNameForThemeStem();
    void testComfyResourcesArchFromCheckpoint();
    void testComfyResourcesControlModeHelpers();
    void testGetSelectionModifiersAndBounds();
    void testPrepareGenerateWorkflowKindPromotion();
    void testSelectionModifiersInvertReplaceBackgroundOnly();
    void testPrepareDiffusionInputExtentInpaintContext();
    void testApplyNsfwFilterToWorkflowOutput();
    void testCompositeJobResultOnDocumentPassthrough();
    void testInpaintCompositeMaskedServerResultPreservesContext();
    void testPrepareLiveWorkflowKindPromotion();
    void testLiveMinMaskSizeByArch();
    void testResolveSamplingFromStyleStrength();
    void testBuildRefineRegionFooocusBranch();
    void testBuildRefineRegionInpaintControlNet();
    void testApplyStrengthResolvedSamplingToRefine();
    void testBuildRefineRegionSplitSigmasAtStrength();
    void testDiscoverInpaintingWorkflowGraphContext();
    void testLiveFinalizePreservesRefineRegionGraph();
    void testBuildRefineRegionSkipsServerColorMatchWithLiveFinalize();
    void testBuildInpaintUseReferenceAndColorMatch();
    void testBuildInpaintRefinementUpscalePass();
    void testCustomInpaintContextAndParams();
    void testRecentlyUsedSyncDocumentDefaultsFields();
    void testCustomWorkflowKritaSelectionPrepare();
    void testCustomWorkflowKritaSelectionPrepareAndExpand();
    void testCustomWorkflowInvalidSelectionContext();
    void testCustomWorkflowLayerExportAndFingerprint();
    void testCustomWorkflowFullSelectionPipeline();
    void testExpandCustomKritaWorkflowNodes();
    void testExpandCustomKritaWorkflowParameterAndStyle();
    void testCustomWorkflowLiveCapturePolicy();
    void testPrepareCustomWorkflowStyleAndPrompts();
    void testExtractLorasFromPromptAndMerge();
    void testComfyWorkflowEngineBuildTextToImage();
    void testComfyWorkflowEngineApplyCheckpointStyleOptions();
    void testComfyStyleCollectionEntryToJson();
    void testComfyLocalizationTranslate();
    void testComfyLocalizationLoadFrenchJson();
    void testComfyFileRecordHashAndSerialization();
    void testComfyFileLibraryPreferredCheckpoint();
    void testComfyWorkflowEngineBuildRefine();
    void testComfyWorkflowEngineBuildInpaint();
    void testComfyWorkflowEngineBuildLive();
    void testComfyWorkflowEngineBuildAnimationFrame();
    void testComfyWorkflowEngineBuildUpscaleSimple();
    void testComfyWorkflowEngineBuildUpscaleRefine();
    void testComfyWorkflowEngineBuildUpscaleTiled();
    void testComfyWorkflowEngineFluxCfgCap();
    void testComfyControlLayerJsonRoundtrip();
    void testComfyWorkflowEngineApplyControlNet();
    void testComfyWorkflowEngineApplyIpAdapter();
    void testComfyControlLayerNeedsGenerateUpload();
    void testComfyControlLayerUiModeKeys();
    void testComfyControlLayerCanGenerateJob();
    void testComfyOpenPoseFromJsonToSvg();
    void testComfyThemePaletteAndIcons();
    void testComfyInpaintModeDetectAndInstructions();
    void testComfyPromptTranslationHelpers();
    void testComfyWorkflowEnginePromptTranslationNodes();
    void testComfyWorkflowEngineApplyRegionalGeneration();
    void testComfyRegionProcessMaskOverlap();
    void testComfyRegionProcessMaskInvertBackground();
    void testDocumentUiJsonRegionControlRoundtrip();
    void testComfyRegionLinkLayerIds();
    void testComfyRegionLinkEffectiveMaskSource();
    void testRegionUiStateLayerIdsJson();
    void testComfyControlLayerHasStructuralAmong();
};

void ComfyUIRemoteDockTest::testDockCreationAndObserverName()
{
    ComfyUIRemoteDock dock;
    QVERIFY(dock.observerName() == QLatin1String("ComfyUIRemoteDock"));
    dock.setViewManager(nullptr);
    dock.setCanvas(nullptr);
    dock.unsetCanvas();
}

void ComfyUIRemoteDockTest::testSetViewManagerAndCanvas()
{
    ComfyUIRemoteDock dock;
    dock.setViewManager(nullptr);
    QVERIFY(true); // no crash
    dock.setCanvas(nullptr);
    QVERIFY(true);
    dock.unsetCanvas();
    QVERIFY(true);
}

void ComfyUIRemoteDockTest::testDockObjectName()
{
    ComfyUIRemoteDock dock;
    // §10.2: KoDockFactory sets objectName to factory id ("imageDiffusion"); direct construction leaves it empty
    QVERIFY(dock.objectName().isEmpty() || dock.objectName() == QLatin1String("ComfyUIRemote")
            || dock.objectName() == QLatin1String("imageDiffusion"));
}

void ComfyUIRemoteDockTest::testRegionsConfigRoundtrip()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup cfg = config->group("ComfyUIRemote");

    const int count = 3;
    cfg.writeEntry("RegionsCount", count);
    cfg.writeEntry("Region_0_Name", QString("Background"));
    cfg.writeEntry("Region_0_Prompt", QString("a sky"));
    cfg.writeEntry("Region_0_MaskSource", QString("selection"));
    cfg.writeEntry("Region_1_Name", QString("Foreground"));
    cfg.writeEntry("Region_1_Prompt", QString("a tree"));
    cfg.writeEntry("Region_1_MaskSource", QString("layer:Layer 1"));
    cfg.writeEntry("Region_2_Name", QString("Mid"));
    cfg.writeEntry("Region_2_Prompt", QString("hills"));
    cfg.writeEntry("Region_2_MaskSource", QString("selection"));
    config->sync();

    KConfigGroup readCfg = config->group("ComfyUIRemote");
    QCOMPARE(readCfg.readEntry("RegionsCount", 0), count);
    QCOMPARE(readCfg.readEntry("Region_0_Name", QString()), QString("Background"));
    QCOMPARE(readCfg.readEntry("Region_0_Prompt", QString()), QString("a sky"));
    QCOMPARE(readCfg.readEntry("Region_0_MaskSource", QString()), QString("selection"));
    QCOMPARE(readCfg.readEntry("Region_1_MaskSource", QString()), QString("layer:Layer 1"));
}

void ComfyUIRemoteDockTest::testRegionsConfigEmpty()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup cfg = config->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", 0);
    config->sync();

    KConfigGroup readCfg = config->group("ComfyUIRemote");
    QCOMPARE(readCfg.readEntry("RegionsCount", -1), 0);
}

void ComfyUIRemoteDockTest::testRegionsConfigSingleRegion()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup cfg = config->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", 1);
    cfg.writeEntry("Region_0_Name", QString("Only"));
    cfg.writeEntry("Region_0_Prompt", QString("single"));
    cfg.writeEntry("Region_0_MaskSource", QString("selection"));
    config->sync();

    KConfigGroup readCfg = config->group("ComfyUIRemote");
    QCOMPARE(readCfg.readEntry("RegionsCount", 0), 1);
    QCOMPARE(readCfg.readEntry("Region_0_Name", QString()), QString("Only"));
    QCOMPARE(readCfg.readEntry("Region_0_MaskSource", QString()), QString("selection"));
}

void ComfyUIRemoteDockTest::testRegionsConfigManyRegions()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup cfg = config->group("ComfyUIRemote");
    const int n = 6;
    cfg.writeEntry("RegionsCount", n);
    for (int i = 0; i < n; i++) {
        cfg.writeEntry(QString("Region_%1_Name").arg(i), QString("Region%1").arg(i));
        cfg.writeEntry(QString("Region_%1_Prompt").arg(i), QString("prompt%1").arg(i));
        cfg.writeEntry(QString("Region_%1_MaskSource").arg(i),
                       (i % 2) ? QString("selection") : QString("layer:L%1").arg(i));
    }
    config->sync();

    KConfigGroup readCfg = config->group("ComfyUIRemote");
    QCOMPARE(readCfg.readEntry("RegionsCount", 0), n);
    QCOMPARE(readCfg.readEntry("Region_1_MaskSource", QString()), QString("selection"));
    QCOMPARE(readCfg.readEntry("Region_2_MaskSource", QString()), QString("layer:L2"));
}

void ComfyUIRemoteDockTest::testPresetsConfigRoundtrip()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup mainCfg = config->group("ComfyUIRemote");
    QStringList names;
    names << "MyPreset1" << "MyPreset2";
    mainCfg.writeEntry("PresetNames", names);
    config->sync();

    KConfigGroup preset1 = config->group("ComfyUIRemote_Preset_MyPreset1");
    preset1.writeEntry("Prompt", "portrait");
    preset1.writeEntry("Negative", "blur");
    preset1.writeEntry("Width", 768);
    preset1.writeEntry("Height", 512);
    preset1.writeEntry("Checkpoint", "v1-5-pruned.safetensors");
    config->sync();

    KConfigGroup readMain = config->group("ComfyUIRemote");
    QStringList readNames = readMain.readEntry("PresetNames", QStringList());
    QCOMPARE(readNames.size(), 2);
    QVERIFY(readNames.contains("MyPreset1"));
    QVERIFY(readNames.contains("MyPreset2"));

    KConfigGroup readPreset1 = config->group("ComfyUIRemote_Preset_MyPreset1");
    QCOMPARE(readPreset1.readEntry("Prompt", QString()), QString("portrait"));
    QCOMPARE(readPreset1.readEntry("Width", 0), 768);
    QCOMPARE(readPreset1.readEntry("Checkpoint", QString()), QString("v1-5-pruned.safetensors"));
}

void ComfyUIRemoteDockTest::testPresetCheckpointRoundtrip()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup preset = config->group("ComfyUIRemote_Preset_CheckpointTest");
    preset.writeEntry("Checkpoint", "my-inpainting.ckpt");
    config->sync();

    KConfigGroup readPreset = config->group("ComfyUIRemote_Preset_CheckpointTest");
    QCOMPARE(readPreset.readEntry("Checkpoint", QString()), QString("my-inpainting.ckpt"));
    QCOMPARE(readPreset.readEntry("Checkpoint", QString("default")), QString("my-inpainting.ckpt"));
}

void ComfyUIRemoteDockTest::testPresetGroupDeleteSemantics()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup preset = config->group("ComfyUIRemote_Preset_ToDelete");
    preset.writeEntry("Prompt", "deleted");
    config->sync();

    config->deleteGroup("ComfyUIRemote_Preset_ToDelete");
    config->sync();

    KConfigGroup readAgain = config->group("ComfyUIRemote_Preset_ToDelete");
    QCOMPARE(readAgain.readEntry("Prompt", QString("default")), QString("default"));
}

void ComfyUIRemoteDockTest::testRegionMaskSourceDefaultValue()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup cfg = config->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", 1);
    cfg.writeEntry("Region_0_Name", QString("NoMaskKey"));
    cfg.writeEntry("Region_0_Prompt", QString("x"));
    config->sync();

    KConfigGroup readCfg = config->group("ComfyUIRemote");
    QString maskSource = readCfg.readEntry("Region_0_MaskSource", "selection");
    QCOMPARE(maskSource, QString("selection"));
}

void ComfyUIRemoteDockTest::testRegionsConfigEmptyNameStored()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup cfg = config->group("ComfyUIRemote");
    cfg.writeEntry("RegionsCount", 1);
    cfg.writeEntry("Region_0_Name", QString());
    cfg.writeEntry("Region_0_Prompt", QString("only prompt"));
    cfg.writeEntry("Region_0_MaskSource", QString("selection"));
    config->sync();

    KConfigGroup readCfg = config->group("ComfyUIRemote");
    QCOMPARE(readCfg.readEntry("Region_0_Name", QString("x")), QString());
    QCOMPARE(readCfg.readEntry("Region_0_Prompt", QString()), QString("only prompt"));
}

void ComfyUIRemoteDockTest::testPresetsConfigEmpty()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup mainCfg = config->group("ComfyUIRemote");
    mainCfg.writeEntry("PresetNames", QStringList());
    config->sync();

    KConfigGroup readMain = config->group("ComfyUIRemote");
    QStringList readNames = readMain.readEntry("PresetNames", QStringList() << "x");
    QCOMPARE(readNames.size(), 0);
}

void ComfyUIRemoteDockTest::testDefaultWorkflowJsonFormat()
{
    // Same structure as defaultWorkflow in ComfyUIRemoteDock.cpp (text2img)
    const char workflow[] = R"({
 "3": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["5", 0], "model": ["4", 0], "negative": ["7", 0], "positive": ["6", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"}},
 "5": {"class_type": "EmptyLatentImage", "inputs": {"batch_size": 1, "height": 512, "width": 512}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "7": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "8": {"class_type": "VAEDecode", "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
 "9": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI", "images": ["8", 0]}}
})";
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(workflow), &err);
    QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
    QVERIFY(doc.isObject());
    QJsonObject obj = doc.object();
    QVERIFY(obj.contains("3"));
    QVERIFY(obj.contains("4"));
    QVERIFY(obj.contains("5"));
    QVERIFY(obj.contains("6"));
    QVERIFY(obj.contains("7"));
    QVERIFY(obj.contains("8"));
    QVERIFY(obj.contains("9"));
    QCOMPARE(obj["4"].toObject()["class_type"].toString(), QString("CheckpointLoaderSimple"));
    QCOMPARE(obj["9"].toObject()["class_type"].toString(), QString("SaveImage"));
}

void ComfyUIRemoteDockTest::testDefaultWorkflowNodeInputs()
{
    const char workflow[] = R"({
 "3": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["5", 0], "model": ["4", 0], "negative": ["7", 0], "positive": ["6", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"}},
 "5": {"class_type": "EmptyLatentImage", "inputs": {"batch_size": 1, "height": 512, "width": 512}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "7": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": ""}},
 "8": {"class_type": "VAEDecode", "inputs": {"samples": ["3", 0], "vae": ["4", 2]}},
 "9": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI", "images": ["8", 0]}}
})";
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(workflow));
    QJsonObject obj = doc.object();
    QJsonObject n3 = obj["3"].toObject();
    QJsonObject i3 = n3["inputs"].toObject();
    QVERIFY(i3.contains("latent_image"));
    QJsonArray latent = i3["latent_image"].toArray();
    QCOMPARE(latent.at(0).toString(), QString("5"));
    QCOMPARE(latent.at(1).toInt(), 0);
}

void ComfyUIRemoteDockTest::testInpaintingWorkflowJsonFormat()
{
    // Same structure as inpaintingWorkflowTemplate (placeholders replaced with sample values)
    const char workflow[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "canvas.png"}},
 "2": {"class_type": "LoadImage", "inputs": {"image": "mask.png"}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "a landscape"}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "blur"}},
 "7": {"class_type": "VAEEncodeForInpaint", "inputs": {"grow_mask_by": 6, "mask": ["2", 1], "pixels": ["1", 0], "vae": ["4", 2]}},
 "8": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["7", 0], "model": ["4", 0], "negative": ["6", 0], "positive": ["5", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["4", 2]}},
 "10": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_region", "images": ["9", 0]}}
})";
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(workflow), &err);
    QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
    QVERIFY(doc.isObject());
    QJsonObject obj = doc.object();
    QVERIFY(obj.contains("1"));
    QVERIFY(obj.contains("2"));
    QVERIFY(obj.contains("7"));
    QVERIFY(obj.contains("10"));
    QCOMPARE(obj["1"].toObject()["class_type"].toString(), QString("LoadImage"));
    QCOMPARE(obj["7"].toObject()["class_type"].toString(), QString("VAEEncodeForInpaint"));
    QVERIFY(obj["7"].toObject()["inputs"].toObject().contains("mask"));
    QVERIFY(obj["7"].toObject()["inputs"].toObject().contains("pixels"));
}

void ComfyUIRemoteDockTest::testInpaintingWorkflowNodeLinks()
{
    const char workflow[] = R"({
 "1": {"class_type": "LoadImage", "inputs": {"image": "canvas.png"}},
 "2": {"class_type": "LoadImage", "inputs": {"image": "mask.png"}},
 "4": {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": "v1-5-pruned-emaonly.safetensors"}},
 "5": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "a landscape"}},
 "6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["4", 1], "text": "blur"}},
 "7": {"class_type": "VAEEncodeForInpaint", "inputs": {"grow_mask_by": 6, "mask": ["2", 1], "pixels": ["1", 0], "vae": ["4", 2]}},
 "8": {"class_type": "KSampler", "inputs": {"cfg": 8, "denoise": 1, "latent_image": ["7", 0], "model": ["4", 0], "negative": ["6", 0], "positive": ["5", 0], "sampler_name": "euler", "scheduler": "normal", "seed": 0, "steps": 20}},
 "9": {"class_type": "VAEDecode", "inputs": {"samples": ["8", 0], "vae": ["4", 2]}},
 "10": {"class_type": "SaveImage", "inputs": {"filename_prefix": "ComfyUI_region", "images": ["9", 0]}}
})";
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(workflow));
    QJsonObject obj = doc.object();
    QJsonObject n7 = obj["7"].toObject();
    QJsonObject i7 = n7["inputs"].toObject();
    QJsonArray mask = i7["mask"].toArray();
    QJsonArray pixels = i7["pixels"].toArray();
    QCOMPARE(mask.at(0).toString(), QString("2"));
    QCOMPARE(mask.at(1).toInt(), 1);
    QCOMPARE(pixels.at(0).toString(), QString("1"));
    QCOMPARE(pixels.at(1).toInt(), 0);

    QJsonObject n8 = obj["8"].toObject();
    QJsonObject i8 = n8["inputs"].toObject();
    QJsonArray latent = i8["latent_image"].toArray();
    QCOMPARE(latent.at(0).toString(), QString("7"));
    QCOMPARE(latent.at(1).toInt(), 0);
}

void ComfyUIRemoteDockTest::testMigrateDockLayoutComfyUIRemoteToImageDiffusion()
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup mainWin(cfg, QStringLiteral("MainWindow"));
    KConfigGroup legacy = mainWin.group(QStringLiteral("DockWidget ComfyUIRemote"));
    legacy.writeEntry(QStringLiteral("DockArea"), 2);
    legacy.writeEntry(QStringLiteral("Locked"), true);
    legacy.writeEntry(QStringLiteral("xPosition"), 10);
    legacy.writeEntry(QStringLiteral("yPosition"), 20);
    legacy.writeEntry(QStringLiteral("width"), 400);
    legacy.writeEntry(QStringLiteral("height"), 500);
    cfg->sync();

    ComfyUIUtils::migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(cfg);

    KConfigGroup afterLegacy = mainWin.group(QStringLiteral("DockWidget ComfyUIRemote"));
    QVERIFY(afterLegacy.keyList().isEmpty());

    KConfigGroup current = mainWin.group(QStringLiteral("DockWidget imageDiffusion"));
    QCOMPARE(current.readEntry(QStringLiteral("DockArea"), -1), 2);
    QVERIFY(current.readEntry(QStringLiteral("Locked"), false));
    QCOMPARE(current.readEntry(QStringLiteral("xPosition"), 0), 10);
    QCOMPARE(current.readEntry(QStringLiteral("yPosition"), 0), 20);
    QCOMPARE(current.readEntry(QStringLiteral("width"), 0), 400);
    QCOMPARE(current.readEntry(QStringLiteral("height"), 0), 500);
}

void ComfyUIRemoteDockTest::testMigrateDockLayoutSkipsWhenImageDiffusionHasDockArea()
{
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QString(), KSharedConfig::SimpleConfig);
    KConfigGroup mainWin(cfg, QStringLiteral("MainWindow"));
    KConfigGroup legacy = mainWin.group(QStringLiteral("DockWidget ComfyUIRemote"));
    legacy.writeEntry(QStringLiteral("DockArea"), 1);
    legacy.writeEntry(QStringLiteral("width"), 300);
    KConfigGroup current = mainWin.group(QStringLiteral("DockWidget imageDiffusion"));
    current.writeEntry(QStringLiteral("DockArea"), 4);
    cfg->sync();

    ComfyUIUtils::migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(cfg);

    KConfigGroup legacyAfter = mainWin.group(QStringLiteral("DockWidget ComfyUIRemote"));
    QVERIFY(legacyAfter.hasKey(QStringLiteral("DockArea")));
    QCOMPARE(legacyAfter.readEntry(QStringLiteral("DockArea"), -1), 1);

    KConfigGroup currentAfter = mainWin.group(QStringLiteral("DockWidget imageDiffusion"));
    QCOMPARE(currentAfter.readEntry(QStringLiteral("DockArea"), -1), 4);
    QVERIFY(!currentAfter.hasKey(QStringLiteral("width")));
}

void ComfyUIRemoteDockTest::testDiffusionScaleModeNormalizeAndAdjust()
{
    QCOMPARE(ComfyUIUtils::normalizeDiffusionScaleMode(QString()), QStringLiteral("resize"));
    QCOMPARE(ComfyUIUtils::normalizeDiffusionScaleMode(QStringLiteral("NONE")), QStringLiteral("none"));
    QCOMPARE(ComfyUIUtils::normalizeDiffusionScaleMode(QStringLiteral("bogus")), QStringLiteral("resize"));
    QJsonObject o;
    o.insert(QStringLiteral("diffusion_scale_mode"), QStringLiteral("none"));
    double m = 2.0;
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(o, &m);
    QCOMPARE(m, 1.0);
    o.insert(QStringLiteral("diffusion_scale_mode"), QStringLiteral("resize"));
    m = 2.0;
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(o, &m);
    QCOMPARE(m, 2.0);
    o.insert(QStringLiteral("diffusion_scale_mode"), QStringLiteral("upscale_small"));
    m = 2.0;
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(o, &m);
    QCOMPARE(m, 1.5);
    m = 1.2;
    ComfyUIUtils::adjustEffectiveResolutionMultiplierForDiffusionScaleMode(o, &m);
    QCOMPARE(m, 1.2);
}

void ComfyUIRemoteDockTest::testComfyImageScaleMethodForScaleMode()
{
    QCOMPARE(ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(QStringLiteral("upscale_quality")), QStringLiteral("lanczos"));
    QCOMPARE(ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(QStringLiteral("upscale_fast")), QStringLiteral("bicubic"));
    QCOMPARE(ComfyUIUtils::comfyImageScaleMethodForDiffusionScaleMode(QStringLiteral("resize")), QStringLiteral("bilinear"));
}

void ComfyUIRemoteDockTest::testUniformTileGridCount2D()
{
    QCOMPARE(ComfyUIUtils::estimateUniformTileGridCount2D(500, 500, 512, -1), 1);
    QVERIFY(ComfyUIUtils::estimateUniformTileGridCount2D(1000, 1000, 512, 32) >= 4);
}

void ComfyUIRemoteDockTest::testDiffusionTileLayoutApi()
{
    ComfyUIUtils::DiffusionTileLayout L = ComfyUIUtils::DiffusionTileLayout::fromUniformGrid(1000, 1000, 512, 32);
    QCOMPARE(L.totalTiles(), ComfyUIUtils::estimateUniformTileGridCount2D(1000, 1000, 512, 32));
    QVERIFY(L.tileCount > 1);
    for (int i = 0; i < L.totalTiles(); ++i) {
        const QRect r = L.bounds(i);
        QVERIFY(!r.isEmpty());
        const QPoint c = L.coord(i);
        QCOMPARE(L.tileIndex(c), i);
        QCOMPARE(L.start(c), r.topLeft());
        QCOMPARE(L.end(c), r.bottomRight());
    }
    ComfyUIUtils::DiffusionTileLayout D =
        ComfyUIUtils::DiffusionTileLayout::fromDenoiseStrength(QSize(1024, 1024), 256, 1.0, 8, -1);
    QVERIFY(D.tileExtent % 8 == 0);
    QVERIFY(D.totalTiles() >= 1);
}

void ComfyUIRemoteDockTest::testDiffusionUpscaleTileEstimateExtentPx()
{
    QJsonObject o;
    QCOMPARE(ComfyUIUtils::diffusionUpscaleTileEstimateExtentPx(o), 512);
    o.insert(QStringLiteral("upscale_tile_estimate_extent"), 300);
    QCOMPARE(ComfyUIUtils::diffusionUpscaleTileEstimateExtentPx(o), 256);
    o.insert(QStringLiteral("upscale_tile_estimate_extent"), 4000);
    QCOMPARE(ComfyUIUtils::diffusionUpscaleTileEstimateExtentPx(o), 2048);
    o.insert(QStringLiteral("upscale_tile_estimate_extent"), 768);
    QCOMPARE(ComfyUIUtils::diffusionUpscaleTileEstimateExtentPx(o), 768);
}

void ComfyUIRemoteDockTest::testApplyUpscaleRefineVaedecodeTiling()
{
    QJsonObject wf;
    QJsonObject n8;
    n8.insert(QStringLiteral("class_type"), QStringLiteral("VAEDecode"));
    QJsonObject i8;
    i8.insert(QStringLiteral("samples"), QJsonArray{QStringLiteral("7"), 0});
    i8.insert(QStringLiteral("vae"), QJsonArray{QStringLiteral("4"), 2});
    n8.insert(QStringLiteral("inputs"), i8);
    wf.insert(QStringLiteral("8"), n8);
    QJsonObject settings;
    settings.insert(QStringLiteral("upscale_tile_estimate_extent"), 512);
    ComfyUIUtils::applyUpscaleRefineVaedecodeTiling(wf, QStringLiteral("8"), 0, 32, settings);
    QCOMPARE(wf[QStringLiteral("8")].toObject()[QStringLiteral("class_type")].toString(), QStringLiteral("VAEDecodeTiled"));
    const QJsonObject in = wf[QStringLiteral("8")].toObject()[QStringLiteral("inputs")].toObject();
    QCOMPARE(in[QStringLiteral("tile_size")].toInt(), 512);
    QCOMPARE(in[QStringLiteral("overlap")].toInt(), 64);
    ComfyUIUtils::applyUpscaleRefineVaedecodeTiling(wf, QStringLiteral("8"), 1, 48, settings);
    QCOMPARE(wf[QStringLiteral("8")].toObject()[QStringLiteral("inputs")].toObject()[QStringLiteral("overlap")].toInt(), 48);
}

void ComfyUIRemoteDockTest::testControlPresetsBuiltinDefault()
{
    ComfyUIUtils::reloadControlPresetsCache();
    const QJsonObject root = ComfyUIUtils::builtinControlPresetsRoot();
    const QList<ComfyUIUtils::ControlLayerPreset> ps =
        ComfyUIUtils::controlPresetsForMode(root, QStringLiteral("default"), QString());
    QCOMPARE(ps.size(), 3);
    QCOMPARE(ps.at(0).strength, 0.7);
    QCOMPARE(ps.at(0).start, 0.0);
    QCOMPARE(ps.at(0).end, 0.5);
    QCOMPARE(ps.at(1).strength, 1.0);
    QCOMPARE(ps.at(1).end, 0.8);
    QCOMPARE(ps.at(2).strength, 1.0);
    QCOMPARE(ps.at(2).end, 1.0);
}

void ComfyUIRemoteDockTest::testControlPresetsArchKeyFallback()
{
    QJsonObject entryFlux;
    entryFlux.insert(QStringLiteral("strength"), 0.5);
    entryFlux.insert(QStringLiteral("start"), 0.1);
    entryFlux.insert(QStringLiteral("end"), 0.9);
    QJsonArray flux;
    flux.append(entryFlux);
    QJsonObject entryAll;
    entryAll.insert(QStringLiteral("strength"), 0.99);
    entryAll.insert(QStringLiteral("start"), 0.0);
    entryAll.insert(QStringLiteral("end"), 1.0);
    QJsonArray all;
    all.append(entryAll);
    QJsonObject mode;
    mode.insert(QStringLiteral("flux"), flux);
    mode.insert(QStringLiteral("all"), all);
    QJsonObject root;
    root.insert(QStringLiteral("custommode"), mode);
    const QList<ComfyUIUtils::ControlLayerPreset> withArch =
        ComfyUIUtils::controlPresetsForMode(root, QStringLiteral("custommode"), QStringLiteral("flux"));
    QCOMPARE(withArch.size(), 1);
    QCOMPARE(withArch.at(0).strength, 0.5);
    const QList<ComfyUIUtils::ControlLayerPreset> fallback =
        ComfyUIUtils::controlPresetsForMode(root, QStringLiteral("custommode"), QStringLiteral("missing"));
    QCOMPARE(fallback.size(), 1);
    QCOMPARE(fallback.at(0).strength, 0.99);
}

void ComfyUIRemoteDockTest::testResolveDefaultControlLayerPreset()
{
    ComfyUIUtils::reloadControlPresetsCache();
    QJsonObject s;
    s.insert(QStringLiteral("control_layer_default_preset_index"), 2);
    ComfyUIUtils::ControlLayerPreset p;
    QVERIFY(ComfyUIUtils::resolveDefaultControlLayerPreset(s, &p));
    QCOMPARE(p.strength, 1.0);
    QCOMPARE(p.start, 0.0);
    QCOMPARE(p.end, 1.0);
    s.insert(QStringLiteral("control_layer_default_preset_index"), 0);
    QVERIFY(ComfyUIUtils::resolveDefaultControlLayerPreset(s, &p));
    QCOMPARE(p.strength, 0.7);
}

void ComfyUIRemoteDockTest::testIntervalSliderSignalsAndBounds()
{
    ComfyUIIntervalSlider slider;
    slider.setRange(0, 100);

    QSignalSpy rangeSpy(&slider, SIGNAL(rangeChanged(int,int)));
    QSignalSpy intervalSpy(&slider, SIGNAL(intervalChanged(int,int)));

    slider.setRange(10, 90);
    QCOMPARE(rangeSpy.count(), 1);
    QVERIFY(intervalSpy.count() >= 1);
    QCOMPARE(slider.minimum(), 10);
    QCOMPARE(slider.maximum(), 90);

    const int before = intervalSpy.count();
    slider.setInterval(80, 20); // swapped by widget; low <= high is enforced
    QCOMPARE(slider.lowValue(), 20);
    QCOMPARE(slider.highValue(), 80);
    QVERIFY(intervalSpy.count() == before + 1);
}

void ComfyUIRemoteDockTest::testBuildControlImageWorkflow()
{
    const QJsonObject wf = ComfyUIUtils::buildControlImageWorkflow(
        QStringLiteral("input.png"), QStringLiteral("canny_edge"), 896, true);
    QVERIFY(!wf.isEmpty());
    QCOMPARE(wf.value(QStringLiteral("1")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("LoadImage"));
    QCOMPARE(wf.value(QStringLiteral("2")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("CannyEdgePreprocessor"));
    QCOMPARE(wf.value(QStringLiteral("2")).toObject()
                 .value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("resolution")).toInt(),
             896);
    QCOMPARE(wf.value(QStringLiteral("4")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("ImageInvert"));
    QCOMPARE(wf.value(QStringLiteral("9")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("SaveImage"));

    const QJsonObject scribbleWf = ComfyUIUtils::buildControlImageWorkflow(
        QStringLiteral("input.png"), QStringLiteral("scribble"), 256, false);
    QCOMPARE(scribbleWf.value(QStringLiteral("2")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("PiDiNetPreprocessor"));
    QCOMPARE(scribbleWf.value(QStringLiteral("3")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("ScribblePreprocessor"));
    QCOMPARE(scribbleWf.value(QStringLiteral("3")).toObject()
                 .value(QStringLiteral("inputs")).toObject()
                 .value(QStringLiteral("resolution")).toInt(),
             512);
    QCOMPARE(scribbleWf.value(QStringLiteral("4")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("ImageInvert"));

    const QJsonObject lineWf = ComfyUIUtils::buildControlImageWorkflow(
        QStringLiteral("input.png"), QStringLiteral("line_art"), 640, false);
    QCOMPARE(lineWf.value(QStringLiteral("4")).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("ImageInvert"));
    QVERIFY(ComfyUIUtils::isControlModeLines(QStringLiteral("line_art")));
    QVERIFY(!ComfyUIUtils::isControlModeLines(QStringLiteral("depth")));

    const QJsonObject poseWf = ComfyUIUtils::buildControlImageWorkflow(
        QStringLiteral("input.png"), QStringLiteral("pose"), 1024, false);
    const QJsonObject poseInputs = poseWf.value(QStringLiteral("2")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(poseInputs.value(QStringLiteral("bbox_detector")).toString(),
             QStringLiteral("yolox_l.onnx"));
    QCOMPARE(poseInputs.value(QStringLiteral("pose_estimator")).toString(),
             QStringLiteral("dw-ll_ucoco_384_bs5.torchscript.pt"));
    QCOMPARE(poseInputs.value(QStringLiteral("resolution")).toInt(), 1024);

    const QJsonObject handsWf = ComfyUIUtils::buildControlImageWorkflow(
        QStringLiteral("input.png"), QStringLiteral("hands"), 200, false);
    const QJsonObject handsInputs = handsWf.value(QStringLiteral("2")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(handsInputs.value(QStringLiteral("resolution")).toInt(), 256);

    const QJsonObject unsupported = ComfyUIUtils::buildControlImageWorkflow(
        QStringLiteral("input.png"), QStringLiteral("unknown_mode"), 1024, false);
    QVERIFY(unsupported.isEmpty());
}

void ComfyUIRemoteDockTest::testTryResolveCustomWorkflowJsonApiPassthrough()
{
    QJsonObject api;
    QJsonObject n3;
    n3.insert(QStringLiteral("class_type"), QStringLiteral("KSampler"));
    QJsonObject in3;
    in3.insert(QStringLiteral("seed"), 0);
    n3.insert(QStringLiteral("inputs"), in3);
    api.insert(QStringLiteral("3"), n3);
    QString err;
    QVERIFY(ComfyUIUtils::tryResolveCustomWorkflowJsonToApi(&api, QJsonObject(), &err));
    QVERIFY(api.contains(QStringLiteral("3")));
}

void ComfyUIRemoteDockTest::testConvertComfyUiWorkflowUiToApiEmptyLatent()
{
    const QJsonObject objectInfo = QJsonDocument::fromJson(QByteArray(R"json({
        "EmptyLatentImage": {
            "input": {
                "required": {
                    "width": ["INT", {"default": 512, "max": 8192, "min": 64}],
                    "height": ["INT", {"default": 512, "max": 8192, "min": 64}],
                    "batch_size": ["INT", {"default": 1, "max": 4096, "min": 1}]
                }
            }
        }
    })json"))
                                     .object();

    const QJsonObject uiWf = QJsonDocument::fromJson(QByteArray(R"json({
        "version": 1,
        "nodes": [{
            "id": 5,
            "type": "EmptyLatentImage",
            "inputs": [
                {"name": "width", "type": "INT", "link": null},
                {"name": "height", "type": "INT", "link": null},
                {"name": "batch_size", "type": "INT", "link": null}
            ],
            "widgets_values": [640, 512, 2]
        }],
        "links": []
    })json"))
                               .object();

    QJsonObject out;
    const auto r = ComfyUIUtils::convertComfyUiWorkflowUiToApi(uiWf, objectInfo, &out);
    QVERIFY2(r.first, qPrintable(r.second));
    QCOMPARE(out.keys().size(), 1);
    const QJsonObject n5 = out.value(QStringLiteral("5")).toObject();
    QCOMPARE(n5.value(QStringLiteral("class_type")).toString(), QStringLiteral("EmptyLatentImage"));
    const QJsonObject inputs = n5.value(QStringLiteral("inputs")).toObject();
    QCOMPARE(inputs.value(QStringLiteral("width")).toInt(), 640);
    QCOMPARE(inputs.value(QStringLiteral("height")).toInt(), 512);
    QCOMPARE(inputs.value(QStringLiteral("batch_size")).toInt(), 2);
}

void ComfyUIRemoteDockTest::testConvertComfyUiWorkflowUiToApiThreeTupleLink()
{
    // §13.101: links as [link_id, source_node_id, source_output_slot] — target from node inputs
    const QJsonObject objectInfo = QJsonDocument::fromJson(QByteArray(R"json({
        "CheckpointLoaderSimple": {
            "input": {
                "required": {
                    "ckpt_name": ["COMBO", []]
                }
            }
        },
        "CLIPTextEncode": {
            "input": {
                "required": {
                    "clip": ["CLIP", {}],
                    "text": ["STRING", {"default": ""}]
                }
            }
        }
    })json"))
                                     .object();

    const QJsonObject uiWf = QJsonDocument::fromJson(QByteArray(R"json({
        "version": 1,
        "nodes": [
            {
                "id": 4,
                "type": "CheckpointLoaderSimple",
                "inputs": [],
                "widgets_values": ["model.safetensors"]
            },
            {
                "id": 6,
                "type": "CLIPTextEncode",
                "inputs": [
                    {"name": "clip", "type": "CLIP", "link": 12},
                    {"name": "text", "type": "STRING", "link": null}
                ],
                "widgets_values": ["a cat"]
            }
        ],
        "links": [[12, 4, 1]]
    })json"))
                               .object();

    QJsonObject out;
    const auto r = ComfyUIUtils::convertComfyUiWorkflowUiToApi(uiWf, objectInfo, &out);
    QVERIFY2(r.first, qPrintable(r.second));
    const QJsonObject n4 = out.value(QStringLiteral("4")).toObject();
    QCOMPARE(n4.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("ckpt_name")).toString(),
             QStringLiteral("model.safetensors"));
    const QJsonObject n6 = out.value(QStringLiteral("6")).toObject();
    const QJsonObject in6 = n6.value(QStringLiteral("inputs")).toObject();
    const QJsonArray clipRef = in6.value(QStringLiteral("clip")).toArray();
    QCOMPARE(clipRef.size(), 2);
    QCOMPARE(clipRef.at(0).toString(), QStringLiteral("4"));
    QCOMPARE(clipRef.at(1).toInt(), 1);
    QCOMPARE(in6.value(QStringLiteral("text")).toString(), QStringLiteral("a cat"));
}

void ComfyUIRemoteDockTest::testCompositeControlImageOntoExtent()
{
    QImage small(2, 2, QImage::Format_ARGB32);
    small.fill(Qt::white);
    const QImage out =
        ComfyUIUtils::compositeControlImageOntoExtent(small, QSize(8, 8), QRect(3, 3, 2, 2));
    QCOMPARE(out.size(), QSize(8, 8));
    QCOMPARE(out.pixel(0, 0), qRgb(0, 0, 0));
    QVERIFY(out.pixel(3, 3) != qRgb(0, 0, 0));
    const QImage passthrough = ComfyUIUtils::compositeControlImageOntoExtent(small, QSize(2, 2), QRect(0, 0, 2, 2));
    QCOMPARE(passthrough.size(), QSize(2, 2));
}

void ComfyUIRemoteDockTest::testKritaIconNameForThemeStem()
{
    QCOMPARE(ComfyTheme::kritaIconNameForThemeStem(QStringLiteral("workspace-generation")), QStringLiteral("tools-wizard"));
    QCOMPARE(ComfyTheme::kritaIconNameForThemeStem(QStringLiteral("queue-active")), QStringLiteral("run-build"));
    QCOMPARE(ComfyTheme::kritaIconNameForThemeStem(QStringLiteral("star")), QStringLiteral("rating"));
    QCOMPARE(ComfyTheme::kritaIconNameForThemeStem(QStringLiteral("not-a-real-theme-stem-xyz")),
             QStringLiteral("applications-graphics"));
}

void ComfyUIRemoteDockTest::testComfyResourcesArchFromCheckpoint()
{
    QCOMPARE(ComfyResources::archToKey(ComfyResources::archFromCheckpointName(QStringLiteral("sd_xl_base.safetensors"))),
             QStringLiteral("sdxl"));
    QCOMPARE(ComfyResources::archToKey(ComfyResources::archFromCheckpointName(QStringLiteral("flux1-dev.safetensors"))),
             QStringLiteral("flux"));
    QCOMPARE(ComfyResources::archToKey(ComfyResources::archFromCheckpointName(QStringLiteral("flux_kontext.safetensors"))),
             QStringLiteral("flux_k"));
    QCOMPARE(ComfyUIUtils::classifyCheckpointArch(QStringLiteral("v1-5-pruned-emaonly.safetensors")),
             QStringLiteral("sd15"));
    QCOMPARE(ComfyWorkflowEngine::resolveArch(QStringLiteral("unknown.ckpt"), QStringLiteral("sdxl")),
             ComfyResources::Arch::Sdxl);
}

void ComfyUIRemoteDockTest::testComfyResourcesControlModeHelpers()
{
    QVERIFY(ComfyResources::ControlMode::isIpAdapter(QStringLiteral("reference")));
    QVERIFY(ComfyResources::ControlMode::isLines(QStringLiteral("canny_edge")));
    QVERIFY(!ComfyResources::ControlMode::isStructural(QStringLiteral("reference")));
    QVERIFY(ComfyResources::ControlMode::isStructural(QStringLiteral("depth")));
    QVERIFY(ComfyResources::ControlMode::isPartOfImage(QStringLiteral("reference")));
    QVERIFY(ComfyResources::ControlMode::isPartOfImage(QStringLiteral("line_art")));
    QVERIFY(ComfyResources::ControlMode::isPartOfImage(QStringLiteral("blur")));
    QVERIFY(!ComfyResources::ControlMode::isPartOfImage(QStringLiteral("depth")));
    QVERIFY(!ComfyResources::ControlMode::isPartOfImage(QStringLiteral("canny_edge")));
    QVERIFY(ComfyResources::supportsRegions(ComfyResources::Arch::Sdxl));
    QVERIFY(!ComfyResources::supportsRegions(ComfyResources::Arch::Flux));
}

void ComfyUIRemoteDockTest::testGetSelectionModifiersAndBounds()
{
    const auto fillMods = ComfyUIUtils::getSelectionModifiers(QStringLiteral("sdxl"), QStringLiteral("fill"), 1.0);
    QVERIFY(!fillMods.invert);
    QVERIFY(fillMods.square == false);

    const auto replaceMods =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sdxl"), QStringLiteral("replace_background"), 1.0);
    QVERIFY(replaceMods.invert);
    QVERIFY(replaceMods.featherRel <= 0.01);

    const auto refineMods =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sdxl"), QStringLiteral("replace_background"), 0.5);
    QVERIFY(!refineMods.invert);

    QCOMPARE(ComfyUIUtils::resolveInpaintMode(QStringLiteral("custom"), 1000, 800, QRect(10, 10, 50, 50)),
             QStringLiteral("custom"));
    QCOMPARE(ComfyUIUtils::resolveInpaintMode(QStringLiteral("automatic"), 1000, 800, QRect(0, 0, 1000, 800)),
             QStringLiteral("expand"));

    const QRect mask(100, 100, 80, 60);
    const QRect refineBounds = ComfyUIUtils::computeInpaintDiffusionBounds(1000, 800, mask, true);
    QCOMPARE(refineBounds, mask);
    const QRect fillBounds = ComfyUIUtils::computeInpaintDiffusionBounds(1000, 800, mask, false);
    QVERIFY(fillBounds.width() >= 512);
    QVERIFY(fillBounds.contains(mask));

    const QRect original(200, 200, 40, 30);
    const auto mods = ComfyUIUtils::getSelectionModifiers(QStringLiteral("sdxl"), QStringLiteral("fill"), 1.0);
    const ComfyUIUtils::SelectionPreProcess pp =
        ComfyUIUtils::calcSelectionPreProcessFromModifiers(original, 1000, 800, mods);
    QVERIFY(pp.feather > 0);
    QVERIFY(pp.grow >= mods.padOffsetPx);
}

void ComfyUIRemoteDockTest::testPrepareGenerateWorkflowKindPromotion()
{
    // Document promotion rules without KisImage (logic mirror of prepare()).
    auto promoted = [](bool refineInitial, bool hasMask) -> ComfyPrepareGenerateWorkflow::WorkflowKind {
        ComfyPrepareGenerateWorkflow::WorkflowKind kind =
            refineInitial ? ComfyPrepareGenerateWorkflow::WorkflowKind::Refine
                          : ComfyPrepareGenerateWorkflow::WorkflowKind::Generate;
        if (hasMask) {
            if (kind == ComfyPrepareGenerateWorkflow::WorkflowKind::Generate)
                kind = ComfyPrepareGenerateWorkflow::WorkflowKind::Inpaint;
            else if (kind == ComfyPrepareGenerateWorkflow::WorkflowKind::Refine)
                kind = ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
        }
        return kind;
    };
    QCOMPARE(promoted(false, true), ComfyPrepareGenerateWorkflow::WorkflowKind::Inpaint);
    QCOMPARE(promoted(true, true), ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion);
    QCOMPARE(promoted(true, false), ComfyPrepareGenerateWorkflow::WorkflowKind::Refine);
    QCOMPARE(promoted(false, false), ComfyPrepareGenerateWorkflow::WorkflowKind::Generate);
}

void ComfyUIRemoteDockTest::testSelectionModifiersInvertReplaceBackgroundOnly()
{
    const auto modsFill =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sd15"), QStringLiteral("fill"), 1.0);
    QVERIFY(!modsFill.invert);

    const auto modsReplaceHalf =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sd15"), QStringLiteral("replace_background"), 0.5);
    QVERIFY(!modsReplaceHalf.invert);

    const auto modsReplaceFull =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sd15"), QStringLiteral("replace_background"), 1.0);
    QVERIFY(modsReplaceFull.invert);
    QCOMPARE(modsReplaceFull.featherRel, 0.01);
}

void ComfyUIRemoteDockTest::testPrepareDiffusionInputExtentInpaintContext()
{
    const ComfyUIUtils::DiffusionPreparedExtent small =
        ComfyUIUtils::prepareDiffusionInputExtent(QSize(200, 180), ComfyResources::Arch::Sd15);
    QVERIFY(small.initial.width() >= 512);
    QVERIFY(small.initial.height() >= 512);
    QCOMPARE(small.initial.width() % 16, 0);
    QCOMPARE(small.initial.height() % 16, 0);

    const ComfyUIUtils::DiffusionPreparedExtent large =
        ComfyUIUtils::prepareDiffusionInputExtent(QSize(1024, 768), ComfyResources::Arch::Sd15);
    QCOMPARE(large.initial, QSize(1024, 768));
}

void ComfyUIRemoteDockTest::testApplyNsfwFilterToWorkflowOutput()
{
    QJsonObject wf;
    wf.insert(QStringLiteral("4"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("VAEDecode")},
                          {QStringLiteral("inputs"), QJsonObject{}}});
    wf.insert(QStringLiteral("5"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("images"), QJsonArray{QStringLiteral("4"), 0}}}}});
    ComfyWorkflowEngine::applyNsfwFilterToWorkflowOutput(&wf, 0.0);
    QCOMPARE(wf.value(QStringLiteral("5")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray().at(0).toString(),
             QStringLiteral("4"));
    ComfyWorkflowEngine::applyNsfwFilterToWorkflowOutput(&wf, 0.8);
    bool hasFilter = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == QLatin1String("ETN_NSFWFilter"))
            hasFilter = true;
    }
    QVERIFY(hasFilter);
    const QJsonArray saveImages =
        wf.value(QStringLiteral("5")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray();
    QVERIFY(saveImages.at(0).toString() != QStringLiteral("4"));
}

void ComfyUIRemoteDockTest::testCompositeJobResultOnDocumentPassthrough()
{
    QImage patch(32, 32, QImage::Format_ARGB32);
    patch.fill(Qt::green);
    const QImage out = ComfyUIUtils::compositeJobResultOnDocument(KisImageSP(), {}, patch, QRect(), false);
    QCOMPARE(out.size(), patch.size());
}

void ComfyUIRemoteDockTest::testInpaintCompositeMaskedServerResultPreservesContext()
{
    // Mirrors upstream image.draw_image: masked server patch merged onto context.
    QImage context(64, 64, QImage::Format_ARGB32);
    context.fill(QColor(100, 120, 140));
    QImage mask(64, 64, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x)
            mask.setPixel(x, y, qRgb(255, 255, 255));
    }
    QImage server(64, 64, QImage::Format_RGB32);
    server.fill(Qt::black);
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x)
            server.setPixel(x, y, qRgb(200, 50, 50));
    }
    QImage out = context;
    const QImage compositeMask = ComfyUIUtils::denoiseToCompositingMask(mask, 0, 0, 0);
    ComfyUIUtils::compositeWithMask(out, server, compositeMask);
    QCOMPARE(out.pixel(8, 8), context.pixel(8, 8));
    QCOMPARE(qRed(out.pixel(32, 32)), 200);
    QCOMPARE(qGreen(out.pixel(32, 32)), 50);
}

void ComfyUIRemoteDockTest::testPrepareLiveWorkflowKindPromotion()
{
    auto liveKind = [](bool refineInitial, bool hasMask) -> ComfyPrepareGenerateWorkflow::WorkflowKind {
        ComfyPrepareGenerateWorkflow::WorkflowKind kind =
            refineInitial ? ComfyPrepareGenerateWorkflow::WorkflowKind::Refine
                          : ComfyPrepareGenerateWorkflow::WorkflowKind::Generate;
        if (hasMask)
            kind = ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion;
        return kind;
    };
    QCOMPARE(liveKind(false, true), ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion);
    QCOMPARE(liveKind(true, true), ComfyPrepareGenerateWorkflow::WorkflowKind::RefineRegion);
    QCOMPARE(liveKind(true, false), ComfyPrepareGenerateWorkflow::WorkflowKind::Refine);
    QCOMPARE(liveKind(false, false), ComfyPrepareGenerateWorkflow::WorkflowKind::Generate);
}

void ComfyUIRemoteDockTest::testLiveMinMaskSizeByArch()
{
    const auto sd15Mods =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sd15"), QStringLiteral("fill"), 1.0, 512);
    QCOMPARE(sd15Mods.sizeMinPx, 512);
    const auto sdxlMods =
        ComfyUIUtils::getSelectionModifiers(QStringLiteral("sdxl"), QStringLiteral("fill"), 1.0, 800);
    QCOMPARE(sdxlMods.sizeMinPx, 800);

    QRect region(100, 100, 80, 60);
    const QRect doc(0, 0, 512, 512);
    const QRect padded = ComfyUIUtils::padMaskBounds(region, doc, 8, 512, 16, true);
    QVERIFY(padded.width() >= 512);
    QVERIFY(padded.height() >= 512);
}

void ComfyUIRemoteDockTest::testResolveSamplingFromStyleStrength()
{
    const ComfyUIUtils::ResolvedSamplingInputs at85 =
        ComfyUIUtils::resolveSamplingFromStyle(nullptr, QJsonObject(), QStringLiteral("euler"), 20, 8.0, 0.85, true);
    QCOMPARE(at85.totalSteps, 20);
    QVERIFY(at85.startAtStep > 0);
    QVERIFY(at85.denoiseStrength < 1.0);
    QVERIFY(qAbs(at85.denoiseStrength - 0.85) < 0.08);

    const ComfyUIUtils::ResolvedSamplingInputs full =
        ComfyUIUtils::resolveSamplingFromStyle(nullptr, QJsonObject(), QStringLiteral("euler"), 20, 8.0, 1.0, false);
    QCOMPARE(full.startAtStep, 0);
    QCOMPARE(full.denoiseStrength, 1.0);
}

void ComfyUIRemoteDockTest::testBuildRefineRegionFooocusBranch()
{
    ComfyWorkflowEngine::RefineRegionParams rp;
    rp.refine.imageName = QStringLiteral("img.png");
    rp.maskImageName = QStringLiteral("mask.png");
    rp.refine.arch = ComfyResources::Arch::Sdxl;
    rp.useInpaintModel = true;
    rp.fooocusInpaintHead = QStringLiteral("inpaint_head.safetensors");
    rp.fooocusInpaintPatch = QStringLiteral("inpaint_patch.safetensors");
    const QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rp);
    bool hasInpaintCond = false;
    bool hasFooocus = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("INPAINT_VAEEncodeInpaintConditioning"))
            hasInpaintCond = true;
        if (cls == QLatin1String("INPAINT_ApplyFooocusInpaint"))
            hasFooocus = true;
    }
    QVERIFY(hasInpaintCond);
    QVERIFY(hasFooocus);
}

void ComfyUIRemoteDockTest::testBuildRefineRegionInpaintControlNet()
{
    ComfyWorkflowEngine::RefineRegionParams rp;
    rp.refine.imageName = QStringLiteral("img.png");
    rp.maskImageName = QStringLiteral("mask.png");
    rp.refine.arch = ComfyResources::Arch::Sd15;
    rp.useInpaintModel = true;
    rp.controlNetInpaintFile = QStringLiteral("inpaint.safetensors");
    const QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rp);
    bool hasInpaintCn = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("ControlNetInpaintingAliMamaApply"))
            hasInpaintCn = true;
    }
    QVERIFY(hasInpaintCn);
}

void ComfyUIRemoteDockTest::testApplyStrengthResolvedSamplingToRefine()
{
    ComfyWorkflowEngine::RefineParams rp;
    rp.steps = 20;
    rp.denoise = 1.0;
    rp.sampler = QStringLiteral("euler");
    rp.scheduler = QStringLiteral("normal");
    rp.cfg = 8.0;
    ComfyUIUtils::applyStrengthResolvedSamplingToRefine(
        &rp, nullptr, QJsonObject(), rp.sampler, rp.steps, rp.cfg, 0.85);
    QCOMPARE(rp.steps, 20);
    QVERIFY(rp.denoise < 1.0);
    QVERIFY(qAbs(rp.denoise - 0.85) < 0.08);

    rp.imageName = QStringLiteral("canvas.png");
    rp.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    QJsonObject wf = ComfyWorkflowEngine::buildRefine(rp);
    ComfyWorkflowEngine::finishWorkflowWithSamplerCustom(
        &wf, QStringLiteral("6"), ComfyResources::Arch::Sd15, 512, 512, rp.denoise);
    int splitStep = -1;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        if (node.value(QStringLiteral("class_type")).toString() == QLatin1String("SplitSigmas")) {
            splitStep = node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("step")).toInt();
            break;
        }
    }
    QVERIFY(splitStep > 0);
    QCOMPARE(splitStep, 3);
}

void ComfyUIRemoteDockTest::testBuildRefineRegionSplitSigmasAtStrength()
{
    ComfyWorkflowEngine::RefineRegionParams rp;
    rp.refine.imageName = QStringLiteral("img.png");
    rp.maskImageName = QStringLiteral("mask.png");
    rp.refine.steps = 20;
    rp.refine.denoise = 0.85;
    rp.refine.arch = ComfyResources::Arch::Sd15;
    const QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rp);
    int splitStep = -1;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        if (node.value(QStringLiteral("class_type")).toString() == QLatin1String("SplitSigmas")) {
            splitStep = node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("step")).toInt();
            break;
        }
    }
    QVERIFY(splitStep > 0);
    QCOMPARE(splitStep, 3);
}

void ComfyUIRemoteDockTest::testDiscoverInpaintingWorkflowGraphContext()
{
    ComfyWorkflowEngine::RefineRegionParams rp;
    rp.refine.imageName = QStringLiteral("img.png");
    rp.maskImageName = QStringLiteral("mask.png");
    rp.refine.arch = ComfyResources::Arch::Sdxl;
    const QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rp);
    QVERIFY(ComfyWorkflowEngine::isInpaintingTemplateWorkflow(wf));
    QVERIFY(!ComfyWorkflowEngine::isImg2imgRefineWorkflow(wf));

    const ComfyWorkflowEngine::WorkflowGraphContext ctx = ComfyWorkflowEngine::discoverWorkflowGraphContext(wf);
    QVERIFY(wf.contains(ctx.samplerNodeId));
    QCOMPARE(wf.value(ctx.samplerNodeId).toObject().value(QStringLiteral("class_type")).toString(),
             QStringLiteral("SamplerCustomAdvanced"));
    QCOMPARE(ctx.positiveNodeId, QStringLiteral("5"));
    QCOMPARE(ctx.negativeNodeId, QStringLiteral("6"));
    QVERIFY(ctx.samplerNodeId != QStringLiteral("3"));
}

void ComfyUIRemoteDockTest::testLiveFinalizePreservesRefineRegionGraph()
{
    ComfyWorkflowEngine::RefineRegionParams rp;
    rp.refine.imageName = QStringLiteral("img.png");
    rp.maskImageName = QStringLiteral("mask.png");
    rp.refine.arch = ComfyResources::Arch::Sdxl;
    rp.refine.denoise = 0.62;
    rp.refine.steps = 20;
    QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rp);
    QVERIFY(ComfyWorkflowEngine::isInpaintingTemplateWorkflow(wf));

    QString condId;
    QString samplerId;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("INPAINT_VAEEncodeInpaintConditioning"))
            condId = it.key();
        if (cls == QLatin1String("SamplerCustomAdvanced"))
            samplerId = it.key();
    }
    QVERIFY2(!condId.isEmpty(), "SDXL refine_region must use inpaint conditioning latent");
    QVERIFY2(!samplerId.isEmpty(), "SamplerCustomAdvanced missing");

    ComfyWorkflowEngine::applyCheckpointStyleOptions(&wf, QString(), 0, ComfyResources::Arch::Sdxl);
    ComfyWorkflowEngine::applyIpAdapterLayers(&wf, {}, ComfyResources::Arch::Sdxl);
    ComfyWorkflowEngine::applyControlNetLayers(&wf, {}, ComfyResources::Arch::Sdxl);

    QVERIFY(wf.contains(condId));
    const QJsonObject sampler = wf.value(samplerId).toObject();
    const QJsonArray latent =
        sampler.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("latent_image")).toArray();
    QVERIFY2(latent.size() >= 2, "sampler latent_image missing");
    QCOMPARE(latent.at(0).toString(), condId);
    QCOMPARE(latent.at(1).toInt(), 3);
}

void ComfyUIRemoteDockTest::testBuildRefineRegionSkipsServerColorMatchWithLiveFinalize()
{
    ComfyWorkflowEngine::RefineRegionParams rp;
    rp.refine.imageName = QStringLiteral("img.png");
    rp.maskImageName = QStringLiteral("mask.png");
    rp.refine.arch = ComfyResources::Arch::Sdxl;
    rp.refine.denoise = 0.75;
    rp.refine.steps = 20;
    rp.colorMatch = true;
    rp.extentWidth = 800;
    rp.extentHeight = 800;
    rp.contextExtentWidth = 800;
    rp.contextExtentHeight = 800;
    rp.growMaskBy = 10;
    rp.featherMaskBy = 20;
    QJsonObject wf = ComfyWorkflowEngine::buildRefineRegion(rp);
    QVERIFY(rp.colorMatch);

    ComfyWorkflowEngine::applyCheckpointStyleOptions(&wf, QString(), 0, ComfyResources::Arch::Sdxl);
    ComfyWorkflowEngine::applyIpAdapterLayers(&wf, {}, ComfyResources::Arch::Sdxl);
    ComfyWorkflowEngine::applyControlNetLayers(&wf, {}, ComfyResources::Arch::Sdxl);
    ComfyUIUtils::applyPerformancePreferencesToWorkflow(wf);

    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        QVERIFY2(it.value().toObject().value(QStringLiteral("class_type")).toString()
                     != QLatin1String("INPAINT_ColorMatch"),
                 "live finalize path must keep refine_region off server ColorMatch");
    }
    const QJsonArray saveImages =
        wf.value(QStringLiteral("10")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray();
    QCOMPARE(saveImages.at(0).toString(), QStringLiteral("9"));
}

void ComfyUIRemoteDockTest::testBuildInpaintUseReferenceAndColorMatch()
{
    ComfyWorkflowEngine::InpaintBuildParams bp;
    bp.imageName = QStringLiteral("img.png");
    bp.maskImageName = QStringLiteral("mask.png");
    bp.arch = ComfyResources::Arch::Sdxl;
    bp.useReference = true;
    bp.colorMatch = true;
    bp.useInpaintModel = true;
    bp.fooocusInpaintHead = QStringLiteral("inpaint_head.safetensors");
    bp.fooocusInpaintPatch = QStringLiteral("inpaint_patch.safetensors");
    const QJsonObject wf = ComfyWorkflowEngine::buildInpaint(bp);
    bool hasIp = false;
    bool hasColorMatch = false;
    bool hasFooocus = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls.contains(QLatin1String("IPAdapter")))
            hasIp = true;
        if (cls == QLatin1String("INPAINT_ColorMatch"))
            hasColorMatch = true;
        if (cls == QLatin1String("INPAINT_ApplyFooocusInpaint"))
            hasFooocus = true;
    }
    QVERIFY(hasIp);
    QVERIFY(hasColorMatch);
    QVERIFY(hasFooocus);
}

void ComfyUIRemoteDockTest::testBuildInpaintRefinementUpscalePass()
{
    ComfyWorkflowEngine::InpaintBuildParams bp;
    bp.imageName = QStringLiteral("img.png");
    bp.maskImageName = QStringLiteral("mask.png");
    bp.refinementUpscale = true;
    bp.refinementScaleMode = QStringLiteral("upscale_small");
    bp.initialExtentWidth = 512;
    bp.initialExtentHeight = 512;
    bp.desiredExtentWidth = 1024;
    bp.desiredExtentHeight = 1024;
    bp.contextExtentWidth = 2048;
    bp.contextExtentHeight = 2048;
    bp.steps = 20;
    const QJsonObject wf = ComfyWorkflowEngine::buildInpaint(bp);
    QVERIFY(!wf.isEmpty());

    int upscaleLoaderCount = 0;
    int upscaleWithModelCount = 0;
    int secondSamplerCount = 0;
    int setLatentNoiseMaskCount = 0;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("UpscaleModelLoader"))
            ++upscaleLoaderCount;
        if (cls == QLatin1String("ImageUpscaleWithModel"))
            ++upscaleWithModelCount;
        if (cls == QLatin1String("SamplerCustomAdvanced"))
            ++secondSamplerCount;
        if (cls == QLatin1String("SetLatentNoiseMask"))
            ++setLatentNoiseMaskCount;
    }
    QCOMPARE(upscaleLoaderCount, 1);
    QCOMPARE(upscaleWithModelCount, 1);
    QVERIFY(secondSamplerCount >= 2);
    QCOMPARE(setLatentNoiseMaskCount, 2);

    QString loaderModel;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        if (node.value(QStringLiteral("class_type")).toString() != QLatin1String("UpscaleModelLoader"))
            continue;
        loaderModel = node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("model_name")).toString();
        break;
    }
    QCOMPARE(loaderModel, QStringLiteral("RealESRGAN_x2plus.pth"));

    const QJsonArray saveImages =
        wf.value(QStringLiteral("10")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray();
    QVERIFY(saveImages.size() >= 2);
    const QString saveSource = saveImages.at(0).toString();
    QVERIFY(saveSource != QStringLiteral("9"));
}

void ComfyUIRemoteDockTest::testCustomInpaintContextAndParams()
{
    QString ctx;
    QString layerId;
    ComfyUIUtils::decodeInpaintContextComboData(QVariant(QStringLiteral("mask_bounds")), &ctx, &layerId);
    QCOMPARE(ctx, QStringLiteral("mask_bounds"));
    QVERIFY(layerId.isEmpty());

    ComfyUIUtils::decodeInpaintContextComboData(QVariant(QStringLiteral("layer_bounds")), &ctx, &layerId);
    QCOMPARE(ctx, QStringLiteral("automatic"));
    QVERIFY(layerId.isEmpty());

    const QString uid = QStringLiteral("a1b2c3d4-e5f6-7890-abcd-ef1234567890");
    ComfyUIUtils::decodeInpaintContextComboData(QVariant(uid), &ctx, &layerId);
    QCOMPARE(ctx, QStringLiteral("layer_bounds"));
    QCOMPARE(layerId, uid);

    const ComfyUIUtils::InpaintParams editing =
        ComfyUIUtils::customInpaintGetParams(QStringLiteral("blur"), true, true, true);
    QCOMPARE(editing.fillKind, QStringLiteral("none"));
    QVERIFY(editing.useInpaintModel);
    QVERIFY(editing.useConditionMask);

    const ComfyUIUtils::InpaintParams fill =
        ComfyUIUtils::customInpaintGetParams(QStringLiteral("blur"), false, false, false);
    QCOMPARE(fill.fillKind, QStringLiteral("blur"));

    QVERIFY(!ComfyUIUtils::customInpaintGetContext(KisImageSP(), QStringLiteral("automatic"), QString(), QRect(0, 0, 10, 10)));
    QVERIFY(!ComfyUIUtils::customInpaintGetContext(KisImageSP(), QStringLiteral("mask_bounds"), QString(), QRect(1, 2, 3, 4)));

    QCOMPARE(ComfyUIUtils::inpaintContextKeyFromJson(QJsonValue(1)), QStringLiteral("mask_bounds"));
    QCOMPARE(ComfyUIUtils::inpaintContextKeyFromJson(QJsonValue(QStringLiteral("entire_image"))),
             QStringLiteral("entire_image"));
    QCOMPARE(ComfyUIUtils::inpaintModeKeyFromJson(QJsonValue(6)), QStringLiteral("custom"));
    QCOMPARE(ComfyUIUtils::fillModeKeyFromJson(QJsonValue(2)), QStringLiteral("blur"));

    ComfyUIUtils::InpaintWorkspaceSnapshot snap;
    snap.mode = QStringLiteral("custom");
    snap.fill = QStringLiteral("blur");
    snap.context = QStringLiteral("layer_bounds");
    snap.contextLayerId = QStringLiteral("a1b2c3d4-e5f6-7890-abcd-ef1234567890");
    const QJsonObject saved = ComfyUIUtils::inpaintWorkspaceToJson(snap);
    QCOMPARE(saved.value(QStringLiteral("mode")).toInt(), 6);
    QCOMPARE(saved.value(QStringLiteral("fill")).toInt(), 2);
    QCOMPARE(saved.value(QStringLiteral("context")).toInt(), 3);
    QVERIFY(saved.value(QStringLiteral("context_layer_id")).toString().contains(QLatin1Char('{')));
    ComfyUIUtils::InpaintWorkspaceSnapshot loaded;
    QVERIFY(ComfyUIUtils::inpaintWorkspaceFromJson(saved, &loaded));
    QCOMPARE(loaded.mode, snap.mode);
    QCOMPARE(loaded.fill, snap.fill);
    QCOMPARE(loaded.context, snap.context);
    QCOMPARE(loaded.contextLayerId, snap.contextLayerId);

    // Python-plugin ui.json slice (serialize uses enum .value)
    QJsonObject pythonSlice;
    pythonSlice.insert(QStringLiteral("mode"), 6);
    pythonSlice.insert(QStringLiteral("fill"), 2);
    pythonSlice.insert(QStringLiteral("use_inpaint"), true);
    pythonSlice.insert(QStringLiteral("use_prompt_focus"), false);
    pythonSlice.insert(QStringLiteral("context"), 3);
    pythonSlice.insert(QStringLiteral("context_layer_id"),
                       QStringLiteral("{a1b2c3d4-e5f6-7890-abcd-ef1234567890}"));
    ComfyUIUtils::InpaintWorkspaceSnapshot fromPython;
    QVERIFY(ComfyUIUtils::inpaintWorkspaceFromJson(pythonSlice, &fromPython));
    QCOMPARE(fromPython.mode, QStringLiteral("custom"));
    QCOMPARE(fromPython.fill, QStringLiteral("blur"));
    QCOMPARE(fromPython.context, QStringLiteral("layer_bounds"));
    QCOMPARE(fromPython.contextLayerId, snap.contextLayerId);
}

void ComfyUIRemoteDockTest::testRecentlyUsedSyncDocumentDefaultsFields()
{
    QCOMPARE(ComfyUIUtils::inpaintContextForFreshDocumentDefaults(QStringLiteral("layer_bounds")),
             QStringLiteral("automatic"));

    QJsonObject settings;
    QJsonObject dd;
    dd.insert(QStringLiteral("inpaint_mode"), QStringLiteral("replace_background"));
    dd.insert(QStringLiteral("inpaint_fill"), QStringLiteral("border"));
    dd.insert(QStringLiteral("inpaint_use_model"), false);
    dd.insert(QStringLiteral("inpaint_use_prompt_focus"), true);
    dd.insert(QStringLiteral("inpaint_context"), QStringLiteral("entire_image"));
    dd.insert(QStringLiteral("batch_count"), 3);
    settings.insert(QStringLiteral("document_defaults"), dd);
    const QJsonObject loaded = ComfyUIUtils::documentDefaultsFromSettingsRoot(settings);
    QCOMPARE(loaded.value(QStringLiteral("inpaint_mode")).toString(), QStringLiteral("replace_background"));
    QCOMPARE(loaded.value(QStringLiteral("inpaint_fill")).toString(), QStringLiteral("border"));
    QCOMPARE(loaded.value(QStringLiteral("inpaint_use_model")).toBool(), false);
    QCOMPARE(loaded.value(QStringLiteral("inpaint_use_prompt_focus")).toBool(), true);
    QCOMPARE(loaded.value(QStringLiteral("inpaint_context")).toString(), QStringLiteral("entire_image"));
    QCOMPARE(loaded.value(QStringLiteral("batch_count")).toInt(), 3);
}

void ComfyUIRemoteDockTest::testCustomWorkflowKritaSelectionPrepare()
{
    QJsonObject selInputs;
    selInputs.insert(QStringLiteral("context"), QStringLiteral("mask_bounds"));
    selInputs.insert(QStringLiteral("padding"), 10);

    ComfyUIUtils::MaskFromSelectionResult mask;
    mask.valid = true;
    mask.originalBounds = QRect(100, 100, 50, 50);
    mask.paddedBounds = QRect(90, 90, 70, 70);
    mask.maskGray = QImage(70, 70, QImage::Format_Grayscale8);
    mask.maskGray.fill(255);

    const QRect doc(0, 0, 512, 512);
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult prep =
        ComfyUIUtils::prepareCustomWorkflowMask(selInputs, mask, doc);
    QVERIFY(prep.hasSelectionMask);
    QVERIFY(prep.captureBounds.width() >= 50);
    QVERIFY(!prep.maskInCaptureCoords.isNull());

    const ComfyUIUtils::SelectionModifiers mods =
        ComfyUIUtils::getSelectionModifiersForContext(QStringLiteral("mask_bounds"), 1.0);
    QCOMPARE(mods.multiple, 1);
    QCOMPARE(mods.sizeMinPx, 0);

    QCOMPARE(ComfyUIUtils::getInpaintContextFromSelectionNode(selInputs), QStringLiteral("mask_bounds"));
    QVERIFY(ComfyUIUtils::workflowContainsKritaInjectionNodes(
        QJsonObject{{QStringLiteral("9"),
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaSelection")}}}}));
}

void ComfyUIRemoteDockTest::testCustomWorkflowKritaSelectionPrepareAndExpand()
{
    QJsonObject autoInputs;
    autoInputs.insert(QStringLiteral("context"), QStringLiteral("automatic"));
    autoInputs.insert(QStringLiteral("padding"), 8);

    ComfyUIUtils::MaskFromSelectionResult mask;
    mask.valid = true;
    mask.originalBounds = QRect(100, 100, 50, 50);
    mask.paddedBounds = QRect(80, 80, 90, 90);
    mask.maskGray = QImage(90, 90, QImage::Format_Grayscale8);
    mask.maskGray.fill(255);

    const QRect doc(0, 0, 512, 512);
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult autoPrep =
        ComfyUIUtils::prepareCustomWorkflowMask(autoInputs, mask, doc);
    QVERIFY(autoPrep.ok);
    QVERIFY(autoPrep.hasSelectionMask);
    QVERIFY(autoPrep.captureBounds.width() >= 50);
    QCOMPARE(autoPrep.maskInCaptureCoords.size(), autoPrep.captureBounds.size());

    // Upstream test_prepare_mask: automatic + padding 3 on 40×40 mask → 48×48 bounds
    QJsonObject autoPadInputs;
    autoPadInputs.insert(QStringLiteral("context"), QStringLiteral("automatic"));
    autoPadInputs.insert(QStringLiteral("padding"), 3);
    ComfyUIUtils::MaskFromSelectionResult padMask;
    padMask.valid = true;
    padMask.originalBounds = QRect(10, 10, 40, 40);
    padMask.paddedBounds = QRect(10, 10, 40, 40);
    padMask.maskGray = QImage(40, 40, QImage::Format_Grayscale8);
    padMask.maskGray.fill(255);
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult autoPadPrep =
        ComfyUIUtils::prepareCustomWorkflowMask(autoPadInputs, padMask, QRect(0, 0, 100, 100));
    QVERIFY(autoPadPrep.ok);
    QCOMPARE(autoPadPrep.captureBounds, QRect(6, 6, 48, 48));

    QJsonObject maskBoundsInputs;
    maskBoundsInputs.insert(QStringLiteral("context"), QStringLiteral("mask_bounds"));
    maskBoundsInputs.insert(QStringLiteral("padding"), 3);
    ComfyUIUtils::MaskFromSelectionResult selMask;
    selMask.valid = true;
    selMask.originalBounds = QRect(12, 12, 34, 34);
    selMask.paddedBounds = QRect(10, 10, 40, 40);
    selMask.maskGray = QImage(40, 40, QImage::Format_Grayscale8);
    selMask.maskGray.fill(255);
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult maskBoundsPrep =
        ComfyUIUtils::prepareCustomWorkflowMask(maskBoundsInputs, selMask, QRect(0, 0, 100, 100));
    QVERIFY(maskBoundsPrep.ok);
    QCOMPARE(maskBoundsPrep.captureBounds, QRect(9, 9, 40, 40));

    QJsonObject entireInputs;
    entireInputs.insert(QStringLiteral("context"), QStringLiteral("entire_image"));
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult entirePrep =
        ComfyUIUtils::prepareCustomWorkflowMask(entireInputs, mask, doc);
    QCOMPARE(entirePrep.captureBounds, doc);

    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaSelection")},
                          {QStringLiteral("inputs"), autoInputs}});
    wf.insert(QStringLiteral("2"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("seed"), QJsonArray{QStringLiteral("1"), 2}},
                                       {QStringLiteral("steps"), QJsonArray{QStringLiteral("1"), 3}},
                                       {QStringLiteral("cfg"), QJsonArray{QStringLiteral("1"), 1}},
                                       {QStringLiteral("sampler_name"), QStringLiteral("euler")},
                                       {QStringLiteral("scheduler"), QStringLiteral("normal")},
                                       {QStringLiteral("denoise"), 1.0},
                                       {QStringLiteral("model"), QJsonArray{QStringLiteral("9"), 0}},
                                       {QStringLiteral("positive"), QJsonArray{QStringLiteral("9"), 0}},
                                       {QStringLiteral("negative"), QJsonArray{QStringLiteral("9"), 0}},
                                       {QStringLiteral("latent_image"), QJsonArray{QStringLiteral("9"), 0}}}}});

    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams p;
    p.workflow = wf;
    p.maskImageName = QStringLiteral("mask.png");
    p.captureBounds = autoPrep.captureBounds;
    p.hasSelectionMask = true;
    p.seed = 99;

    const QJsonObject out = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(p);
    QVERIFY(!out.contains(QStringLiteral("1")));
    const QJsonObject samplerInputs = out.value(QStringLiteral("2")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(samplerInputs.value(QStringLiteral("seed")).toInt(), autoPrep.captureBounds.x());
    QCOMPARE(samplerInputs.value(QStringLiteral("steps")).toInt(), autoPrep.captureBounds.y());
    QVERIFY(samplerInputs.value(QStringLiteral("cfg")).toBool());

    bool foundMaskLoad = false;
    for (auto it = out.constBegin(); it != out.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        if (node.value(QStringLiteral("class_type")).toString() == QLatin1String("LoadImage")
            && node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image")).toString()
                   == QLatin1String("mask.png")) {
            foundMaskLoad = true;
            break;
        }
    }
    QVERIFY(foundMaskLoad);

    QCOMPARE(ComfyUIUtils::layerPlaceholderReplacementForArch(ComfyResources::Arch::Flux2_4b),
             QStringLiteral("image {}"));
    QString fluxPrompt = QStringLiteral("scene <layer:Hair>");
    ComfyUIUtils::extractLayerPlaceholders(fluxPrompt, QStringLiteral("image {}"));
    QCOMPARE(fluxPrompt, QStringLiteral("scene image 1"));

    ComfyStyleEntry style;
    style.stylePrompt = QStringLiteral("{prompt}");
    style.loras = QJsonArray();
    const ComfyUIUtils::CustomWorkflowEvaluatedPrompts fluxEv =
        ComfyUIUtils::prepareCustomWorkflowStyleAndPrompts(QStringLiteral("sky <layer:BG>"), QString(), &style, 1, 7.0,
                                                           QString(), ComfyResources::Arch::Flux2_9b);
    QVERIFY(fluxEv.ok);
    QVERIFY(fluxEv.positiveFinal.contains(QStringLiteral("image 1")));
    QVERIFY(!fluxEv.positiveFinal.contains(QStringLiteral("<layer:")));
}

void ComfyUIRemoteDockTest::testCustomWorkflowInvalidSelectionContext()
{
    QVERIFY(ComfyUIUtils::isValidCustomWorkflowSelectionContext(QStringLiteral("automatic")));
    QVERIFY(ComfyUIUtils::isValidCustomWorkflowSelectionContext(QStringLiteral("mask_bounds")));
    QVERIFY(ComfyUIUtils::isValidCustomWorkflowSelectionContext(QStringLiteral("entire_image")));
    QVERIFY(!ComfyUIUtils::isValidCustomWorkflowSelectionContext(QStringLiteral("layer_bounds")));

    QJsonObject badInputs;
    badInputs.insert(QStringLiteral("context"), QStringLiteral("layer_bounds"));
    ComfyUIUtils::MaskFromSelectionResult mask;
    mask.valid = true;
    mask.originalBounds = QRect(10, 10, 20, 20);
    mask.paddedBounds = QRect(5, 5, 30, 30);
    mask.maskGray = QImage(30, 30, QImage::Format_Grayscale8);
    mask.maskGray.fill(255);
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult badPrep =
        ComfyUIUtils::prepareCustomWorkflowMask(badInputs, mask, QRect(0, 0, 128, 128));
    QVERIFY(!badPrep.ok);
    QVERIFY(!badPrep.errorMessage.isEmpty());
}

void ComfyUIRemoteDockTest::testCustomWorkflowLayerExportAndFingerprint()
{
    QImage doc(64, 64, QImage::Format_ARGB32);
    doc.fill(Qt::black);
    QPainter p(&doc);
    p.fillRect(20, 20, 10, 10, Qt::white);
    p.end();
    const QRect exportRect(20, 20, 10, 10);
    const QImage cropped = ComfyUIUtils::cropImageToDocumentRect(doc, exportRect, QRect(0, 0, 64, 64));
    QCOMPARE(cropped.size(), exportRect.size());

    ComfyUIUtils::CustomWorkflowKritaCapture capture;
    capture.ok = true;
    capture.captureBounds = exportRect;
    capture.canvasImage = cropped;
    capture.hasSelectionMask = false;
    const QByteArray fp1 = ComfyUIUtils::computeCustomWorkflowInputFingerprint(
        QJsonObject{{QStringLiteral("1"), QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")}}}},
        capture, 42, QStringLiteral("cat"), QString(), QJsonArray(), {});
    const QByteArray fp2 = ComfyUIUtils::computeCustomWorkflowInputFingerprint(
        QJsonObject{{QStringLiteral("1"), QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")}}}},
        capture, 42, QStringLiteral("cat"), QString(), QJsonArray(), {});
    QCOMPARE(fp1, fp2);
    const QByteArray fp3 = ComfyUIUtils::computeCustomWorkflowInputFingerprint(
        QJsonObject{{QStringLiteral("1"), QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")}}}},
        capture, 43, QStringLiteral("cat"), QString(), QJsonArray(), {});
    QVERIFY(fp3 != fp1);
}

void ComfyUIRemoteDockTest::testCustomWorkflowFullSelectionPipeline()
{
    // P9 #14 unit chain: prepare_mask → capture bounds → expand ETN_KritaSelection outputs.
    QJsonObject selInputs;
    selInputs.insert(QStringLiteral("context"), QStringLiteral("mask_bounds"));
    selInputs.insert(QStringLiteral("padding"), 4);

    ComfyUIUtils::MaskFromSelectionResult mask;
    mask.valid = true;
    mask.originalBounds = QRect(64, 64, 32, 32);
    mask.paddedBounds = QRect(56, 56, 48, 48);
    mask.maskGray = QImage(48, 48, QImage::Format_Grayscale8);
    mask.maskGray.fill(255);

    const QRect doc(0, 0, 256, 256);
    const ComfyUIUtils::CustomWorkflowMaskPrepareResult prep =
        ComfyUIUtils::prepareCustomWorkflowMask(selInputs, mask, doc);
    QVERIFY(prep.ok);
    QVERIFY(prep.hasSelectionMask);
    QCOMPARE(prep.maskInCaptureCoords.size(), prep.captureBounds.size());

    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaSelection")},
                          {QStringLiteral("inputs"), selInputs}});
    wf.insert(QStringLiteral("2"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaCanvas")},
                          {QStringLiteral("inputs"), QJsonObject{}}});
    wf.insert(QStringLiteral("3"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("seed"), QJsonArray{QStringLiteral("1"), 2}},
                                       {QStringLiteral("steps"), QJsonArray{QStringLiteral("1"), 3}},
                                       {QStringLiteral("cfg"), QJsonArray{QStringLiteral("1"), 1}}}}});

    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams ep;
    ep.workflow = wf;
    ep.canvasImageName = QStringLiteral("canvas.png");
    ep.maskImageName = QStringLiteral("mask.png");
    ep.captureBounds = prep.captureBounds;
    ep.hasSelectionMask = true;
    ep.seed = 7;

    const QJsonObject expanded = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(ep);
    QVERIFY(!expanded.contains(QStringLiteral("1")));
    QVERIFY(!expanded.contains(QStringLiteral("2")));
    const QJsonObject samplerInputs =
        expanded.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(samplerInputs.value(QStringLiteral("seed")).toInt(), prep.captureBounds.x());
    QCOMPARE(samplerInputs.value(QStringLiteral("steps")).toInt(), prep.captureBounds.y());
    QVERIFY(samplerInputs.value(QStringLiteral("cfg")).toBool());

    ComfyUIUtils::CustomWorkflowKritaCapture capture;
    capture.ok = true;
    capture.captureBounds = prep.captureBounds;
    capture.canvasImage = prep.maskInCaptureCoords.convertToFormat(QImage::Format_ARGB32);
    capture.maskImage = prep.maskInCaptureCoords;
    capture.hasSelectionMask = true;
    const QByteArray fp = ComfyUIUtils::computeCustomWorkflowInputFingerprint(
        wf, capture, 7, QStringLiteral("test prompt"), QString(), QJsonArray(), {});
    QVERIFY(!fp.isEmpty());

    ComfyStyleEntry style;
    style.stylePrompt = QStringLiteral("{prompt}");
    style.negativePrompt = QStringLiteral("bad");
    style.loras = QJsonArray();
    const ComfyUIUtils::CustomWorkflowEvaluatedPrompts ev =
        ComfyUIUtils::prepareCustomWorkflowStyleAndPrompts(QStringLiteral("sunset"), QStringLiteral("blur"), &style, 9,
                                                           7.0, QString(), ComfyResources::Arch::Sd15);
    QVERIFY(ev.ok);
    QCOMPARE(ev.metadata.value(QStringLiteral("prompt")).toString(), QStringLiteral("sunset"));
    QCOMPARE(ev.metadata.value(QStringLiteral("prompt_final")).toString(), QStringLiteral("sunset"));
    QCOMPARE(ev.negativeFinal, QStringLiteral("bad, blur"));
}

void ComfyUIRemoteDockTest::testExpandCustomKritaWorkflowNodes()
{
    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaCanvas")},
                          {QStringLiteral("inputs"), QJsonObject{}}});
    wf.insert(QStringLiteral("2"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaSelection")},
                          {QStringLiteral("inputs"), QJsonObject{}}});
    wf.insert(QStringLiteral("3"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("images"), QJsonArray{QStringLiteral("1"), 0}}}}});

    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams p;
    p.workflow = wf;
    p.canvasImageName = QStringLiteral("canvas.png");
    p.maskImageName = QStringLiteral("mask.png");
    p.captureBounds = QRect(10, 20, 100, 80);
    p.hasSelectionMask = true;
    p.seed = 42;

    const QJsonObject out = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(p);
    QVERIFY(!out.contains(QStringLiteral("1")));
    QVERIFY(!out.contains(QStringLiteral("2")));
    QVERIFY(out.contains(QStringLiteral("3")));
    const QJsonArray imgLink =
        out.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("images")).toArray();
    QVERIFY(imgLink.at(0).toString() != QStringLiteral("1"));
    QCOMPARE(imgLink.at(0).toString(), QStringLiteral("4"));
}

void ComfyUIRemoteDockTest::testExpandCustomKritaWorkflowParameterAndStyle()
{
    QVERIFY(ComfyUIUtils::workflowNeedsCustomKritaExpansion(
        QJsonObject{{QStringLiteral("5"),
                     QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_Parameter")}}}}));
    QVERIFY(!ComfyUIUtils::workflowNeedsCustomKritaExpansion(
        QJsonObject{{QStringLiteral("5"), QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")}}}}));

    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_Parameter")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("name"), QStringLiteral("cfg_scale")},
                                       {QStringLiteral("default"), 7.5}}}});
    wf.insert(QStringLiteral("2"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("KSampler")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("cfg"), QJsonArray{QStringLiteral("1"), 0}}}}});

    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams p;
    p.workflow = wf;
    p.captureBounds = QRect(0, 0, 64, 64);
    const QJsonObject paramOut = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(p);
    QVERIFY(!paramOut.contains(QStringLiteral("1")));
    const QJsonValue cfgVal =
        paramOut.value(QStringLiteral("2")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("cfg"));
    QCOMPARE(cfgVal.toDouble(), 7.5);

    QJsonObject styleWf;
    styleWf.insert(QStringLiteral("10"),
                   QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaStyleAndPrompt")},
                               {QStringLiteral("inputs"), QJsonObject{}}});
    styleWf.insert(QStringLiteral("11"),
                   QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPTextEncode")},
                               {QStringLiteral("inputs"),
                                QJsonObject{{QStringLiteral("text"), QJsonArray{QStringLiteral("10"), 3}},
                                            {QStringLiteral("clip"), QJsonArray{QStringLiteral("10"), 1}}}}});
    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams sp;
    sp.workflow = styleWf;
    sp.captureBounds = QRect(0, 0, 512, 512);
    sp.checkpoint = QStringLiteral("sd_xl_base.safetensors");
    sp.positivePrompt = QStringLiteral("a cat");
    sp.negativePrompt = QStringLiteral("blurry");
    sp.sampler = QStringLiteral("dpmpp_2m");
    sp.scheduler = QStringLiteral("karras");
    sp.steps = 25;
    sp.cfg = 6.0;
    const QJsonObject styleOut = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(sp);
    QVERIFY(!styleOut.contains(QStringLiteral("10")));
    QVERIFY(styleOut.contains(QStringLiteral("11")));
    const QString text =
        styleOut.value(QStringLiteral("11")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("text")).toString();
    QCOMPARE(text, QStringLiteral("a cat"));
}

void ComfyUIRemoteDockTest::testCustomWorkflowLiveCapturePolicy()
{
    QVERIFY(ComfyUIUtils::customWorkflowCaptureExcludesInternal(false));
    QVERIFY(!ComfyUIUtils::customWorkflowCaptureExcludesInternal(true));
    QVERIFY(!ComfyUIUtils::customWorkflowNodeUsesLiveSampling(QStringLiteral("regular"), true));
    QVERIFY(ComfyUIUtils::customWorkflowNodeUsesLiveSampling(QStringLiteral("live"), false));
    QVERIFY(ComfyUIUtils::customWorkflowNodeUsesLiveSampling(QStringLiteral("auto"), true));

    QJsonObject wf;
    wf.insert(QStringLiteral("1"),
              QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ETN_KritaStyle")},
                          {QStringLiteral("inputs"),
                           QJsonObject{{QStringLiteral("name"), QStringLiteral("style")},
                                       {QStringLiteral("sampler_preset"), QStringLiteral("live")}}}});
    QVERIFY(ComfyUIUtils::customWorkflowNodeUsesLiveSampling(QStringLiteral("live"), false));

    ComfyWorkflowEngine::ExpandCustomKritaWorkflowParams ks;
    ks.workflow = wf;
    ks.captureBounds = QRect(0, 0, 64, 64);
    ComfyWorkflowEngine::CustomWorkflowStyleExpandInput styleIn;
    styleIn.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    styleIn.positivePrompt = QStringLiteral("style pos");
    styleIn.negativePrompt = QStringLiteral("style neg");
    styleIn.sampler = QStringLiteral("euler");
    styleIn.steps = 12;
    styleIn.cfg = 5.5;
    ks.kritaStyleByNodeId.insert(QStringLiteral("1"), styleIn);
    ks.workflow.insert(QStringLiteral("2"),
                       QJsonObject{{QStringLiteral("class_type"), QStringLiteral("CLIPTextEncode")},
                                   {QStringLiteral("inputs"),
                                    QJsonObject{{QStringLiteral("text"), QJsonArray{QStringLiteral("1"), 3}},
                                                {QStringLiteral("clip"), QJsonArray{QStringLiteral("1"), 1}}}}});
    const QJsonObject styleOut = ComfyWorkflowEngine::expandCustomKritaWorkflowNodes(ks);
    QVERIFY(!styleOut.contains(QStringLiteral("1")));
    QCOMPARE(styleOut.value(QStringLiteral("2"))
                 .toObject()
                 .value(QStringLiteral("inputs"))
                 .toObject()
                 .value(QStringLiteral("text"))
                 .toString(),
             QStringLiteral("style pos"));
}

void ComfyUIRemoteDockTest::testPrepareCustomWorkflowStyleAndPrompts()
{
    ComfyStyleEntry style;
    style.stylePrompt = QStringLiteral("masterpiece, {prompt}");
    style.negativePrompt = QStringLiteral("low quality");
    style.loras = QJsonArray();

    const ComfyUIUtils::CustomWorkflowEvaluatedPrompts ev =
        ComfyUIUtils::prepareCustomWorkflowStyleAndPrompts(QStringLiteral("a cat"), QStringLiteral("blur"),
                                                           &style, 42, 7.0, QString());
    QVERIFY(ev.positiveFinal.contains(QStringLiteral("a cat")));
    QVERIFY(ev.positiveFinal.contains(QStringLiteral("masterpiece")));
    QCOMPARE(ev.negativeFinal, QStringLiteral("low quality, blur"));

    const ComfyUIUtils::CustomWorkflowEvaluatedPrompts noNeg =
        ComfyUIUtils::prepareCustomWorkflowStyleAndPrompts(QStringLiteral("x"), QStringLiteral("y"), &style, 1, 1.0,
                                                           QString());
    QVERIFY(noNeg.negativeFinal.isEmpty());
}

void ComfyUIRemoteDockTest::testExtractLorasFromPromptAndMerge()
{
    ComfyFileLibrary::instance().init();
    ComfyFileLibrary::instance().updateRemoteLoras({QStringLiteral("hero.safetensors")});

    const ComfyUIUtils::ExtractLorasFromPromptResult extracted =
        ComfyUIUtils::extractLorasFromPrompt(QStringLiteral("a cat <lora:hero:0.75>"));
    QCOMPARE(extracted.cleanedPrompt, QStringLiteral("a cat"));
    QCOMPARE(extracted.loras.size(), 1);
    QCOMPARE(extracted.loras.first().name, QStringLiteral("hero.safetensors"));
    QCOMPARE(extracted.loras.first().strength, 0.75);

    QList<ComfyWorkflowEngine::CheckpointLoraWeight> base;
    ComfyWorkflowEngine::CheckpointLoraWeight existing;
    existing.name = QStringLiteral("hero.safetensors");
    existing.strengthModel = 0.5;
    existing.strengthClip = 0.5;
    base.append(existing);
    ComfyWorkflowEngine::CheckpointLoraWeight fromPrompt;
    fromPrompt.name = QStringLiteral("hero.safetensors");
    fromPrompt.strengthModel = 0.75;
    fromPrompt.strengthClip = 0.75;
    const QList<ComfyWorkflowEngine::CheckpointLoraWeight> merged =
        ComfyWorkflowEngine::mergeCheckpointLorasUnique(base, {fromPrompt});
    QCOMPARE(merged.size(), 1);
    QCOMPARE(merged.first().strengthModel, 0.75);

    const ComfyUIUtils::ExtractLorasFromPromptResult missing =
        ComfyUIUtils::extractLorasFromPrompt(QStringLiteral("<lora:nonexistent:1>"));
    QVERIFY(!missing.errorMessage.isEmpty());

    ComfyStyleEntry style;
    style.stylePrompt = QStringLiteral("{prompt}");
    style.negativePrompt = QString();
    style.loras = QJsonArray();
    const ComfyUIUtils::CustomWorkflowEvaluatedPrompts withLora =
        ComfyUIUtils::prepareCustomWorkflowStyleAndPrompts(QStringLiteral("scene <lora:hero:0.5>"), QString(), &style,
                                                           1, 7.0, QString());
    QVERIFY(withLora.ok);
    QVERIFY(!withLora.positiveFinal.contains(QStringLiteral("<lora:")));
    QCOMPARE(withLora.promptLoras.size(), 1);
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildTextToImage()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.checkpoint = QStringLiteral("sd_xl_base.safetensors");
    p.arch = ComfyResources::Arch::Sdxl;
    p.width = 768;
    p.height = 512;
    p.seed = 42;
    p.steps = 30;
    p.cfg = 6.5;
    p.positivePrompt = QStringLiteral("mountain lake");
    p.negativePrompt = QStringLiteral("blurry");
    const QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    QVERIFY(!wf.isEmpty());
    const QJsonObject i3 = wf.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(i3.value(QStringLiteral("steps")).toInt(), 30);
    QCOMPARE(static_cast<qint64>(i3.value(QStringLiteral("seed")).toDouble()), 42);
    const QJsonObject i5 = wf.value(QStringLiteral("5")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(i5.value(QStringLiteral("width")).toInt(), 768);
    QCOMPARE(i5.value(QStringLiteral("height")).toInt(), 512);
    const QJsonObject i6 = wf.value(QStringLiteral("6")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(i6.value(QStringLiteral("text")).toString(), QStringLiteral("mountain lake"));
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineApplyCheckpointStyleOptions()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.width = 512;
    p.height = 512;
    p.positivePrompt = QStringLiteral("test");
    QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    QVERIFY(!wf.isEmpty());
    ComfyWorkflowEngine::applyCheckpointStyleOptions(
        &wf, QStringLiteral("ae.safetensors"), 2, ComfyResources::Arch::Sd15);
    bool hasClipLayer = false;
    bool hasVaeLoader = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString ct = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (ct == QLatin1String("CLIPSetLastLayer"))
            hasClipLayer = true;
        if (ct == QLatin1String("VAELoader"))
            hasVaeLoader = true;
    }
    QVERIFY(hasClipLayer);
    QVERIFY(hasVaeLoader);

    QJsonObject wfFlux = ComfyWorkflowEngine::buildTextToImage(p);
    ComfyWorkflowEngine::applyCheckpointStyleOptions(&wfFlux, QString(), 2, ComfyResources::Arch::Flux);
    hasClipLayer = false;
    for (auto it = wfFlux.constBegin(); it != wfFlux.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == QLatin1String("CLIPSetLastLayer"))
            hasClipLayer = true;
    }
    QVERIFY(!hasClipLayer);
}

void ComfyUIRemoteDockTest::testComfyStyleCollectionEntryToJson()
{
    ComfyStyleEntry e;
    e.name = QStringLiteral("Test Style");
    e.architecture = QStringLiteral("sd15");
    e.checkpoints = QStringList{QStringLiteral("ckpt.safetensors")};
    e.vae = QStringLiteral("vae-ft-mse.safetensors");
    e.clipSkip = 2;
    e.preferredResolution = 768;
    e.vPredictionZsnr = true;
    e.selfAttentionGuidance = true;
    const QJsonObject o = ComfyStyleCollection::instance().entryToJson(e);
    QCOMPARE(o.value(QStringLiteral("vae")).toString(), QStringLiteral("vae-ft-mse.safetensors"));
    QCOMPARE(o.value(QStringLiteral("clip_skip")).toInt(), 2);
    QCOMPARE(o.value(QStringLiteral("preferred_resolution")).toInt(), 768);
    QVERIFY(o.value(QStringLiteral("v_prediction_zsnr")).toBool());
    QVERIFY(o.value(QStringLiteral("self_attention_guidance")).toBool());
}

void ComfyUIRemoteDockTest::testComfyLocalizationTranslate()
{
    ComfyLocalization::instance().loadLanguageForTest(
        QStringLiteral("fr"), QStringLiteral("Français"),
        {{QStringLiteral("Add"), QStringLiteral("Ajouter")},
         {QStringLiteral("Missing key"), QStringLiteral("Should not appear")}});
    QCOMPARE(ComfyLocalization::instance().translate(QStringLiteral("Add")), QStringLiteral("Ajouter"));
    QCOMPARE(ComfyLocalization::instance().translate(QStringLiteral("Unknown")), QStringLiteral("Unknown"));
    QCOMPARE(ComfyLocalization::instance().translate(QStringLiteral("Hello %1"), QStringLiteral("world")),
             QStringLiteral("Hello world"));
}

void ComfyUIRemoteDockTest::testComfyLocalizationLoadFrenchJson()
{
    const QString path = ComfyUIUtils::pluginInstallDataDir() + QStringLiteral("/language/fr.json");
    if (!QFile::exists(path))
        QSKIP("Bundled fr.json not available (dev tree or install data missing)");
    ComfyLocalization::instance().init();
    bool foundFr = false;
    for (const ComfyLanguageInfo &lang : ComfyLocalization::instance().availableLanguages()) {
        if (lang.id == QLatin1String("fr")) {
            foundFr = true;
            break;
        }
    }
    QVERIFY(foundFr);
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject trans = root.value(QStringLiteral("translations")).toObject();
    QHash<QString, QString> map;
    for (auto it = trans.constBegin(); it != trans.constEnd(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty())
            map.insert(it.key(), it.value().toString());
    }
    ComfyLocalization::instance().loadLanguageForTest(QStringLiteral("fr"), QStringLiteral("Français"), map);
    const QString add = ComfyLocalization::instance().translate(QStringLiteral("Add"));
    QVERIFY(add.contains(QStringLiteral("jouter"), Qt::CaseInsensitive));
}

void ComfyUIRemoteDockTest::testComfyFileRecordHashAndSerialization()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/test_lora.safetensors");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("lora-bytes");
    f.close();

    ComfyFileRecord rec = ComfyFileRecord::local(path, ComfyFileFormat::Lora, true);
    QVERIFY(!rec.hash.isEmpty());
    QCOMPARE(ComfyFileLibraryUtil::sha256Base64OfFile(path), rec.hash);

    rec.setMeta(QStringLiteral("strength_percent"), 80);
    rec.setMeta(QStringLiteral("enabled"), true);
    const QJsonObject o = rec.toJson();
    ComfyFileRecord round = ComfyFileRecord::fromJson(o);
    QCOMPARE(round.id, rec.id);
    QCOMPARE(round.hash, rec.hash);
    QCOMPARE(round.meta(QStringLiteral("strength_percent")).toInt(), 80);
    QVERIFY(round.meta(QStringLiteral("enabled")).toBool());
}

void ComfyUIRemoteDockTest::testComfyFileLibraryPreferredCheckpoint()
{
    const QStringList style = {QStringLiteral("missing.ckpt"), QStringLiteral("dreamshaper_8.safetensors")};
    const QStringList server = {QStringLiteral("other.safetensors"), QStringLiteral("dreamshaper_8.safetensors")};
    QCOMPARE(ComfyFileLibrary::preferredCheckpoint(style, server), QStringLiteral("dreamshaper_8.safetensors"));
    QCOMPARE(ComfyFileLibrary::preferredCheckpoint(QStringList{QStringLiteral("nope")}, server),
             QStringLiteral("not-found"));
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildRefine()
{
    ComfyWorkflowEngine::RefineParams p;
    p.checkpoint = QStringLiteral("sd_xl_base.safetensors");
    p.imageName = QStringLiteral("canvas.png");
    p.denoise = 0.4;
    p.positivePrompt = QStringLiteral("refined scene");
    p.negativePrompt = QStringLiteral("blur");
    const QJsonObject wf = ComfyWorkflowEngine::buildRefine(p);
    QVERIFY(!wf.isEmpty());
    QCOMPARE(wf.value(QStringLiteral("1")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image")).toString(),
             QStringLiteral("canvas.png"));
    const QJsonObject i6 = wf.value(QStringLiteral("6")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(i6.value(QStringLiteral("denoise")).toDouble(), 0.4);
    const QJsonArray latent = i6.value(QStringLiteral("latent_image")).toArray();
    QCOMPARE(latent.at(0).toString(), QStringLiteral("2"));
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildInpaint()
{
    ComfyWorkflowEngine::InpaintBuildParams p;
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.imageName = QStringLiteral("canvas.png");
    p.maskImageName = QStringLiteral("mask.png");
    p.growMaskBy = 12;
    p.denoise = 0.6;
    p.positivePrompt = QStringLiteral("inpaint subject");
    const QJsonObject wf = ComfyWorkflowEngine::buildInpaint(p);
    QVERIFY(!wf.isEmpty());
    QCOMPARE(wf.value(QStringLiteral("1")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image")).toString(),
             QStringLiteral("canvas.png"));
    QCOMPARE(wf.value(QStringLiteral("2")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image")).toString(),
             QStringLiteral("mask.png"));
    const QJsonObject i7 = wf.value(QStringLiteral("7")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(i7.value(QStringLiteral("grow_mask_by")).toInt(), 0);
    bool hasSamplerCustom = false;
    int splitStep = -1;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        const QString cls = node.value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("SamplerCustomAdvanced"))
            hasSamplerCustom = true;
        if (cls == QLatin1String("SplitSigmas"))
            splitStep = node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("step")).toInt();
    }
    QVERIFY(hasSamplerCustom);
    QVERIFY(!wf.contains(QStringLiteral("8")));
    QVERIFY(splitStep > 0);
    QCOMPARE(splitStep, 8);
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildLive()
{
    ComfyWorkflowEngine::LiveParams p;
    p.imageName = QStringLiteral("live_canvas.png");
    p.denoise = 0.55;
    p.steps = 8;
    p.positivePrompt = QStringLiteral("live scene");
    const QJsonObject wf = ComfyWorkflowEngine::buildLive(p);
    QVERIFY(!wf.isEmpty());
    QCOMPARE(wf.value(QStringLiteral("1")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("image")).toString(),
             QStringLiteral("live_canvas.png"));
    const double denoise = wf.value(QStringLiteral("6")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("denoise")).toDouble();
    QCOMPARE(denoise, 0.55);
    QCOMPARE(wf.value(QStringLiteral("6")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("steps")).toInt(), 8);
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildAnimationFrame()
{
    QCOMPARE(ComfyWorkflowEngine::animationFrameSeed(100, 0, 4), static_cast<qint64>(100));
    QCOMPARE(ComfyWorkflowEngine::animationFrameSeed(100, 2, 4), static_cast<qint64>(108));

    ComfyWorkflowEngine::AnimationFrameParams af;
    af.base.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    af.base.width = 768;
    af.base.height = 512;
    af.base.positivePrompt = QStringLiteral("frame prompt");
    af.batchBaseSeed = 42;
    af.frameIndex = 3;
    af.batchSeedStep = 2;
    const QJsonObject wf = ComfyWorkflowEngine::buildAnimationFrame(af);
    QVERIFY(!wf.isEmpty());
    const double seed = wf.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("seed")).toDouble();
    QCOMPARE(static_cast<qint64>(seed), static_cast<qint64>(48));
    const int w = wf.value(QStringLiteral("5")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("width")).toInt();
    QCOMPARE(w, 768);
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildUpscaleSimple()
{
    ComfyWorkflowEngine::UpscaleSimpleParams p;
    p.imageName = QStringLiteral("up.png");
    p.targetWidth = 2048;
    p.targetHeight = 1536;
    const QJsonObject wf = ComfyWorkflowEngine::buildUpscaleSimple(p);
    QVERIFY(!wf.isEmpty());
    const QJsonObject i2 = wf.value(QStringLiteral("2")).toObject().value(QStringLiteral("inputs")).toObject();
    QCOMPARE(i2.value(QStringLiteral("width")).toInt(), 2048);
    QCOMPARE(i2.value(QStringLiteral("height")).toInt(), 1536);
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildUpscaleRefine()
{
    ComfyWorkflowEngine::UpscaleRefineParams p;
    p.imageName = QStringLiteral("up.png");
    p.scaleWidth = 1024;
    p.denoise = 0.3;
    p.positivePrompt = QStringLiteral("sharp details");
    const QJsonObject wf = ComfyWorkflowEngine::buildUpscaleRefine(p);
    QVERIFY(!wf.isEmpty());
    bool hasSamplerCustom = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("SamplerCustomAdvanced"))
            hasSamplerCustom = true;
    }
    QVERIFY(hasSamplerCustom);
    QVERIFY(!wf.contains(QStringLiteral("7")));
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineBuildUpscaleTiled()
{
    ComfyWorkflowEngine::UpscaleTiledParams p;
    p.imageName = QStringLiteral("up.png");
    p.scaledWidth = 1024;
    p.scaledHeight = 768;
    p.minTileSize = 512;
    p.tileOverlapPx = 32;
    const QJsonObject wf = ComfyWorkflowEngine::buildUpscaleTiled(p);
    QVERIFY(!wf.isEmpty());
    bool hasTileLayout = false;
    bool hasMerge = false;
    for (auto it = wf.begin(); it != wf.end(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QStringLiteral("ETN_TileLayout"))
            hasTileLayout = true;
        if (cls == QStringLiteral("ETN_MergeImageTile"))
            hasMerge = true;
    }
    QVERIFY(hasTileLayout);
    QVERIFY(hasMerge);
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineFluxCfgCap()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.arch = ComfyResources::Arch::Flux;
    p.cfg = 8.0;
    const QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    const double cfg = wf.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("cfg")).toDouble();
    QCOMPARE(cfg, 3.5);
}

void ComfyUIRemoteDockTest::testComfyControlLayerJsonRoundtrip()
{
    ComfyControlLayerEntry e;
    e.mode = QStringLiteral("depth");
    e.layerName = QStringLiteral("Sketch");
    e.presetValue = 2;
    e.strength = 75;
    const QJsonObject o = e.toJson();
    const ComfyControlLayerEntry back = ComfyControlLayerEntry::fromJson(o);
    QCOMPARE(back.mode, e.mode);
    QCOMPARE(back.layerName, e.layerName);
    QVERIFY(ComfyControlLayer::allModeKeys().contains(QStringLiteral("reference")));
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineApplyControlNet()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.arch = ComfyResources::Arch::Sd15;
    QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    ComfyWorkflowEngine::ControlNetLayerInput in;
    in.mode = QStringLiteral("depth");
    in.imageName = QStringLiteral("control_0.png");
    in.strength = 0.8;
    in.startPercent = 0.0;
    in.endPercent = 1.0;
    QVERIFY(ComfyWorkflowEngine::applyControlNetLayers(&wf, {in}, ComfyResources::Arch::Sd15));
    const QJsonArray pos = wf.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("positive")).toArray();
    QVERIFY(!pos.isEmpty());
    QVERIFY(wf.contains(QStringLiteral("50")) || wf.contains(QStringLiteral("51")));
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineApplyIpAdapter()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.arch = ComfyResources::Arch::Sd15;
    QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    ComfyWorkflowEngine::IpAdapterLayerInput in;
    in.mode = QStringLiteral("reference");
    in.imageName = QStringLiteral("ref.png");
    in.strength = 0.6;
    QVERIFY(ComfyWorkflowEngine::applyIpAdapterLayers(&wf, {in}, ComfyResources::Arch::Sd15));
    const QJsonArray model = wf.value(QStringLiteral("3")).toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("model")).toArray();
    QVERIFY(!model.isEmpty());
    QVERIFY(model.at(0).toString() != QStringLiteral("4"));
    bool hasIpEmbeds = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QStringLiteral("IPAdapterEmbeds")) {
            hasIpEmbeds = true;
            break;
        }
    }
    QVERIFY(hasIpEmbeds);
}

void ComfyUIRemoteDockTest::testComfyControlLayerNeedsGenerateUpload()
{
    ComfyControlLayerEntry e;
    e.layerName = QStringLiteral("Sketch");
    e.mode = QStringLiteral("reference");
    QVERIFY(ComfyControlLayer::needsGenerateUpload(e));
    e.mode = QStringLiteral("depth");
    QVERIFY(ComfyControlLayer::needsGenerateUpload(e));
    e.layerName.clear();
    QVERIFY(!ComfyControlLayer::needsGenerateUpload(e));
}

void ComfyUIRemoteDockTest::testComfyControlLayerUiModeKeys()
{
    const QStringList ui = ComfyControlLayer::uiModeKeys();
    QVERIFY(ui.contains(QStringLiteral("reference")));
    QVERIFY(ui.contains(QStringLiteral("depth")));
    QVERIFY(ui.contains(QStringLiteral("pose")));
    QVERIFY(!ui.contains(QStringLiteral("inpaint")));
    QVERIFY(!ui.contains(QStringLiteral("universal")));
    QVERIFY(ComfyControlLayer::modeHasRange(QStringLiteral("depth")));
    QVERIFY(!ComfyControlLayer::modeHasRange(QStringLiteral("reference")));
}

void ComfyUIRemoteDockTest::testComfyControlLayerCanGenerateJob()
{
    ComfyControlLayerEntry e;
    e.layerName = QStringLiteral("Sketch");
    e.mode = QStringLiteral("depth");
    QVERIFY(ComfyControlLayer::canGenerateJob(e));
    e.mode = QStringLiteral("reference");
    QVERIFY(!ComfyControlLayer::canGenerateJob(e));
    e.mode = QStringLiteral("pose");
    QVERIFY(ComfyControlLayer::canGenerateJob(e));
    e.layerName.clear();
    QVERIFY(!ComfyControlLayer::canGenerateJob(e));
    QVERIFY(!ComfyUIUtils::buildControlImageWorkflow(QStringLiteral("x.png"), QStringLiteral("depth"), 512, false)
                 .isEmpty());
}

void ComfyUIRemoteDockTest::testComfyOpenPoseFromJsonToSvg()
{
    QJsonArray kp;
    for (int i = 0; i < ComfyOpenPose::jointCount * 3; ++i) {
        if (i % 3 == 2)
            kp.append(1.0);
        else if (i < 6)
            kp.append(static_cast<double>(10 + i));
        else
            kp.append(0.0);
    }
    QJsonObject person;
    person.insert(QStringLiteral("pose_keypoints_2d"), kp);
    QJsonObject root;
    root.insert(QStringLiteral("canvas_width"), 123);
    root.insert(QStringLiteral("canvas_height"), 456);
    root.insert(QStringLiteral("people"), QJsonArray{person});
    ComfyOpenPose::Pose pose = ComfyOpenPose::Pose::fromOpenPoseJson(root);
    QCOMPARE(pose.extent, QSize(123, 456));
    QVERIFY(pose.peopleCount >= 1);
    QVERIFY(pose.joints.size() >= 2);
    const QString svg = pose.toSvg();
    QVERIFY(svg.contains(QStringLiteral("P00_J00")));
    QVERIFY(svg.contains(QStringLiteral("<line id=\"P00_B")));
    pose.scaleToExtent(QSize(246, 912));
    QCOMPARE(pose.extent, QSize(246, 912));
}

void ComfyUIRemoteDockTest::testComfyThemePaletteAndIcons()
{
    const ComfyTheme::Palette pal = ComfyTheme::palette();
    QVERIFY(!pal.base.isEmpty());
    QVERIFY(!pal.highlight.isEmpty());
    QVERIFY(ComfyTheme::flatComboStyleSheet().contains(QStringLiteral("QComboBox")));
    const bool dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    QCOMPARE(ComfyTheme::isDarkTheme(), dark);
    QVERIFY(!ComfyTheme::icon(QStringLiteral("inpaint-fill")).isNull());
    QVERIFY(!ComfyTheme::icon(QStringLiteral("settings")).isNull());
    QVERIFY(!ComfyTheme::icon(QStringLiteral("star")).isNull());
    // Region chip stems must match dropdown keys (underscores kept).
    QVERIFY(!ComfyTheme::icon(QStringLiteral("control-soft_edge")).isNull());
    QVERIFY(!ComfyTheme::icon(QStringLiteral("control-line_art")).isNull());
    QVERIFY(!ComfyTheme::icon(QStringLiteral("control-canny_edge")).isNull());
    QVERIFY(!ComfyTheme::icon(QStringLiteral("control-depth")).isNull());
    QVERIFY(ComfyTheme::icon(QStringLiteral("control-soft-edge")).isNull());
    QVERIFY(!ComfyTheme::checkpointIcon(ComfyResources::Arch::Sdxl).isNull());
    QCOMPARE(ComfyTheme::kritaIconNameForThemeStem(QStringLiteral("queue-active")),
             ComfyTheme::kritaIconNameForThemeStem(QStringLiteral("queue-active")));
}

void ComfyUIRemoteDockTest::testComfyInpaintModeDetectAndInstructions()
{
    QCOMPARE(ComfyUIUtils::defaultFillKindForInpaintMode(QStringLiteral("add_object")), QStringLiteral("neutral"));
    QCOMPARE(ComfyUIUtils::defaultFillKindForInpaintMode(QStringLiteral("remove_object")), QStringLiteral("inpaint"));
    QCOMPARE(ComfyUIUtils::defaultFillKindForInpaintMode(QStringLiteral("replace_background")), QStringLiteral("replace"));

    ComfyUIUtils::InpaintParams addObj = ComfyUIUtils::detectInpaintParams(
        QStringLiteral("add_object"), QStringLiteral("sd15"), 0.6, false, false, false);
    QCOMPARE(addObj.fillKind, QStringLiteral("neutral"));
    QVERIFY(addObj.useInpaintModel);

    ComfyUIUtils::InpaintParams removeObj = ComfyUIUtils::detectInpaintParams(
        QStringLiteral("remove_object"), QStringLiteral("sd15"), 0.6, false, false, false);
    QCOMPARE(removeObj.fillKind, QStringLiteral("inpaint"));

    ComfyUIUtils::InpaintParams replaceBg = ComfyUIUtils::detectInpaintParams(
        QStringLiteral("replace_background"), QStringLiteral("sd15"), 0.6, false, false, false);
    QCOMPARE(replaceBg.fillKind, QStringLiteral("replace"));

    ComfyUIUtils::InpaintParams qwenFill = ComfyUIUtils::detectInpaintParams(
        QStringLiteral("fill"), QStringLiteral("qwen"), 1.0, false, false, false);
    QCOMPARE(qwenFill.fillKind, QStringLiteral("blur"));
    QVERIFY(!qwenFill.isEditMode);

    ComfyUIUtils::InpaintParams fluxKFill = ComfyUIUtils::detectInpaintParams(
        QStringLiteral("fill"), QStringLiteral("flux_k"), 1.0, false, false, false);
    QCOMPARE(fluxKFill.fillKind, QStringLiteral("none"));
    QVERIFY(fluxKFill.isEditMode);

    const QString instr = ComfyUIUtils::buildInpaintPromptInstructions(QStringLiteral("add_object"), QStringLiteral("sd15"));
    QVERIFY(instr.isEmpty());
    const QString flux2Instr =
        ComfyUIUtils::buildInpaintPromptInstructions(QStringLiteral("fill"), QStringLiteral("flux2_4b"));
    QVERIFY(flux2Instr.contains(QStringLiteral("green spaces")));

    const QString merged =
        ComfyUIUtils::prependInpaintPromptInstructions(QStringLiteral("a cat"), QStringLiteral("remove_object"), QStringLiteral("flux_k"));
    QVERIFY(merged.startsWith(QStringLiteral("Remove the object.")));
    QVERIFY(merged.contains(QStringLiteral("a cat")));
}

void ComfyUIRemoteDockTest::testComfyPromptTranslationHelpers()
{
    QJsonObject enabled;
    enabled.insert(QStringLiteral("translation_enabled"), true);
    enabled.insert(QStringLiteral("prompt_translation"), QStringLiteral("de"));
    QCOMPARE(ComfyUIUtils::activePromptTranslationLanguage(enabled), QStringLiteral("de"));

    QJsonObject disabled = enabled;
    disabled.insert(QStringLiteral("translation_enabled"), false);
    QVERIFY(ComfyUIUtils::activePromptTranslationLanguage(disabled).isEmpty());

    QCOMPARE(ComfyUIUtils::wrapPromptWithTranslationLanguage(QStringLiteral("hello"), QStringLiteral("de")),
             QStringLiteral("lang:de hello lang:en "));
    QCOMPARE(ComfyUIUtils::wrapPromptWithTranslationLanguage(QString(), QStringLiteral("de")), QString());
}

void ComfyUIRemoteDockTest::testComfyWorkflowEnginePromptTranslationNodes()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.positivePrompt = QStringLiteral("cat");
    p.negativePrompt = QStringLiteral("ugly");
    p.promptTranslationLanguage = QStringLiteral("de");
    const QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    QVERIFY(!wf.isEmpty());

    int translateNodes = 0;
    int clipLinked = 0;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        if (ct == QLatin1String("ETN_Translate")) {
            ++translateNodes;
            const QString text = node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("text")).toString();
            QVERIFY(text.contains(QStringLiteral("lang:de")));
        }
        if (ct == QLatin1String("CLIPTextEncode")) {
            const QJsonValue text =
                node.value(QStringLiteral("inputs")).toObject().value(QStringLiteral("text"));
            if (text.isArray())
                ++clipLinked;
        }
    }
    QCOMPARE(translateNodes, 2);
    QCOMPARE(clipLinked, 2);

    p.promptTranslationLanguage.clear();
    const QJsonObject wfOff = ComfyWorkflowEngine::buildTextToImage(p);
    for (auto it = wfOff.constBegin(); it != wfOff.constEnd(); ++it) {
        QVERIFY(it.value().toObject().value(QStringLiteral("class_type")).toString() != QLatin1String("ETN_Translate"));
    }
}

void ComfyUIRemoteDockTest::testComfyWorkflowEngineApplyRegionalGeneration()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.arch = ComfyResources::Arch::Sdxl;
    QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    QList<ComfyWorkflowEngine::RegionalPromptInput> regions;
    ComfyWorkflowEngine::RegionalPromptInput bg;
    bg.positivePrompt = QStringLiteral("background scene");
    bg.isBackground = true;
    regions.append(bg);
    ComfyWorkflowEngine::RegionalPromptInput r1;
    r1.positivePrompt = QStringLiteral("red hair character");
    r1.maskImageName = QStringLiteral("mask1.png");
    r1.promptTranslationLanguage = QStringLiteral("fr");
    regions.append(r1);
    ComfyWorkflowEngine::RegionalPromptInput r2;
    r2.positivePrompt = QStringLiteral("blue sky");
    r2.maskImageName = QStringLiteral("mask2.png");
    regions.append(r2);
    const ComfyWorkflowEngine::RegionalWorkflowNodes nodes =
        ComfyWorkflowEngine::applyRegionalGeneration(&wf, regions, QStringLiteral("4"), QStringLiteral("6"),
                                                     QStringLiteral("7"));
    QVERIFY(nodes.applied);
    bool hasEtn = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString ct = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (ct == QLatin1String("ETN_AttentionMask") || ct == QLatin1String("ETN_DefineRegion"))
            hasEtn = true;
    }
    QVERIFY(hasEtn);
    bool hasTranslate = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == QLatin1String("ETN_Translate")) {
            hasTranslate = true;
            const QString text =
                it.value().toObject().value(QStringLiteral("inputs")).toObject().value(QStringLiteral("text")).toString();
            QVERIFY(text.contains(QStringLiteral("lang:fr")));
        }
    }
    QVERIFY(hasTranslate);
}

void ComfyUIRemoteDockTest::testComfyRegionProcessMaskOverlap()
{
    QImage top(4, 4, QImage::Format_Grayscale8);
    top.fill(255);
    QImage bottom(4, 4, QImage::Format_Grayscale8);
    bottom.fill(0);
    for (int x = 0; x < 4; x++)
        bottom.setPixel(x, 2, qRgb(255, 255, 255));
    const QImage bottomOnly = ComfyRegionProcess::maskSubtract(bottom, top);
    QCOMPARE(ComfyRegionProcess::maskAverage(bottomOnly), 0.0);
    const QImage topOnly = ComfyRegionProcess::maskSubtract(top, bottom);
    QVERIFY(ComfyRegionProcess::maskAverage(topOnly) > 0.5);
    const QImage unionMask = ComfyRegionProcess::maskAdd(top, bottom);
    QVERIFY(ComfyRegionProcess::maskAverage(unionMask) > 0.9);
}

void ComfyUIRemoteDockTest::testComfyRegionProcessMaskInvertBackground()
{
    QImage partial(10, 10, QImage::Format_Grayscale8);
    partial.fill(0);
    for (int x = 3; x < 7; x++)
        for (int y = 3; y < 7; y++)
            partial.setPixel(x, y, qRgb(200, 200, 200));
    const QImage inverted = ComfyRegionProcess::maskInvert(partial);
    QVERIFY(ComfyRegionProcess::maskAverage(inverted) > ComfyRegionProcess::maskAverage(partial));
}

void ComfyUIRemoteDockTest::testComfyControlLayerHasStructuralAmong()
{
    ComfyControlLayerEntry depth;
    depth.mode = QStringLiteral("depth");
    depth.layerName = QStringLiteral("Sketch");
    ComfyControlLayerEntry reference;
    reference.mode = QStringLiteral("reference");
    reference.layerName = QStringLiteral("Ref");
    QVERIFY(ComfyControlLayer::hasStructuralControlAmong({depth}));
    QVERIFY(!ComfyControlLayer::hasStructuralControlAmong({reference}));
    QVERIFY(ComfyControlLayer::hasStructuralControlAmong({reference, depth}));
}

void ComfyUIRemoteDockTest::testComfyRegionLinkEffectiveMaskSource()
{
    ComfyUIRemoteDock::Private::RegionEntry e;
    e.maskSource = QStringLiteral("selection");
    e.layerIds = QStringLiteral("not-a-valid-uuid");
    QCOMPARE(ComfyRegionLink::effectiveMaskSource(e, KisImageSP()), QStringLiteral("selection"));
}

void ComfyUIRemoteDockTest::testComfyRegionLinkLayerIds()
{
    const QUuid a = QUuid::createUuid();
    const QUuid b = QUuid::createUuid();
    const QString aStr = a.toString(QUuid::WithoutBraces);
    const QString bStr = b.toString(QUuid::WithoutBraces);

    QString csv = ComfyRegionLink::joinLayerIds({aStr});
    QVERIFY(ComfyRegionLink::containsLayerId(csv, a));
    QVERIFY(!ComfyRegionLink::containsLayerId(csv, b));

    csv = ComfyRegionLink::toggleLayerId(csv, b);
    QVERIFY(ComfyRegionLink::containsLayerId(csv, a));
    QVERIFY(ComfyRegionLink::containsLayerId(csv, b));

    csv = ComfyRegionLink::toggleLayerId(csv, a);
    QVERIFY(!ComfyRegionLink::containsLayerId(csv, a));
    QVERIFY(ComfyRegionLink::containsLayerId(csv, b));

    QCOMPARE(ComfyRegionLink::parseLayerIds(QStringLiteral("  %1 , , %2 ")).size(), 2);
}

void ComfyUIRemoteDockTest::testRegionUiStateLayerIdsJson()
{
    ComfyRegionUiStateEntry e;
    e.name = QStringLiteral("Face");
    e.positive = QStringLiteral("smile");
    e.layerIds = QStringLiteral("abc-def,123-456");
    const QJsonObject o = regionUiStateEntryToJson(e);
    QCOMPARE(o.value(QStringLiteral("layer_ids")).toString(), e.layerIds);
    const ComfyRegionUiStateEntry back = regionUiStateEntryFromJson(o);
    QCOMPARE(back.layerIds, e.layerIds);
}

void ComfyUIRemoteDockTest::testDocumentUiJsonRegionControlRoundtrip()
{
    ComfyControlLayerEntry cl;
    cl.mode = QStringLiteral("depth");
    cl.layerName = QStringLiteral("Sketch");
    cl.layerId = QStringLiteral("uuid-1");
    cl.presetValue = 2;
    cl.strength = 60;

    ComfyRegionUiStateEntry region;
    region.name = QStringLiteral("Hair");
    region.positive = QStringLiteral("red hair");
    region.maskSource = QStringLiteral("layer:Hair");
    region.controlLayers = {cl};

    const QJsonObject rootWrap =
        rootRegionUiWrapToJson(QStringLiteral("a portrait"), QStringLiteral("blurry"), {region});
    QCOMPARE(rootWrap.value(QStringLiteral("positive")).toString(), QStringLiteral("a portrait"));
    QCOMPARE(rootWrap.value(QStringLiteral("negative")).toString(), QStringLiteral("blurry"));
    const QJsonArray regions = rootWrap.value(QStringLiteral("regions")).toArray();
    QCOMPARE(regions.size(), 1);

    QString pos;
    QString neg;
    QList<ComfyRegionUiStateEntry> parsed;
    QVERIFY(rootRegionUiWrapFromJson(rootWrap, &pos, &neg, &parsed));
    QCOMPARE(pos, QStringLiteral("a portrait"));
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first().positive, QStringLiteral("red hair"));
    QCOMPARE(parsed.first().maskSource, QStringLiteral("layer:Hair"));
    QCOMPARE(parsed.first().controlLayers.size(), 1);
    QCOMPARE(parsed.first().controlLayers.first().mode, QStringLiteral("depth"));

    QJsonObject ui;
    ui.insert(QStringLiteral("version"), persistenceFormatVersion);
    ui.insert(QStringLiteral("root"), rootWrap);
    ui.insert(QStringLiteral("control"), ComfyControlLayer::toJsonArray({cl}));
    bool found = false;
    const QJsonArray fromUi = readRegionUiArrayFromDocumentUi(ui, &found);
    QVERIFY(found);
    QCOMPARE(fromUi.size(), 1);
    const ComfyRegionUiStateEntry fromArr = regionUiStateEntryFromJson(fromUi.at(0).toObject());
    QCOMPARE(fromArr.positive, QStringLiteral("red hair"));

    const ComfyRegionUiStateEntry pythonStyle = regionUiStateEntryFromJson(
        QJsonObject{{QStringLiteral("positive"), QStringLiteral("sky only")},
                    {QStringLiteral("layer_ids"), QStringLiteral("")},
                    {QStringLiteral("control"), QJsonArray()}});
    QCOMPARE(pythonStyle.positive, QStringLiteral("sky only"));
    QCOMPARE(pythonStyle.maskSource, QStringLiteral("selection"));
}

SIMPLE_TEST_MAIN(ComfyUIRemoteDockTest)
#include "ComfyUIRemoteDockTest.moc"
