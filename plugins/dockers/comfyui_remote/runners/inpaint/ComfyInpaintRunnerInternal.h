/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QSize>
#include <QString>

namespace ComfyInpaintRunnerInternal {

/// Grep logcat with: adb logcat | grep INPAINT_DIAG
struct InpaintDiagSnapshot {
    QString event;
    QString pluginVersion;
    QString workflowKind;
    QString archKey;
    QString checkpoint;
    double strength0to1 = -1.0;
    double denoise = -1.0;
    bool useInpaintModel = false;
    bool refineRegion = false;
    bool serverPreMasked = false;
    bool editMode = false;
    QString effectiveMode;
    QString modifierMode;
    QRect selectionOriginal;
    QRect maskPaddedBounds;
    QRect contextBounds;
    QRect targetBoundsRelative;
    QSize nativeContextSize;
    QSize uploadContextSize;
    QSize diffusionExtent;
    int grow = -1;
    int feather = -1;
    int blend = -1;
    QString imageUploadName;
    QString maskUploadName;
    QString graphSummary;
    QString latentPath;
    double rawNonBlack = -1.0;
    double compositeNonBlack = -1.0;
    QString compositePath;
    QString verdict;
    QString contextPixels;
    QString maskPixels;
    QString serverPixels;
    QString outputPixels;
};

/// One-line pixel summary for logcat (size, mean RGB, non-black fraction, center/corner samples).
QString describeImagePixels(const QImage &image, const QString &label = QString());

double imageNonBlackFraction(const QImage &image);

/// latentPath, node count, denoise, server-side mask nodes (refine should not use ETN_ApplyMaskToImage).
QString summarizeWorkflowGraph(const QJsonObject &workflow, QString *latentPathOut = nullptr);

QString inpaintFailureVerdict(double rawNonBlack, double compositeNonBlack, const QString &latentPath,
                              const QString &archKey, double denoise, bool refineRegion);

void logInpaintDiag(const InpaintDiagSnapshot &snap);

/// Grep logcat with: adb logcat | grep LIVE_DIAG
void logLiveDiag(const InpaintDiagSnapshot &snap);

QImage cropContextResultToTarget(const QImage &image, const QRect &contextBounds, const QRect &targetBounds);

struct InpaintCompositeParams {
    QImage serverResult;
    QImage contextImage;
    QImage compositingMask;
    QRect contextBounds;
    QRect targetBounds;
    int preprocessGrow = 0;
    int preprocessFeather = 0;
    int preprocessBlend = 0;
    QSize diffusionExtent;
    bool refineRegionWorkflow = false;
    /// Server already applied grow/feather + apply_mask (fill patch). Client only threshold+shrink(blend).
    bool serverPreMasked = false;
};

struct InpaintCompositeResult {
    QImage output;
    QString pathTaken;
};

/// Shared fill + refine: merge masked server output onto captured context (upstream draw_image).
InpaintCompositeResult compositeInpaintServerOntoContext(const InpaintCompositeParams &params);

} // namespace ComfyInpaintRunnerInternal
