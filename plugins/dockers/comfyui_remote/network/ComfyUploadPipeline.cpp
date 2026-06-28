/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUploadPipeline.h"

#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QUrl>

namespace ComfyUploadPipeline {

QStringList collectMissingLoraUploadPaths(const QStringList &serverLoraFilenames)
{
    QStringList paths;
    ComfyFileLibrary::instance().init();
    for (const ComfyFileRecord *rec :
         ComfyFileLibrary::instance().localLorasMissingOnServer(serverLoraFilenames)) {
        if (rec && !rec->path.isEmpty())
            paths.append(rec->path);
    }
    return paths;
}

QImage prepareControlLayerImageForUpload(KisImageSP image,
                                         const ComfyControlLayerEntry &entry,
                                         const QSize &targetSize)
{
    if (!image)
        return QImage();
    QImage img = ComfyUIUtils::getLayerProjectionAsQImage(image, entry.layerName);
    if (img.isNull())
        return img;
    if (targetSize.isValid() && (img.width() != targetSize.width() || img.height() != targetSize.height()))
        img = img.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (ComfyUIUtils::isControlModeLines(entry.mode)) {
        img = img.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); y++) {
            for (int x = 0; x < img.width(); x++) {
                const QRgb px = img.pixel(x, y);
                const int a = qAlpha(px);
                img.setPixel(x, y, a > 0 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
            }
        }
    }
    return img;
}

QList<ImageItem> buildControlUploadItems(KisImageSP image,
                                         const QList<ComfyControlLayerEntry> &layers,
                                         const ControlUploadOptions &options)
{
    QList<ImageItem> items;
    for (const ComfyControlLayerEntry &entry : layers) {
        if (!ComfyControlLayer::needsGenerateUpload(entry))
            continue;
        ImageItem item;
        item.statusText = ComfyTr::tr("Uploading control layer %1…", entry.layerName);
        item.filenameHint =
            QStringLiteral("%1%2.png").arg(options.filenamePrefix).arg(items.size());
        item.errorContext = entry.layerName;
        item.prepareImage = [image, entry, targetSize = options.targetSize]() {
            return prepareControlLayerImageForUpload(image, entry, targetSize);
        };
        items.append(item);
    }
    return items;
}

Run::Run(QNetworkAccessManager *nam, QObject *parent)
    : QObject(parent)
    , m_nam(nam)
{
}

void Run::cancel()
{
    m_active = false;
}

bool Run::checkShouldContinue()
{
    if (m_handlers.shouldContinue && !m_handlers.shouldContinue()) {
        finishCancelled();
        return false;
    }
    return true;
}

void Run::start(const QString &serverBaseUrl,
                const QStringList &loraPaths,
                const QList<ImageItem> &images,
                Handlers handlers,
                QStringList *serverLoraFilenamesInOut)
{
    m_serverBaseUrl = serverBaseUrl.trimmed();
    m_loraPaths = loraPaths;
    m_images = images;
    m_handlers = std::move(handlers);
    m_serverLoraFilenames = serverLoraFilenamesInOut;
    m_loraIndex = 0;
    m_imageIndex = 0;
    m_uploadedImageNames.clear();
    m_active = true;

    if (!m_nam) {
        finishError(ComfyTr::tr("Not connected to server."));
        return;
    }
    if (m_serverBaseUrl.isEmpty()) {
        finishError(ComfyTr::tr("Enter a server URL first."));
        return;
    }
    if (!checkShouldContinue())
        return;

    runNextLora();
}

void Run::runNextLora()
{
    if (!m_active || !checkShouldContinue())
        return;

    while (m_loraIndex < m_loraPaths.size()) {
        const QString path = m_loraPaths.at(m_loraIndex++);
        const QString baseName = QFileInfo(path).fileName();
        if (baseName.isEmpty() || !QFile::exists(path))
            continue;

        if (m_handlers.setStatusText)
            m_handlers.setStatusText(ComfyTr::tr("Uploading LoRA %1…", baseName));
        if (m_handlers.setProgressKind)
            m_handlers.setProgressKind(true);

        QNetworkReply *reply =
            ComfyUIUtils::tryUploadLoraFileViaEtnApi(m_nam, m_serverBaseUrl, path, this);
        if (!reply) {
            finishError(ComfyTr::tr("Could not read LoRA file %1 for upload.", baseName));
            return;
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, baseName]() {
            reply->deleteLater();
            if (!m_active || !checkShouldContinue())
                return;

            if (m_handlers.setProgressKind)
                m_handlers.setProgressKind(false);

            const QVariant codeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const int code = codeVar.isValid() ? codeVar.toInt() : 0;
            const bool ok =
                (reply->error() == QNetworkReply::NoError && (code == 200 || code == 201 || code == 204));
            if (!ok) {
                const QString codeStr = code > 0 ? QString::number(code) : QStringLiteral("—");
                finishError(ComfyTr::tr(
                    "LoRA upload failed for %1 (HTTP %2). Install the file on the server or use Styles → Upload.",
                    baseName,
                    codeStr));
                return;
            }

            if (m_serverLoraFilenames
                && !m_serverLoraFilenames->contains(baseName, Qt::CaseInsensitive)) {
                m_serverLoraFilenames->append(baseName);
                m_serverLoraFilenames->sort(Qt::CaseInsensitive);
            }
            if (m_handlers.onLoraUploaded)
                m_handlers.onLoraUploaded(baseName);

            runNextLora();
        });
        return;
    }

    runNextImage();
}

void Run::runNextImage()
{
    if (!m_active || !checkShouldContinue())
        return;

    while (m_imageIndex < m_images.size()) {
        const ImageItem item = m_images.at(m_imageIndex++);
        const QImage img = item.prepareImage ? item.prepareImage() : QImage();
        if (img.isNull()) {
            const QString ctx = item.errorContext.trimmed();
            if (!ctx.isEmpty())
                finishError(ComfyTr::tr("Could not export control layer \"%1\".", ctx));
            else
                finishError(ComfyTr::tr("Could not prepare image for upload."));
            return;
        }

        QUrl uploadUrl =
            ComfyUIUtils::comfyResolveApiUrl(m_serverBaseUrl, QStringLiteral("upload/image"));
        if (!uploadUrl.isValid()) {
            finishError(ComfyTr::tr("Invalid server URL."));
            return;
        }

        auto *tmp = new QTemporaryFile(this);
        tmp->setFileTemplate(tmp->fileTemplate() + QStringLiteral(".png"));
        tmp->open();
        tmp->close();
        if (!img.save(tmp->fileName())) {
            finishError(ComfyTr::tr("Could not save image for upload."));
            return;
        }

        if (m_handlers.setStatusText && !item.statusText.isEmpty())
            m_handlers.setStatusText(item.statusText);
        if (m_handlers.setProgressKind)
            m_handlers.setProgressKind(true);

        tmp->open();
        auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart part;
        const QString fname =
            item.filenameHint.isEmpty() ? QStringLiteral("krita_upload.png") : item.filenameHint;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"image\"; filename=\"%1\"").arg(fname)));
        part.setBodyDevice(tmp);
        tmp->setParent(multiPart);
        multiPart->append(part);

        const auto onUploaded = item.onUploaded;

        QNetworkRequest reqUp(uploadUrl);
        ComfyUIUtils::setComfyUIRequestHeaders(reqUp);
        QNetworkReply *replyUp = m_nam->post(reqUp, multiPart);
        multiPart->setParent(replyUp);

        connect(replyUp, &QNetworkReply::finished, this, [this, replyUp, onUploaded]() {
            replyUp->deleteLater();
            if (!m_active || !checkShouldContinue())
                return;

            if (m_handlers.setProgressKind)
                m_handlers.setProgressKind(false);

            if (replyUp->error() != QNetworkReply::NoError) {
                finishError(ComfyTr::tr("Image upload error: %1", replyUp->errorString()));
                return;
            }
            const QString name =
                QJsonDocument::fromJson(replyUp->readAll()).object().value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                finishError(ComfyTr::tr("Server did not return image name."));
                return;
            }
            if (onUploaded)
                onUploaded(name);
            m_uploadedImageNames.append(name);
            runNextImage();
        });
        return;
    }

    finishSuccess();
}

void Run::finishSuccess()
{
    if (!m_active)
        return;
    m_active = false;
    if (m_handlers.setProgressKind)
        m_handlers.setProgressKind(false);
    if (m_handlers.onComplete) {
        Result result;
        result.uploadedImageNames = m_uploadedImageNames;
        m_handlers.onComplete(result);
    }
    deleteLater();
}

void Run::finishError(const QString &msg)
{
    if (!m_active)
        return;
    m_active = false;
    if (m_handlers.setProgressKind)
        m_handlers.setProgressKind(false);
    if (m_handlers.setStatusMessage)
        m_handlers.setStatusMessage(msg, true);
    if (m_handlers.onAbort)
        m_handlers.onAbort();
    deleteLater();
}

void Run::finishCancelled()
{
    if (!m_active)
        return;
    m_active = false;
    if (m_handlers.setProgressKind)
        m_handlers.setProgressKind(false);
    if (m_handlers.onCancelled)
        m_handlers.onCancelled();
    else if (m_handlers.onAbort)
        m_handlers.onAbort();
    deleteLater();
}

} // namespace ComfyUploadPipeline
