/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyInpaintRunnerInternal.h"

#include "ComfyUIUtils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QtGlobal>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace ComfyInpaintRunnerInternal {

namespace {

QString rectDiag(const QRect &r)
{
    if (r.isEmpty())
        return QString();
    return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

QString sizeDiag(const QSize &s)
{
    if (!s.isValid())
        return QString();
    return QStringLiteral("%1x%2").arg(s.width()).arg(s.height());
}

void appendField(QString *msg, const char *key, const QString &val)
{
    if (!msg || val.isEmpty())
        return;
    *msg += QStringLiteral(" %1=%2").arg(QLatin1String(key), val);
}

void appendBool(QString *msg, const char *key, bool val, bool set)
{
    if (!msg || !set)
        return;
    *msg += QStringLiteral(" %1=%2").arg(QLatin1String(key)).arg(val ? QStringLiteral("1") : QStringLiteral("0"));
}

void appendDouble(QString *msg, const char *key, double val)
{
    if (!msg || val < 0.0)
        return;
    *msg += QStringLiteral(" %1=%2").arg(QLatin1String(key)).arg(val, 0, 'f', 3);
}

} // namespace

double imageNonBlackFraction(const QImage &image)
{
    if (image.isNull())
        return 0.0;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    int nonBlack = 0;
    const int total = rgb.width() * rgb.height();
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = rgb.pixel(x, y);
            if (qMax(qRed(px), qMax(qGreen(px), qBlue(px))) > 20)
                ++nonBlack;
        }
    }
    return total > 0 ? nonBlack / double(total) : 0.0;
}

QString summarizeWorkflowGraph(const QJsonObject &workflow, QString *latentPathOut)
{
    if (latentPathOut)
        latentPathOut->clear();
    if (workflow.isEmpty())
        return QStringLiteral("nodes=0 latentPath=empty");

    bool hasInpaintCond = false;
    bool hasVaeEncode = false;
    bool hasLatentNoiseMask = false;
    bool hasApplyMaskToImage = false;
    bool hasSaveImage = false;
    double denoise = -1.0;
    for (auto it = workflow.constBegin(); it != workflow.constEnd(); ++it) {
        const QJsonObject node = it.value().toObject();
        const QString cls = node.value(QStringLiteral("class_type")).toString();
        if (cls == QLatin1String("INPAINT_VAEEncodeInpaintConditioning"))
            hasInpaintCond = true;
        if (cls == QLatin1String("VAEEncode"))
            hasVaeEncode = true;
        if (cls == QLatin1String("SetLatentNoiseMask"))
            hasLatentNoiseMask = true;
        if (cls == QLatin1String("ETN_ApplyMaskToImage"))
            hasApplyMaskToImage = true;
        if (cls == QLatin1String("SaveImage"))
            hasSaveImage = true;
        if (cls == QLatin1String("KSampler") || it.key() == QStringLiteral("8")) {
            const QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
            if (inputs.contains(QStringLiteral("denoise")))
                denoise = inputs.value(QStringLiteral("denoise")).toDouble();
        }
    }
    const char *latentPath = hasInpaintCond ? "inpaint_conditioning"
                           : (hasLatentNoiseMask ? "vae_encode_noise_mask" : "other");
    if (latentPathOut)
        *latentPathOut = QString::fromLatin1(latentPath);

    return QStringLiteral("nodes=%1 latentPath=%2 hasVaeEncode=%3 hasLatentNoiseMask=%4 hasInpaintCond=%5 "
                          "hasApplyMaskToImage=%6 hasSaveImage=%7 denoise=%8")
        .arg(workflow.size())
        .arg(QString::fromLatin1(latentPath))
        .arg(hasVaeEncode ? 1 : 0)
        .arg(hasLatentNoiseMask ? 1 : 0)
        .arg(hasInpaintCond ? 1 : 0)
        .arg(hasApplyMaskToImage ? 1 : 0)
        .arg(hasSaveImage ? 1 : 0)
        .arg(denoise >= 0.0 ? QString::number(denoise, 'f', 3) : QStringLiteral("n/a"));
}

QString inpaintFailureVerdict(double rawNonBlack, double compositeNonBlack, const QString &latentPath,
                              const QString &archKey, double denoise, bool refineRegion)
{
    if (rawNonBlack < 0.05) {
        if (archKey == QLatin1String("sdxl") && latentPath == QLatin1String("vae_encode_noise_mask")
            && denoise > 0.0 && denoise < 1.0)
            return QStringLiteral("server_black_sdxl_old_latent_path");
        if (refineRegion && latentPath == QLatin1String("vae_encode_noise_mask"))
            return QStringLiteral("server_black_refine_old_latent_path");
        return QStringLiteral("server_black");
    }
    if (compositeNonBlack < 0.05)
        return QStringLiteral("composite_black_raw_ok");
    return QStringLiteral("ok");
}

void logInpaintDiag(const InpaintDiagSnapshot &s)
{
    QString msg = QStringLiteral("INPAINT_DIAG event=%1").arg(s.event);
    appendField(&msg, "plugin", s.pluginVersion);
    appendField(&msg, "workflowKind", s.workflowKind);
    appendField(&msg, "arch", s.archKey);
    appendField(&msg, "checkpoint", s.checkpoint);
    appendDouble(&msg, "strength", s.strength0to1);
    appendDouble(&msg, "denoise", s.denoise);
    if (s.event == QStringLiteral("prepare") || s.event == QStringLiteral("build"))
        appendBool(&msg, "useInpaintModel", s.useInpaintModel, true);
    appendBool(&msg, "refineRegion", s.refineRegion, true);
    appendBool(&msg, "serverPreMasked", s.serverPreMasked, s.event == QStringLiteral("composite")
                                                              || s.event == QStringLiteral("prepare"));
    appendBool(&msg, "editMode", s.editMode, s.event == QStringLiteral("prepare"));
    appendField(&msg, "effectiveMode", s.effectiveMode);
    appendField(&msg, "modifierMode", s.modifierMode);
    appendField(&msg, "selection", rectDiag(s.selectionOriginal));
    appendField(&msg, "maskPadded", rectDiag(s.maskPaddedBounds));
    appendField(&msg, "contextBounds", rectDiag(s.contextBounds));
    appendField(&msg, "targetRel", rectDiag(s.targetBoundsRelative));
    appendField(&msg, "nativeCtx", sizeDiag(s.nativeContextSize));
    appendField(&msg, "uploadCtx", sizeDiag(s.uploadContextSize));
    appendField(&msg, "diffusion", sizeDiag(s.diffusionExtent));
    if (s.grow >= 0)
        msg += QStringLiteral(" grow=%1").arg(s.grow);
    if (s.feather >= 0)
        msg += QStringLiteral(" feather=%1").arg(s.feather);
    if (s.blend >= 0)
        msg += QStringLiteral(" blend=%1").arg(s.blend);
    appendField(&msg, "imageUpload", s.imageUploadName);
    appendField(&msg, "maskUpload", s.maskUploadName);
    appendField(&msg, "graph", s.graphSummary);
    appendField(&msg, "latentPath", s.latentPath);
    appendDouble(&msg, "rawNonBlack", s.rawNonBlack);
    appendDouble(&msg, "compositeNonBlack", s.compositeNonBlack);
    appendField(&msg, "compositePath", s.compositePath);
    appendField(&msg, "verdict", s.verdict);
    appendField(&msg, "contextPx", s.contextPixels);
    appendField(&msg, "maskPx", s.maskPixels);
    appendField(&msg, "serverPx", s.serverPixels);
    appendField(&msg, "outputPx", s.outputPixels);
    qCWarning(KIS_COMFYUI_REMOTE).noquote() << msg;
}

QString describeImagePixels(const QImage &image, const QString &label)
{
    if (image.isNull())
        return QStringLiteral("%1 null").arg(label);
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    qint64 sumR = 0;
    qint64 sumG = 0;
    qint64 sumB = 0;
    int nonBlack = 0;
    const int total = rgb.width() * rgb.height();
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb px = rgb.pixel(x, y);
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            sumR += r;
            sumG += g;
            sumB += b;
            if (qMax(r, qMax(g, b)) > 20)
                ++nonBlack;
        }
    }
    const double inv = total > 0 ? 1.0 / total : 0.0;
    const QRgb center = rgb.pixel(rgb.width() / 2, rgb.height() / 2);
    const QRgb corner = rgb.pixel(qMin(2, rgb.width() - 1), qMin(2, rgb.height() - 1));
    return QStringLiteral("%1 size=%2x%3 meanRGB=(%4,%5,%6) nonBlack=%7 center=(%8,%9,%10) corner=(%11,%12,%13)")
        .arg(label)
        .arg(rgb.width())
        .arg(rgb.height())
        .arg(int(sumR * inv + 0.5))
        .arg(int(sumG * inv + 0.5))
        .arg(int(sumB * inv + 0.5))
        .arg(total > 0 ? QString::number(nonBlack / double(total), 'f', 3) : QStringLiteral("n/a"))
        .arg(qRed(center))
        .arg(qGreen(center))
        .arg(qBlue(center))
        .arg(qRed(corner))
        .arg(qGreen(corner))
        .arg(qBlue(corner));
}

QImage cropContextResultToTarget(const QImage &image, const QRect &contextBounds, const QRect &targetBounds)
{
    if (image.isNull() || contextBounds.isEmpty() || targetBounds.isEmpty())
        return image;

    const QSize contextSize(contextBounds.width(), contextBounds.height());
    const QRect targetLocal = targetBounds.translated(-contextBounds.topLeft());

    // Server already returned the masked target patch (buildInpaint targetBoundsRelative path).
    if (image.size() == targetBounds.size())
        return image;

    // Full context canvas — copy the target sub-rectangle in context coordinates.
    if (image.size() == contextSize) {
        if (targetLocal.isEmpty())
            return image;
        QRect local = targetLocal & QRect(QPoint(0, 0), image.size());
        if (local.isEmpty())
            return image;
        QImage cropped = image.copy(local);
        if (cropped.size() != targetBounds.size())
            cropped = cropped.scaled(targetBounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return cropped;
    }

    // Near-target output (rounding) — scale to expected patch size.
    if (qAbs(image.width() - targetBounds.width()) <= 8 && qAbs(image.height() - targetBounds.height()) <= 8)
        return image.scaled(targetBounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    return image;
}

InpaintCompositeResult compositeInpaintServerOntoContext(const InpaintCompositeParams &params)
{
    InpaintCompositeResult result;
    const QSize contextSize = params.contextImage.size();
    const QRect targetLocal =
        params.targetBounds.translated(-params.contextBounds.topLeft());
    const QSize nativePatchSize = targetLocal.size();

    QImage serverResult = params.serverResult;
    if (!serverResult.isNull() && contextSize.isValid() && serverResult.size() != contextSize
        && serverResult.size() != nativePatchSize) {
        if (params.diffusionExtent.isValid() && serverResult.size() == params.diffusionExtent) {
            serverResult = serverResult.scaled(contextSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            result.pathTaken = QStringLiteral("scale_server_from_diffusion_extent");
        } else if (nativePatchSize.isValid()) {
            serverResult = serverResult.scaled(nativePatchSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            result.pathTaken = QStringLiteral("scale_server_to_native_patch");
        }
    }

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "compositeInpaintServerOntoContext: workflow=" << (params.refineRegionWorkflow ? "refine_region" : "inpaint")
        << " contextBounds=" << params.contextBounds << " targetBounds=" << params.targetBounds
        << " targetLocal=" << targetLocal << " preprocessGrow=" << params.preprocessGrow
        << " feather=" << params.preprocessFeather << " blend=" << params.preprocessBlend
        << " " << describeImagePixels(params.contextImage, QStringLiteral("context"))
        << " " << describeImagePixels(params.compositingMask, QStringLiteral("mask"))
        << " " << describeImagePixels(serverResult, QStringLiteral("server"))
        << (result.pathTaken.isEmpty() ? QString() : QStringLiteral(" preScale=") + result.pathTaken);

    if (serverResult.isNull()) {
        result.pathTaken = QStringLiteral("no_server_use_context");
        result.output = params.contextImage;
        return result;
    }

    if (params.compositingMask.isNull()) {
        result.pathTaken = QStringLiteral("no_mask_use_server");
        result.output = serverResult.convertToFormat(QImage::Format_ARGB32);
        return result;
    }

    QImage compositeMask = params.serverPreMasked
                               ? ComfyUIUtils::denoiseToCompositingMask(params.compositingMask, 0, 0,
                                                                        params.preprocessBlend)
                               : ComfyUIUtils::denoiseToCompositingMask(
                                     params.compositingMask, params.preprocessGrow, params.preprocessFeather,
                                     params.preprocessBlend);
    if (contextSize.isValid() && compositeMask.size() != contextSize) {
        qCWarning(KIS_COMFYUI_REMOTE) << "compositeInpaintServerOntoContext: scaling compositing mask"
                                      << compositeMask.size() << "->" << contextSize;
        compositeMask =
            compositeMask.scaled(contextSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const QImage serverRgb = serverResult.convertToFormat(QImage::Format_RGB32);
    if (contextSize.isValid() && serverRgb.size() == contextSize) {
        result.output = params.contextImage.convertToFormat(QImage::Format_ARGB32);
        ComfyUIUtils::compositeWithMask(result.output, serverRgb, compositeMask);
        result.pathTaken = QStringLiteral("full_context");
    } else if (nativePatchSize.isValid() && serverRgb.size() == nativePatchSize) {
        result.output = params.contextImage.convertToFormat(QImage::Format_ARGB32);
        QImage patch = result.output.copy(targetLocal);
        ComfyUIUtils::compositeWithMask(patch, serverRgb, compositeMask.copy(targetLocal));
        ComfyUIUtils::blitImageInto(result.output, patch, targetLocal.topLeft());
        result.pathTaken = QStringLiteral("patch_blit_into_context");
    } else if (contextSize.isValid()) {
        result.output = params.contextImage.convertToFormat(QImage::Format_ARGB32);
        const QImage scaled = serverRgb.scaled(contextSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        ComfyUIUtils::compositeWithMask(result.output, scaled, compositeMask);
        result.pathTaken = QStringLiteral("scale_server_to_context");
    } else if (nativePatchSize.isValid()) {
        result.output = params.contextImage.convertToFormat(QImage::Format_ARGB32);
        const QImage scaled = serverRgb.scaled(nativePatchSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage patch = result.output.copy(targetLocal);
        ComfyUIUtils::compositeWithMask(patch, scaled, compositeMask.copy(targetLocal));
        ComfyUIUtils::blitImageInto(result.output, patch, targetLocal.topLeft());
        result.pathTaken = QStringLiteral("scale_server_patch_blit");
    } else {
        result.pathTaken = QStringLiteral("fallback_server_only");
        result.output = serverResult.convertToFormat(QImage::Format_ARGB32);
    }

    qCWarning(KIS_COMFYUI_REMOTE).nospace()
        << "compositeInpaintServerOntoContext: path=" << result.pathTaken
        << " " << describeImagePixels(result.output, QStringLiteral("output"));
    return result;
}

} // namespace ComfyInpaintRunnerInternal
