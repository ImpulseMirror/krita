/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <KSharedConfig>
#include <KConfigGroup>

#include <thread>

namespace ComfyUIUtils {

// §13.135: Strip whole lines whose stripped form starts with "//" (for settings/workflow JSON; does not handle #)
QByteArray stripJsonLineComments(QByteArray data)
{
    QList<QByteArray> out;
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (!line.trimmed().startsWith("//"))
            out.append(line);
    }
    return out.join('\n');
}
// §3.1: Settings path → user_data_dir/settings.json; load/save with // line comments
QString settingsFilePath()
{
    return pluginUserDataDir() + QStringLiteral("/settings.json");
}

QJsonObject loadSettingsJson()
{
    QFile f(settingsFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject();
    QByteArray data = stripJsonLineComments(f.readAll());
    f.close();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QJsonObject();
    return doc.object();
}

QString savedServerUrl()
{
    const QJsonObject settings = loadSettingsJson();
    if (settings.contains(QStringLiteral("server_url"))) {
        const QString fromJson = settings.value(QStringLiteral("server_url")).toString().trimmed();
        if (!fromJson.isEmpty())
            return fromJson;
    }
    const KConfigGroup cfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));
    return cfg.readEntry(QStringLiteral("ServerUrl"), QString()).trimmed();
}

bool saveSettingsJson(const QJsonObject &obj)
{
    QSaveFile f(settingsFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return f.commit();
}

void dumpComfyPromptPayloadIfEnabled(const QJsonObject &payload)
{
    if (!loadSettingsJson().value(QStringLiteral("debug_dump_workflow")).toBool(false)
        && !loadSettingsJson().value(QStringLiteral("dump_workflow")).toBool(false))
        return;
    const QString dir = pluginLogDir();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/last_comfy_prompt.json");
    const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Indented);
    // §4.8: When multi-threading is on, avoid blocking the UI thread on very large prompt dumps.
    constexpr int kBackgroundDumpMinBytes = 512 * 1024;
    if (multiThreadingEnabled() && json.size() >= kBackgroundDumpMinBytes) {
        std::thread([path, json]() {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(json);
        }).detach();
        return;
    }
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(json);
}
} // namespace ComfyUIUtils
