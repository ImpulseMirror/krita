/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_UPLOAD_PIPELINE_H_
#define COMFY_UPLOAD_PIPELINE_H_

#include "ComfyControlLayer.h"

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>

#include <kis_types.h>

class QNetworkAccessManager;

namespace ComfyUploadPipeline {

/// One PNG upload step (control layer, region mask, canvas, …).
struct ImageItem {
    QString statusText;
    QString filenameHint;
    /// Shown in error messages when prepareImage returns null.
    QString errorContext;
    std::function<QImage()> prepareImage;
    /// Optional — e.g. assign regional mask name by index.
    std::function<void(const QString &serverName)> onUploaded;
};

struct Result {
    QStringList uploadedImageNames;
};

struct Handlers {
    std::function<void(bool isUpload)> setProgressKind;
    std::function<void(const QString &text)> setStatusText;
    std::function<void(const QString &msg, bool isError)> setStatusMessage;
    /// After each LoRA PUT succeeds — dock updates remote filename cache.
    std::function<void(const QString &loraBaseName)> onLoraUploaded;
    std::function<void(const Result &result)> onComplete;
    std::function<void()> onAbort;
    /// When false, pipeline stops without error (Live mode unchecked).
    std::function<bool()> shouldContinue;
    std::function<void()> onCancelled;
};

struct ControlUploadOptions {
    QString filenamePrefix = QStringLiteral("control_");
    QSize targetSize;
};

/// Local LoRA files missing on the connected server.
QStringList collectMissingLoraUploadPaths(const QStringList &serverLoraFilenames,
                                          const QStringList &extraLoraNames = {});

QImage prepareControlLayerImageForUpload(KisImageSP image,
                                         const ComfyControlLayerEntry &entry,
                                         const QSize &targetSize = QSize());

QList<ImageItem> buildControlUploadItems(KisImageSP image,
                                         const QList<ComfyControlLayerEntry> &layers,
                                         const ControlUploadOptions &options = ControlUploadOptions());

/// Sequential LoRA uploads, then optional PNG `/upload/image` steps.
class Run : public QObject
{
    Q_OBJECT
public:
    explicit Run(QNetworkAccessManager *nam, QObject *parent = nullptr);

    /// `serverLoraFilenamesInOut` optional; appended on each successful LoRA upload.
    void start(const QString &serverBaseUrl,
               const QStringList &loraPaths,
               const QList<ImageItem> &images,
               Handlers handlers,
               QStringList *serverLoraFilenamesInOut = nullptr);

    void cancel();

private:
    bool checkShouldContinue();
    void runNextLora();
    void runNextImage();
    void finishSuccess();
    void finishError(const QString &msg);
    void finishCancelled();

    QNetworkAccessManager *m_nam = nullptr;
    QString m_serverBaseUrl;
    QStringList m_loraPaths;
    QList<ImageItem> m_images;
    Handlers m_handlers;
    QStringList *m_serverLoraFilenames = nullptr;

    int m_loraIndex = 0;
    int m_imageIndex = 0;
    QStringList m_uploadedImageNames;
    bool m_active = false;
};

} // namespace ComfyUploadPipeline

#endif
