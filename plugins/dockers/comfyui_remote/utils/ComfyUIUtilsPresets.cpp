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

namespace {

// §13.56: Minimal preset table (same keys as samplers.json fields: sampler, scheduler, steps, minimum_steps, cfg).
const char g_builtinSamplersJson[] = R"json({
    "Default - Euler": {"sampler": "euler", "scheduler": "normal", "steps": 20, "minimum_steps": 1, "cfg": 8.0},
    "DPM++ 2M Karras": {"sampler": "dpmpp_2m", "scheduler": "karras", "steps": 25, "minimum_steps": 10, "cfg": 7.0},
    "Euler Ancestral": {"sampler": "euler_ancestral", "scheduler": "normal", "steps": 24, "minimum_steps": 1, "cfg": 7.0}
})json";

static void mergePresetJsonFile(QJsonObject *merged, const QString &path)
{
    if (!merged || path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(f.readAll()), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject fo = doc.object();
    for (auto it = fo.constBegin(); it != fo.constEnd(); ++it) {
        if (it.value().isObject())
            merged->insert(it.key(), it.value());
    }
}

static QJsonObject loadMergedSamplerPresets()
{
    ComfyUIUtils::ensureBundledPluginDataInstalled();
    QJsonObject merged;
    const QString installPath = ComfyUIUtils::pluginInstallDataDir() + QStringLiteral("/presets/samplers.json");
    mergePresetJsonFile(&merged, installPath);
    if (merged.isEmpty()) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(g_builtinSamplersJson), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            merged = doc.object();
    }
    QDir().mkpath(ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets"));
    mergePresetJsonFile(&merged, ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets/samplers.json"));
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
    ComfyUIUtils::ensureBundledPluginDataInstalled();
    QJsonObject merged;
    mergePresetJsonFile(&merged, ComfyUIUtils::pluginInstallDataDir() + QStringLiteral("/presets/control.json"));
    if (merged.isEmpty()) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(g_builtinControlJson), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            merged = doc.object();
    }
    QDir().mkpath(ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets"));
    mergePresetJsonFile(&merged, ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/presets/control.json"));
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
                         double *outCfg,
                         QString *outLora)
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
    if (outLora)
        *outLora = o.value(QStringLiteral("lora")).toString();
    return true;
}

QStringList visibleSamplerPresetNames(const QString &ensureIncluded)
{
    QSet<QString> names;
    const QJsonObject root = builtinSamplerPresetsRoot();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        if (o.isEmpty())
            continue;
        if (o.value(QStringLiteral("hidden")).toBool() && it.key() != ensureIncluded)
            continue;
        names.insert(it.key());
    }
    for (const ComfyStyleEntry &s : ComfyStyleCollection::instance().all()) {
        if (!s.samplerPresetName.isEmpty())
            names.insert(s.samplerPresetName);
        if (!s.liveSamplerPresetName.isEmpty())
            names.insert(s.liveSamplerPresetName);
    }
    if (!ensureIncluded.isEmpty())
        names.insert(ensureIncluded);
    QStringList out = names.values();
    out.sort();
    return out;
}

QStringList styleCheckpointWarnings(const ComfyStyleEntry &style,
                                    const QStringList &serverCheckpointNames,
                                    const QJsonObject &objectInfoRoot)
{
    QStringList warn;
    const QString ckpt = ComfyFileLibrary::preferredCheckpoint(style.checkpoints, serverCheckpointNames);
    if (ckpt == QLatin1String("not-found") && !style.checkpoints.isEmpty())
        warn.append(ComfyTr::tr("The checkpoint used by this style is not installed."));

    const ComfyResources::Arch arch =
        ComfyWorkflowEngine::resolveArch(style.checkpoints.isEmpty() ? QString() : style.checkpoints.first(),
                                       style.architecture);
    if (ckpt != QLatin1String("not-found")) {
        const QString archLabel = ComfyResources::archDisplayName(arch);
        if (!archLabel.isEmpty() && arch != ComfyResources::Arch::Sd15 && arch != ComfyResources::Arch::Sdxl) {
            // Best-effort: surface arch hint when using non-SD15/SDXL ecosystems (full workload API deferred).
            Q_UNUSED(objectInfoRoot);
        }
    }

    const bool needsBundledVae = ComfyResources::isFluxLike(arch) || arch == ComfyResources::Arch::Chroma
                                 || arch == ComfyResources::Arch::Sd3;
    if (needsBundledVae && ckpt != QLatin1String("not-found")) {
        const QStringList vaes = vaeNamesFromObjectInfo(objectInfoRoot);
        if (vaes.isEmpty() && style.vae.trimmed().isEmpty())
            warn.append(ComfyTr::tr("The VAE for this diffusion model is not installed"));
    }
    return warn;
}

ResolvedSamplerInputs resolveSamplerForLive(const ComfyStyleEntry *styleEntry,
                                            const QJsonObject &settings,
                                            const QString &dockSamplerText,
                                            int dockSteps,
                                            double dockCfg)
{
    ResolvedSamplerInputs r;
    QString key;
    if (styleEntry && !styleEntry->liveSamplerPresetName.isEmpty())
        key = styleEntry->liveSamplerPresetName;
    else
        key = settings.value(QStringLiteral("live_sampler_preset")).toString().trimmed();
    if (!key.isEmpty()) {
        const QJsonObject root = builtinSamplerPresetsRoot();
        QString sam, sch;
        int st, minSt;
        double cfg = 8.0;
        if (samplerPresetLookup(root, key, &sam, &sch, &st, &minSt, &cfg)) {
            r.sampler = sam;
            r.scheduler = sch;
            r.steps = qMax(st, minSt);
            r.minSteps = minSt;
            r.cfg = cfg;
            return r;
        }
    }
    if (styleEntry && styleEntry->liveSamplerPresetName.isEmpty()) {
        r.sampler = dockSamplerText.trimmed().isEmpty() ? QStringLiteral("euler") : dockSamplerText.trimmed();
        r.scheduler = QStringLiteral("normal");
        r.steps = styleEntry->liveSamplerSteps > 0 ? styleEntry->liveSamplerSteps : dockSteps;
        r.cfg = styleEntry->liveCfgScale > 0 ? styleEntry->liveCfgScale : dockCfg;
        return r;
    }
    r.sampler = dockSamplerText.trimmed().isEmpty() ? QStringLiteral("euler") : dockSamplerText.trimmed();
    r.scheduler = QStringLiteral("normal");
    r.steps = dockSteps;
    r.cfg = dockCfg;
    return r;
}

} // namespace ComfyUIUtils
