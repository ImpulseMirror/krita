/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"
#include "ComfyStyleCollection.h"
#include "ComfyResources.h"
#include "ComfyWorkflowEngine.h"
#include "ComfyTheme.h"
#include "ComfyControlLayer.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QRandomGenerator>
#include <QHash>
#include <QBuffer>
#include <QCryptographicHash>
#include <QImageWriter>
#include <QRegularExpression>
#include <QUuid>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QPainter>
#include <thread>
#include <KoColorConversionTransformation.h>

#ifdef COMFYUI_HAVE_KARCHIVE
#include <KZip>
#endif

#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_annotation.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>
#include <krita_utils.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorProfile.h>
#include <KoColorModelStandardIds.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>


namespace ComfyUIUtils {


void applyTiledVaePreferenceToWorkflow(QJsonObject &workflow)
{
    QJsonObject s = loadSettingsJson();
    QString mode = s.value(QStringLiteral("tiled_vae_mode")).toString();
    if (mode.isEmpty())
        mode = s.value(QStringLiteral("tiled_vae_always")).toBool(false) ? QStringLiteral("always") : QStringLiteral("automatic");
    if (mode != QLatin1String("always"))
        return;

    const QStringList nodeIds = workflow.keys();
    for (const QString &nodeId : nodeIds) {
        const QJsonValue v = workflow.value(nodeId);
        if (!v.isObject())
            continue;
        QJsonObject node = v.toObject();
        if (node.value(QStringLiteral("class_type")).toString() != QLatin1String("VAEDecode"))
            continue;
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        node.insert(QStringLiteral("class_type"), QStringLiteral("VAEDecodeTiled"));
        inputs.insert(QStringLiteral("tile_size"), 512);
        inputs.insert(QStringLiteral("overlap"), 64);
        inputs.insert(QStringLiteral("temporal_size"), 64);
        inputs.insert(QStringLiteral("temporal_overlap"), 8);
        node.insert(QStringLiteral("inputs"), inputs);
        workflow.insert(nodeId, node);
    }
}

void applyDynamicCachingPreferenceToWorkflow(QJsonObject &workflow)
{
    const QJsonObject s = loadSettingsJson();
    if (!s.value(QStringLiteral("dynamic_caching")).toBool(false))
        return;

    const QStringList nodeIds = workflow.keys();
    for (const QString &nodeId : nodeIds) {
        const QJsonValue v = workflow.value(nodeId);
        if (!v.isObject())
            continue;
        QJsonObject node = v.toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        const bool maybeFbc = ct.contains(QStringLiteral("BlockCache"), Qt::CaseInsensitive)
            || ct.contains(QStringLiteral("FBCache"), Qt::CaseInsensitive)
            || ct.contains(QStringLiteral("TeaCache"), Qt::CaseInsensitive)
            || ct.contains(QStringLiteral("FirstBlock"), Qt::CaseInsensitive);
        if (!maybeFbc)
            continue;
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        bool touched = false;
        const QStringList toggleKeys = { QStringLiteral("enable"), QStringLiteral("enabled"), QStringLiteral("use_fb"),
                                         QStringLiteral("use_fbc"), QStringLiteral("active"), QStringLiteral("on") };
        for (const QString &k : toggleKeys) {
            if (!inputs.contains(k))
                continue;
            const QJsonValue cur = inputs.value(k);
            if (cur.isBool() && !cur.toBool()) {
                inputs.insert(k, true);
                touched = true;
            } else if (cur.isDouble() && cur.toInt() == 0) {
                inputs.insert(k, 1);
                touched = true;
            }
        }
        if (touched) {
            node.insert(QStringLiteral("inputs"), inputs);
            workflow.insert(nodeId, node);
        }
    }
}

void applyPerformancePreferencesToWorkflow(QJsonObject &workflow)
{
    applyTiledVaePreferenceToWorkflow(workflow);
    applyDynamicCachingPreferenceToWorkflow(workflow);
}

QJsonObject buildControlImageWorkflow(const QString &inputImageName,
                                      const QString &controlMode,
                                      int resolution,
                                      bool invertOutput)
{
    const QString mode = controlMode.trimmed().toLower();
    QString preprocessor;
    QJsonObject preInputs;
    bool useScribbleSecondStage = false;
    if (mode == QLatin1String("hands")) {
        preprocessor = QStringLiteral("MeshGraphormer-DepthMapPreprocessor");
        preInputs.insert(QStringLiteral("mask_type"), QStringLiteral("based_on_depth"));
        preInputs.insert(QStringLiteral("rand_seed"), 0);
    } else if (mode == QLatin1String("scribble")) {
        preprocessor = QStringLiteral("PiDiNetPreprocessor");
        preInputs.insert(QStringLiteral("safe"), QStringLiteral("enable"));
        useScribbleSecondStage = true;
    } else if (mode == QLatin1String("line_art")) {
        preprocessor = QStringLiteral("LineArtPreprocessor");
        preInputs.insert(QStringLiteral("coarse"), QStringLiteral("disable"));
    } else if (mode == QLatin1String("soft_edge")) {
        preprocessor = QStringLiteral("AnyLineArtPreprocessor_aux");
        preInputs.insert(QStringLiteral("merge_with_lineart"), QStringLiteral("lineart_standard"));
        preInputs.insert(QStringLiteral("lineart_lower_bound"), 0);
        preInputs.insert(QStringLiteral("lineart_upper_bound"), 1);
        preInputs.insert(QStringLiteral("object_min_size"), 36);
        preInputs.insert(QStringLiteral("object_connectivity"), 1);
    } else if (mode == QLatin1String("canny_edge")) {
        preprocessor = QStringLiteral("CannyEdgePreprocessor");
        preInputs.insert(QStringLiteral("low_threshold"), 80);
        preInputs.insert(QStringLiteral("high_threshold"), 200);
    } else if (mode == QLatin1String("depth")) {
        preprocessor = QStringLiteral("DepthAnythingV2Preprocessor");
        preInputs.insert(QStringLiteral("ckpt_name"), QStringLiteral("depth_anything_v2_vitb.pth"));
    } else if (mode == QLatin1String("normal")) {
        preprocessor = QStringLiteral("BAE-NormalMapPreprocessor");
    } else if (mode == QLatin1String("pose")) {
        preprocessor = QStringLiteral("DWPreprocessor");
        preInputs.insert(QStringLiteral("detect_hand"), QStringLiteral("enable"));
        preInputs.insert(QStringLiteral("detect_body"), QStringLiteral("enable"));
        preInputs.insert(QStringLiteral("detect_face"), QStringLiteral("enable"));
        preInputs.insert(QStringLiteral("bbox_detector"), QStringLiteral("yolox_l.onnx"));
        preInputs.insert(QStringLiteral("pose_estimator"), QStringLiteral("dw-ll_ucoco_384_bs5.torchscript.pt"));
    } else if (mode == QLatin1String("segmentation")) {
        preprocessor = QStringLiteral("OneFormer-COCO-SemSegPreprocessor");
    } else {
        return QJsonObject();
    }

    // §13.53: \p resolution = shortest side of extent; normalize to 64-multiple; non-hands ≥512.
    int res = qMax(64, resolution);
    res = ((res + 63) / 64) * 64;
    if (mode != QLatin1String("hands")) {
        res = qMax(512, res);
    }
    preInputs.insert(QStringLiteral("image"), QJsonArray{QStringLiteral("1"), 0});
    preInputs.insert(QStringLiteral("resolution"), res);

    QJsonObject workflow;
    workflow.insert(QStringLiteral("1"),
                    QJsonObject{{QStringLiteral("class_type"), QStringLiteral("LoadImage")},
                                {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("image"), inputImageName}}}});
    workflow.insert(QStringLiteral("2"),
                    QJsonObject{{QStringLiteral("class_type"), preprocessor},
                                {QStringLiteral("inputs"), preInputs}});
    QString sourceNode = QStringLiteral("2");
    if (useScribbleSecondStage) {
        workflow.insert(QStringLiteral("3"),
                        QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ScribblePreprocessor")},
                                    {QStringLiteral("inputs"), QJsonObject{
                                                                      {QStringLiteral("image"), QJsonArray{QStringLiteral("2"), 0}},
                                                                      {QStringLiteral("resolution"), res},
                                                                  }}});
        sourceNode = QStringLiteral("3");
    }
    const bool shouldInvert = invertOutput || isControlModeLines(mode);
    if (shouldInvert) {
        workflow.insert(QStringLiteral("4"),
                        QJsonObject{{QStringLiteral("class_type"), QStringLiteral("ImageInvert")},
                                    {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("image"), QJsonArray{sourceNode, 0}}}}});
        sourceNode = QStringLiteral("4");
    }
    workflow.insert(QStringLiteral("9"),
                    QJsonObject{{QStringLiteral("class_type"), QStringLiteral("SaveImage")},
                                {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("filename_prefix"), QStringLiteral("ComfyUI_control")},
                                                                       {QStringLiteral("images"), QJsonArray{sourceNode, 0}}}}});
    return workflow;
}

bool isControlModeLines(const QString &controlMode)
{
    const QString m = controlMode.trimmed().toLower();
    return m == QLatin1String("scribble")
        || m == QLatin1String("line_art")
        || m == QLatin1String("soft_edge")
        || m == QLatin1String("canny_edge");
}

QImage compositeControlImageOntoExtent(const QImage &processedCrop,
                                       const QSize &fullExtentSize,
                                       const QRect &cropInExtentCoords)
{
    if (processedCrop.isNull() || !fullExtentSize.isValid() || fullExtentSize.width() <= 0
        || fullExtentSize.height() <= 0)
        return processedCrop;
    if (!cropInExtentCoords.isValid())
        return processedCrop;
    const QRect fullRect(QPoint(0, 0), fullExtentSize);
    if (cropInExtentCoords == fullRect)
        return processedCrop;
    QImage out(fullExtentSize.width(), fullExtentSize.height(), QImage::Format_ARGB32);
    out.fill(Qt::black);
    const QImage patch =
        processedCrop.scaled(cropInExtentCoords.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&out);
    painter.drawImage(cropInExtentCoords.topLeft(), patch);
    return out;
}

void applyUpscaleRefineVaedecodeTiling(QJsonObject &workflow,
                                      const QString &decodeNodeId,
                                      int tileOverlapMode,
                                      int customOverlapPx,
                                      const QJsonObject &settingsRoot)
{
    if (!workflow.contains(decodeNodeId))
        return;
    QJsonObject node = workflow.value(decodeNodeId).toObject();
    const QString ct = node.value(QStringLiteral("class_type")).toString();
    if (ct != QLatin1String("VAEDecode") && ct != QLatin1String("VAEDecodeTiled"))
        return;

    const int tileSize = diffusionUpscaleTileEstimateExtentPx(settingsRoot);
    int overlap = 64;
    if (tileOverlapMode == 1)
        overlap = qBound(8, customOverlapPx, qMax(8, tileSize - 1));
    else
        overlap = qBound(8, tileSize / 8, 128);

    QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
    node.insert(QStringLiteral("class_type"), QStringLiteral("VAEDecodeTiled"));
    inputs.insert(QStringLiteral("tile_size"), tileSize);
    inputs.insert(QStringLiteral("overlap"), overlap);
    inputs.insert(QStringLiteral("temporal_size"), 64);
    inputs.insert(QStringLiteral("temporal_overlap"), 8);
    node.insert(QStringLiteral("inputs"), inputs);
    workflow.insert(decodeNodeId, node);
}

} // namespace ComfyUIUtils
