/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLiveRunnerInternal.h"

#include "ComfyInpaintRunnerInternal.h"
#include "ComfyLiveScheduler.h"
#include "ComfyPrepareWorkflow.h"
#include "ComfyUIRemoteDockPrivate.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QBuffer>

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>
#include <Qt>

namespace ComfyLiveRunnerInternal {

namespace {

QByteArray imagePngDigest(const QImage &image)
{
    if (image.isNull())
        return QByteArray();
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    image.save(&buf, "PNG");
    return QCryptographicHash::hash(png, QCryptographicHash::Sha256);
}

} // namespace

void LiveSchedulerState::reset()
{
    lastFingerprint.clear();
    lastChangeMs = 0;
    oldestChangeMs = 0;
    hasChanges = true;
    generationStartMs = 0;
    generationTimesMs.clear();
}

bool LiveSchedulerState::shouldGenerate(const QByteArray &fingerprint, qint64 nowMs)
{
    if (fingerprint.isEmpty())
        return false;
    if (lastFingerprint != fingerprint) {
        lastFingerprint = fingerprint;
        lastChangeMs = nowMs;
        if (!hasChanges)
            oldestChangeMs = nowMs;
        hasChanges = true;
    }
    const qint64 sinceLast = nowMs - lastChangeMs;
    const qint64 sinceOldest = oldestChangeMs > 0 ? nowMs - oldestChangeMs : sinceLast;
    return hasChanges
           && (sinceLast >= gracePeriodMs() || sinceOldest >= 3000);
}

void LiveSchedulerState::notifyGenerationStarted(qint64 nowMs)
{
    generationStartMs = nowMs;
    hasChanges = false;
}

void LiveSchedulerState::notifyGenerationFinished(qint64 nowMs)
{
    if (generationStartMs > 0)
        generationTimesMs.append(nowMs - generationStartMs);
    while (generationTimesMs.size() > 10)
        generationTimesMs.removeFirst();
    generationStartMs = 0;
}

int LiveSchedulerState::gracePeriodMs() const
{
    if (generationTimesMs.isEmpty())
        return 0;
    qint64 sum = 0;
    for (qint64 t : generationTimesMs)
        sum += t;
    const qint64 avg = sum / generationTimesMs.size();
    return avg > 1500 ? 250 : 0;
}

QByteArray computeLiveInputFingerprint(const ComfyPrepareLiveWorkflow::Result &prep,
                                       const QString &positivePrompt,
                                       const QString &negativePrompt,
                                       int seed,
                                       bool editMode,
                                       const QImage &canvasForFingerprint)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(seed));
    hash.addData(QByteArray::number(prep.strength0to1, 'f', 4));
    hash.addData(editMode ? "1" : "0");
    hash.addData(positivePrompt.toUtf8());
    hash.addData(negativePrompt.toUtf8());
    const QRect b = prep.contextBounds;
    hash.addData(QByteArray::number(b.x()));
    hash.addData(QByteArray::number(b.y()));
    hash.addData(QByteArray::number(b.width()));
    hash.addData(QByteArray::number(b.height()));
    const QImage &canvas = !canvasForFingerprint.isNull() ? canvasForFingerprint : prep.contextImage;
    if (!canvas.isNull())
        hash.addData(imagePngDigest(canvas));
    if (!prep.compositingMaskCropped.isNull())
        hash.addData(imagePngDigest(prep.compositingMaskCropped));
    return hash.result();
}

bool livePipelineBusy(const ComfyUIRemoteDock::Private *d)
{
    if (!d)
        return false;
    return d->liveRt.livePipelineBusy || !d->liveRt.livePromptId.isEmpty() || d->liveRt.liveAwaitingLoraUploads
           || d->liveRt.liveAwaitingControlUploads || d->liveRt.liveAwaitingRegionMaskUploads;
}

QImage compositeLiveServerResult(const QImage &serverResult, const ComfyPrepareLiveWorkflow::Result &prep)
{
    if (!prep.hasMask && prep.workflowKind != ComfyPrepareWorkflow::WorkflowKind::RefineRegion)
        return serverResult;

    const QImage contextImage =
        prep.nativeContextImage.isNull() ? prep.contextImage : prep.nativeContextImage;
    const QImage compositingMask = prep.nativeCompositingMask.isNull() ? prep.compositingMaskCropped
                                                                       : prep.nativeCompositingMask;

    ComfyInpaintRunnerInternal::InpaintCompositeParams params;
    params.serverResult = serverResult;
    params.contextImage = contextImage;
    params.compositingMask = compositingMask;
    params.contextBounds = prep.contextBounds;
    params.targetBounds = prep.maskPaddedBounds;
    params.preprocessGrow = prep.preprocess.grow;
    params.preprocessFeather = prep.preprocess.feather;
    params.preprocessBlend = prep.preprocess.blend;
    params.diffusionExtent = prep.diffusionExtent;
    params.refineRegionWorkflow = prep.workflowKind == ComfyPrepareWorkflow::WorkflowKind::RefineRegion;
    params.serverPreMasked = false;

    return ComfyInpaintRunnerInternal::compositeInpaintServerOntoContext(params).output;
}

QImage cropLiveResultToTarget(const QImage &image, const ComfyPrepareLiveWorkflow::Result &prep)
{
    if (!prep.hasMask || image.isNull())
        return image;
    const QRect contextBounds = prep.contextBounds;
    const QRect targetBounds = prep.maskPaddedBounds;
    if (contextBounds.isEmpty() || targetBounds.isEmpty())
        return image;

    const QSize contextSize(contextBounds.width(), contextBounds.height());
    const QRect targetLocal = targetBounds.translated(-contextBounds.topLeft());
    if (image.size() == targetBounds.size())
        return image;
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
    if (qAbs(image.width() - targetBounds.width()) <= 8 && qAbs(image.height() - targetBounds.height()) <= 8)
        return image.scaled(targetBounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return image;
}

QUrl comfyImageUploadUrl(const QString &serverUrl)
{
    QUrl uploadUrl(serverUrl.trimmed());
    QString up = uploadUrl.path();
    if (up.isEmpty() || up == QLatin1Char('/'))
        uploadUrl.setPath(QStringLiteral("/upload/image"));
    else if (!up.endsWith(QLatin1Char('/')))
        uploadUrl.setPath(up + QStringLiteral("/upload/image"));
    else
        uploadUrl.setPath(up + QStringLiteral("upload/image"));
    return uploadUrl;
}

} // namespace ComfyLiveRunnerInternal
