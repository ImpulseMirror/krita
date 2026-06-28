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
};

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
