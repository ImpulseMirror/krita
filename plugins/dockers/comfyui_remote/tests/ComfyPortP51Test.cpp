/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <simpletest.h>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ComfyFileLibrary.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyStyleCollection.h"
#include "ComfyUIUtils.h"

class ComfyPortP51Test : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testStyleCollectionLoadsBuiltinDigitalArtwork();
    void testStyleCollectionFilteredBuiltinSubset();
    void testStyleCollectionSaveUserOverrideRoundtrip();
    void testBuiltinSamplerPresetLookupDpm2M();
    void testMergeLibraryLoraTagsIntoPrompt();
    void testLocalLorasMissingOnServer();
    void testLorasJsonSaveLoadRoundtrip();
    void testFileCollectionRemoveMissingLocal();
    void testDocumentUiJsonVersionAndRootRoundtrip();
    void testWorkflowEngineStyleVaeAndClipSkip();
    void testUpscaleTiledLayoutSpecPythonParity();
    void testUpscaleTiledPerTileControlNet();
    void testUpscaleTiledSamplerCustomAdvanced();
    void testBundledPluginDataLayout();
    void testBuildGenerateUsesSamplerCustom();
    void testBuildControlPreviewInEngine();
};

void ComfyPortP51Test::init()
{
#ifdef COMFYUI_ENABLE_TEST_HOOKS
    QVERIFY(m_tempDir.isValid());
    ComfyUITestHooks::setPluginUserDataDirOverride(m_tempDir.path());
    QDir().mkpath(m_tempDir.path() + QStringLiteral("/database"));
    ComfyFileLibrary::instance().resetLorasDatabaseForTests(m_tempDir.path() + QStringLiteral("/database/loras.json"));
#endif
}

void ComfyPortP51Test::cleanup()
{
#ifdef COMFYUI_ENABLE_TEST_HOOKS
    ComfyUITestHooks::clearPluginUserDataDirOverride();
#endif
}

void ComfyPortP51Test::testStyleCollectionLoadsBuiltinDigitalArtwork()
{
    if (ComfyStyleCollection::instance().builtinStylesDir().isEmpty())
        QSKIP("Bundled styles dir unavailable");
    ComfyStyleCollection::instance().reload();
    const ComfyStyleEntry *st =
        ComfyStyleCollection::instance().findByStyleId(QStringLiteral("built-in/digital-artwork.json"));
    QVERIFY(st);
    QCOMPARE(st->name, QStringLiteral("Digital Artwork"));
    QCOMPARE(st->architecture, QStringLiteral("auto"));
    QVERIFY(st->isBuiltin);
    QVERIFY(st->checkpoints.contains(QStringLiteral("dreamshaper_8.safetensors")));
    QCOMPARE(st->samplerPresetName, QStringLiteral("Default - DPM++ 2M"));
    QVERIFY(st->usesNegativePrompt());
}

void ComfyPortP51Test::testStyleCollectionFilteredBuiltinSubset()
{
    ComfyStyleCollection::instance().reload();
    const QList<const ComfyStyleEntry *> all = ComfyStyleCollection::instance().filtered(true);
    const QList<const ComfyStyleEntry *> userOnly = ComfyStyleCollection::instance().filtered(false);
    QVERIFY(all.size() >= userOnly.size());
    int builtinCount = 0;
    for (const ComfyStyleEntry *e : all) {
        if (e && e->isBuiltin)
            ++builtinCount;
    }
    QVERIFY(builtinCount >= 1);
}

void ComfyPortP51Test::testStyleCollectionSaveUserOverrideRoundtrip()
{
    ComfyStyleEntry e;
    e.filepath = QStringLiteral("/virtual/custom-test.json");
    e.name = QStringLiteral("P51 Custom");
    e.architecture = QStringLiteral("sdxl");
    e.checkpoints = QStringList{QStringLiteral("test.ckpt")};
    e.vae = QStringLiteral("vae.safetensors");
    e.clipSkip = 1;
    e.preferredResolution = 1024;
    e.vPredictionZsnr = true;
    e.selfAttentionGuidance = true;
    e.samplerPresetName = QStringLiteral("Default - DPM++ 2M");
    e.samplerSteps = 22;
    e.cfgScale = 6.5;

    const QString saved = ComfyStyleCollection::instance().saveEntryToUserStyles(e);
    QVERIFY(!saved.isEmpty());
    QVERIFY(QFile::exists(saved));

    const ComfyStyleEntry *loaded = ComfyStyleCollection::instance().findByStyleId(QStringLiteral("custom-test.json"));
    QVERIFY(loaded);
    QCOMPARE(loaded->name, QStringLiteral("P51 Custom"));
    QCOMPARE(loaded->clipSkip, 1);
    QCOMPARE(loaded->preferredResolution, 1024);
    QVERIFY(loaded->vPredictionZsnr);
    QVERIFY(!loaded->isBuiltin);

    QFile::remove(saved);
    ComfyStyleCollection::instance().reload();
}

void ComfyPortP51Test::testBuiltinSamplerPresetLookupDpm2M()
{
    const QJsonObject root = builtinSamplerPresetsRoot();
    QVERIFY(!root.isEmpty());
    QString sampler;
    QString scheduler;
    int steps = 0;
    int minSteps = 0;
    double cfg = 0.0;
    QVERIFY(samplerPresetLookup(root,
                                QStringLiteral("Default - DPM++ 2M"),
                                &sampler,
                                &scheduler,
                                &steps,
                                &minSteps,
                                &cfg));
    QCOMPARE(sampler, QStringLiteral("dpmpp_2m"));
    QCOMPARE(scheduler, QStringLiteral("karras"));
    QCOMPARE(steps, 20);
    QCOMPARE(minSteps, 4);
    QCOMPARE(cfg, 7.0);
    QVERIFY(!samplerPresetLookup(root, QStringLiteral("No Such Preset"), &sampler, &scheduler, &steps, &minSteps, &cfg));
}

void ComfyPortP51Test::testMergeLibraryLoraTagsIntoPrompt()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString loraPath = tmp.path() + QStringLiteral("/hero.safetensors");
    QFile f(loraPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("lora");
    f.close();

    ComfyFileRecord rec = ComfyFileRecord::local(loraPath, ComfyFileFormat::Lora, false);
    rec.setMeta(QStringLiteral("enabled"), true);
    rec.setMeta(QStringLiteral("strength_percent"), 50);
    QJsonArray arr;
    arr.append(rec.toJson());
    QVERIFY(saveLorasJsonArray(arr));

    const QString merged = mergeLibraryLoraTagsIntoPositivePrompt(QStringLiteral("a cat"));
    QVERIFY(merged.contains(QStringLiteral("<lora:hero.safetensors:0.50>")));
    QVERIFY(merged.startsWith(QStringLiteral("a cat")));
}

void ComfyPortP51Test::testLocalLorasMissingOnServer()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString loraPath = tmp.path() + QStringLiteral("/missing.safetensors");
    QFile f(loraPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    ComfyFileRecord rec = ComfyFileRecord::local(loraPath, ComfyFileFormat::Lora, false);
    rec.setMeta(QStringLiteral("enabled"), true);
    QJsonArray arr;
    arr.append(rec.toJson());
    QVERIFY(saveLorasJsonArray(arr));
    ComfyFileLibrary::instance().init();

    const QStringList server = {QStringLiteral("other.safetensors")};
    const QList<const ComfyFileRecord *> missing =
        ComfyFileLibrary::instance().localLorasMissingOnServer(server);
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing.first()->id, QStringLiteral("missing.safetensors"));

    ComfyFileRecord disabled = rec;
    disabled.setMeta(QStringLiteral("enabled"), false);
    QVERIFY(saveLorasJsonArray(QJsonArray{disabled.toJson()}));
    QCOMPARE(ComfyFileLibrary::instance().localLorasMissingOnServer(server).size(), 0);
}

void ComfyPortP51Test::testLorasJsonSaveLoadRoundtrip()
{
    ComfyFileRecord a = ComfyFileRecord::remote(QStringLiteral("remote.safetensors"), ComfyFileFormat::Lora);
    a.setMeta(QStringLiteral("enabled"), true);
    a.setMeta(QStringLiteral("strength_percent"), 75);
    QVERIFY(saveLorasJsonArray(QJsonArray{a.toJson()}));

    const QJsonArray loaded = loadLorasJsonArray();
    QCOMPARE(loaded.size(), 1);
    const ComfyFileRecord round = ComfyFileRecord::fromJson(loaded.at(0).toObject());
    QCOMPARE(round.id, QStringLiteral("remote.safetensors"));
    QCOMPARE(round.meta(QStringLiteral("strength_percent")).toInt(), 75);
}

void ComfyPortP51Test::testFileCollectionRemoveMissingLocal()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString db = tmp.path() + QStringLiteral("/loras.json");
    const QString existing = tmp.path() + QStringLiteral("/keep.safetensors");
    QFile keep(existing);
    QVERIFY(keep.open(QIODevice::WriteOnly));
    keep.write("k");
    keep.close();

    ComfyFileCollection col(db);
    ComfyFileRecord localKeep = ComfyFileRecord::local(existing, ComfyFileFormat::Lora, false);
    ComfyFileRecord localGone =
        ComfyFileRecord::local(tmp.path() + QStringLiteral("/gone.safetensors"), ComfyFileFormat::Lora, false);
    col.add(localKeep);
    col.add(localGone);
    col.save();
    QFile::remove(tmp.path() + QStringLiteral("/gone.safetensors"));

    ComfyFileCollection reload(db);
    reload.load();
    QCOMPARE(reload.files().size(), 1);
    QCOMPARE(reload.files().first().id, QStringLiteral("keep.safetensors"));
}

void ComfyPortP51Test::testDocumentUiJsonVersionAndRootRoundtrip()
{
    ComfyRegionUiStateEntry region;
    region.name = QStringLiteral("Sky");
    region.positive = QStringLiteral("blue sky");
    region.maskSource = QStringLiteral("selection");

    const QJsonObject rootWrap =
        rootRegionUiWrapToJson(QStringLiteral("landscape"), QStringLiteral("blur"), {region});
    QJsonObject ui;
    ui.insert(QStringLiteral("version"), persistenceFormatVersion);
    ui.insert(QStringLiteral("root"), rootWrap);
    ui.insert(QStringLiteral("upscale"),
              QJsonObject{{QStringLiteral("mode"), QStringLiteral("factor")},
                          {QStringLiteral("factor"), 2}});

    bool found = false;
    const QJsonArray regions = readRegionUiArrayFromDocumentUi(ui, &found);
    QVERIFY(found);
    QCOMPARE(regions.size(), 1);
    QCOMPARE(regionUiStateEntryFromJson(regions.at(0).toObject()).positive, QStringLiteral("blue sky"));
    QCOMPARE(ui.value(QStringLiteral("version")).toInt(), persistenceFormatVersion);
    QCOMPARE(ui.value(QStringLiteral("upscale")).toObject().value(QStringLiteral("factor")).toInt(), 2);
}

void ComfyPortP51Test::testUpscaleTiledLayoutSpecPythonParity()
{
    const ComfyUIUtils::UpscaleTiledLayoutSpec autoLayout = ComfyUIUtils::computeUpscaleTiledLayoutSpec(
        2048, 1536, ComfyResources::Arch::Sdxl, 0, 0.35, -1);
    QVERIFY(autoLayout.minTileSize >= 8);
    QVERIFY(autoLayout.padding >= 8);
    QVERIFY(autoLayout.blending > 0);
    QVERIFY(autoLayout.totalTiles >= 1);

    const ComfyUIUtils::UpscaleTiledLayoutSpec customLayout = ComfyUIUtils::computeUpscaleTiledLayoutSpec(
        2048, 1536, ComfyResources::Arch::Sdxl, 1024, 0.35, 48);
    QCOMPARE(customLayout.padding % 8, 0);
    QVERIFY(customLayout.totalTiles >= autoLayout.totalTiles || customLayout.padding >= autoLayout.padding);

    ComfyWorkflowEngine::UpscaleTiledParams p;
    p.imageName = QStringLiteral("up.png");
    p.scaledWidth = 2048;
    p.scaledHeight = 1536;
    p.denoise = 0.35;
    p.tileOverlapPx = -1;
    p.arch = ComfyResources::Arch::Sdxl;
    const QJsonObject wf = ComfyWorkflowEngine::buildUpscaleTiled(p);
    QVERIFY(!wf.isEmpty());
    QJsonObject tileLayoutInputs;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString() == QLatin1String("ETN_TileLayout")) {
            tileLayoutInputs = it.value().toObject().value(QStringLiteral("inputs")).toObject();
            break;
        }
    }
    QVERIFY(!tileLayoutInputs.isEmpty());
    QCOMPARE(tileLayoutInputs.value(QStringLiteral("padding")).toInt(), autoLayout.padding);
    QCOMPARE(tileLayoutInputs.value(QStringLiteral("blending")).toInt(), autoLayout.blending);
    QCOMPARE(tileLayoutInputs.value(QStringLiteral("min_tile_size")).toInt(), autoLayout.minTileSize);
}

void ComfyPortP51Test::testUpscaleTiledPerTileControlNet()
{
    ComfyWorkflowEngine::UpscaleTiledParams p;
    p.imageName = QStringLiteral("up.png");
    p.scaledWidth = 2048;
    p.scaledHeight = 1536;
    p.denoise = 0.35;
    p.tileOverlapPx = -1;
    p.arch = ComfyResources::Arch::Sdxl;
    ComfyWorkflowEngine::ControlNetLayerInput cn;
    cn.mode = QStringLiteral("depth");
    cn.imageName = QStringLiteral("control_depth.png");
    cn.strength = 0.8;
    p.controlLayers.append(cn);
    const QJsonObject wf = ComfyWorkflowEngine::buildUpscaleTiled(p);
    QVERIFY(!wf.isEmpty());
    int extractTiles = 0;
    int controlApply = 0;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("ETN_ExtractImageTile"))
            extractTiles++;
        if (cls == QLatin1String("ControlNetApplyAdvanced"))
            controlApply++;
    }
    QVERIFY(extractTiles >= 2);
    QVERIFY(controlApply >= 1);
}

void ComfyPortP51Test::testUpscaleTiledSamplerCustomAdvanced()
{
    ComfyWorkflowEngine::UpscaleTiledParams p;
    p.imageName = QStringLiteral("up.png");
    p.scaledWidth = 2048;
    p.scaledHeight = 1536;
    p.denoise = 0.35;
    p.arch = ComfyResources::Arch::Sdxl;
    const QJsonObject wf = ComfyWorkflowEngine::buildUpscaleTiled(p);
    QVERIFY(!wf.isEmpty());
    bool hasSamplerCustom = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("SamplerCustomAdvanced")) {
            hasSamplerCustom = true;
            break;
        }
    }
    QVERIFY(hasSamplerCustom);
}

void ComfyPortP51Test::testBundledPluginDataLayout()
{
    const QString base = ComfyUIUtils::pluginInstallDataDir();
    if (base.isEmpty())
        QSKIP("pluginInstallDataDir unavailable in this build");
    QDir styles(base + QStringLiteral("/styles"));
    QCOMPARE(styles.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files).size(), 14);
    QVERIFY(QFileInfo::exists(base + QStringLiteral("/presets/samplers.json")));
    QVERIFY(QFileInfo::exists(base + QStringLiteral("/presets/control.json")));
    QVERIFY(QFileInfo::exists(base + QStringLiteral("/tags/Danbooru.csv")));
    QVERIFY(QFileInfo::exists(base + QStringLiteral("/language/en.json")));
    ComfyStyleCollection::instance().reload();
    QVERIFY(ComfyStyleCollection::instance().all().size() >= 14);
}

void ComfyPortP51Test::testBuildGenerateUsesSamplerCustom()
{
    ComfyWorkflowEngine::GenerateParams p;
    p.checkpoint = QStringLiteral("v1-5-pruned-emaonly.safetensors");
    p.width = 512;
    p.height = 512;
    p.positivePrompt = QStringLiteral("test");
    p.negativePrompt = QStringLiteral("bad");
    p.arch = ComfyResources::Arch::Sd15;
    const QJsonObject wf = ComfyWorkflowEngine::buildGenerate(p);
    QVERIFY(!wf.isEmpty());
    bool hasCustom = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        if (it.value().toObject().value(QStringLiteral("class_type")).toString()
            == QLatin1String("SamplerCustomAdvanced")) {
            hasCustom = true;
            break;
        }
    }
    QVERIFY(hasCustom);
    QVERIFY(!wf.contains(QStringLiteral("3")));
}

void ComfyPortP51Test::testBuildControlPreviewInEngine()
{
    ComfyWorkflowEngine::ControlPreviewParams p;
    p.uploadedImageName = QStringLiteral("canvas.png");
    p.mode = QStringLiteral("depth");
    p.resolutionBase = 512;
    const QJsonObject wf = ComfyWorkflowEngine::buildControlPreview(p);
    QVERIFY(!wf.isEmpty());
}

void ComfyPortP51Test::testWorkflowEngineStyleVaeAndClipSkip()
{
    ComfyWorkflowEngine::TextToImageParams p;
    p.checkpoint = QStringLiteral("dreamshaper_8.safetensors");
    p.positivePrompt = QStringLiteral("test");
    p.negativePrompt = QStringLiteral("bad");
    p.width = 512;
    p.height = 512;
    QJsonObject wf = ComfyWorkflowEngine::buildTextToImage(p);
    ComfyWorkflowEngine::applyCheckpointStyleOptions(&wf,
                                                     QStringLiteral("vae-ft-mse.safetensors"),
                                                     2,
                                                     ComfyResources::Arch::Sd15);
    bool hasVaeLoader = false;
    bool hasClipLayer = false;
    for (auto it = wf.constBegin(); it != wf.constEnd(); ++it) {
        const QString cls = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("VAELoader"))
            hasVaeLoader = true;
        if (cls == QLatin1String("CLIPSetLastLayer"))
            hasClipLayer = true;
    }
    QVERIFY(hasVaeLoader);
    QVERIFY(hasClipLayer);
}

private:
    QTemporaryDir m_tempDir;
};

SIMPLE_TEST_MAIN(ComfyPortP51Test)
#include "ComfyPortP51Test.moc"
