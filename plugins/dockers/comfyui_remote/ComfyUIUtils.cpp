/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"

#include <cmath>
#include <algorithm>
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
#include <QSet>
#include <QHash>
#include <QBuffer>
#include <QImageWriter>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
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
#include <kis_annotation.h>
#include <kis_node.h>
#include <kis_selection.h>
#include <kis_paint_device.h>
#include <kis_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <KoColorSpaceRegistry.h>
#include <KoColorProfile.h>
#include <KoColorModelStandardIds.h>
#include <KisViewManager.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <klocalizedstring.h>
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace ComfyUIUtils {

qint64 documentEmbeddedHistoryStorageBytes(KisImageSP image)
{
    if (!image)
        return 0;
    qint64 total = 0;
    const QString prefix = documentAnnotationKey(QStringLiteral("result"));
    for (auto it = image->beginAnnotations(); it != image->endAnnotations(); ++it) {
        const KisAnnotationSP ann = *it;
        if (!ann)
            continue;
        const QString t = ann->type();
        if (t.startsWith(prefix))
            total += static_cast<qint64>(ann->annotation().size());
    }
    return total;
}

qint64 documentEmbeddedHistoryLimitBytes(const QJsonObject &settings)
{
    int mb = settings.value(QStringLiteral("history_document_storage_mb")).toInt(0);
    if (mb <= 0)
        mb = settings.value(QStringLiteral("history_storage")).toInt(20);
    mb = qBound(5, mb, 2000);
    return static_cast<qint64>(mb) * 1024ll * 1024ll;
}

static int uiJsonStoredVersion(const QJsonObject &o)
{
    const QJsonValue v = o.value(QStringLiteral("version"));
    if (v.isUndefined() || v.isNull()) {
        return 0;
    }
    if (v.isDouble()) {
        return int(v.toDouble());
    }
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().trimmed().toInt(&ok);
        return ok ? n : 0;
    }
    return 0;
}

DocumentUiJsonLoadOutcome loadDocumentUiJsonWithMeta(KisImageSP image)
{
    QJsonObject def;
    def.insert(QStringLiteral("version"), persistenceFormatVersion);
    DocumentUiJsonLoadOutcome out;
    out.object = def;
    out.rawVersionFromFile = persistenceFormatVersion;
    if (!image) {
        return out;
    }
    const QString key = documentUiJsonAnnotationKey();
    KisAnnotationSP ann = image->annotation(key);
    if (!ann || ann->annotation().isEmpty()) {
        return out;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(ann->annotation(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        out.parseFailed = true;
        out.parseError = (err.error != QJsonParseError::NoError) ? err.errorString() : QStringLiteral("JSON root is not an object");
        return out;
    }
    QJsonObject o = doc.object();
    const int ver = uiJsonStoredVersion(o);
    out.rawVersionFromFile = ver > 0 ? ver : persistenceFormatVersion;
    if (ver > persistenceFormatVersion) {
        qWarning() << "ComfyUI ui.json version" << ver << "is newer than supported" << persistenceFormatVersion
                   << "- loading default embedded state.";
        out.resetToDefaultsDueToFutureVersion = true;
        out.object = def;
        return out;
    }
    if (ver <= 0) {
        o.insert(QStringLiteral("version"), persistenceFormatVersion);
    } else if (!o.contains(QStringLiteral("version"))) {
        o.insert(QStringLiteral("version"), persistenceFormatVersion);
    }
    out.object = o;
    return out;
}

QJsonObject loadDocumentUiJsonObject(KisImageSP image)
{
    return loadDocumentUiJsonWithMeta(image).object;
}

bool documentHasStoredUiJsonPayload(KisImageSP image)
{
    if (!image)
        return false;
    const KisAnnotationSP ann = image->annotation(documentUiJsonAnnotationKey());
    return ann && !ann->annotation().isEmpty();
}

QJsonObject documentDefaultsFromSettingsRoot(const QJsonObject &settingsRoot)
{
    return settingsRoot.value(QStringLiteral("document_defaults")).toObject();
}

QString inpaintContextForFreshDocumentDefaults(const QString &storedContext)
{
    QString n = storedContext.trimmed().toLower();
    n.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (n == QLatin1String("layer_bounds"))
        return QStringLiteral("automatic");
    if (n.isEmpty())
        return QStringLiteral("automatic");
    return storedContext.trimmed();
}

QString historyResultLogicalKey(int slot)
{
    return QStringLiteral("result%1.webp").arg(slot);
}

static int parseResultSlotFromAnnotationType(const QString &fullType)
{
    const QString pfx = documentAnnotationKey(QString());
    if (!fullType.startsWith(pfx))
        return -1;
    const QString logical = fullType.mid(pfx.length());
    static const QRegularExpression re(QStringLiteral("^result(\\d+)(\\.webp)?$"));
    const QRegularExpressionMatch m = re.match(logical);
    if (!m.hasMatch())
        return -1;
    return m.captured(1).toInt();
}

int maxHistorySlotFromDocument(KisImageSP image, const QJsonObject &uiRoot)
{
    int maxS = -1;
    const QJsonArray h = uiRoot.value(QStringLiteral("history")).toArray();
    for (const QJsonValue &v : h) {
        const int s = v.toObject().value(QStringLiteral("slot")).toInt(-1);
        if (s > maxS)
            maxS = s;
    }
    if (image) {
        for (auto it = image->beginAnnotations(); it != image->endAnnotations(); ++it) {
            const KisAnnotationSP ann = *it;
            if (!ann)
                continue;
            const int s = parseResultSlotFromAnnotationType(ann->type());
            if (s > maxS)
                maxS = s;
        }
    }
    return maxS;
}

static QByteArray encodeSingleHistoryImage(const QImage &src, const QString &fmtNorm, const QJsonObject &settings)
{
    QImage im = src;
    if (im.format() != QImage::Format_RGBA8888 && im.format() != QImage::Format_ARGB32)
        im = im.convertToFormat(QImage::Format_ARGB32);
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QByteArray formatBa;
    int quality = -1;
    if (fmtNorm == QLatin1String("jpeg") || fmtNorm == QLatin1String("jpg")) {
        formatBa = QByteArrayLiteral("jpeg");
        quality = saveImageQualityJpeg(settings);
    } else if (fmtNorm == QLatin1String("png") || fmtNorm == QLatin1String("png_small")) {
        formatBa = QByteArrayLiteral("png");
        if (fmtNorm == QLatin1String("png_small"))
            quality = 75;
    } else {
        formatBa = QByteArrayLiteral("webp");
        quality = saveImageQualityWebp(settings);
    }
    QImageWriter w(&buf, formatBa);
    if (quality >= 0)
        w.setQuality(quality);
    if (!w.write(im))
        return QByteArray();
    return buf.data();
}

HistoryImageEncodeResult encodeHistoryImagesFromPaths(const QStringList &paths, const QJsonObject &settings)
{
    HistoryImageEncodeResult r;
    QString fmt = settings.value(QStringLiteral("history_format")).toString().trimmed().toLower();
    if (fmt.isEmpty())
        fmt = QStringLiteral("webp");
    int pos = 0;
    for (const QString &path : paths) {
        QImage img(path);
        if (img.isNull())
            return HistoryImageEncodeResult();
        const QByteArray chunk = encodeSingleHistoryImage(img, fmt, settings);
        if (chunk.isEmpty())
            return HistoryImageEncodeResult();
        r.data.append(chunk);
        pos += chunk.size();
        r.offsets.append(pos);
    }
    return r;
}

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

void extractLoraFilenamesFromObjectInfo(const QJsonObject &root, QStringList *out)
{
    out->clear();
    QSet<QString> seen;
    static const QStringList nodeKeys = { QStringLiteral("LoraLoader"), QStringLiteral("LoraLoaderModelOnly"),
                                          QStringLiteral("FluxLoraLoader"), QStringLiteral("LoraLoaderAdvanced") };
    static const QStringList loraKeys = { QStringLiteral("lora_name"), QStringLiteral("model_name") };
    for (const QString &nn : nodeKeys) {
        const QJsonObject nodeInfo = root.value(nn).toObject();
        const QJsonObject required =
            nodeInfo.value(QStringLiteral("input")).toObject().value(QStringLiteral("required")).toObject();
        for (const QString &lk : loraKeys) {
            const QJsonValue loraVal = required.value(lk);
            if (!loraVal.isArray())
                continue;
            const QJsonArray arr = loraVal.toArray();
            if (arr.isEmpty() || !arr.at(0).isArray())
                continue;
            for (const QJsonValue &v : arr.at(0).toArray()) {
                const QString s = v.toString();
                if (!s.isEmpty())
                    seen.insert(s);
            }
        }
    }
    *out = QStringList(seen.begin(), seen.end());
    out->sort(Qt::CaseInsensitive);
}

bool loraFilenameKnownOnServer(const QString &libraryBasename, const QStringList &serverEntries)
{
    if (libraryBasename.isEmpty() || serverEntries.isEmpty())
        return false;
    for (const QString &s : serverEntries) {
        if (s.compare(libraryBasename, Qt::CaseInsensitive) == 0)
            return true;
        if (s.endsWith(QLatin1Char('/') + libraryBasename, Qt::CaseInsensitive))
            return true;
        if (QFileInfo(s).fileName().compare(libraryBasename, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

static QString checkpointModelInfoFilename(const QJsonObject &o)
{
    QString n = o.value(QStringLiteral("filename")).toString().trimmed();
    if (n.isEmpty())
        n = o.value(QStringLiteral("name")).toString().trimmed();
    return n;
}

static QString checkpointNormKey(const QString &pathOrName)
{
    return QFileInfo(pathOrName.trimmed()).fileName().toLower();
}

static QJsonArray etnModelInfoItemsArray(const QJsonDocument &doc)
{
    if (!doc.isObject())
        return QJsonArray();
    const QJsonObject root = doc.object();
    static const QStringList keys = { QStringLiteral("items"), QStringLiteral("models"), QStringLiteral("checkpoints"),
                                      QStringLiteral("data"), QStringLiteral("results") };
    for (const QString &k : keys) {
        const QJsonValue v = root.value(k);
        if (v.isArray())
            return v.toArray();
    }
    return QJsonArray();
}

// §13.75: Exclude unknown arch, refiner-only, and non-Flux inpaint-only checkpoints.
static bool checkpointPassesMainListFilter(const QJsonObject &info)
{
    const QJsonValue archV = info.value(QStringLiteral("arch"));
    if (archV.isNull())
        return false;
    QString archStr;
    if (archV.isString())
        archStr = archV.toString().trimmed();
    else if (archV.isDouble())
        archStr = QString::number(archV.toInt());
    if (archStr.isEmpty())
        return false;

    const bool isFlux = archStr.contains(QStringLiteral("flux"), Qt::CaseInsensitive);
    if (info.value(QStringLiteral("is_refiner")).toBool(false))
        return false;
    if (info.value(QStringLiteral("is_inpaint")).toBool(false) && !isFlux)
        return false;
    return true;
}

QStringList filterCheckpointNamesWithEtnModelInfo(const QStringList &checkpointLoaderNames, const QJsonDocument &modelInfoResponse)
{
    if (checkpointLoaderNames.isEmpty())
        return checkpointLoaderNames;
    const QJsonArray items = etnModelInfoItemsArray(modelInfoResponse);
    if (items.isEmpty())
        return checkpointLoaderNames;

    QHash<QString, QJsonObject> byNormKey;
    byNormKey.reserve(items.size() + 8);
    for (const QJsonValue &v : items) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString fn = checkpointModelInfoFilename(o);
        if (fn.isEmpty())
            continue;
        byNormKey.insert(checkpointNormKey(fn), o);
        byNormKey.insert(fn.toLower(), o);
    }

    QStringList out;
    out.reserve(checkpointLoaderNames.size());
    for (const QString &name : checkpointLoaderNames) {
        const QString nk = checkpointNormKey(name);
        const auto it = byNormKey.constFind(nk);
        if (it == byNormKey.cend()) {
            out.append(name);
            continue;
        }
        if (checkpointPassesMainListFilter(it.value()))
            out.append(name);
    }
    return out.isEmpty() ? checkpointLoaderNames : out;
}

namespace {

// §13.56: Minimal preset table (same keys as samplers.json fields: sampler, scheduler, steps, minimum_steps, cfg).
const char g_builtinSamplersJson[] = R"json({
    "Default - Euler": {"sampler": "euler", "scheduler": "normal", "steps": 20, "minimum_steps": 1, "cfg": 8.0},
    "DPM++ 2M Karras": {"sampler": "dpmpp_2m", "scheduler": "karras", "steps": 25, "minimum_steps": 10, "cfg": 7.0},
    "Euler Ancestral": {"sampler": "euler_ancestral", "scheduler": "normal", "steps": 24, "minimum_steps": 1, "cfg": 7.0}
})json";

static QJsonObject loadMergedSamplerPresets()
{
    QJsonObject merged;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(g_builtinSamplersJson), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject())
        merged = doc.object();
    const QString path = ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets/samplers.json");
    QDir().mkpath(ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets"));
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument ud = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(f.readAll()), &err);
        f.close();
        if (err.error == QJsonParseError::NoError && ud.isObject()) {
            const QJsonObject fo = ud.object();
            for (auto it = fo.constBegin(); it != fo.constEnd(); ++it) {
                if (it.value().isObject())
                    merged.insert(it.key(), it.value());
            }
        }
    }
    return merged;
}

static QJsonObject g_samplerPresetsCache;
static bool g_samplerPresetsLoaded = false;

static void refreshSamplerPresetsCache()
{
    g_samplerPresetsCache = loadMergedSamplerPresets();
    g_samplerPresetsLoaded = true;
}

static void ensureSamplerPresetsLoaded()
{
    if (!g_samplerPresetsLoaded)
        refreshSamplerPresetsCache();
}

} // namespace

QJsonObject builtinSamplerPresetsRoot()
{
    ensureSamplerPresetsLoaded();
    return g_samplerPresetsCache;
}

void reloadSamplerPresetsCache()
{
    refreshSamplerPresetsCache();
}

namespace {

// §13.55: Same top-level shape as ai_diffusion/presets/control.json (mode → { "all" | arch → [ { strength, start, end }, … ] }).
const char g_builtinControlJson[] = R"json({
    "default": {
        "all": [
            {"strength": 0.7, "start": 0.0, "end": 0.5},
            {"strength": 1.0, "start": 0.0, "end": 1.0}
        ]
    }
})json";

static QJsonObject loadMergedControlPresets()
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(g_builtinControlJson), &err);
    QJsonObject merged;
    if (err.error == QJsonParseError::NoError && doc.isObject())
        merged = doc.object();
    const QString path = ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets/control.json");
    QDir().mkpath(ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets"));
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument ud = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(f.readAll()), &err);
        f.close();
        if (err.error == QJsonParseError::NoError && ud.isObject()) {
            const QJsonObject fo = ud.object();
            for (auto it = fo.constBegin(); it != fo.constEnd(); ++it) {
                if (it.value().isObject())
                    merged.insert(it.key(), it.value());
            }
        }
    }
    return merged;
}

static QJsonObject g_controlPresetsCache;
static bool g_controlPresetsLoaded = false;

static void refreshControlPresetsCache()
{
    g_controlPresetsCache = loadMergedControlPresets();
    g_controlPresetsLoaded = true;
}

static void ensureControlPresetsLoaded()
{
    if (!g_controlPresetsLoaded)
        refreshControlPresetsCache();
}

} // namespace

QJsonObject builtinControlPresetsRoot()
{
    ensureControlPresetsLoaded();
    return g_controlPresetsCache;
}

void reloadControlPresetsCache()
{
    refreshControlPresetsCache();
}

QList<ControlLayerPreset> controlPresetsForMode(const QJsonObject &root, const QString &controlMode, const QString &archKey)
{
    QList<ControlLayerPreset> out;
    const QJsonObject modeObj = root.value(controlMode).toObject();
    if (modeObj.isEmpty())
        return out;
    QJsonArray arr;
    if (!archKey.isEmpty()) {
        const QJsonValue archV = modeObj.value(archKey);
        if (archV.isArray() && !archV.toArray().isEmpty())
            arr = archV.toArray();
    }
    if (arr.isEmpty())
        arr = modeObj.value(QStringLiteral("all")).toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        ControlLayerPreset p;
        p.strength = o.value(QStringLiteral("strength")).toDouble(1.0);
        p.start = o.value(QStringLiteral("start")).toDouble(0.0);
        p.end = o.value(QStringLiteral("end")).toDouble(1.0);
        out.append(p);
    }
    return out;
}

bool resolveDefaultControlLayerPreset(const QJsonObject &settings, ControlLayerPreset *out, const QString &archKey)
{
    if (!out)
        return false;
    const QJsonObject root = builtinControlPresetsRoot();
    const QList<ControlLayerPreset> ps = controlPresetsForMode(root, QStringLiteral("default"), archKey);
    if (ps.isEmpty())
        return false;
    const int saved = settings.value(QStringLiteral("control_layer_default_preset_index")).toInt(0);
    const int idx = qBound(0, saved, qMin(3, ps.size() - 1));
    *out = ps.at(idx);
    return true;
}

bool samplerPresetLookup(const QJsonObject &root,
                         const QString &presetName,
                         QString *outSampler,
                         QString *outScheduler,
                         int *outSteps,
                         int *outMinimumSteps,
                         double *outCfg)
{
    if (!outSampler || !outScheduler || !outSteps || !outMinimumSteps || !outCfg)
        return false;
    const QJsonObject o = root.value(presetName).toObject();
    if (o.isEmpty())
        return false;
    const QString sam = o.value(QStringLiteral("sampler")).toString();
    if (sam.isEmpty())
        return false;
    *outSampler = sam;
    *outScheduler = o.value(QStringLiteral("scheduler")).toString(QStringLiteral("normal"));
    *outSteps = o.value(QStringLiteral("steps")).toInt(20);
    *outMinimumSteps = o.value(QStringLiteral("minimum_steps")).toInt(1);
    if (o.contains(QStringLiteral("cfg")))
        *outCfg = o.value(QStringLiteral("cfg")).toDouble(8.0);
    else
        *outCfg = o.value(QStringLiteral("cfg_scale")).toDouble(8.0);
    return true;
}

ResolvedSamplerInputs resolveSamplerForLive(const QJsonObject &settings,
                                            const QString &dockSamplerText,
                                            int dockSteps,
                                            double dockCfg)
{
    ResolvedSamplerInputs r;
    const QString key = settings.value(QStringLiteral("live_sampler_preset")).toString().trimmed();
    if (!key.isEmpty()) {
        const QJsonObject root = builtinSamplerPresetsRoot();
        QString sam, sch;
        int st, minSt;
        double cfg = 8.0;
        if (samplerPresetLookup(root, key, &sam, &sch, &st, &minSt, &cfg)) {
            r.sampler = sam;
            r.scheduler = sch;
            r.steps = qMax(st, minSt);
            r.cfg = cfg;
            return r;
        }
    }
    r.sampler = dockSamplerText.trimmed().isEmpty() ? QStringLiteral("euler") : dockSamplerText.trimmed();
    r.scheduler = QStringLiteral("normal");
    r.steps = dockSteps;
    r.cfg = dockCfg;
    return r;
}

// §13.204: Single source of truth — change only here for packaging and UI
QString pluginVersion()
{
    return QStringLiteral("1.0.0");
}

QString intersticeApiBaseUrl()
{
    QString base = QString::fromUtf8(qgetenv("INTERSTICE_URL")).trimmed();
    if (base.isEmpty())
        return QStringLiteral("https://api.interstice.cloud");
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

QString intersticeWebBaseUrl()
{
    QString base = QString::fromUtf8(qgetenv("INTERSTICE_WEB_URL")).trimmed();
    if (base.isEmpty())
        return QStringLiteral("https://www.interstice.cloud");
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

// §13.191: Same contract and message format as document.check_color_mode()
std::pair<bool, QString> checkColorMode(KisImageSP image)
{
    if (!image || !image->colorSpace()) {
        return {true, QString()};  // no document → allow (stub behavior per §13.42)
    }
    const KoColorSpace *cs = image->colorSpace();
    if (cs->colorModelId() != RGBAColorModelID) {
        return {false,
                i18n("Incompatible document: Color model must be RGB/Alpha (current model: %1)", cs->colorModelId().name())};
    }
    if (cs->colorDepthId() != Integer8BitsColorDepthID) {
        return {false,
                i18n("Incompatible document: Color depth must be 8-bit integer (current depth: %1)", cs->colorDepthId().name())};
    }
    return {true, QString()};
}

QString historyCacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    QString path = base + QStringLiteral("/krita/comfyui_remote");
    QDir().mkpath(path);
    return path;
}

// §13.66: user_data_dir — AppDataLocation + "ai_diffusion" when path contains "krita"; else GenericDataLocation + "krita-ai-diffusion".
// Migrates legacy C++ path …/krita/comfyui_remote → resolved dir when only legacy has settings.json.
QString pluginUserDataDir()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString legacyBase = appData.isEmpty() ? QDir::tempPath() : appData;
    const QString legacyPath = legacyBase + QStringLiteral("/krita/comfyui_remote");
    const QString settingsFile = QStringLiteral("/settings.json");

    QString resolved;
    if (!appData.isEmpty() && appData.contains(QLatin1String("krita"), Qt::CaseInsensitive)) {
        resolved = appData + QStringLiteral("/ai_diffusion");
    } else {
        QString gen = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (gen.isEmpty())
            gen = QDir::homePath();
        resolved = gen + QStringLiteral("/krita-ai-diffusion");
    }

    const QString newSettings = resolved + settingsFile;
    const QString oldSettings = legacyPath + settingsFile;
    if (!QFile::exists(newSettings) && QFile::exists(oldSettings)) {
        QFileInfo ri(resolved);
        if (ri.exists()) {
            QDir rd(resolved);
            if (rd.exists() && rd.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
                rd.rmdir(resolved);
        }
        QDir().mkpath(QFileInfo(resolved).path());
        if (!QFile::rename(legacyPath, resolved)) {
            QDir().mkpath(legacyPath);
            return legacyPath;
        }
    }

    QDir().mkpath(resolved);
    return resolved;
}

namespace {

// Directory containing this plugin's shared library (Python plugin_dir equivalent for §13.66).
QString pluginBinaryDirectory()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    typedef QString (*Symbol)();
    Symbol sym = pluginVersion;
    void *addr = reinterpret_cast<void *>(sym);
    Dl_info info;
    if (dladdr(addr, &info) == 0 || !info.dli_fname)
        return QString();
    return QFileInfo(QFile::decodeName(info.dli_fname)).absolutePath();
#elif defined(Q_OS_WIN)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&pluginVersion), &module)
        || !module) {
        return QString();
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return QString();
    return QFileInfo(QString::fromWCharArray(buf)).absolutePath();
#else
    return QString();
#endif
}

// §13.66: Legacy folder `.logs` → log_dir (user_data_dir/logs). Python v1.14 used plugin_dir/.logs;
// also migrate user_data_dir/.logs if present (e.g. older C++ layouts).
void migrateLegacyDotLogsPath(const QString &legacyDotLogs, const QString &logDir)
{
    const QFileInfo legInfo(legacyDotLogs);
    if (!legInfo.isDir())
        return;

    if (!QFileInfo::exists(logDir) && QFile::rename(legacyDotLogs, logDir))
        return;

    QDir().mkpath(logDir);
    QDir leg(legacyDotLogs);
    const QFileInfoList top = leg.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : top) {
        const QString dest = logDir + QLatin1Char('/') + fi.fileName();
        if (QFile::exists(dest))
            continue;
        if (!QFile::rename(fi.absoluteFilePath(), dest)) {
            if (fi.isDir()) {
                QDir srcDir(fi.absoluteFilePath());
                QDir().mkpath(dest);
                const QFileInfoList inner = srcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo &inFi : inner) {
                    const QString d2 = dest + QLatin1Char('/') + inFi.fileName();
                    if (!QFile::exists(d2))
                        QFile::rename(inFi.absoluteFilePath(), d2);
                }
                if (srcDir.entryList(QDir::NoDotAndDotDot).isEmpty())
                    srcDir.rmdir(fi.absoluteFilePath());
            }
        }
    }
    leg.refresh();
    if (leg.entryList(QDir::NoDotAndDotDot).isEmpty())
        leg.rmdir(legacyDotLogs);
}

} // namespace

QString pluginLogDir()
{
    const QString userDataDir = pluginUserDataDir();
    const QString path = userDataDir + QStringLiteral("/logs");
    migrateLegacyDotLogsPath(userDataDir + QStringLiteral("/.logs"), path);
    const QString binDir = pluginBinaryDirectory();
    if (!binDir.isEmpty())
        migrateLegacyDotLogsPath(binDir + QStringLiteral("/.logs"), path);
    QDir().mkpath(path);
    return path;
}

// §13.148: database_dir for LoRA persistence (user_data_dir/database); checkpoints stay in-memory from server
QString pluginDatabaseDir()
{
    QString path = pluginUserDataDir() + QStringLiteral("/database");
    QDir().mkpath(path);
    return path;
}

QString workflowsStorageDir()
{
    QString path = pluginUserDataDir() + QStringLiteral("/workflows");
    QDir().mkpath(path);
    return path;
}

QString tagsStorageDir()
{
    QString path = pluginUserDataDir() + QStringLiteral("/tags");
    QDir().mkpath(path);
    return path;
}

QStringList tagKeywordsForAutocomplete(const QJsonObject &settingsIn)
{
    const QJsonObject settings = settingsIn.isEmpty() ? loadSettingsJson() : settingsIn;
    QString tagDir = settings.value(QStringLiteral("tag_directory")).toString().trimmed();
    if (tagDir.isEmpty())
        tagDir = tagsStorageDir();

    QJsonArray arr = settings.value(QStringLiteral("tag_files")).toArray();
    QStringList stems;
    if (arr.isEmpty()) {
        stems << QStringLiteral("Danbooru") << QStringLiteral("e621");
    } else {
        for (const QJsonValue &v : arr) {
            const QString t = v.toString().trimmed();
            if (!t.isEmpty())
                stems.append(t);
        }
    }

    QSet<QString> seen;
    QStringList out;
    for (const QString &stem : stems) {
        const QString primary = tagDir + QLatin1Char('/') + stem + QStringLiteral(".csv");
        QString path = primary;
        if (!QFile::exists(path))
            path = tagsStorageDir() + QLatin1Char('/') + stem + QStringLiteral(".csv");
        const QStringList rowTags = loadTagCsvTags(path);
        for (const QString &tag : rowTags) {
            if (tag.isEmpty() || seen.contains(tag))
                continue;
            seen.insert(tag);
            out.append(tag);
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QStringList listLocalWorkflowJsonFilenames()
{
    QStringList names;
    QDir dir(workflowsStorageDir());
    if (!dir.exists())
        return names;
    const QStringList fl = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
    for (const QString &f : fl)
        names.append(f);
    return names;
}

QString lorasJsonPath()
{
    return pluginDatabaseDir() + QStringLiteral("/loras.json");
}

QJsonArray loadLorasJsonArray()
{
    QFile f(lorasJsonPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QByteArray data = stripJsonLineComments(f.readAll());
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
        return {};
    if (doc.isArray())
        return doc.array();
    if (doc.isObject()) {
        const QJsonArray nested = doc.object().value(QStringLiteral("loras")).toArray();
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}

bool saveLorasJsonArray(const QJsonArray &arr)
{
    QSaveFile f(lorasJsonPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return f.commit();
}

QString mergeLibraryLoraTagsIntoPositivePrompt(const QString &positivePrompt)
{
    const QJsonArray arr = loadLorasJsonArray();
    QStringList tags;
    tags.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("enabled")).toBool(true))
            continue;
        QString fn = o.value(QStringLiteral("filename")).toString().trimmed();
        if (fn.isEmpty())
            fn = o.value(QStringLiteral("name")).toString().trimmed();
        if (fn.isEmpty())
            continue;
        const int pct = o.value(QStringLiteral("strength_percent")).toInt(100);
        if (pct <= 0)
            continue;
        const double w = qBound(0.01, pct / 100.0, 4.0);
        const QString base = QFileInfo(fn).fileName();
        if (base.isEmpty())
            continue;
        tags.append(QStringLiteral("<lora:%1:%2>").arg(base, QString::number(w, 'f', 2)));
    }
    if (tags.isEmpty())
        return positivePrompt;
    const QString suffix = tags.join(QLatin1Char(' '));
    const QString t = positivePrompt.trimmed();
    if (t.isEmpty())
        return suffix;
    return t + QStringLiteral(", ") + suffix;
}

// §13.165: Plugin installation path check — warn if not under expected location (dockers/ or .git)
void checkPluginInstallationPath()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    // Resolve path of the current shared library (plugin .so/.dylib)
    typedef QString (*Symbol)();
    Symbol sym = pluginVersion;
    void *addr = reinterpret_cast<void *>(sym);
    Dl_info info;
    if (dladdr(addr, &info) == 0 || !info.dli_fname)
        return;
    QString libPath = QFile::decodeName(info.dli_fname);
    QFileInfo fi(libPath);
    QDir pluginDir = fi.absoluteDir();
    QString parentName = pluginDir.dirName();
    pluginDir.cdUp();
    QString grandParentName = pluginDir.dirName();
    // Expected: we are in .../dockers/comfyui_remote/ (parent of plugin dir = "dockers") or under a .git tree
    bool underDockers = (parentName == QLatin1String("comfyui_remote") && grandParentName == QLatin1String("dockers"));
    bool underGit = false;
    for (QDir d(fi.absoluteDir().absolutePath()); d.exists(); ) {
        if (QFileInfo(d, QStringLiteral(".git")).exists()) {
            underGit = true;
            break;
        }
        if (!d.cdUp())
            break;
    }
    if (!underDockers && !underGit) {
        qWarning("ComfyUI Remote: Plugin is not installed in a 'dockers' directory, this may break user files and settings. Detected installation path is: %s", qPrintable(fi.absoluteFilePath()));
    }
#endif
}

void migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion()
{
    migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(KSharedConfig::openConfig());
}

void migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(const KSharedConfigPtr &cfg)
{
    if (!cfg) {
        return;
    }
    KConfigGroup mainWin(cfg, QStringLiteral("MainWindow"));
    KConfigGroup legacy = mainWin.group(QStringLiteral("DockWidget ComfyUIRemote"));
    if (legacy.keyList().isEmpty())
        return;
    KConfigGroup current = mainWin.group(QStringLiteral("DockWidget imageDiffusion"));
    if (current.hasKey(QStringLiteral("DockArea")))
        return;
    if (legacy.hasKey(QStringLiteral("Locked")))
        current.writeEntry(QStringLiteral("Locked"), legacy.readEntry(QStringLiteral("Locked"), false));
    if (legacy.hasKey(QStringLiteral("DockArea")))
        current.writeEntry(QStringLiteral("DockArea"), legacy.readEntry(QStringLiteral("DockArea"), 0));
    if (legacy.hasKey(QStringLiteral("xPosition")))
        current.writeEntry(QStringLiteral("xPosition"), legacy.readEntry(QStringLiteral("xPosition"), 0));
    if (legacy.hasKey(QStringLiteral("yPosition")))
        current.writeEntry(QStringLiteral("yPosition"), legacy.readEntry(QStringLiteral("yPosition"), 0));
    if (legacy.hasKey(QStringLiteral("width")))
        current.writeEntry(QStringLiteral("width"), legacy.readEntry(QStringLiteral("width"), 0));
    if (legacy.hasKey(QStringLiteral("height")))
        current.writeEntry(QStringLiteral("height"), legacy.readEntry(QStringLiteral("height"), 0));
    legacy.deleteGroup();
    cfg->sync();
}

// §3.1: Settings path → user_data_dir/settings.json; load/save with // line comments
QString settingsFilePath()
{
    return pluginUserDataDir() + QStringLiteral("/settings.json");
}

QJsonObject loadSettingsJson()
{
    QFile f(settingsFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject();
    QByteArray data = stripJsonLineComments(f.readAll());
    f.close();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

bool saveSettingsJson(const QJsonObject &obj)
{
    QSaveFile f(settingsFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return f.commit();
}

void dumpComfyPromptPayloadIfEnabled(const QJsonObject &payload)
{
    if (!loadSettingsJson().value(QStringLiteral("dump_workflow")).toBool(false))
        return;
    const QString dir = pluginLogDir();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/last_comfy_prompt.json");
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Indented);
    // §4.8: When multi-threading is on, avoid blocking the UI thread on very large prompt dumps.
    constexpr int kBackgroundDumpMinBytes = 512 * 1024;
    if (multiThreadingEnabled() && json.size() >= kBackgroundDumpMinBytes) {
        std::thread([path, json]() {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(json);
        }).detach();
        return;
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(json);
}

QString formatComfySystemStatsDeviceLine(const QJsonObject &root)
{
    const QJsonArray devs = root.value(QStringLiteral("devices")).toArray();
    if (devs.isEmpty())
        return i18n("Device: (no GPU info in server response)");

    QStringList parts;
    for (const QJsonValue &v : devs) {
        const QJsonObject d = v.toObject();
        const QString name = d.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        const QString type = d.value(QStringLiteral("type")).toString();
        const double vram = d.value(QStringLiteral("vram_total")).toDouble();
        int gb = 0;
        if (vram > 0.0)
            gb = static_cast<int>(vram / (1024.0 * 1024.0 * 1024.0) + 0.5);
        const QString t = type.isEmpty() ? QStringLiteral("Device") : type.toUpper();
        if (gb > 0)
            parts.append(QStringLiteral("%1 %2 (%3 GB)").arg(t, name, QString::number(gb)));
        else
            parts.append(QStringLiteral("%1 %2").arg(t, name));
    }
    if (parts.isEmpty())
        return i18n("Device: (unparsed system_stats)");
    return i18n("Device: %1", parts.join(QLatin1String(" · ")));
}

QString inferAutoPerformancePresetKey(const QJsonObject &root)
{
    const QJsonArray devs = root.value(QStringLiteral("devices")).toArray();
    if (devs.isEmpty())
        return QStringLiteral("medium");
    const QJsonObject d0 = devs.at(0).toObject();
    const QString type = d0.value(QStringLiteral("type")).toString().toLower();
    if (type == QLatin1String("cpu"))
        return QStringLiteral("cpu");
    if (type == QLatin1String("cloud"))
        return QStringLiteral("cloud");
    const double vram = d0.value(QStringLiteral("vram_total")).toDouble();
    int gb = 0;
    if (vram > 0.0)
        gb = static_cast<int>(vram / (1024.0 * 1024.0 * 1024.0) + 0.5);
    if (gb > 0 && gb <= 6)
        return QStringLiteral("low");
    if (gb > 0 && gb <= 12)
        return QStringLiteral("medium");
    if (gb > 0)
        return QStringLiteral("high");
    return QStringLiteral("medium");
}

static void performancePresetTableBatchAndResolution(const QString &tier, int *batch, double *resMul)
{
    if (tier == QLatin1String("cpu")) {
        *batch = 1;
    } else if (tier == QLatin1String("low")) {
        *batch = 2;
    } else if (tier == QLatin1String("medium")) {
        *batch = 4;
    } else if (tier == QLatin1String("high")) {
        *batch = 6;
    } else if (tier == QLatin1String("cloud")) {
        *batch = 8;
    } else {
        *batch = 4;
    }
    *resMul = 1.0;
}

void generationPerformanceBatchResolution(const QJsonObject &settingsJson,
                                          const QJsonObject &systemStatsRoot,
                                          int dockBatch,
                                          double dockResolutionMultiplier,
                                          int *outBatch,
                                          double *outResolutionMultiplier)
{
    if (!outBatch || !outResolutionMultiplier)
        return;
    QString preset = settingsJson.value(QStringLiteral("performance_preset")).toString();
    if (preset.isEmpty())
        preset = QStringLiteral("auto");
    if (preset == QLatin1String("custom")) {
        *outBatch = qBound(1, dockBatch, 256);
        *outResolutionMultiplier = dockResolutionMultiplier <= 0.0 ? 1.0 : dockResolutionMultiplier;
        return;
    }
    QString tier = preset;
    if (preset == QLatin1String("auto"))
        tier = inferAutoPerformancePresetKey(systemStatsRoot);
    int b = 4;
    double r = 1.0;
    performancePresetTableBatchAndResolution(tier, &b, &r);
    *outBatch = b;
    *outResolutionMultiplier = r;
}

QString normalizeDiffusionScaleMode(const QString &rawFromSettings)
{
    QString k = rawFromSettings.trimmed().toLower();
    if (k == QLatin1String("none") || k == QLatin1String("resize") || k == QLatin1String("upscale_small")
        || k == QLatin1String("upscale_fast") || k == QLatin1String("upscale_quality")) {
        return k;
    }
    return QStringLiteral("resize");
}

void adjustEffectiveResolutionMultiplierForDiffusionScaleMode(const QJsonObject &settingsRoot, double *resolutionMultiplier)
{
    if (!resolutionMultiplier)
        return;
    const QString mode = normalizeDiffusionScaleMode(settingsRoot.value(QStringLiteral("diffusion_scale_mode")).toString());
    if (mode == QLatin1String("none")) {
        *resolutionMultiplier = 1.0;
        return;
    }
    // §13.23: upscale_small applies when scale factor is under ~1.5× (bilinear / light upscale path).
    if (mode == QLatin1String("upscale_small") && *resolutionMultiplier > 1.5)
        *resolutionMultiplier = 1.5;
}

QString comfyImageScaleMethodForDiffusionScaleMode(const QString &normalizedScaleMode)
{
    const QString m = normalizeDiffusionScaleMode(normalizedScaleMode);
    if (m == QLatin1String("upscale_quality"))
        return QStringLiteral("lanczos");
    if (m == QLatin1String("upscale_fast"))
        return QStringLiteral("bicubic");
    return QStringLiteral("bilinear");
}

void DiffusionTileLayout::initGridFromExtent()
{
    const int width = imageExtent.width();
    const int height = imageExtent.height();
    if (width <= 0 || height <= 0 || tileExtent <= 1) {
        gridW = gridH = tileCount = 0;
        step = 1;
        return;
    }
    int overlap = padding;
    if (overlap < 0)
        overlap = qBound(8, tileExtent / 8, 128);
    overlap = qBound(0, overlap, tileExtent - 1);
    step = qMax(1, tileExtent - overlap);
    auto count1d = [this](int dim) {
        if (dim <= tileExtent)
            return 1;
        return 1 + (dim - tileExtent + step - 1) / step;
    };
    gridW = count1d(width);
    gridH = count1d(height);
    tileCount = gridW * gridH;
}

DiffusionTileLayout DiffusionTileLayout::fromUniformGrid(int width, int height, int tileExtentPx, int overlapPx,
                                                         int minTileSize, int blendPx)
{
    DiffusionTileLayout L;
    L.imageExtent = QSize(width, height);
    L.tileExtent = tileExtentPx;
    L.minSize = minTileSize;
    L.padding = overlapPx;
    L.blending = blendPx;
    L.initGridFromExtent();
    return L;
}

DiffusionTileLayout DiffusionTileLayout::fromDenoiseStrength(QSize extent, int minTileSize, double strength0to1,
                                                           int multiple, int overlapPx)
{
    const int w = extent.width();
    const int h = extent.height();
    double t = qBound(0.0, strength0to1, 1.0);
    // Higher denoise → smaller tiles (more VRAM-friendly splits); lower → larger tiles.
    int te = static_cast<int>(std::lround(static_cast<double>(minTileSize) * (1.0 + 3.0 * (1.0 - t))));
    const int maxDim = qMax(w, h);
    if (maxDim > 0)
        te = qMin(te, maxDim);
    te = qMax(minTileSize, te);
    if (multiple > 1)
        te = ((te + multiple - 1) / multiple) * multiple;
    if (maxDim > 0)
        te = qMin(te, maxDim);
    te = qMax(minTileSize, te);
    return fromUniformGrid(w, h, te, overlapPx, minTileSize, 0);
}

QPoint DiffusionTileLayout::coord(int index) const
{
    if (gridW <= 0 || index < 0 || index >= tileCount)
        return QPoint(-1, -1);
    return QPoint(index % gridW, index / gridW);
}

int DiffusionTileLayout::tileIndex(QPoint tileCoord) const
{
    if (tileCoord.x() < 0 || tileCoord.y() < 0 || tileCoord.x() >= gridW || tileCoord.y() >= gridH)
        return -1;
    return tileCoord.y() * gridW + tileCoord.x();
}

QRect DiffusionTileLayout::boundsAtTileCoord(QPoint c) const
{
    const int w = imageExtent.width();
    const int h = imageExtent.height();
    if (c.x() < 0 || c.y() < 0)
        return QRect();
    const int x0 = c.x() * step;
    const int y0 = c.y() * step;
    const int x1 = qMin(x0 + tileExtent, w);
    const int y1 = qMin(y0 + tileExtent, h);
    return QRect(x0, y0, qMax(0, x1 - x0), qMax(0, y1 - y0));
}

QRect DiffusionTileLayout::bounds(int index) const
{
    const QPoint c = coord(index);
    if (c.x() < 0)
        return QRect();
    return boundsAtTileCoord(c);
}

QPoint DiffusionTileLayout::start(QPoint tileCoord) const
{
    if (tileCoord.x() < 0 || tileCoord.y() < 0)
        return QPoint(-1, -1);
    return QPoint(tileCoord.x() * step, tileCoord.y() * step);
}

QPoint DiffusionTileLayout::end(QPoint tileCoord) const
{
    const QRect r = boundsAtTileCoord(tileCoord);
    if (r.isEmpty())
        return QPoint(-1, -1);
    return QPoint(r.right(), r.bottom());
}

int estimateUniformTileGridCount2D(int width, int height, int tileExtent, int overlapPx)
{
    return DiffusionTileLayout::fromUniformGrid(width, height, tileExtent, overlapPx).totalTiles();
}

int diffusionUpscaleTileEstimateExtentPx(const QJsonObject &settingsRoot)
{
    const int v = settingsRoot.value(QStringLiteral("upscale_tile_estimate_extent")).toInt(512);
    return qBound(256, v, 2048);
}

void clampExtentToMaxMegapixels(int *width, int *height)
{
    if (!width || !height) return;
    int w = *width;
    int h = *height;
    if (w <= 0 || h <= 0) return;

    QJsonObject s = loadSettingsJson();
    if (s.value(QStringLiteral("max_pixel_auto")).toBool(true))
        return;
    const int maxMp = s.value(QStringLiteral("max_pixel_count_mp")).toInt(8);
    if (maxMp <= 0) return;

    const qint64 maxArea = static_cast<qint64>(maxMp) * 1000000LL;
    qint64 area = static_cast<qint64>(w) * static_cast<qint64>(h);
    if (area <= maxArea) return;

    double scale = std::sqrt(static_cast<double>(maxArea) / static_cast<double>(area));
    w = qMax(64, static_cast<int>(w * scale));
    h = qMax(64, static_cast<int>(h * scale));
    area = static_cast<qint64>(w) * h;
    int guard = 0;
    while (area > maxArea && (w > 64 || h > 64) && guard++ < 16384) {
        if (w >= h)
            w = qMax(64, w - 1);
        else
            h = qMax(64, h - 1);
        area = static_cast<qint64>(w) * h;
    }
    *width = w;
    *height = h;
}

// §13.45 / §13.193: Frame paths for Live (.live-frames/frame-N.webp) and Animation (.animation/frame-N.png)
QString liveFramesDirectory(const QString &documentPath)
{
    if (documentPath.isEmpty()) return QString();
    QFileInfo info(documentPath);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".live-frames");
}

QString animationFramesDirectory(const QString &documentPath)
{
    if (documentPath.isEmpty()) return QString();
    QFileInfo info(documentPath);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".animation");
}

QString liveFramePath(const QString &documentPath, int frameIndex)
{
    return liveFramesDirectory(documentPath) + QStringLiteral("/frame-%1.webp").arg(frameIndex);
}

QString animationFramePath(const QString &documentPath, int frameIndex)
{
    return animationFramesDirectory(documentPath) + QStringLiteral("/frame-%1.png").arg(frameIndex);
}

bool filesContentsEqual(const QString &pathA, const QString &pathB)
{
    if (pathA.isEmpty() || pathB.isEmpty())
        return false;
    if (pathA == pathB)
        return true;
    QFileInfo ia(pathA), ib(pathB);
    if (!ia.exists() || !ib.exists() || ia.size() != ib.size())
        return false;
    QFile fa(pathA), fb(pathB);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
        return false;
    constexpr qint64 kChunk = 1 << 20;
    while (!fa.atEnd()) {
        const QByteArray ba = fa.read(kChunk);
        const QByteArray bb = fb.read(kChunk);
        if (ba != bb)
            return false;
    }
    return true;
}

// §13.35: strip_prompt_comments — text after '#' removed unless escaped as '\#'
QString stripPromptComments(QString text)
{
    const QChar hash = QLatin1Char('#');
    const QChar backslash = QLatin1Char('\\');
    QStringList lines = text.split(QLatin1Char('\n'));
    QStringList result;
    for (const QString &line : lines) {
        QString out;
        for (int i = 0; i < line.size(); ++i) {
            if (line[i] == backslash && i + 1 < line.size() && line[i + 1] == hash) {
                out.append(line.mid(i, 2));
                ++i;
                continue;
            }
            if (line[i] == hash) {
                break;
            }
            out.append(line[i]);
        }
        result.append(out);
    }
    return result.join(QLatin1Char('\n'));
}

QString sanitizePrompt(const QString &prompt)
{
    if (prompt.trimmed().isEmpty())
        return QStringLiteral("no prompt");
    QString s = prompt.left(40);
    QString out;
    for (const QChar &c : s) {
        if (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('_') || c == QLatin1Char('-'))
            out.append(c);
    }
    return out.trimmed().isEmpty() ? QStringLiteral("no prompt") : out.trimmed();
}

QString kritaIconNameForThemeStem(const QString &stem)
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> h;
        static const struct {
            const char *stem;
            const char *kritaIcon;
        } rows[] = {
            {"workspace-generation", "tools-wizard"},
            {"workspace-upscaling", "view-zoom"},
            {"workspace-live", "view-refresh"},
            {"workspace-animation", "video-x-generic"},
            {"workspace-custom", "project-development-open"},
            {"apply", "dialog-ok"},
            {"apply-layer", "document-edit"},
            {"cancel", "dialog-cancel"},
            {"generate", "tools-wizard"},
            {"refine", "transform-scale"},
            {"refine-region", "transform-crop"},
            {"random", "random"},
            {"seed", "random"},
            {"settings", "configure"},
            {"save", "document-save"},
            {"discard", "edit-delete"},
            {"upload", "upload"},
            {"import", "document-import"},
            {"reset", "view-refresh"},
            {"remove", "list-remove"},
            {"filter", "view-filter"},
            {"more", "overflow-menu"},
            {"queue-active", "run-build"},
            {"queue-inactive", "dialog-ok"},
            {"queue-upload", "network-transmit-receive"},
            {"queue-waiting", "chronometer"},
            {"play", "media-playback-start"},
            {"pause", "media-playback-pause"},
            {"record", "media-record"},
            {"record-active", "media-record"},
            {"region-add", "list-add"},
            {"region-prompt", "insert-text"},
            {"region-alpha", "draw-freehand"},
            {"region-alpha-active", "format-stroke-color"},
            {"root", "folder"},
            {"context", "edit-paste"},
            {"context-automatic", "system-run"},
            {"context-mask", "path-mask-edit"},
            {"context-layer", "layer-visible-on"},
            {"context-image", "image-x-generic"},
            {"fill", "fill-color"},
            {"fill-empty", "draw-eraser"},
            {"inpaint-automatic", "tools-wizard"},
            {"inpaint-fill", "fill-color"},
            {"inpaint-expand", "transform-scale"},
            {"inpaint-add_object", "list-add"},
            {"inpaint-remove_object", "list-remove"},
            {"inpaint-replace_background", "view-preview"},
            {"inpaint-custom", "preferences-desktop-color"},
            {"control-add", "list-add"},
            {"control-generate", "tools-wizard"},
            {"add-pose", "edit-image"},
            {"control-reference", "link"},
            {"control-style", "color-picker-black"},
            {"control-composition", "view-grid"},
            {"control-face", "im-user"},
            {"control-inpaint", "draw-brush"},
            {"control-universal", "applications-graphics"},
            {"control-scribble", "draw-freehand"},
            {"control-line_art", "draw-line"},
            {"control-soft_edge", "blur"},
            {"control-canny_edge", "path-shape"},
            {"control-depth", "view-media-visualization"},
            {"control-normal", "map-flat"},
            {"control-pose", "edit-image"},
            {"control-segmentation", "select-rectangular"},
            {"control-hands", "preferences-desktop-peripherals"},
            {"control-blur", "blur"},
            {"control-stencil", "draw-brush"},
            {"link", "link"},
            {"link-active", "link"},
            {"link-off", "link-off"},
            {"link-disabled", "link-off"},
            {"warning", "dialog-warning"},
            {"alert", "dialog-warning"},
            {"interstice", "internet-web-browser"},
            {"resolution-multiplier", "zoom-original"},
            {"file-json", "text-x-ldif"},
            {"file-kra", "application-x-krita"},
            {"web-connection", "network-connect"},
            {"comfyui", "applications-graphics"},
            {"star", "rating"},
            {"logo-128", "view-preview"},
            {"sd-version-15", "applications-graphics"},
            {"sd-version-xl", "applications-graphics"},
            {"sd-version-3", "applications-graphics"},
            {"sd-version-flux", "applications-graphics"},
            {"sd-version-flux-k", "applications-graphics"},
            {"sd-version-flux-2", "applications-graphics"},
            {"sd-version-illu", "applications-graphics"},
            {"sd-version-illu-v", "applications-graphics"},
            {"sd-version-chroma", "applications-graphics"},
            {"sd-version-qwen", "applications-graphics"},
            {"sd-version-z-image", "applications-graphics"},
        };
        for (const auto &r : rows) {
            h.insert(QString::fromLatin1(r.stem), QString::fromLatin1(r.kritaIcon));
        }
        return h;
    }();
    const QString v = map.value(stem);
    return v.isEmpty() ? QStringLiteral("applications-graphics") : v;
}

QString formatSaveImageFileName(const QString &templateStr, const QString &documentName, const QString &jobTimestamp,
                                int jobIndex1Based, const QString &promptTrimmed)
{
    QString t = templateStr.trimmed();
    if (t.isEmpty())
        t = QStringLiteral("{document_name}-generated-{job_timestamp}-{job_index}-{prompt}");

    auto sanitizeDocName = [](const QString &s) {
        QString o;
        for (const QChar &c : s) {
            if (c != QLatin1Char('/') && c != QLatin1Char('\\') && c != QLatin1Char(':') && c != QLatin1Char('*')
                && c != QLatin1Char('?') && c != QLatin1Char('"') && c != QLatin1Char('<') && c != QLatin1Char('>')
                && c != QLatin1Char('|'))
                o.append(c);
        }
        const QString tr = o.trimmed();
        return tr.isEmpty() ? QStringLiteral("image") : tr;
    };

    const QString promptSeg = sanitizePrompt(promptTrimmed);
    QString out = t;
    out.replace(QStringLiteral("{document_name}"), sanitizeDocName(documentName));
    out.replace(QStringLiteral("{job_timestamp}"), jobTimestamp);
    out.replace(QStringLiteral("{job_index}"), QString::number(jobIndex1Based));
    out.replace(QStringLiteral("{prompt}"), promptSeg);

    QString final;
    for (const QChar &c : out) {
        if (c.unicode() < 32 || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
            || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"') || c == QLatin1Char('<')
            || c == QLatin1Char('>') || c == QLatin1Char('|'))
            final.append(QLatin1Char('_'));
        else
            final.append(c);
    }
    final = final.trimmed();
    return final.isEmpty() ? QStringLiteral("generated") : final;
}

int saveImageQualityJpeg(const QJsonObject &settings)
{
    if (settings.contains(QStringLiteral("save_image_quality_jpeg")))
        return qBound(0, settings.value(QStringLiteral("save_image_quality_jpeg")).toInt(85), 100);
    if (settings.contains(QStringLiteral("save_image_jpeg_quality")))
        return qBound(0, settings.value(QStringLiteral("save_image_jpeg_quality")).toInt(90), 100);
    return 85;
}

int saveImageQualityWebp(const QJsonObject &settings)
{
    return qBound(0, settings.value(QStringLiteral("save_image_quality_webp")).toInt(80), 100);
}

// §13.201: Smallest parenthesis or angle block containing cursor; else (-1, 0)
static std::pair<int, int> selectParenthesisBlock(const QString &text, int cursorPos,
    const QChar &openCh, const QChar &closeCh)
{
    if (cursorPos < 0 || cursorPos > text.size()) return {-1, 0};
    int start = -1;
    int depth = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == openCh) {
            if (depth == 0) start = i;
            ++depth;
        } else if (text[i] == closeCh) {
            --depth;
            if (depth == 0 && start >= 0 && cursorPos >= start && cursorPos <= i + 1)
                return {start, i - start + 1};
        }
    }
    return {-1, 0};
}

// §13.201: Word (alphanumeric/underscore) containing cursor
static std::pair<int, int> selectCurrentWord(const QString &text, int cursorPos)
{
    if (cursorPos < 0 || cursorPos > text.size()) return {-1, 0};
    int start = cursorPos;
    while (start > 0) {
        QChar c = text[start - 1];
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            --start;
        else
            break;
    }
    int end = cursorPos;
    while (end < text.size()) {
        QChar c = text[end];
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            ++end;
        else
            break;
    }
    if (start >= end) return {-1, 0};
    return {start, end - start};
}

std::pair<int, int> attentionSegmentRange(const QString &text, int cursorPos)
{
    if (text.isEmpty() || cursorPos < 0) return {-1, 0};
    cursorPos = qBound(0, cursorPos, text.size());
    // §8.5 / §13.35 / §13.201: bracket pairs (), <>, [], {} — same order as inner checks in reference parse path
    auto pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('('), QLatin1Char(')'));
    if (pr.first >= 0) return pr;
    pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('<'), QLatin1Char('>'));
    if (pr.first >= 0) return pr;
    pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('['), QLatin1Char(']'));
    if (pr.first >= 0) return pr;
    pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('{'), QLatin1Char('}'));
    if (pr.first >= 0) return pr;
    return selectCurrentWord(text, cursorPos);
}

// §8.5 / §13.35: Parse (word:weight), <…>, […], {…}; adjust weight by delta; clamp [−2.0, 2.0]
QString editAttentionWeight(const QString &segment, double delta)
{
    if (segment.isEmpty()) return segment;
    QChar openCh = segment[0];
    QChar closeCh;
    if (openCh == QLatin1Char('('))
        closeCh = QLatin1Char(')');
    else if (openCh == QLatin1Char('<'))
        closeCh = QLatin1Char('>');
    else if (openCh == QLatin1Char('['))
        closeCh = QLatin1Char(']');
    else if (openCh == QLatin1Char('{'))
        closeCh = QLatin1Char('}');
    else
        return segment;
    int closeIdx = segment.indexOf(closeCh, 1);
    if (closeIdx < 0) return segment;
    QString inner = segment.mid(1, closeIdx - 1).trimmed();
    double weight = 1.0;
    int colonIdx = inner.indexOf(QLatin1Char(':'));
    if (colonIdx >= 0) {
        bool ok = false;
        weight = inner.mid(colonIdx + 1).trimmed().toDouble(&ok);
        if (!ok) weight = 1.0;
        inner = inner.left(colonIdx).trimmed();
    }
    weight = qBound(-2.0, weight + delta, 2.0);
    QString out;
    out.append(openCh);
    if (!inner.isEmpty()) out.append(inner);
    if (qAbs(weight - 1.0) > 1e-6) {
        out.append(QLatin1Char(':'));
        out.append(QString::number(weight, 'f', 1));
    }
    out.append(closeCh);
    return out;
}

// §13.135: Strip whole lines whose stripped form starts with "//" (for settings/workflow JSON; does not handle #)
QByteArray stripJsonLineComments(QByteArray data)
{
    QList<QByteArray> out;
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.trimmed().startsWith("//"))
            out.append(line);
    }
    return out.join('\n');
}

namespace
{
struct LinkEntry {
    int id = -1;
    int fromNode = -1;
    int fromSlot = -1;
    int toNode = -1;
    int toSlot = -1;
};

int jsonIntFlexible(const QJsonValue &v)
{
    if (v.isDouble())
        return static_cast<int>(v.toDouble());
    if (v.isString())
        return v.toString().toInt();
    return v.toInt();
}

bool isWidgetTypeTag(const QString &t)
{
    return t == QStringLiteral("INT") || t == QStringLiteral("FLOAT") || t == QStringLiteral("BOOLEAN")
        || t == QStringLiteral("BOOL") || t == QStringLiteral("STRING") || t == QStringLiteral("COMBO")
        || t == QStringLiteral("LIST");
}

bool isConnectionInputSpec(const QJsonValue &specVal)
{
    if (!specVal.isArray())
        return false;
    const QJsonArray a = specVal.toArray();
    if (a.isEmpty())
        return false;
    return !isWidgetTypeTag(a.at(0).toString());
}

bool uiInputHasLink(const QJsonObject &inObj)
{
    if (!inObj.contains(QStringLiteral("link")))
        return false;
    const QJsonValue v = inObj.value(QStringLiteral("link"));
    if (v.isNull() || v.isUndefined())
        return false;
    if (v.isBool())
        return false;
    if (v.isDouble())
        return v.toDouble() != 0.0;
    if (v.isString()) {
        const QString s = v.toString().trimmed();
        return !s.isEmpty() && s != QStringLiteral("null");
    }
    return true;
}

QJsonValue coerceWidgetForSpec(const QJsonValue &w, const QJsonValue &specVal)
{
    if (!specVal.isArray())
        return w;
    const QJsonArray a = specVal.toArray();
    if (a.isEmpty())
        return w;
    const QString t = a.at(0).toString();
    if (t == QStringLiteral("INT")) {
        if (w.isDouble())
            return static_cast<int>(w.toDouble());
        if (w.isString())
            return w.toString().toInt();
    }
    return w;
}

QJsonValue resolveOutputToApi(const QHash<int, QJsonObject> &nodesById,
                              const QHash<int, LinkEntry> &linksById,
                              int fromNode,
                              int fromSlot,
                              int depth)
{
    if (depth > 64)
        return QJsonValue();
    const QJsonObject srcNode = nodesById.value(fromNode);
    if (srcNode.isEmpty())
        return QJsonValue();
    const QString stype = srcNode.value(QStringLiteral("type")).toString();
    if (stype == QStringLiteral("Reroute")) {
        for (auto it = linksById.constBegin(); it != linksById.constEnd(); ++it) {
            const LinkEntry &e = it.value();
            if (e.toNode != fromNode)
                continue;
            const QJsonValue r = resolveOutputToApi(nodesById, linksById, e.fromNode, e.fromSlot, depth + 1);
            if (!r.isNull() && !r.isUndefined())
                return r;
        }
        return QJsonValue();
    }
    if (stype == QStringLiteral("PrimitiveNode") || stype.startsWith(QStringLiteral("Primitive"))) {
        const QJsonArray wv = srcNode.value(QStringLiteral("widgets_values")).toArray();
        if (!wv.isEmpty())
            return wv.at(0);
        return QJsonValue();
    }
    QJsonArray ref;
    ref.append(QString::number(fromNode));
    ref.append(fromSlot);
    return ref;
}

bool parseLinksArray(const QJsonArray &linksArr, QHash<int, LinkEntry> *out)
{
    for (const QJsonValue &lv : linksArr) {
        if (lv.isArray()) {
            const QJsonArray a = lv.toArray();
            if (a.size() >= 6) {
                LinkEntry e;
                e.id = jsonIntFlexible(a.at(0));
                e.fromNode = jsonIntFlexible(a.at(1));
                e.fromSlot = jsonIntFlexible(a.at(2));
                e.toNode = jsonIntFlexible(a.at(3));
                e.toSlot = jsonIntFlexible(a.at(4));
                if (e.id >= 0)
                    out->insert(e.id, e);
            } else if (a.size() >= 3) {
                // §13.101: [link_id, source_node_id, source_output_slot] — target inferred from node inputs
                LinkEntry e;
                e.id = jsonIntFlexible(a.at(0));
                e.fromNode = jsonIntFlexible(a.at(1));
                e.fromSlot = jsonIntFlexible(a.at(2));
                e.toNode = -1;
                e.toSlot = -1;
                if (e.id >= 0)
                    out->insert(e.id, e);
            }
        } else if (lv.isObject()) {
            const QJsonObject o = lv.toObject();
            LinkEntry e;
            e.id = jsonIntFlexible(o.value(QStringLiteral("id")));
            e.fromNode = jsonIntFlexible(o.value(QStringLiteral("origin_id")));
            e.fromSlot = jsonIntFlexible(o.value(QStringLiteral("origin_slot")));
            e.toNode = jsonIntFlexible(o.value(QStringLiteral("target_id")));
            e.toSlot = jsonIntFlexible(o.value(QStringLiteral("target_slot")));
            if (e.id >= 0)
                out->insert(e.id, e);
        }
    }
    return true;
}

void enrichLinkTargetsFromNodeInputs(const QHash<int, QJsonObject> &nodesById, QHash<int, LinkEntry> *linksById)
{
    for (auto nit = nodesById.constBegin(); nit != nodesById.constEnd(); ++nit) {
        const int nodeId = nit.key();
        const QJsonArray uiInputs = nit.value().value(QStringLiteral("inputs")).toArray();
        for (int i = 0; i < uiInputs.size(); ++i) {
            const QJsonObject inObj = uiInputs.at(i).toObject();
            if (!uiInputHasLink(inObj))
                continue;
            const int linkId = jsonIntFlexible(inObj.value(QStringLiteral("link")));
            if (!linksById->contains(linkId))
                continue;
            LinkEntry e = linksById->value(linkId);
            if (e.toNode >= 0)
                continue;
            e.toNode = nodeId;
            e.toSlot = i;
            linksById->insert(linkId, e);
        }
    }
}

bool shouldSkipUiNodeForApi(const QString &type)
{
    return type == QStringLiteral("Note") || type == QStringLiteral("MarkdownNote") || type == QStringLiteral("Reroute")
        || type == QStringLiteral("PrimitiveNode") || type.startsWith(QStringLiteral("Primitive"));
}

} // namespace

QPair<bool, QString> convertComfyUiWorkflowUiToApi(const QJsonObject &uiWorkflow,
                                                 const QJsonObject &objectInfoRoot,
                                                 QJsonObject *outApi)
{
    if (!outApi)
        return qMakePair(false, QString());
    outApi->clear();
    if (objectInfoRoot.isEmpty())
        return qMakePair(false,
                         i18n("UI workflow conversion needs ComfyUI node definitions (connect and refresh object_info)."));

    const QJsonArray nodesArr = uiWorkflow.value(QStringLiteral("nodes")).toArray();
    const QJsonArray linksArr = uiWorkflow.value(QStringLiteral("links")).toArray();
    if (nodesArr.isEmpty())
        return qMakePair(false, i18n("UI workflow has no nodes."));

    QHash<int, QJsonObject> nodesById;
    for (const QJsonValue &nv : nodesArr) {
        if (!nv.isObject())
            continue;
        const QJsonObject n = nv.toObject();
        const int id = jsonIntFlexible(n.value(QStringLiteral("id")));
        if (id >= 0)
            nodesById.insert(id, n);
    }

    QHash<int, LinkEntry> linksById;
    parseLinksArray(linksArr, &linksById);
    enrichLinkTargetsFromNodeInputs(nodesById, &linksById);

    for (const QJsonValue &nv : nodesArr) {
        if (!nv.isObject())
            continue;
        const QJsonObject uiNode = nv.toObject();
        const int nodeId = jsonIntFlexible(uiNode.value(QStringLiteral("id")));
        const QString classType = uiNode.value(QStringLiteral("type")).toString();
        if (nodeId < 0 || classType.isEmpty() || shouldSkipUiNodeForApi(classType))
            continue;

        const QJsonObject nodeDef = objectInfoRoot.value(classType).toObject();
        const QJsonObject inputWrapper = nodeDef.value(QStringLiteral("input")).toObject();
        const QJsonObject required = inputWrapper.value(QStringLiteral("required")).toObject();
        const QJsonObject optional = inputWrapper.value(QStringLiteral("optional")).toObject();
        if (required.isEmpty() && optional.isEmpty())
            return qMakePair(false,
                             i18n("Node type \"%1\" is not in object_info — connect to the matching ComfyUI server.",
                                  classType));

        const QJsonArray uiInputs = uiNode.value(QStringLiteral("inputs")).toArray();
        const QJsonArray widgetsValues = uiNode.value(QStringLiteral("widgets_values")).toArray();
        int widgetIdx = 0;
        QJsonObject inputs;

        for (int i = 0; i < uiInputs.size(); ++i) {
            const QJsonObject inObj = uiInputs.at(i).toObject();
            const QString name = inObj.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                continue;
            QJsonValue specVal;
            if (required.contains(name))
                specVal = required.value(name);
            else if (optional.contains(name))
                specVal = optional.value(name);
            else
                continue;

            if (uiInputHasLink(inObj)) {
                const int linkId = jsonIntFlexible(inObj.value(QStringLiteral("link")));
                if (!linksById.contains(linkId))
                    return qMakePair(false,
                                     i18n("Unknown link id %1 on node %2, input \"%3\".", linkId, nodeId, name));
                const LinkEntry &le = linksById.value(linkId);
                if (le.toNode >= 0 && le.toNode != nodeId)
                    return qMakePair(false,
                                     i18n("Link %1 does not target node %2 (input \"%3\").", linkId, nodeId, name));
                const QJsonValue resolved =
                    resolveOutputToApi(nodesById, linksById, le.fromNode, le.fromSlot, 0);
                if (resolved.isNull() || resolved.isUndefined())
                    return qMakePair(false,
                                     i18n("Could not resolve link %1 (node %2, input \"%3\").", linkId, nodeId, name));
                inputs.insert(name, resolved);
            } else {
                if (isConnectionInputSpec(specVal)) {
                    // Unconnected optional socket — omit
                    continue;
                }
                if (widgetIdx >= widgetsValues.size())
                    return qMakePair(false,
                                     i18n("Not enough widget values for node %1 (input \"%2\").", nodeId, name));
                const QJsonValue w = widgetsValues.at(widgetIdx++);
                inputs.insert(name, coerceWidgetForSpec(w, specVal));
            }
        }

        // ComfyUI often saves nodes with no `inputs` array — only widgets_values in object_info order
        if (uiInputs.isEmpty() && !widgetsValues.isEmpty()) {
            int wix = 0;
            const auto appendWidgetInputs = [&](const QJsonObject &section) {
                for (auto it = section.begin(); it != section.end(); ++it) {
                    if (wix >= widgetsValues.size())
                        return;
                    const QString key = it.key();
                    if (inputs.contains(key))
                        continue;
                    if (isConnectionInputSpec(it.value()))
                        continue;
                    inputs.insert(key, coerceWidgetForSpec(widgetsValues.at(wix++), it.value()));
                }
            };
            appendWidgetInputs(required);
            appendWidgetInputs(optional);
        }

        QJsonObject apiNode;
        apiNode.insert(QStringLiteral("class_type"), classType);
        apiNode.insert(QStringLiteral("inputs"), inputs);
        outApi->insert(QString::number(nodeId), apiNode);
    }

    if (outApi->isEmpty())
        return qMakePair(false, i18n("No exportable nodes found in UI workflow (after filtering notes/primitives)."));
    return qMakePair(true, QString());
}

bool tryResolveCustomWorkflowJsonToApi(QJsonObject *inOut, const QJsonObject &objectInfoRoot, QString *errorOut)
{
    if (!inOut)
        return false;
    if (!inOut->contains(QStringLiteral("nodes")) || !inOut->contains(QStringLiteral("links")))
        return true;
    const QJsonValue nodesV = inOut->value(QStringLiteral("nodes"));
    const QJsonValue linksV = inOut->value(QStringLiteral("links"));
    if (!nodesV.isArray() || !linksV.isArray())
        return true;
    QJsonObject api;
    const auto r = convertComfyUiWorkflowUiToApi(*inOut, objectInfoRoot, &api);
    if (!r.first) {
        if (errorOut)
            *errorOut = r.second;
        return false;
    }
    *inOut = api;
    return true;
}

void setComfyUIRequestHeaders(QNetworkRequest &req)
{
    req.setRawHeader(QByteArrayLiteral("ngrok-skip-browser-warning"), QByteArrayLiteral("69420"));
}

QUrl comfyResolveApiUrl(const QString &baseUrlTrimmed, const QString &relativeApiPath)
{
    QString base = baseUrlTrimmed.trimmed();
    QString rel = relativeApiPath;
    if (rel.startsWith(QLatin1Char('/')))
        rel = rel.mid(1);
    QUrl url(base);
    if (!url.isValid())
        return url;
    QString path = url.path();
    if (path.isEmpty() || path == QLatin1String("/"))
        url.setPath(QLatin1Char('/') + rel);
    else if (!path.endsWith(QLatin1Char('/')))
        url.setPath(path + QLatin1Char('/') + rel);
    else
        url.setPath(path + rel);
    return url;
}

QUrl comfyWebSocketUrlForClient(const QString &httpUrlTrimmed, const QString &clientId)
{
    QString base = httpUrlTrimmed.trimmed();
    if (base.isEmpty())
        return QUrl();
    if (!base.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        && !base.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        base = QStringLiteral("http://") + base;
    }
    QUrl http(base);
    if (!http.isValid() || http.host().isEmpty())
        return QUrl();
    const bool tls = http.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0;
    QUrl ws;
    ws.setScheme(tls ? QStringLiteral("wss") : QStringLiteral("ws"));
    ws.setHost(http.host());
    if (http.port() > 0)
        ws.setPort(http.port());
    ws.setPath(QStringLiteral("/ws"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("clientId"), clientId);
    ws.setQuery(q);
    return ws;
}

QNetworkReply *tryUploadLoraFileViaEtnApi(QNetworkAccessManager *nam,
                                          const QString &baseUrlTrimmed,
                                          const QString &localFilePath,
                                          QObject *parentForReply)
{
    if (!nam || baseUrlTrimmed.trimmed().isEmpty())
        return nullptr;
    auto *device = new QFile(localFilePath);
    if (!device->open(QIODevice::ReadOnly)) {
        delete device;
        return nullptr;
    }
    const QString baseName = QFileInfo(localFilePath).fileName();
    if (baseName.isEmpty()) {
        delete device;
        return nullptr;
    }
    QUrl root(baseUrlTrimmed.trimmed());
    QString rootPath = root.path();
    if (!rootPath.endsWith(QLatin1Char('/')))
        root.setPath(rootPath + QLatin1Char('/'));
    const QString rel = QStringLiteral("api/etn/upload/loras/")
        + QString::fromUtf8(QUrl::toPercentEncoding(baseName, QByteArray(), QByteArray("/")));
    const QUrl target = root.resolved(QUrl(rel, QUrl::StrictMode));

    QNetworkRequest req(target);
    setComfyUIRequestHeaders(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    QNetworkReply *reply = nam->put(req, device);
    if (parentForReply)
        reply->setParent(parentForReply);
    return reply;
}

// §13.142: Show LCM deprecation message when backend rejects LCM (cloud/managed server)
QString formatServerErrorMessage(const QString &serverError)
{
    if (serverError.toLower().contains(QLatin1String("lcm")))
        return i18n("LCM is no longer supported by the server. Please change the Style's sampling method to 'Realtime - Hyper'.");
    return serverError;
}

// §13.35: eval_wildcards — {option1|option2|...}, deterministic from seed, evaluated during workflow build
QString evalWildcards(QString text, quint32 seed)
{
    QRandomGenerator rng(seed);
    const int maxIterations = 10;
    for (int iter = 0; iter < maxIterations; ++iter) {
        int start = text.indexOf(QLatin1Char('{'));
        if (start < 0) break;
        int depth = 1;
        int end = start + 1;
        for (; end < text.size(); ++end) {
            QChar c = text[end];
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}')) {
                --depth;
                if (depth == 0) break;
            }
        }
        if (end >= text.size()) break;
        QString content = text.mid(start + 1, end - start - 1);
        QStringList options;
        int d = 0;
        int segStart = 0;
        for (int i = 0; i <= content.size(); ++i) {
            if (i == content.size()) {
                options.append(content.mid(segStart).trimmed());
                break;
            }
            QChar c = content[i];
            if (c == QLatin1Char('{')) ++d;
            else if (c == QLatin1Char('}')) --d;
            else if (c == QLatin1Char('|') && d == 0) {
                options.append(content.mid(segStart, i - segStart).trimmed());
                segStart = i + 1;
            }
        }
        if (options.isEmpty()) break;
        int idx = static_cast<int>(rng.bounded(static_cast<quint32>(options.size())));
        QString chosen = options.at(idx);
        text.replace(start, end - start + 1, chosen);
    }
    return text;
}

// §13.35: extract_layers — <layer:name> replaced with "Picture {n}", returns ordered layer names for workflow binding
QStringList extractLayerPlaceholders(QString &prompt)
{
    QStringList layerNames;
    const QString prefix = QStringLiteral("<layer:");
    int idx = 0;
    int n = 1;
    while ((idx = prompt.indexOf(prefix, idx)) >= 0) {
        int nameStart = idx + prefix.size();
        int end = prompt.indexOf(QLatin1Char('>'), nameStart);
        if (end < 0) break;
        QString name = prompt.mid(nameStart, end - nameStart).trimmed();
        if (!name.isEmpty()) {
            layerNames.append(name);
            QString replacement = QStringLiteral("Picture %1").arg(n);
            prompt.replace(idx, end - idx + 1, replacement);
            n++;
            idx += replacement.size();
        } else {
            idx = end + 1;
        }
    }
    return layerNames;
}

const QStringList &comfyUiSpecSection58NodeClassTypes()
{
    static const QStringList list = {
        // ETN_ (comfyui-tooling-nodes) — spec §13.58
        QStringLiteral("ETN_LoadImageCache"),
        QStringLiteral("ETN_SaveImageCache"),
        QStringLiteral("ETN_Translate"),
        QStringLiteral("ETN_ApplyMaskToImage"),
        QStringLiteral("ETN_LoadImageBase64"),
        QStringLiteral("ETN_LoadMaskBase64"),
        QStringLiteral("ETN_InjectImage"),
        QStringLiteral("ETN_InjectMask"),
        QStringLiteral("ETN_ReturnImage"),
        QStringLiteral("ETN_NSFWFilter"),
        QStringLiteral("ETN_BackgroundRegion"),
        QStringLiteral("ETN_DefineRegion"),
        QStringLiteral("ETN_ListRegionMasks"),
        QStringLiteral("ETN_AttentionMask"),
        QStringLiteral("ETN_TileLayout"),
        QStringLiteral("ETN_ExtractImageTile"),
        QStringLiteral("ETN_ExtractMaskTile"),
        QStringLiteral("ETN_MergeImageTile"),
        QStringLiteral("ETN_GenerateTileMask"),
        QStringLiteral("ETN_ReferenceImage"),
        QStringLiteral("ETN_ApplyReferenceImages"),
        QStringLiteral("ETN_KritaCanvas"),
        QStringLiteral("ETN_KritaSelection"),
        QStringLiteral("ETN_Parameter"),
        QStringLiteral("ETN_KritaImageLayer"),
        QStringLiteral("ETN_KritaMaskLayer"),
        QStringLiteral("ETN_KritaStyle"),
        QStringLiteral("ETN_KritaStyleAndPrompt"),
        // INPAINT_ (comfyui-inpaint-nodes)
        QStringLiteral("INPAINT_LoadFooocusInpaint"),
        QStringLiteral("INPAINT_ShrinkMask"),
        QStringLiteral("INPAINT_StabilizeMask"),
        QStringLiteral("INPAINT_ColorMatch"),
        // Control / preprocess (controlnet_aux etc.)
        QStringLiteral("MeshGraphormer-DepthMapPreprocessor"),
        QStringLiteral("PiDiNetPreprocessor"),
        QStringLiteral("ScribblePreprocessor"),
        QStringLiteral("LineArtPreprocessor"),
        QStringLiteral("AnyLineArtPreprocessor_aux"),
        QStringLiteral("CannyEdgePreprocessor"),
        QStringLiteral("DepthAnythingV2Preprocessor"),
        QStringLiteral("BAE-NormalMapPreprocessor"),
        QStringLiteral("OneFormer-COCO-SemSegPreprocessor"),
        QStringLiteral("DWPreprocessor"),
        // Other named in §13.58
        QStringLiteral("GrowMask"),
        QStringLiteral("ImageUpscaleWithModel"),
        // Core nodes emitted by this port’s default API workflows (ComfyUIWorkflows.cpp)
        QStringLiteral("CheckpointLoaderSimple"),
        QStringLiteral("KSampler"),
        QStringLiteral("CLIPTextEncode"),
        QStringLiteral("EmptyLatentImage"),
        QStringLiteral("VAEDecode"),
        QStringLiteral("VAEDecodeTiled"),
        QStringLiteral("VAEEncode"),
        QStringLiteral("VAEEncodeForInpaint"),
        QStringLiteral("LoadImage"),
        QStringLiteral("SaveImage"),
        QStringLiteral("ImageScale"),
    };
    return list;
}

QStringList specSection58NodesPresentInObjectInfo(const QJsonObject &objectInfoRoot)
{
    const QSet<QString> keys(objectInfoRoot.keys().begin(), objectInfoRoot.keys().end());
    QStringList out;
    for (const QString &name : comfyUiSpecSection58NodeClassTypes()) {
        if (keys.contains(name))
            out.append(name);
    }
    return out;
}

namespace {
QVariant jsonDefaultToVariant(const QJsonValue &v, CustomWorkflowParamSlot::Kind kind)
{
    switch (kind) {
    case CustomWorkflowParamSlot::Kind::ParameterBool:
        if (v.isBool())
            return v.toBool();
        if (v.isDouble())
            return v.toInt() != 0;
        if (v.isString()) {
            const QString s = v.toString().trimmed().toLower();
            return (s == QLatin1String("true") || s == QLatin1String("1") || s == QLatin1String("yes"));
        }
        return false;
    case CustomWorkflowParamSlot::Kind::ParameterInt:
        if (v.isDouble())
            return static_cast<int>(v.toDouble());
        if (v.isString())
            return v.toString().toInt();
        return 0;
    case CustomWorkflowParamSlot::Kind::ParameterFloat:
        if (v.isDouble())
            return v.toDouble();
        if (v.isString())
            return v.toString().toDouble();
        return 0.0;
    default:
        if (v.isBool())
            return v.toBool();
        if (v.isDouble())
            return v.toDouble();
        if (v.isString())
            return v.toString();
        return QVariant();
    }
}

} // namespace

QList<CustomWorkflowParamSlot> discoverCustomWorkflowParameterSlots(const QJsonObject &workflowRoot)
{
    QList<CustomWorkflowParamSlot> out;
    for (auto it = workflowRoot.begin(); it != workflowRoot.end(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject node = it.value().toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        CustomWorkflowParamSlot slot;
        slot.nodeId = it.key();
        if (ct == QLatin1String("ETN_Parameter")) {
            const QString ptype = inputs.value(QStringLiteral("type")).toString();
            if (ptype.isEmpty() || ptype == QLatin1String("auto"))
                continue;
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Parameter");
            slot.typeStr = ptype;
            const QJsonValue defV = inputs.value(QStringLiteral("default"));
            const double jmin = inputs.value(QStringLiteral("min")).toDouble(-2147483648.0);
            const double jmax = inputs.value(QStringLiteral("max")).toDouble(2147483647.0);
            slot.minV = jmin;
            slot.maxV = jmax;
            if (ptype == QLatin1String("number (integer)")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterInt;
                slot.defaultValue = jsonDefaultToVariant(defV, slot.kind);
            } else if (ptype == QLatin1String("number")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterFloat;
                slot.defaultValue = jsonDefaultToVariant(defV, slot.kind);
            } else if (ptype == QLatin1String("toggle")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterBool;
                slot.defaultValue = jsonDefaultToVariant(defV, slot.kind);
            } else if (ptype == QLatin1String("text")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterText;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
            } else if (ptype == QLatin1String("prompt (positive)")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterPromptPositive;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
            } else if (ptype == QLatin1String("prompt (negative)")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterPromptNegative;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
            } else if (ptype == QLatin1String("choice")) {
                slot.kind = CustomWorkflowParamSlot::Kind::ParameterChoice;
                slot.defaultValue = defV.isString() ? defV.toString() : defV.toVariant().toString();
                const QJsonArray ch = inputs.value(QStringLiteral("choices")).toArray();
                for (const QJsonValue &cv : ch)
                    slot.choices.append(cv.toString());
            } else {
                slot.kind = CustomWorkflowParamSlot::Kind::Unsupported;
                slot.paramName = inputs.value(QStringLiteral("name")).toString(QStringLiteral("?")) + QLatin1String(": ") + ptype;
            }
            out.append(slot);
        } else if (ct == QLatin1String("ETN_KritaStyle")) {
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Style");
            slot.kind = CustomWorkflowParamSlot::Kind::KritaStyleSampler;
            slot.defaultValue = inputs.value(QStringLiteral("sampler_preset")).toString(QStringLiteral("auto"));
            out.append(slot);
        } else if (ct == QLatin1String("ETN_KritaImageLayer")) {
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Image");
            slot.kind = CustomWorkflowParamSlot::Kind::KritaImageLayer;
            out.append(slot);
        } else if (ct == QLatin1String("ETN_KritaMaskLayer")) {
            slot.paramName = inputs.value(QStringLiteral("name")).toString();
            if (slot.paramName.isEmpty())
                slot.paramName = QStringLiteral("Mask");
            slot.kind = CustomWorkflowParamSlot::Kind::KritaMaskLayer;
            out.append(slot);
        }
    }
    std::sort(out.begin(), out.end(), [](const CustomWorkflowParamSlot &a, const CustomWorkflowParamSlot &b) {
        return a.paramName.localeAwareCompare(b.paramName) < 0;
    });
    return out;
}

QString paintLayerNameByUuid(KisImageSP image, const QString &uuidWithoutBraces)
{
    if (!image || uuidWithoutBraces.isEmpty())
        return QString();
    KisNodeSP root = image->rootLayer();
    if (!root)
        return QString();
    QList<KisNodeSP> nodes;
    nodes.append(root);
    while (!nodes.isEmpty()) {
        KisNodeSP n = nodes.takeFirst();
        if (dynamic_cast<KisPaintLayer *>(n.data())
            && n->uuid().toString(QUuid::WithoutBraces) == uuidWithoutBraces) {
            return n->name();
        }
        for (int i = 0; i < n->childCount(); ++i)
            nodes.append(n->child(i));
    }
    return QString();
}

void applyCustomWorkflowParameterValues(QJsonObject &workflowRoot,
                                        const QMap<QString, QVariant> &valuesByKey,
                                        KisImageSP layerResolutionImage)
{
    if (valuesByKey.isEmpty())
        return;
    const QStringList keys = workflowRoot.keys();
    for (const QString &nodeId : keys) {
        QJsonObject node = workflowRoot.value(nodeId).toObject();
        const QString ct = node.value(QStringLiteral("class_type")).toString();
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        QString pname = inputs.value(QStringLiteral("name")).toString();
        if (ct == QLatin1String("ETN_Parameter")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Parameter");
        } else if (ct == QLatin1String("ETN_KritaStyle")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Style");
        } else if (ct == QLatin1String("ETN_KritaImageLayer")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Image");
        } else if (ct == QLatin1String("ETN_KritaMaskLayer")) {
            if (pname.isEmpty())
                pname = QStringLiteral("Mask");
        }
        if (ct == QLatin1String("ETN_KritaImageLayer") || ct == QLatin1String("ETN_KritaMaskLayer")) {
            if (!layerResolutionImage || !valuesByKey.contains(nodeId))
                continue;
            const QString resolved = paintLayerNameByUuid(layerResolutionImage, valuesByKey.value(nodeId).toString());
            if (!resolved.isEmpty())
                inputs.insert(QStringLiteral("name"), resolved);
            node.insert(QStringLiteral("inputs"), inputs);
            workflowRoot.insert(nodeId, node);
            continue;
        }
        if (!valuesByKey.contains(pname))
            continue;
        const QVariant v = valuesByKey.value(pname);
        if (ct == QLatin1String("ETN_Parameter")) {
            inputs.insert(QStringLiteral("default"), QJsonValue::fromVariant(v));
        } else if (ct == QLatin1String("ETN_KritaStyle")) {
            inputs.insert(QStringLiteral("sampler_preset"), v.toString());
        }
        node.insert(QStringLiteral("inputs"), inputs);
        workflowRoot.insert(nodeId, node);
    }
}

// §13.103: At most one ETN_KritaStyleAndPrompt node in custom workflow
QPair<bool, QString> validateCustomWorkflowStyleAndPromptNodes(const QJsonObject &workflow)
{
    const QString nodeType = QStringLiteral("ETN_KritaStyleAndPrompt");
    int count = 0;
    for (auto it = workflow.begin(); it != workflow.end(); ++it) {
        if (!it.value().isObject()) continue;
        QString ct = it.value().toObject().value(QStringLiteral("class_type")).toString();
        if (ct == nodeType)
            count++;
    }
    if (count > 1)
        return qMakePair(false, i18n("Workflow contains multiple 'Krita Style & Prompt' nodes, but only one is allowed."));
    return qMakePair(true, QString());
}

// §13.154: Entire-document selection → no mask, generation uses full image
bool isSelectionEntireDocument(KisImageSP image, KisViewManager *viewManager)
{
    if (!image || !viewManager) return false;
    KisSelectionSP sel = viewManager->selection();
    if (!sel || !sel->pixelSelection()) return false;
    QRect docBounds = image->bounds();
    if (docBounds.isEmpty()) return false;
    QRect rect = sel->pixelSelection()->selectedExactRect();
    if (rect.x() != 0 || rect.y() != 0
        || rect.width() != docBounds.width() || rect.height() != docBounds.height()) {
        return false;
    }
    KisPaintDeviceSP dev = sel->pixelSelection();
    const int ps = dev->pixelSize();
    if (ps <= 0) return false;
    QVector<quint8> data(rect.width() * rect.height() * ps);
    dev->readBytes(data.data(), rect.x(), rect.y(), rect.width(), rect.height());
    for (int i = 0; i < data.size(); i += ps) {
        if (ps == 1) {
            if (data[i] != 0xff) return false;
        } else {
            for (int c = 0; c < ps; ++c)
                if (data[i + c] != 0xff) return false;
        }
    }
    return true;
}

// §13.206: Classify checkpoint filename for arch (use_inpaint_model by arch: sd15 strength>0.5, sdxl>0.8, flux==1.0)
QString classifyCheckpointArch(const QString &ckptName)
{
    const QString lower = ckptName.trimmed().toLower();
    // Edit models first (flux_k, qwen_e, qwen_e_p, qwen_l)
    if (lower.contains(QLatin1String("flux_k")) || lower.contains(QLatin1String("flux-k")))
        return QStringLiteral("flux_k");
    if (lower.contains(QLatin1String("qwen")))
        return QStringLiteral("qwen_e");
    // flux2_4b: for edit_reference + fill/expand → FillMode.green, use_inpaint_model
    if ((lower.contains(QLatin1String("flux2")) || lower.contains(QLatin1String("4b"))) && lower.contains(QLatin1String("flux")))
        return QStringLiteral("flux2_4b");
    if (lower.contains(QLatin1String("flux")) || lower.contains(QLatin1String("zimage")))
        return QStringLiteral("flux");
    if (lower.contains(QLatin1String("sdxl")) || lower.contains(QLatin1String("xl")) || lower.contains(QLatin1String("turbo")))
        return QStringLiteral("sdxl");
    if (lower.contains(QLatin1String("v1-5")) || lower.contains(QLatin1String("sd1.5")) || lower.contains(QLatin1String("sd15")) || lower.contains(QLatin1String("v1.5")))
        return QStringLiteral("sd15");
    return QStringLiteral("unknown");
}

// §13.206: True when arch is an edit model (flux_k, qwen_e, qwen_e_p, qwen_l)
bool isArchEdit(const QString &ckptName)
{
    const QString arch = classifyCheckpointArch(ckptName);
    return arch == QLatin1String("flux_k") || arch.startsWith(QLatin1String("qwen"));
}

// §13.206: detect_inpaint() — InpaintParams from mode, arch, strength, conditioning
InpaintParams detectInpaintParams(const QString &mode, const QString &arch, double strength0to1,
                                 bool positiveEmpty, bool hasStructuralControl, bool editReference)
{
    InpaintParams p;
    p.isEditMode = (arch == QLatin1String("flux_k") || arch.startsWith(QLatin1String("qwen")));
    if (p.isEditMode) {
        p.fillKind = QStringLiteral("none");
        p.useReference = false;
        p.useConditionMask = false;
        p.useInpaintModel = (strength0to1 >= 1.0);  // edit models: use_inpaint when strength == 1.0
        return p;
    }
    const bool fillOrExpand = (mode == QLatin1String("fill") || mode == QLatin1String("expand"));
    if (editReference && fillOrExpand && arch == QLatin1String("flux2_4b")) {
        p.fillKind = QStringLiteral("green");
        p.useInpaintModel = true;
        p.useReference = false;
        p.useConditionMask = false;
        return p;
    }
    if (editReference) {
        p.fillKind = QStringLiteral("none");
        p.useInpaintModel = false;
        p.useReference = false;
        p.useConditionMask = false;
        return p;
    }
    // Fill by mode (when edit_reference is False)
    if (mode == QLatin1String("fill"))
        p.fillKind = QStringLiteral("blur");
    else if (mode == QLatin1String("expand"))
        p.fillKind = QStringLiteral("border");
    else if (mode == QLatin1String("add_object"))
        p.fillKind = QStringLiteral("neutral");
    else if (mode == QLatin1String("remove_object"))
        p.fillKind = QStringLiteral("inpaint");
    else if (mode == QLatin1String("replace_background"))
        p.fillKind = QStringLiteral("replace");
    else
        p.fillKind = QStringLiteral("blur");
    p.useReference = fillOrExpand && positiveEmpty;
    // use_inpaint_model by arch
    p.useInpaintModel = (arch == QLatin1String("sd15") && strength0to1 > 0.5)
        || (arch == QLatin1String("sdxl") && strength0to1 > 0.8)
        || ((arch == QLatin1String("flux") || arch == QLatin1String("flux2_4b")) && strength0to1 >= 1.0);
    // use_condition_mask (SD1.5 only): add_object, positive non-empty, no structural control
    p.useConditionMask = (arch == QLatin1String("sd15")) && (mode == QLatin1String("add_object"))
        && !positiveEmpty && !hasStructuralControl;
    return p;
}

// §13.43: grow from get_selection_modifiers + calc_selection_pre_process (feather_rel × size + feather_min_px; grow = selection_grow_offset + feather/2)
int calcSelectionPreProcessGrow(int extentWidth, int extentHeight, int areaWidth, int areaHeight, double strength0to1,
                                int selectionFeatherPercent, double selectionMinTransition, int selectionGrowOffset)
{
    if (strength0to1 <= 0.0) return clampInpaintGrowFeather(selectionGrowOffset);
    double diagonal = 0.0;
    if (areaWidth > 0 && areaHeight > 0)
        diagonal = std::sqrt(static_cast<double>(areaWidth) * areaWidth + static_cast<double>(areaHeight) * areaHeight);
    if (diagonal <= 0.0 && extentWidth > 0 && extentHeight > 0)
        diagonal = std::sqrt(static_cast<double>(extentWidth) * extentWidth + static_cast<double>(extentHeight) * extentHeight);
    if (diagonal <= 0.0) return clampInpaintGrowFeather(selectionGrowOffset);
    const double featherRel = (selectionFeatherPercent / 100.0) * strength0to1;
    const int featherMinPx = static_cast<int>(std::round(selectionMinTransition * strength0to1));
    const double featherPx = featherRel * diagonal + featherMinPx;
    const int grow = selectionGrowOffset + static_cast<int>(featherPx / 2.0);
    return clampInpaintGrowFeather(grow);
}

// §4.6 / §3.5: selection_feather stored as 0–25 (percent); default 10
void getSelectionModifierSettings(int *selectionFeatherPercent, double *selectionMinTransition, int *selectionGrowOffset)
{
    QJsonObject s = loadSettingsJson();
    if (selectionFeatherPercent)
        *selectionFeatherPercent = qBound(0, s.value(QStringLiteral("selection_feather")).toInt(10), 25);
    if (selectionMinTransition)
        *selectionMinTransition = qBound(0.0, s.value(QStringLiteral("selection_min_transition")).toDouble(0.0), 100.0);
    if (selectionGrowOffset)
        *selectionGrowOffset = qBound(0, s.value(QStringLiteral("selection_grow_offset")).toInt(0), inpaintGrowFeatherMax);
}

// §13.102: SelectionModifiers.invert and .square
bool getSelectionModifiersInvert()
{
    return loadSettingsJson().value(QStringLiteral("selection_invert")).toBool(false);
}

bool getSelectionModifiersSquare()
{
    return loadSettingsJson().value(QStringLiteral("selection_square")).toBool(false);
}

// §13.102: Force bounds to square (max of w,h), centered and clamped to image
QRect makeRectSquare(const QRect &rect, int extentWidth, int extentHeight)
{
    if (rect.isEmpty() || extentWidth <= 0 || extentHeight <= 0) return rect;
    const int side = qMin(qMax(rect.width(), rect.height()), qMin(extentWidth, extentHeight));
    int x = rect.center().x() - side / 2;
    int y = rect.center().y() - side / 2;
    x = qBound(0, x, qMax(0, extentWidth - side));
    y = qBound(0, y, qMax(0, extentHeight - side));
    return QRect(x, y, side, side);
}

QImage getCanvasAsQImage(KisImageSP image)
{
    if (!image || !image->projection()) return QImage();
    QRect bounds = image->bounds();
    if (bounds.isEmpty()) return QImage();
    const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
    return image->projection()->convertToQImage(profile, bounds,
        KoColorConversionTransformation::internalRenderingIntent(),
        KoColorConversionTransformation::internalConversionFlags());
}

// §13.158: create_mask_from_selection equivalent — use Krita selection API (selection(), pixelSelection(), selectedExactRect())
// §13.102: invertSelection inverts the mask (white↔black) after reading
QImage getMaskAsQImage(KisImageSP image, KisViewManager *viewManager, const QString &maskSource, bool invertSelection)
{
    QRect bounds = image->bounds();
    if (bounds.isEmpty()) return QImage();
    QImage maskImage(bounds.width(), bounds.height(), QImage::Format_Grayscale8);
    maskImage.fill(0);

    if (maskSource == "selection") {
        KisSelectionSP sel = viewManager ? viewManager->selection() : nullptr;
        if (!sel || !sel->pixelSelection()) return QImage();
        QRect rect = sel->selectedExactRect();
        rect &= bounds;
        if (rect.isEmpty()) return QImage();
        KisPaintDeviceSP dev = sel->pixelSelection();
        int ps = dev->pixelSize();
        QVector<quint8> data(rect.width() * rect.height() * ps);
        dev->readBytes(data.data(), rect.x(), rect.y(), rect.width(), rect.height());
        for (int y = 0; y < rect.height(); y++) {
            for (int x = 0; x < rect.width(); x++) {
                int srcIdx = (y * rect.width() + x) * ps;
                quint8 v = ps > 0 ? data.value(srcIdx, 0) : 0;
                if (invertSelection) v = 255 - v;
                maskImage.setPixel(rect.x() + x, rect.y() + y, qRgb(v, v, v));
            }
        }
        return maskImage;
    }

    if (maskSource.startsWith("layer:")) {
        QString layerName = maskSource.mid(6);
        KisNodeSP root = image->rootLayer();
        if (!root) return QImage();
        QList<KisNodeSP> nodes;
        nodes.append(root);
        KisNodeSP foundNode;
        while (!nodes.isEmpty()) {
            KisNodeSP n = nodes.takeFirst();
            if (n->name() == layerName) { foundNode = n; break; }
            for (int i = 0; i < static_cast<int>(n->childCount()); i++) nodes.append(n->at(i));
        }
        // §13.157: Mask-type nodes (transparency / filter masks) are KisMask, not KisLayer — use projection exactBounds, not full-image bounds
        if (foundNode) {
            if (auto *foundMask = dynamic_cast<KisMask *>(foundNode.data())) {
                KisPaintDeviceSP dev = foundMask->projection();
                if (!dev) return QImage();
                QRect rect = dev->exactBounds() & bounds;
                if (rect.isEmpty()) return QImage();
                const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
                QImage rgba = dev->convertToQImage(profile, rect.x(), rect.y(), rect.width(), rect.height(),
                                                   KoColorConversionTransformation::internalRenderingIntent(),
                                                   KoColorConversionTransformation::internalConversionFlags());
                if (rgba.isNull()) return QImage();
                for (int y = 0; y < rgba.height(); y++) {
                    for (int x = 0; x < rgba.width(); x++) {
                        const QRgb px = rgba.pixel(x, y);
                        int v = qAlpha(px);
                        if (v == 0) v = qGray(px);
                        maskImage.setPixel(rect.x() + x, rect.y() + y, qRgb(v, v, v));
                    }
                }
                return maskImage;
            }
        }
        KisLayer *foundLayer = foundNode ? dynamic_cast<KisLayer *>(foundNode.data()) : nullptr;
        if (!foundLayer || !foundLayer->projection()) return QImage();
        const KoColorProfile *profile = image->colorSpace() ? image->colorSpace()->profile() : nullptr;
        QImage rgba = foundLayer->projection()->convertToQImage(profile, bounds,
            KoColorConversionTransformation::internalRenderingIntent(),
            KoColorConversionTransformation::internalConversionFlags());
        if (rgba.isNull() || rgba.size() != maskImage.size()) return QImage();
        for (int y = 0; y < rgba.height(); y++) {
            for (int x = 0; x < rgba.width(); x++) {
                int a = qAlpha(rgba.pixel(x, y));
                maskImage.setPixel(x, y, qRgb(a, a, a));
            }
        }
        return maskImage;
    }
    return QImage();
}

void compositeWithMask(QImage &current, const QImage &result, const QImage &mask)
{
    if (current.size() != result.size() || current.size() != mask.size() || result.format() != QImage::Format_RGB32) return;
    if (current.format() != QImage::Format_ARGB32 && current.format() != QImage::Format_RGB32)
        current = current.convertToFormat(QImage::Format_ARGB32);
    QImage res = result.format() == QImage::Format_ARGB32 ? result : result.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < current.height(); y++) {
        for (int x = 0; x < current.width(); x++) {
            int m = qGray(mask.pixel(x, y));
            if (m <= 0) continue;
            QRgb cur = current.pixel(x, y);
            QRgb resPix = res.pixel(x, y);
            if (m >= 255) {
                current.setPixel(x, y, resPix);
            } else {
                int inv = 255 - m;
                current.setPixel(x, y, qRgba(
                    (qRed(cur) * inv + qRed(resPix) * m) / 255,
                    (qGreen(cur) * inv + qGreen(resPix) * m) / 255,
                    (qBlue(cur) * inv + qBlue(resPix) * m) / 255,
                    (qAlpha(cur) * inv + qAlpha(resPix) * m) / 255));
            }
        }
    }
}

QString collectDiagnostics(const QString &pluginVersion, bool redactUser, const QStringList *objectInfoSpec58NodesPresent)
{
    QString out;
    QTextStream s(&out);
    s << "=== ComfyUI Remote (C++) Diagnostics ===\n\n";
    s << "Plugin version: " << pluginVersion << "\n";
    if (QCoreApplication::instance()) {
        s << "Krita version: " << QCoreApplication::applicationVersion() << "\n";
    } else {
        s << "Krita version: (unknown)\n";
    }
    s << "Platform: " << QSysInfo::productType() << " " << QSysInfo::kernelVersion() << "\n";
    s << "Architecture: " << QSysInfo::currentCpuArchitecture() << "\n";
    s << "User data dir: " << pluginUserDataDir() << "\n";
    s << "Log dir: " << pluginLogDir() << "\n";

    if (objectInfoSpec58NodesPresent) {
        s << "\nTechnical specification §13.58 node types found in last GET /object_info:\n";
        if (objectInfoSpec58NodesPresent->isEmpty()) {
            s << "  (none recorded yet — connect to the server or use Refresh on checkpoints/samplers)\n";
        } else {
            s << "  " << objectInfoSpec58NodesPresent->size() << " of " << comfyUiSpecSection58NodeClassTypes().size()
              << ": " << objectInfoSpec58NodesPresent->join(QStringLiteral(", ")) << "\n";
        }
    }

    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    s << "\nSettings (sensitive redacted):\n";
    const QStringList keys = cfg.keyList();
    for (const QString &key : keys) {
        QString val = cfg.readEntry(key, QString());
        if (key.contains("url", Qt::CaseInsensitive) || key.contains("token", Qt::CaseInsensitive)
            || key.contains("auth", Qt::CaseInsensitive) || key.contains("password", Qt::CaseInsensitive)) {
            val = val.isEmpty() ? "(empty)" : "[redacted]";
        }
        s << "  " << key << " = " << val << "\n";
    }

    // §13.171: Diagnostics — last 300 lines of client.log from log_dir (same as Python plugin)
    QString logPath = pluginLogDir() + QStringLiteral("/client.log");
    if (!QFile::exists(logPath)) {
        QDir logDir(pluginLogDir());
        const QStringList logFiles = logDir.entryList(QStringList() << QStringLiteral("*.log"), QDir::Files);
        if (!logFiles.isEmpty()) {
            logPath = logDir.absoluteFilePath(logFiles.first());
        }
    }
    s << "\n--- Last 300 lines of log (" << logPath << ") ---\n";
    if (QFile::exists(logPath)) {
        QFile f(logPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            QStringList lines;
            while (!in.atEnd()) {
                lines << in.readLine();
                if (lines.size() > 300) lines.removeFirst();
            }
            for (const QString &line : lines) {
                s << line << "\n";
            }
        } else {
            s << "(could not open)\n";
        }
    } else {
        s << "(no log file found)\n";
    }

    QString result = out;
    if (redactUser) {
        QString home = QDir::homePath();
        if (!home.isEmpty()) {
            result.replace(home, QStringLiteral("<home>"));
        }
    }
    const int maxLen = 65536 - 5000;
    if (result.size() > maxLen) {
        result = result.left(maxLen) + QStringLiteral("\n\n... (truncated)");
    }
    return result;
}

QString createImgMetadata(const QString &prompt, const QString &negative, int steps, double cfg, qint64 seed,
                          int width, int height, int strength, const QString &samplerName, const QString &checkpoint)
{
    // §13.36: A1111-style "parameters" for PNG tEXt chunk (positive includes <lora:name:weight> from library when caller merges)
    QString s = prompt;
    if (!negative.isEmpty()) {
        s += QStringLiteral("\nNegative prompt: ");
        s += negative;
    }
    s += QStringLiteral("\nSteps: %1, Sampler: %2, CFG scale: %3, Seed: %4, Size: %5×%6, Denoising strength: %7%")
        .arg(steps)
        .arg(samplerName.isEmpty() ? QStringLiteral("euler") : samplerName)
        .arg(cfg, 0, 'f', 1)
        .arg(seed)
        .arg(width).arg(height)
        .arg(strength);
    if (!checkpoint.isEmpty()) {
        s += QStringLiteral(", Model: %1").arg(checkpoint);
    }
    return s;
}

bool ComfyUIUtils::extractZipToDirectory(const QString &zipPath, const QString &destDir, QString *errorOut)
{
#ifdef COMFYUI_HAVE_KARCHIVE
    KZip zip(zipPath);
    if (!zip.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not open update package.");
        return false;
    }
    const KArchiveDirectory *root = zip.directory();
    if (!root) {
        if (errorOut)
            *errorOut = QStringLiteral("Update package is empty or corrupt.");
        return false;
    }
    if (!QDir().mkpath(destDir)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not create extract folder.");
        return false;
    }
    if (!root->copyTo(destDir, true)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not extract update package.");
        return false;
    }
    return true;
#else
    Q_UNUSED(zipPath);
    Q_UNUSED(destDir);
    if (errorOut)
        *errorOut = QStringLiteral("ZIP extraction not available in this build.");
    return false;
#endif
}

// §13.215: Tag CSV — columns tag, type, count, aliases; returns tag column for autocomplete
QStringList loadTagCsvTags(const QString &filePath)
{
    QStringList tags;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return tags;
    QTextStream in(&f);
    QString headerLine = in.readLine();
    if (headerLine.isEmpty()) return tags;
    QStringList headers = headerLine.split(QLatin1Char(','));
    int tagCol = -1;
    for (int i = 0; i < headers.size(); ++i) {
        if (headers[i].trimmed() == QLatin1String("tag")) {
            tagCol = i;
            break;
        }
    }
    if (tagCol < 0) return tags;
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList cols = line.split(QLatin1Char(','));
        if (tagCol < cols.size()) {
            QString tag = cols[tagCol].trimmed();
            if (!tag.isEmpty())
                tags.append(tag);
        }
    }
    return tags;
}

// §13.4: Read A1111 "parameters" or ComfyUI workflow JSON (prompt/workflow text) from image metadata
QPair<QString, QString> readPromptFromImageFile(const QString &filePath)
{
    QPair<QString, QString> out;
    if (filePath.isEmpty()) return out;
    QImageReader reader(filePath);
    QString params = reader.text(QStringLiteral("parameters"));
    if (!params.isEmpty()) {
        const int negIdx = params.indexOf(QStringLiteral("Negative prompt:"));
        if (negIdx < 0) {
            out.first = params.trimmed();
            return out;
        }
        out.first = params.left(negIdx).trimmed();
        QString rest = params.mid(negIdx + 15).trimmed();
        const int stepsIdx = rest.indexOf(QStringLiteral("Steps:"));
        out.second = stepsIdx >= 0 ? rest.left(stepsIdx).trimmed() : rest;
        return out;
    }
    // ComfyUI format: JSON in "prompt" or "workflow" text, CLIPTextEncode nodes
    QString promptJson = reader.text(QStringLiteral("prompt"));
    if (promptJson.isEmpty()) promptJson = reader.text(QStringLiteral("workflow"));
    if (promptJson.isEmpty()) return out;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(promptJson.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return out;
    QJsonObject root = doc.object();
    QStringList clipTexts;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().isObject()) continue;
        QJsonObject node = it.value().toObject();
        if (node.value(QStringLiteral("class_type")).toString() != QLatin1String("CLIPTextEncode")) continue;
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        QString text = inputs.value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) clipTexts.append(text);
    }
    if (clipTexts.size() >= 1) out.first = clipTexts.at(0);
    if (clipTexts.size() >= 2) out.second = clipTexts.at(1);
    return out;
}

QString mergeStylePromptWithInstruction(const QString &styleTemplate, const QString &userInstruction)
{
    const QString u = userInstruction.trimmed();
    const QString t = styleTemplate.trimmed();
    if (styleTemplate.contains(QLatin1String("{prompt}")))
        return QString(styleTemplate).replace(QLatin1String("{prompt}"), userInstruction);
    if (t.isEmpty())
        return u;
    if (u.isEmpty())
        return t;
    return t + QLatin1String(", ") + u;
}

LinkedEditStyleOverride linkedEditStyleOverride(bool editModeEnabled, const QString &dockCkpt, int dockSteps, double dockCfg,
                                                double dockDenoise, const QString &dockSampler, const QString &dockScheduler)
{
    LinkedEditStyleOverride o;
    o.checkpoint = dockCkpt.trimmed();
    o.steps = dockSteps;
    o.cfg = dockCfg;
    o.denoise = dockDenoise;
    o.sampler = dockSampler.trimmed();
    if (o.sampler.isEmpty())
        o.sampler = QStringLiteral("euler");
    o.scheduler = dockScheduler.isEmpty() ? QStringLiteral("normal") : dockScheduler;
    if (!editModeEnabled)
        return o;
    const QString linked = loadSettingsJson().value(QStringLiteral("linked_edit_style")).toString().trimmed();
    if (linked.isEmpty())
        return o;
    const KConfigGroup mainCfg(KSharedConfig::openConfig(), QStringLiteral("ComfyUIRemote"));
    if (!mainCfg.readEntry(QStringLiteral("PresetNames"), QStringList()).contains(linked))
        return o;
    const KConfigGroup presetCfg(KSharedConfig::openConfig(), QStringLiteral("ComfyUIRemote_Preset_") + linked);
    o.active = true;
    o.steps = presetCfg.readEntry(QStringLiteral("Steps"), o.steps);
    o.cfg = presetCfg.readEntry(QStringLiteral("Cfg"), o.cfg);
    const int strPct = qBound(1, presetCfg.readEntry(QStringLiteral("Strength"), 100), 100);
    o.denoise = strPct / 100.0;
    o.sampler = presetCfg.readEntry(QStringLiteral("Sampler"), o.sampler);
    o.scheduler = presetCfg.readEntry(QStringLiteral("Scheduler"), o.scheduler);
    const QString ck = presetCfg.readEntry(QStringLiteral("Checkpoint"), QString()).trimmed();
    if (!ck.isEmpty())
        o.checkpoint = ck;
    o.stylePositiveTemplate = presetCfg.readEntry(QStringLiteral("Prompt"), QString());
    o.styleNegative = presetCfg.readEntry(QStringLiteral("Negative"), QString());
    return o;
}

}
