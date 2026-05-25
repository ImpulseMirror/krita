/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <algorithm>

namespace {
QString normalizeLanguageId(QString id)
{
    id = id.trimmed().toLower();
    id.replace(QLatin1Char('_'), QLatin1Char('-'));
    return id;
}

void mergeTranslationsFromObject(QHash<QString, QString> *out, const QJsonObject &translations)
{
    if (!out)
        return;
    for (auto it = translations.constBegin(); it != translations.constEnd(); ++it) {
        const QJsonValue v = it.value();
        if (!v.isString())
            continue;
        const QString s = v.toString();
        if (s.isEmpty())
            continue;
        out->insert(it.key(), s);
    }
}

bool parseLanguageFile(const QString &filePath, QString *outId, QString *outName, QHash<QString, QString> *outTranslations)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (outId)
        *outId = root.value(QStringLiteral("id")).toString();
    if (outName)
        *outName = root.value(QStringLiteral("name")).toString();
    if (outTranslations)
        mergeTranslationsFromObject(outTranslations, root.value(QStringLiteral("translations")).toObject());
    return true;
}
} // namespace

ComfyLocalization &ComfyLocalization::instance()
{
    static ComfyLocalization s;
    return s;
}

ComfyLocalization::ComfyLocalization() = default;

QString ComfyLocalization::readLanguageSettingId()
{
    const QJsonObject s = ComfyUIUtils::loadSettingsJson();
    QString id = s.value(QStringLiteral("interface_language")).toString();
    if (id.isEmpty())
        id = s.value(QStringLiteral("language")).toString();
    if (id.isEmpty())
        id = QStringLiteral("en");
    return normalizeLanguageId(id);
}

void ComfyLocalization::scanAvailable()
{
    m_available.clear();
    QSet<QString> seen;

    const auto scanDir = [this, &seen](const QString &dirPath) {
        QDir dir(dirPath);
        if (!dir.exists())
            return;
        const QStringList files = dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &fn : files) {
            const QString path = dir.absoluteFilePath(fn);
            QString id;
            QString name;
            if (!parseLanguageFile(path, &id, &name, nullptr))
                continue;
            id = normalizeLanguageId(id);
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen.insert(id);
            ComfyLanguageInfo info;
            info.id = id;
            info.name = name.isEmpty() ? id : name;
            info.filePath = path;
            m_available.append(info);
        }
    };

    ComfyUIUtils::ensureBundledPluginDataInstalled();
    scanDir(ComfyUIUtils::pluginInstallDataDir() + QStringLiteral("/language"));
    scanDir(ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/language"));

    std::sort(m_available.begin(), m_available.end(), [](const ComfyLanguageInfo &a, const ComfyLanguageInfo &b) {
        if (a.id == QLatin1String("en"))
            return true;
        if (b.id == QLatin1String("en"))
            return false;
        return a.name.localeAwareCompare(b.name) < 0;
    });
}

void ComfyLocalization::init()
{
    scanAvailable();
    const QString wantId = readLanguageSettingId();
    for (const ComfyLanguageInfo &lang : m_available) {
        if (lang.id == wantId) {
            loadFromFile(lang.id, lang.filePath);
            return;
        }
    }
    for (const ComfyLanguageInfo &lang : m_available) {
        if (lang.id == QLatin1String("en")) {
            loadFromFile(lang.id, lang.filePath);
            return;
        }
    }
    m_id = QStringLiteral("en");
    m_name = QStringLiteral("English");
    m_translations.clear();
}

bool ComfyLocalization::loadFromFile(const QString &id, const QString &filePath)
{
    QString parsedId;
    QString parsedName;
    QHash<QString, QString> map;
    if (!parseLanguageFile(filePath, &parsedId, &parsedName, &map)) {
        m_id = QStringLiteral("en");
        m_name = QStringLiteral("English");
        m_translations.clear();
        return false;
    }
    m_id = normalizeLanguageId(parsedId.isEmpty() ? id : parsedId);
    m_name = parsedName.isEmpty() ? m_id : parsedName;
    m_translations = map;
    return true;
}

void ComfyLocalization::loadLanguageForTest(const QString &id, const QString &name, const QHash<QString, QString> &translations)
{
    m_id = normalizeLanguageId(id);
    m_name = name;
    m_translations = translations;
}

QString ComfyLocalization::translate(const QString &key) const
{
    if (m_id == QLatin1String("en"))
        return key;
    const auto it = m_translations.constFind(key);
    if (it == m_translations.constEnd())
        return key;
    return *it;
}

QString ComfyLocalization::translate(const QString &key, const QString &arg1) const
{
    return translate(key).arg(arg1);
}

QString ComfyLocalization::translate(const QString &key, const QString &arg1, const QString &arg2) const
{
    return translate(key).arg(arg1, arg2);
}

QString ComfyLocalization::translate(const QString &key, const QString &arg1, const QString &arg2, const QString &arg3) const
{
    return translate(key).arg(arg1, arg2, arg3);
}
