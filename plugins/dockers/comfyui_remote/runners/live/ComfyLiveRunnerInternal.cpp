/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLiveRunnerInternal.h"

#include "ComfyPrepareLiveWorkflow.h"
#include "ComfyUIUtils.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>
#include <Qt>

namespace ComfyLiveRunnerInternal {

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
