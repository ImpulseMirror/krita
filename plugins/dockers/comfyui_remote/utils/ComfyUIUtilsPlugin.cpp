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

// §13.191: Same contract and message format as document.check_color_mode()
std::pair<bool, QString> checkColorMode(KisImageSP image)
{
    if (!image || !image->colorSpace()) {
        return {true, QString()};  // no document → allow (stub behavior per §13.42)
    }
    const KoColorSpace *cs = image->colorSpace();
    if (cs->colorModelId() != RGBAColorModelID) {
        return {false,
                ComfyTr::tr("Incompatible document: Color model must be RGB/Alpha (current model: %1)", cs->colorModelId().name())};
    }
    if (cs->colorDepthId() != Integer8BitsColorDepthID) {
        return {false,
                ComfyTr::tr("Incompatible document: Color depth must be 8-bit integer (current depth: %1)", cs->colorDepthId().name())};
    }
    return {true, QString()};
}




QString formatComfySystemStatsDeviceLine(const QJsonObject &root)
{
    const QJsonArray devs = root.value(QStringLiteral("devices")).toArray();
    if (devs.isEmpty())
        return ComfyTr::tr("Device: (no GPU info in server response)");

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
        return ComfyTr::tr("Device: (unparsed system_stats)");
    return ComfyTr::tr("Device: %1", parts.join(QLatin1String(" · ")));
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
    // FAITHFUL_PORT: spec default is "auto" which infers a batch size from the
    // server's reported VRAM (high → batch=6 on a 24GB card, cloud → 8). On
    // Android the dock's Batch spinner is hidden behind the queue popup, so the
    // user effectively had no way to get "1 image per click" without diving
    // into Settings → Performance and switching to Custom. The compact view
    // is supposed to mirror the desktop Python plugin's UX, which produces a
    // single image per click on a fresh install. Default to Custom so the
    // user's BatchCount setting (default 1) is honoured; the Performance tab
    // still lets power users opt into "auto"/"high"/"cloud" batching.
    if (preset.isEmpty())
        preset = QStringLiteral("custom");
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


} // namespace ComfyUIUtils
