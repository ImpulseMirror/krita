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

QStringList parseCheckpointNamesFromObjectInfoRoot(const QJsonObject &root)
{
    const QJsonObject nodeInfo = root.value(QStringLiteral("CheckpointLoaderSimple")).toObject();
    const QJsonObject input = nodeInfo.value(QStringLiteral("input")).toObject();
    const QJsonObject required = input.value(QStringLiteral("required")).toObject();
    const QJsonValue ckptVal = required.value(QStringLiteral("ckpt_name"));
    QStringList names;
    if (ckptVal.isArray()) {
        const QJsonArray arr = ckptVal.toArray();
        if (!arr.isEmpty() && arr.at(0).isArray()) {
            for (const QJsonValue &v : arr.at(0).toArray())
                names << v.toString();
        }
    }
    return names;
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
        QStringLiteral("INPAINT_MaskedFill"),
        QStringLiteral("INPAINT_MaskedBlur"),
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
        QStringLiteral("ImageCrop"),
        QStringLiteral("CropMask"),
        QStringLiteral("LoadImage"),
        QStringLiteral("SaveImage"),
        QStringLiteral("ImageScale"),
    };
    return list;
}

QStringList vaeNamesFromObjectInfo(const QJsonObject &objectInfoRoot)
{
    return loaderComboNamesFromObjectInfo(objectInfoRoot, QStringLiteral("VAELoader"),
                                          QStringLiteral("vae_name"));
}

QStringList loaderComboNamesFromObjectInfo(const QJsonObject &objectInfoRoot, const QString &nodeClass,
                                           const QString &inputName)
{
    QStringList names;
    const QJsonObject nodeInfo = objectInfoRoot.value(nodeClass).toObject();
    const QJsonObject input = nodeInfo.value(QStringLiteral("input")).toObject();
    const QJsonObject required = input.value(QStringLiteral("required")).toObject();
    const QJsonValue val = required.value(inputName);
    if (val.isArray()) {
        const QJsonArray arr = val.toArray();
        if (!arr.isEmpty() && arr.at(0).isArray()) {
            for (const QJsonValue &v : arr.at(0).toArray())
                names << v.toString();
        }
    }
    return names;
}

QString findModelOnServer(const QStringList &available, const QStringList &searchPatterns)
{
    struct Match {
        QString filename;
        int priority = -1;
    };
    QList<Match> matches;
    for (int patternIndex = 0; patternIndex < searchPatterns.size(); ++patternIndex) {
        const QStringList segments =
            searchPatterns.at(patternIndex).split(QLatin1Char('*'), Qt::SkipEmptyParts);
        if (segments.isEmpty())
            continue;
        for (const QString &filename : available) {
            const QString name = filename.toLower();
            bool ok = true;
            int pos = 0;
            for (const QString &segment : segments) {
                const int idx = name.indexOf(segment.toLower(), pos);
                if (idx < 0) {
                    ok = false;
                    break;
                }
                pos = idx + segment.size();
            }
            if (!ok)
                continue;
            const int priority = name.contains(QStringLiteral("krita")) ? 0 : patternIndex * 100 + name.size();
            matches.append({filename, priority});
        }
    }
    if (matches.isEmpty())
        return QString();
    std::sort(matches.begin(), matches.end(),
              [](const Match &a, const Match &b) { return a.priority < b.priority; });
    return matches.first().filename;
}

QString findControlNetInpaintOnServer(const QJsonObject &objectInfoRoot, ComfyResources::Arch arch)
{
    const QStringList available =
        loaderComboNamesFromObjectInfo(objectInfoRoot, QStringLiteral("ControlNetLoader"),
                                       QStringLiteral("control_net_name"));
    if (available.isEmpty())
        return QString();

    QStringList patterns;
    switch (arch) {
    case ComfyResources::Arch::Sd15:
    case ComfyResources::Arch::Unknown:
        patterns << QStringLiteral("control_v11p_sd15_inpaint");
        break;
    case ComfyResources::Arch::Flux:
    case ComfyResources::Arch::Flux2_4b:
    case ComfyResources::Arch::Flux2_9b:
        patterns << QStringLiteral("flux1-dev-controlnet-inpaint");
        break;
    case ComfyResources::Arch::Illu:
    case ComfyResources::Arch::IlluV:
        patterns << QStringLiteral("noobaiinpainting");
        break;
    default:
        break;
    }
    if (patterns.isEmpty())
        return QString();
    return findModelOnServer(available, patterns);
}

ResolvedFooocusInpaint resolveFooocusInpaintOnServer(const QJsonObject &objectInfoRoot)
{
    ResolvedFooocusInpaint out;
    const QStringList headAvailable =
        loaderComboNamesFromObjectInfo(objectInfoRoot, QStringLiteral("INPAINT_LoadFooocusInpaint"),
                                       QStringLiteral("head"));
    const QStringList patchAvailable =
        loaderComboNamesFromObjectInfo(objectInfoRoot, QStringLiteral("INPAINT_LoadFooocusInpaint"),
                                       QStringLiteral("patch"));
    if (!headAvailable.isEmpty())
        out.head = findModelOnServer(headAvailable, {QStringLiteral("fooocus_inpaint_head.pth")});
    const QStringList &patchPool = !patchAvailable.isEmpty() ? patchAvailable : headAvailable;
    if (!patchPool.isEmpty())
        out.patch = findModelOnServer(patchPool, {QStringLiteral("inpaint_v26.fooocus")});
    return out;
}

ResolvedInpaintServerModels resolveInpaintServerModels(const QJsonObject &objectInfoRoot,
                                                       ComfyResources::Arch arch,
                                                       bool useInpaintModel)
{
    ResolvedInpaintServerModels out;
    if (!useInpaintModel || objectInfoRoot.isEmpty())
        return out;
    out.controlNetInpaintFile = findControlNetInpaintOnServer(objectInfoRoot, arch);
    const ResolvedFooocusInpaint foo = resolveFooocusInpaintOnServer(objectInfoRoot);
    if (foo.isValid()) {
        out.fooocusInpaintHead = foo.head;
        out.fooocusInpaintPatch = foo.patch;
    }
    return out;
}

QStringList specSection58NodesPresentInObjectInfo(const QJsonObject &objectInfoRoot)
{
    // NB: bind QStringList to a named local; .keys() returns by value so calling it
    // twice (once for begin(), once for end()) yields iterators into two different
    // temporaries — UB that crashes inside qHash on arm64.
    const QStringList keyList = objectInfoRoot.keys();
    const QSet<QString> keys(keyList.begin(), keyList.end());
    QStringList out;
    for (const QString &name : comfyUiSpecSection58NodeClassTypes()) {
        if (keys.contains(name))
            out.append(name);
    }
    return out;
}

} // namespace ComfyUIUtils
