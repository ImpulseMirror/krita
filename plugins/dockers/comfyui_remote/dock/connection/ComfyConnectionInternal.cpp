/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyConnectionInternal.h"

#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"

#include <QLabel>
#include <QSet>

namespace ComfyConnectionInternal {

const QStringList &requiredObjectInfoNodes()
{
    static const QStringList list = {
        QStringLiteral("CheckpointLoaderSimple"),
        QStringLiteral("KSampler"),
        QStringLiteral("VAEDecode"),
        QStringLiteral("VAEEncodeForInpaint"),
    };
    return list;
}

static QString missingCustomNodeListItemHtml(const QString &classType)
{
    const QString href = QStringLiteral("https://github.com/comfyanonymous/ComfyUI");
    return QLatin1String("<li>") + classType.toHtmlEscaped() + QLatin1String(" — <a href=\"") + href + QLatin1String("\">")
        + ComfyTr::tr("ComfyUI core / extensions").toHtmlEscaped() + QLatin1String("</a></li>");
}

QString buildMissingNodesListFormat(const QStringList &missingNodes)
{
    QString html;
    html += QLatin1String("<p><b>") + ComfyTr::tr("The following ComfyUI custom nodes are missing or too old") + QLatin1String("</b></p><ul>");
    for (const QString &name : missingNodes)
        html += missingCustomNodeListItemHtml(name);
    html += QLatin1String("</ul><p>") + ComfyTr::tr("Please install or update the custom node package (e.g. ComfyUI Manager or the node's repository).") + QLatin1String("</p>");
    html += QLatin1String("<p>") + ComfyTr::tr("If nodes are still missing, check the ComfyUI output at startup for errors.") + QLatin1String("</p>");
    return html;
}

QString buildMissingResourcesDictFormatHtml(const QStringList &checkpointNames)
{
    QSet<QString> present;
    for (const QString &n : checkpointNames) {
        const QString a = ComfyUIUtils::classifyCheckpointArch(n);
        if (a != QLatin1String("unknown"))
            present.insert(a);
    }
    static const struct {
        const char *classifierKey;
        const char *displayArch;
    } rows[] = {
        {"sd15", "sd15"},
        {"sdxl", "sdxl"},
        {"flux", "flux"},
        {"flux_k", "flux_k"},
        {"flux2_4b", "flux2_4b"},
        {"qwen_e", "qwen"},
    };
    QString html;
    if (checkpointNames.isEmpty()) {
        html += QLatin1String("<p><b>") + ComfyTr::tr("Missing common models (required):") + QLatin1String("</b></p><ul>");
        html += QLatin1String("<li>") + ComfyTr::tr("Checkpoints: server returned no ckpt_name entries in object_info (install models or fix CheckpointLoaderSimple).").toHtmlEscaped()
            + QLatin1String("</li>");
        html += QLatin1String("</ul>");
    }
    html += QLatin1String("<p><b>") + ComfyTr::tr("Detected base models:") + QLatin1String("</b></p><ul>");
    for (const auto &row : rows) {
        const QString key = QString::fromLatin1(row.classifierKey);
        const QString label = QString::fromLatin1(row.displayArch);
        const bool ok = present.contains(key);
        html += QLatin1String("<li><b>") + label.toHtmlEscaped() + QLatin1String("</b>: ");
        if (ok)
            html += ComfyTr::tr("supported").toHtmlEscaped();
        else
            html += ComfyTr::tr("missing %1", QString::fromLatin1(row.displayArch)).toHtmlEscaped();
        html += QLatin1String("</li>");
    }
    html += QLatin1String("</ul>");
    html += QLatin1String("<p>")
        + ComfyTr::tr("Install the required custom nodes and models on your ComfyUI server. Check the client.log file for more details.")
        + QLatin1String("</p>");
    return html;
}

void syncDetectedModelsLabel(ComfyUIRemoteDock::Private *d)
{
    if (!d || !d->labelDetectedModels)
        return;
    if (!d->isConnected || d->lastObjectInfoRoot.isEmpty()) {
        d->labelDetectedModels->setText(
            ComfyTr::tr("Connect to server to see detected architectures (SD 1.5, SD XL, Flux, etc.)."));
        d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
        d->labelDetectedModels->setTextFormat(Qt::PlainText);
        return;
    }
    QStringList missing;
    for (const QString &key : requiredObjectInfoNodes()) {
        if (!d->lastObjectInfoRoot.contains(key))
            missing << key;
    }
    if (!missing.isEmpty()) {
        d->labelDetectedModels->setTextFormat(Qt::RichText);
        d->labelDetectedModels->setText(buildMissingNodesListFormat(missing));
        d->labelDetectedModels->setStyleSheet(QString());
        d->labelDetectedModels->setOpenExternalLinks(true);
        d->labelDetectedModels->setTextInteractionFlags(Qt::TextBrowserInteraction);
        return;
    }
    const QStringList ckptNames = ComfyUIUtils::parseCheckpointNamesFromObjectInfoRoot(d->lastObjectInfoRoot);
    d->labelDetectedModels->setTextFormat(Qt::RichText);
    d->labelDetectedModels->setText(buildMissingResourcesDictFormatHtml(ckptNames));
    d->labelDetectedModels->setStyleSheet(QStringLiteral("color: gray;"));
    d->labelDetectedModels->setOpenExternalLinks(true);
    d->labelDetectedModels->setTextInteractionFlags(Qt::TextBrowserInteraction);
}

} // namespace ComfyConnectionInternal
