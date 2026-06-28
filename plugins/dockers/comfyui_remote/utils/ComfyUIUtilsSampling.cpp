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

bool settingsColorMatchEnabled()
{
    return loadSettingsJson().value(QStringLiteral("color_match")).toBool(true);
}

double settingsNsfwFilterSensitivity()
{
    return qBound(0.0, loadSettingsJson().value(QStringLiteral("nsfw_filter")).toDouble(0.0), 1.0);
}

ResolvedSamplingInputs resolveSamplingFromStyle(const ComfyStyleEntry *styleEntry,
                                                const QJsonObject &settings,
                                                const QString &dockSamplerText,
                                                int dockSteps,
                                                double dockCfg,
                                                double strength0to1,
                                                bool isLive)
{
    ResolvedSamplingInputs out;
    ResolvedSamplerInputs base =
        isLive ? resolveSamplerForLive(styleEntry, settings, dockSamplerText, dockSteps, dockCfg)
               : [&]() {
                     ResolvedSamplerInputs r;
                     QString key = styleEntry && !styleEntry->samplerPresetName.isEmpty()
                                         ? styleEntry->samplerPresetName
                                         : settings.value(QStringLiteral("quality_sampler_preset"))
                                               .toString()
                                               .trimmed();
                     if (!key.isEmpty()) {
                         const QJsonObject root = builtinSamplerPresetsRoot();
                         QString sam, sch;
                         int st = 0, minSt = 1;
                         double cfg = 7.0;
                         if (samplerPresetLookup(root, key, &sam, &sch, &st, &minSt, &cfg)) {
                             r.sampler = sam;
                             r.scheduler = sch;
                             r.steps = qMax(st, minSt);
                             r.minSteps = minSt;
                             r.cfg = cfg;
                             return r;
                         }
                     }
                     r.sampler =
                         dockSamplerText.trimmed().isEmpty() ? QStringLiteral("euler") : dockSamplerText.trimmed();
                     r.scheduler = QStringLiteral("normal");
                     if (styleEntry && styleEntry->samplerSteps > 0)
                         r.steps = styleEntry->samplerSteps;
                     else
                         r.steps = dockSteps;
                     if (styleEntry && styleEntry->cfgScale > 0)
                         r.cfg = styleEntry->cfgScale;
                     else
                         r.cfg = dockCfg;
                     return r;
                 }();
    out.sampler = base.sampler;
    out.scheduler = base.scheduler;
    out.cfg = base.cfg;
    out.totalSteps = qMax(1, base.steps);
    out.startAtStep = 0;
    out.denoiseStrength = 1.0;
    if (strength0to1 < 1.0) {
        int startAt = static_cast<int>(qRound(out.totalSteps * (1.0 - strength0to1)));
        int steps = out.totalSteps;
        const int minSteps = qMax(1, base.minSteps);
        if (steps - startAt < minSteps) {
            steps = static_cast<int>(std::floor(minSteps / qMax(strength0to1, 0.01)));
            startAt = steps - minSteps;
        }
        out.totalSteps = qMax(1, steps);
        out.startAtStep = qBound(0, startAt, out.totalSteps);
        out.denoiseStrength =
            out.totalSteps > 0 ? static_cast<double>(out.totalSteps - out.startAtStep) / static_cast<double>(out.totalSteps)
                               : strength0to1;
    }
    return out;
}

void applyStrengthResolvedSamplingToRefine(ComfyWorkflowEngine::RefineParams *refine,
                                           const ComfyStyleEntry *styleEntry,
                                           const QJsonObject &settings,
                                           const QString &dockSampler,
                                           int dockSteps,
                                           double dockCfg,
                                           double strength0to1)
{
    if (!refine)
        return;
    const ResolvedSamplingInputs sampling = resolveSamplingFromStyle(
        styleEntry, settings, dockSampler, dockSteps, dockCfg, strength0to1, false);
    refine->steps = sampling.totalSteps;
    refine->denoise = sampling.denoiseStrength;
    refine->cfg = sampling.cfg;
    if (!sampling.sampler.isEmpty())
        refine->sampler = sampling.sampler;
    if (!sampling.scheduler.isEmpty())
        refine->scheduler = sampling.scheduler;
}


// §13.206: Classify checkpoint filename for arch (delegates to ComfyResources)
QString classifyCheckpointArch(const QString &ckptName)
{
    return ComfyResources::archToKey(ComfyResources::archFromCheckpointName(ckptName));
}

// §13.206: True when arch is an edit model (flux_k, qwen_e, qwen_e_p, qwen_l)
bool isArchEdit(const QString &ckptName)
{
    const QString arch = classifyCheckpointArch(ckptName);
    return arch == QLatin1String("flux_k") || arch.startsWith(QLatin1String("qwen"));
}

bool hasLinkedEditStyle(const QString &linkedEditStyleId)
{
    return !linkedEditStyleId.trimmed().isEmpty();
}

bool canToggleEditMode(const QString &ckptName, const QString &linkedEditStyleId)
{
    return !isArchEdit(ckptName) && hasLinkedEditStyle(linkedEditStyleId);
}








// §13.158: create_mask_from_selection equivalent — use Krita selection API (selection(), pixelSelection(), selectedExactRect())
// §13.102: invertSelection inverts the mask (white↔black) after reading

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

    // §13.171: Diagnostics — last 300 lines of client.log from log_dir
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

bool extractZipToDirectory(const QString &zipPath, const QString &destDir, QString *errorOut)
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


LinkedEditStyleOverride linkedEditStyleOverride(bool editModeEnabled, const QString &linkedStyleId, const QString &dockCkpt,
                                                int dockSteps, double dockCfg, double dockDenoise, const QString &dockSampler,
                                                const QString &dockScheduler)
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
    const QString linked = linkedStyleId.trimmed();
    if (linked.isEmpty())
        return o;
    const ComfyStyleEntry *linkedStyle = ComfyStyleCollection::instance().findByStyleId(linked);
    if (!linkedStyle)
        return o;
    o.active = true;
    o.stylePositiveTemplate = linkedStyle->stylePrompt;
    o.styleNegative = linkedStyle->negativePrompt;
    o.steps = linkedStyle->samplerSteps;
    o.cfg = linkedStyle->cfgScale;
    if (!linkedStyle->checkpoints.isEmpty())
        o.checkpoint = linkedStyle->checkpoints.first();
    const QJsonObject samplerRoot = builtinSamplerPresetsRoot();
    QString sam, sch;
    int steps = linkedStyle->samplerSteps;
    int minSteps = 1;
    double cfg = linkedStyle->cfgScale;
    if (!linkedStyle->samplerPresetName.isEmpty()
        && samplerPresetLookup(samplerRoot, linkedStyle->samplerPresetName, &sam, &sch, &steps, &minSteps, &cfg)) {
        o.sampler = sam;
        o.scheduler = sch;
        o.steps = qMax(steps, minSteps);
        o.cfg = cfg;
    }
    return o;
}

} // namespace ComfyUIUtils
