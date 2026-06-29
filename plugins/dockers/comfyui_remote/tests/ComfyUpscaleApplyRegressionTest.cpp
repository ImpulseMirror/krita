/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Regression tests for upscale result apply on Android/debug Krita.
 * Crash was: addNodesDirect deferred shape registration, then immediate
 * slotNonUiActivatedNode → SAFE ASSERT "shape" in KisNodeManager.
 * Fix: image->addNode + activateNodeWhenShapeReady before selection.
 */

#include <simpletest.h>
#include <QTest>

#include <QFile>
#include <QStringList>

#include <KoColorSpaceRegistry.h>
#include <KoColorSpaceConstants.h>

#include <kis_image.h>
#include <kis_paint_layer.h>
#include <kis_group_layer.h>

#include "ComfyHistoryInternal.h"

#ifndef COMFYUI_TEST_PLUGIN_DIR
#define COMFYUI_TEST_PLUGIN_DIR "."
#endif

namespace {

QString readPluginSource(const char *relativePath)
{
    const QString path = QStringLiteral(COMFYUI_TEST_PLUGIN_DIR) + QLatin1Char('/') + QString::fromUtf8(relativePath);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

QString functionBody(const QString &source, const char *signature)
{
    const int start = source.indexOf(QString::fromUtf8(signature));
    if (start < 0)
        return QString();
    const int brace = source.indexOf(QLatin1Char('{'), start);
    if (brace < 0)
        return QString();
    int depth = 0;
    for (int i = brace; i < source.size(); ++i) {
        const QChar c = source.at(i);
        if (c == QLatin1Char('{'))
            ++depth;
        else if (c == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                return source.mid(start, i - start + 1);
        }
    }
    return QString();
}

KisImageSP makeTestImage()
{
    KisImageSP image = new KisImage(0, 64, 64, KoColorSpaceRegistry::instance()->rgb8(), "test");
    image->initialRefreshGraph();
    return image;
}

KisPaintLayerSP addPaintLayer(KisImageSP image, const QString &name, KisNodeSP above = KisNodeSP())
{
    KisPaintLayerSP layer(new KisPaintLayer(image, name, OPACITY_OPAQUE_U8));
    image->addNode(layer, image->rootLayer(), above);
    image->waitForDone();
    return layer;
}

QStringList childLayerNames(KisNodeSP parent)
{
    QStringList names;
    for (KisNodeSP child = parent->firstChild(); child; child = child->nextSibling())
        names.append(child->name());
    return names;
}

} // namespace

class ComfyUpscaleApplyRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testHistorySourcesAvoidDeferredNodeManagerDirectOps();
    void testApplyResultFileWithBehaviorUsesImageAddNode();
    void testMoveLayerInParentUsesImageAddNode();
    void testActivateAppliedResultLayerDefersSelection();
    void testUpscalePollUsesApplyResultPath();
    void testUpscaleResultLayerNameFormat();
    void testMoveLayerInParentRepositionsImportedLayer();
    void testRaiseLayerToRootTopMovesLayerToStackTop();
    void testReplaceApplyUsesMergeImportedForReplace();
    void testLiveResultLayerNameFormat();
    void testPrepareLiveUsesDiffusionContextBounds();
    void testLivePollUsesInpaintComposite();
    void testLiveApplyUsesContextBoundsAndLayerName();
    void testReplaceApplyNamesActiveLayerAfterMerge();
    void testLivePollUpdatesCanvasPreviewLayer();
    void testLivePollDockerPreviewUsesGeneratingOverlay();
    void testLiveMaskedPreviewUsesSetResultComposite();
    void testLiveApplyRecompositesWithFeatherAtApplyTime();
    void testLiveApplyBothShortcutPathsUseResolveImagePath();
    void testLivePollCachesRawServerSeparatelyFromApplyImage();
    void testLiveApplyRefreshesCanvasProjection();
    void testLivePollDoesNotTouchCanvasLayers();
};

void ComfyUpscaleApplyRegressionTest::testReplaceApplyUsesMergeImportedForReplace()
{
    const QString applySource = readPluginSource("history/ComfyHistoryApply.cpp");
    QVERIFY(!applySource.isEmpty());
    const QString applyBody = functionBody(applySource, "bool ComfyUIRemoteDock::applyResultFileWithBehavior(");
    QVERIFY2(!applyBody.isEmpty(), "applyResultFileWithBehavior body not found");
    QVERIFY(applyBody.contains(QStringLiteral("KisImageBarrierLock")));
    QVERIFY(applyBody.contains(QStringLiteral("mergeImportedForReplace")));
    QVERIFY(!applyBody.contains(QStringLiteral("image->mergeDown(imported")));

    const QString internalSource = readPluginSource("history/ComfyHistoryInternal.cpp");
    QVERIFY(!internalSource.isEmpty());
    const QString mergeBody = functionBody(internalSource, "bool mergeImportedForReplace(");
    QVERIFY2(!mergeBody.isEmpty(), "mergeImportedForReplace body not found");
    QVERIFY(mergeBody.contains(QStringLiteral("image->mergeDown(imported")));
    QVERIFY(mergeBody.contains(QStringLiteral("image->waitForDone()")));

    const QString tickSource = readPluginSource("runners/live/ComfyLiveRunnerTick.cpp");
    QVERIFY(tickSource.contains(QStringLiteral("liveApplyInProgress")));
}

void ComfyUpscaleApplyRegressionTest::testLiveResultLayerNameFormat()
{
    QCOMPARE(ComfyHistoryInternal::liveResultLayerName(QStringLiteral("a cat on a mat"), 42),
             QStringLiteral("a cat on a mat (42)"));
    const QString longPrompt(250, QLatin1Char('x'));
    const QString trimmed = ComfyHistoryInternal::liveResultLayerName(longPrompt, 7);
    QVERIFY(trimmed.endsWith(QStringLiteral("(7)")));
    QVERIFY(trimmed.size() <= 200 + 3 + 10);
    QVERIFY(!trimmed.startsWith(QStringLiteral("[Generated]")));
}

void ComfyUpscaleApplyRegressionTest::testPrepareLiveUsesDiffusionContextBounds()
{
    const QString source = readPluginSource("workflow/prepare/ComfyPrepareWorkflow.cpp");
    QVERIFY(!source.isEmpty());
    const QString body = functionBody(source, "Result prepareLive(");
    QVERIFY2(!body.isEmpty(), "prepareLive body not found");
    QVERIFY(body.contains(QStringLiteral("computeInpaintDiffusionBounds")));
    QVERIFY(body.contains(QStringLiteral("nativeContextImage")));
    QVERIFY(body.contains(QStringLiteral("nativeTargetBoundsRelative")));
    QVERIFY(body.contains(QStringLiteral("maskPaddedBounds.translated(-contextBounds.topLeft())")));
}

void ComfyUpscaleApplyRegressionTest::testLivePollUsesInpaintComposite()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY(!pollSource.isEmpty());
    QVERIFY(pollSource.contains(QStringLiteral("compositeLiveServerResult")));

    const QString internalSource = readPluginSource("runners/live/ComfyLiveRunnerInternal.cpp");
    QVERIFY(internalSource.contains(QStringLiteral("compositeInpaintServerOntoContext")));
}

void ComfyUpscaleApplyRegressionTest::testLiveApplyUsesContextBoundsAndLayerName()
{
    const QString source = readPluginSource("dock/shortcuts/ComfyUIRemoteDockShortcuts.cpp");
    QVERIFY(!source.isEmpty());
    const QString body = functionBody(source, "void ComfyUIRemoteDock::slotAiDiffusionApply()");
    QVERIFY2(!body.isEmpty(), "slotAiDiffusionApply body not found");
    QVERIFY(body.contains(QStringLiteral("liveResultLayerName")));
    QVERIFY(body.contains(QStringLiteral("livePrepared.contextBounds")));
    QVERIFY(!body.contains(QStringLiteral("maskPaddedBounds.isEmpty()")));
}

void ComfyUpscaleApplyRegressionTest::testReplaceApplyNamesActiveLayerAfterMerge()
{
    const QString source = readPluginSource("history/ComfyHistoryApply.cpp");
    QVERIFY(!source.isEmpty());
    const QString body = functionBody(source, "bool ComfyUIRemoteDock::applyResultFileWithBehavior(");
    QVERIFY2(!body.isEmpty(), "applyResultFileWithBehavior body not found");
    QVERIFY(body.contains(QStringLiteral("nameTarget = activeBefore")));
    QVERIFY(body.contains(QStringLiteral("refreshCanvasProjectionAfterApply(image, nameTarget)")));
}

void ComfyUpscaleApplyRegressionTest::testLivePollUpdatesCanvasPreviewLayer()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY(!pollSource.isEmpty());
    QVERIFY(!pollSource.contains(QStringLiteral("updateLiveCanvasPreview")));

    const QString liveSource = readPluginSource("dock/live/ComfyUIRemoteDockLive.cpp");
    QVERIFY(liveSource.contains(QStringLiteral("removeStaleLiveCanvasPreviewLayer")));
}

void ComfyUpscaleApplyRegressionTest::testLivePollDockerPreviewUsesGeneratingOverlay()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY(pollSource.contains(QStringLiteral("compositeLiveResultPreviewFromContext")));
    QVERIFY(pollSource.contains(QStringLiteral("applyImage")));
    QVERIFY(pollSource.contains(QStringLiteral("maskPaddedBounds")));

    const QString captureSource = readPluginSource("utils/document/ComfyUIUtilsDocumentCapture.cpp");
    QVERIFY(captureSource.contains(QStringLiteral("compositeLiveResultPreviewFromContext")));
    QVERIFY(captureSource.contains(QStringLiteral("DiagCrossPattern")));
    QVERIFY(captureSource.contains(QStringLiteral("CompositionMode_Multiply")));
}

void ComfyUpscaleApplyRegressionTest::testLiveMaskedPreviewUsesSetResultComposite()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY2(!pollSource.contains(QStringLiteral("dockerPreview = applyImage")),
             "masked live preview must use set_result composite, not raw applyImage");
    QVERIFY(pollSource.contains(QStringLiteral("compositeLiveResultPreviewFromContext")));
    QVERIFY(pollSource.contains(QStringLiteral("LiveWorkspace.set_result")));

    const QString captureSource = readPluginSource("utils/document/ComfyUIUtilsDocumentCapture.cpp");
    QVERIFY(captureSource.contains(QStringLiteral("selectionMaskGray")));
    QVERIFY(captureSource.contains(QStringLiteral("maskLine[x] > 127")));
}

void ComfyUpscaleApplyRegressionTest::testLiveApplyRecompositesWithFeatherAtApplyTime()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY(pollSource.contains(QStringLiteral("last_live_raw.png")));
    QVERIFY(pollSource.contains(QStringLiteral("lastLiveRawResultImagePath")));

    const QString applySource = readPluginSource("history/ComfyHistoryApply.cpp");
    const QString resolveBody = functionBody(applySource, "QString ComfyUIRemoteDock::resolveLiveApplyImagePath()");
    QVERIFY2(!resolveBody.isEmpty(), "resolveLiveApplyImagePath body not found");
    QVERIFY(resolveBody.contains(QStringLiteral("compositeLiveServerResultAtApply")));
    QVERIFY(resolveBody.contains(QStringLiteral("last_live_apply_composite.png")));

    const QString shortcutsSource = readPluginSource("dock/shortcuts/ComfyUIRemoteDockShortcuts.cpp");
    QVERIFY(shortcutsSource.contains(QStringLiteral("resolveLiveApplyImagePath")));

    const QString internalSource = readPluginSource("runners/live/ComfyLiveRunnerInternal.cpp");
    QVERIFY(internalSource.contains(QStringLiteral("compositeLiveServerResultAtApply")));
    QVERIFY(internalSource.contains(QStringLiteral("getDocumentImage")));
    QVERIFY(internalSource.contains(QStringLiteral("compositeInpaintServerOntoContext")));
}

void ComfyUpscaleApplyRegressionTest::testLiveApplyBothShortcutPathsUseResolveImagePath()
{
    const QString shortcutsSource = readPluginSource("dock/shortcuts/ComfyUIRemoteDockShortcuts.cpp");
    const QString applyBody = functionBody(shortcutsSource, "void ComfyUIRemoteDock::slotAiDiffusionApply()");
    const QString altBody = functionBody(shortcutsSource, "void ComfyUIRemoteDock::slotAiDiffusionApplyAlternative()");
    QVERIFY2(!applyBody.isEmpty(), "slotAiDiffusionApply body not found");
    QVERIFY2(!altBody.isEmpty(), "slotAiDiffusionApplyAlternative body not found");
    QVERIFY(applyBody.contains(QStringLiteral("resolveLiveApplyImagePath")));
    QVERIFY(altBody.contains(QStringLiteral("resolveLiveApplyImagePath")));
    QVERIFY(!applyBody.contains(QStringLiteral("lastLiveResultImagePath), applyResultFileWithBehavior")));
}

void ComfyUpscaleApplyRegressionTest::testLivePollCachesRawServerSeparatelyFromApplyImage()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY(pollSource.contains(QStringLiteral("compositeLiveServerResult(resultImg, prep)")));
    QVERIFY(pollSource.contains(QStringLiteral("resultImg.save(rawCachePath)")));
    QVERIFY(pollSource.contains(QStringLiteral("lastLiveRawResultImagePath")));
    QVERIFY(pollSource.contains(QStringLiteral("last_live_raw.png")));
    const int compositePos = pollSource.indexOf(QStringLiteral("compositeLiveServerResult(resultImg, prep)"));
    const int rawSavePos = pollSource.indexOf(QStringLiteral("resultImg.save(rawCachePath)"));
    QVERIFY2(compositePos >= 0 && rawSavePos > compositePos,
             "raw server cache must be saved after feathered applyImage composite");
}

void ComfyUpscaleApplyRegressionTest::testLiveApplyRefreshesCanvasProjection()
{
    const QString applySource = readPluginSource("history/ComfyHistoryApply.cpp");
    QVERIFY(applySource.contains(QStringLiteral("refreshCanvasProjectionAfterApply(image, nameTarget)")));

    const QString internalSource = readPluginSource("history/ComfyHistoryInternal.cpp");
    QVERIFY(internalSource.contains(QStringLiteral("void refreshCanvasProjectionAfterApply(")));
    QVERIFY(internalSource.contains(QStringLiteral("image->refreshGraphAsync(refreshRoot)")));
    QVERIFY(internalSource.contains(QStringLiteral("layer->setVisible(false)")));

    const QString shortcutsSource = readPluginSource("dock/shortcuts/ComfyUIRemoteDockShortcuts.cpp");
    const QString applyBody = functionBody(shortcutsSource, "void ComfyUIRemoteDock::slotAiDiffusionApply()");
    QVERIFY2(!applyBody.isEmpty(), "slotAiDiffusionApply body not found");
    QVERIFY(applyBody.contains(QStringLiteral("removeStaleLiveCanvasPreviewLayer")));
    QVERIFY(applyBody.contains(QStringLiteral("refreshCanvasProjectionAfterApply(image, active)")));
}

void ComfyUpscaleApplyRegressionTest::testLivePollDoesNotTouchCanvasLayers()
{
    const QString pollSource = readPluginSource("runners/live/ComfyLiveRunnerPoll.cpp");
    QVERIFY2(!pollSource.contains(QStringLiteral("updateLiveCanvasPreview")),
             "live poll must not modify canvas layers");
    QVERIFY2(!pollSource.contains(QStringLiteral("addNode")),
             "live poll must not add document layers");
    QVERIFY2(!pollSource.contains(QStringLiteral("getDocumentImage")),
             "live poll must not re-capture the document (causes canvas flicker)");
    QVERIFY2(!pollSource.contains(QStringLiteral("compositeLiveResultPreview(")),
             "live poll must use offline context composite only");

    const QString liveUiSource = readPluginSource("ui/builder/generate/ComfyDockUiBuilderGenerateLive.cpp");
    QVERIFY(liveUiSource.contains(QStringLiteral("livePreviewRowWidget")));
    QVERIFY(liveUiSource.contains(QStringLiteral("previewRowLay->addWidget(d->live.liveSpinner")));

    const QString uploadSource = readPluginSource("runners/live/ComfyLiveRunnerUpload.cpp");
    QVERIFY(uploadSource.contains(QStringLiteral("beginUploadPipeline")));
    QVERIFY(uploadSource.contains(QStringLiteral("startLiveSpinner")));

    const QString tickSource = readPluginSource("runners/live/ComfyLiveRunnerTick.cpp");
    QVERIFY(tickSource.contains(QStringLiteral("removeStaleLiveCanvasPreviewLayer")));
    QVERIFY2(!tickSource.contains(QStringLiteral("getDocumentImage")),
             "live tick must not re-capture the document every 100ms");
}

void ComfyUpscaleApplyRegressionTest::testHistorySourcesAvoidDeferredNodeManagerDirectOps()
{
    const QStringList historySources = {
        QStringLiteral("history/ComfyHistoryApply.cpp"),
        QStringLiteral("history/ComfyHistoryInternal.cpp"),
        QStringLiteral("history/ComfyHistoryPreview.cpp"),
    };
    for (const QString &rel : historySources) {
        const QString source = readPluginSource(rel.toUtf8().constData());
        QVERIFY2(!source.isEmpty(), qPrintable(QStringLiteral("missing source: ") + rel));
        QVERIFY2(!source.contains(QStringLiteral("addNodesDirect")),
                 qPrintable(rel + QStringLiteral(" must not use addNodesDirect (Android shape assert)")));
        QVERIFY2(!source.contains(QStringLiteral("moveNodesDirect")),
                 qPrintable(rel + QStringLiteral(" must not use moveNodesDirect (Android shape assert)")));
    }
}

void ComfyUpscaleApplyRegressionTest::testApplyResultFileWithBehaviorUsesImageAddNode()
{
    const QString source = readPluginSource("history/ComfyHistoryApply.cpp");
    QVERIFY(!source.isEmpty());
    const QString body = functionBody(source, "bool ComfyUIRemoteDock::applyResultFileWithBehavior(");
    QVERIFY2(!body.isEmpty(), "applyResultFileWithBehavior body not found");
    QVERIFY(body.contains(QStringLiteral("image->addNode(pl, root, above)")));
    QVERIFY(!body.contains(QStringLiteral("importImage(")));
    QVERIFY(body.contains(QStringLiteral("activateAppliedResultLayer")));
}

void ComfyUpscaleApplyRegressionTest::testMoveLayerInParentUsesImageAddNode()
{
    const QString source = readPluginSource("history/ComfyHistoryInternal.cpp");
    QVERIFY(!source.isEmpty());
    const QString body = functionBody(source, "void moveLayerInParent(");
    QVERIFY2(!body.isEmpty(), "moveLayerInParent body not found");
    QVERIFY(body.contains(QStringLiteral("image->removeNode(layerNode)")));
    QVERIFY(body.contains(QStringLiteral("image->addNode(layerNode, parent, above)")));
}

void ComfyUpscaleApplyRegressionTest::testActivateAppliedResultLayerDefersSelection()
{
    const QString source = readPluginSource("history/ComfyHistoryInternal.cpp");
    QVERIFY(!source.isEmpty());
    const QString body = functionBody(source, "void activateAppliedResultLayer(");
    QVERIFY2(!body.isEmpty(), "activateAppliedResultLayer body not found");
    QVERIFY(body.contains(QStringLiteral("activateNodeWhenReady")));
    QVERIFY(!body.contains(QStringLiteral("slotNonUiActivatedNode")));
}

void ComfyUpscaleApplyRegressionTest::testUpscalePollUsesApplyResultPath()
{
    const QString source = readPluginSource("runners/upscale/ComfyUpscaleRunnerPoll.cpp");
    QVERIFY(!source.isEmpty());
    QVERIFY(source.contains(QStringLiteral("ComfyHistoryInternal::upscaleResultLayerName")));
    QVERIFY(source.contains(QStringLiteral("applyResultFileWithBehavior")));
    QVERIFY(source.contains(QStringLiteral("applyBehaviorFromSettings")));
}

void ComfyUpscaleApplyRegressionTest::testUpscaleResultLayerNameFormat()
{
    QCOMPARE(ComfyHistoryInternal::upscaleResultLayerName(512, 768, 123456789),
             QStringLiteral("[Upscale] 512x768 (123456789)"));
    QCOMPARE(ComfyHistoryInternal::upscaleResultLayerName(1024, 1024, 0),
             QStringLiteral("[Upscale] 1024x1024 (0)"));
}

void ComfyUpscaleApplyRegressionTest::testMoveLayerInParentRepositionsImportedLayer()
{
    KisImageSP image = makeTestImage();
    KisPaintLayerSP bottom = addPaintLayer(image, QStringLiteral("bottom"));
    KisPaintLayerSP top = addPaintLayer(image, QStringLiteral("top"), bottom);
    KisPaintLayerSP imported = addPaintLayer(image, QStringLiteral("[Upscale] 128x128 (42)"), top);

    ComfyHistoryInternal::moveLayerInParent(nullptr, image, imported, image->rootLayer(), bottom, true);

    QCOMPARE(childLayerNames(image->rootLayer()),
             QStringList({QStringLiteral("bottom"),
                          QStringLiteral("[Upscale] 128x128 (42)"),
                          QStringLiteral("top")}));
}

void ComfyUpscaleApplyRegressionTest::testRaiseLayerToRootTopMovesLayerToStackTop()
{
    KisImageSP image = makeTestImage();
    KisPaintLayerSP bottom = addPaintLayer(image, QStringLiteral("bottom"));
    KisPaintLayerSP middle = addPaintLayer(image, QStringLiteral("middle"), bottom);
    KisPaintLayerSP imported = addPaintLayer(image, QStringLiteral("[Upscale] 256x256 (7)"), middle);

    ComfyHistoryInternal::raiseLayerToRootTop(nullptr, image, imported, true);

    QCOMPARE(childLayerNames(image->rootLayer()),
             QStringList({QStringLiteral("bottom"),
                          QStringLiteral("middle"),
                          QStringLiteral("[Upscale] 256x256 (7)")}));
    QCOMPARE(image->rootLayer()->lastChild()->name(), QStringLiteral("[Upscale] 256x256 (7)"));
}

QTEST_MAIN(ComfyUpscaleApplyRegressionTest)
#include "ComfyUpscaleApplyRegressionTest.moc"
