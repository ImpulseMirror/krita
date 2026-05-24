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

#include "ComfyUIRemoteDock.h"
#include "ComfyUIIntervalSlider.h"
#include "ComfyControlLayer.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyUIUtils.h"

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
    void testComfyWorkflowEngineBuildTextToImage();
    void testComfyWorkflowEngineFluxCfgCap();
    void testComfyControlLayerJsonRoundtrip();
    void testComfyWorkflowEngineApplyControlNet();
    void testComfyWorkflowEngineApplyIpAdapter();
    void testComfyControlLayerNeedsGenerateUpload();
    void testComfyWorkflowEngineApplyRegionalGeneration();
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
    QCOMPARE(ps.size(), 2);
    QCOMPARE(ps.at(0).strength, 0.7);
    QCOMPARE(ps.at(0).start, 0.0);
    QCOMPARE(ps.at(0).end, 0.5);
    QCOMPARE(ps.at(1).strength, 1.0);
    QCOMPARE(ps.at(1).end, 1.0);
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
    s.insert(QStringLiteral("control_layer_default_preset_index"), 1);
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
    QCOMPARE(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("workspace-generation")), QStringLiteral("tools-wizard"));
    QCOMPARE(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("queue-active")), QStringLiteral("run-build"));
    QCOMPARE(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("star")), QStringLiteral("rating"));
    QCOMPARE(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("not-a-real-theme-stem-xyz")),
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
    QVERIFY(ComfyResources::supportsRegions(ComfyResources::Arch::Sdxl));
    QVERIFY(!ComfyResources::supportsRegions(ComfyResources::Arch::Flux));
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
}

SIMPLE_TEST_MAIN(ComfyUIRemoteDockTest)
#include "ComfyUIRemoteDockTest.moc"
