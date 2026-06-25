/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyStyleCollection.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

QString comfyDefaultStylePrompt()
{
    return QStringLiteral("best quality, highres");
}

QString comfyDefaultNegativeStylePrompt()
{
    return QStringLiteral("bad quality, low resolution, blurry");
}

ComfyStyleCollection &ComfyStyleCollection::instance()
{
    static ComfyStyleCollection inst;
    return inst;
}

ComfyStyleCollection::ComfyStyleCollection()
{
    reload();
}

QString ComfyStyleCollection::builtinStylesDir() const
{
    const QString base = ComfyUIUtils::pluginInstallDataDir();
    if (base.isEmpty())
        return QString();
    return base + QStringLiteral("/styles");
}

QString ComfyStyleCollection::userStylesDir() const
{
    QString path = ComfyUIUtils::pluginUserDataDir() + QStringLiteral("/styles");
    QDir().mkpath(path);
    return path;
}

ComfyStyleEntry ComfyStyleCollection::loadStyleFile(const QString &path, bool isBuiltin) const
{
    ComfyStyleEntry e;
    e.filepath = path;
    e.isBuiltin = isBuiltin;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return e;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(ComfyUIUtils::stripJsonLineComments(f.readAll()), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return e;
    const QJsonObject o = doc.object();
    e.name = o.value(QStringLiteral("name")).toString(QFileInfo(path).completeBaseName());
    e.architecture = o.value(QStringLiteral("architecture")).toString(QStringLiteral("auto"));
    for (const QJsonValue &v : o.value(QStringLiteral("checkpoints")).toArray())
        if (v.isString())
            e.checkpoints << v.toString();
    if (e.checkpoints.isEmpty()) {
        const QString legacy = o.value(QStringLiteral("sd_checkpoint")).toString();
        if (!legacy.isEmpty())
            e.checkpoints << legacy;
    }
    e.loras = o.value(QStringLiteral("loras")).toArray();
    e.stylePrompt = o.value(QStringLiteral("style_prompt")).toString();
    e.negativePrompt = o.value(QStringLiteral("negative_prompt")).toString();
    e.vae = o.value(QStringLiteral("vae")).toString(QStringLiteral("Checkpoint Default"));
    e.clipSkip = o.value(QStringLiteral("clip_skip")).toInt(0);
    e.vPredictionZsnr = o.value(QStringLiteral("v_prediction_zsnr")).toBool(false);
    e.rescaleCfg = o.value(QStringLiteral("rescale_cfg")).toDouble(0.7);
    e.selfAttentionGuidance = o.value(QStringLiteral("self_attention_guidance")).toBool(false);
    e.preferredResolution = o.value(QStringLiteral("preferred_resolution")).toInt(0);
    e.linkedEditStyle = o.value(QStringLiteral("linked_edit_style")).toString();
    e.samplerPresetName = o.value(QStringLiteral("sampler")).toString();
    e.samplerSteps = o.value(QStringLiteral("sampler_steps")).toInt(20);
    e.cfgScale = o.value(QStringLiteral("cfg_scale")).toDouble(7.0);
    e.liveSamplerPresetName = o.value(QStringLiteral("live_sampler")).toString();
    e.liveSamplerSteps = o.value(QStringLiteral("live_sampler_steps")).toInt(6);
    e.liveCfgScale = o.value(QStringLiteral("live_cfg_scale")).toDouble(1.8);

    if (isBuiltin)
        e.styleId = QStringLiteral("built-in/") + QFileInfo(path).fileName();
    else {
        const QString rel = QDir(userStylesDir()).relativeFilePath(path);
        e.styleId = rel.isEmpty() ? QFileInfo(path).fileName() : rel;
    }
    return e;
}

void ComfyStyleCollection::reload()
{
    m_styles.clear();
    const QString builtinDir = builtinStylesDir();
    if (!builtinDir.isEmpty()) {
        QDir dir(builtinDir);
        const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
        for (const QString &fn : files) {
            ComfyStyleEntry e = loadStyleFile(dir.absoluteFilePath(fn), true);
            if (!e.name.isEmpty())
                m_styles.append(e);
        }
    }
    QDir userDir(userStylesDir());
    const QFileInfoList userFiles =
        userDir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
    for (const QFileInfo &fi : userFiles) {
        ComfyStyleEntry e = loadStyleFile(fi.absoluteFilePath(), false);
        if (!e.name.isEmpty())
            m_styles.append(e);
    }
    std::sort(m_styles.begin(), m_styles.end(), [](const ComfyStyleEntry &a, const ComfyStyleEntry &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
}

QList<const ComfyStyleEntry *> ComfyStyleCollection::filtered(bool showBuiltin) const
{
    QList<const ComfyStyleEntry *> out;
    for (const ComfyStyleEntry &s : m_styles) {
        if (showBuiltin || !s.isBuiltin)
            out.append(&s);
    }
    return out;
}

const ComfyStyleEntry *ComfyStyleCollection::findByStyleId(const QString &styleId) const
{
    const QString id = styleId.trimmed();
    if (id.isEmpty() || id == QLatin1String("none"))
        return nullptr;
    for (const ComfyStyleEntry &s : m_styles) {
        if (s.styleId == id)
            return &s;
    }
    // Legacy ids from old C++ port
    if (id == QLatin1String("portrait"))
        return findByStyleId(QStringLiteral("built-in/cinematic-photo.json"));
    if (id == QLatin1String("landscape"))
        return findByStyleId(QStringLiteral("built-in/cinematic-photo-xl.json"));
    if (id == QLatin1String("anime"))
        return findByStyleId(QStringLiteral("built-in/anime-illustrious.json"));
    if (id == QLatin1String("realistic"))
        return findByStyleId(QStringLiteral("built-in/digital-artwork.json"));
    return nullptr;
}

QJsonObject ComfyStyleCollection::entryToJson(const ComfyStyleEntry &entry) const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), entry.name);
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("architecture"), entry.architecture);
    QJsonArray ckpts;
    for (const QString &c : entry.checkpoints)
        ckpts.append(c);
    o.insert(QStringLiteral("checkpoints"), ckpts);
    o.insert(QStringLiteral("loras"), entry.loras);
    o.insert(QStringLiteral("style_prompt"), entry.stylePrompt);
    o.insert(QStringLiteral("negative_prompt"), entry.negativePrompt);
    o.insert(QStringLiteral("vae"), entry.vae.isEmpty() ? QStringLiteral("Checkpoint Default") : entry.vae);
    o.insert(QStringLiteral("clip_skip"), entry.clipSkip);
    o.insert(QStringLiteral("v_prediction_zsnr"), entry.vPredictionZsnr);
    o.insert(QStringLiteral("rescale_cfg"), entry.rescaleCfg);
    o.insert(QStringLiteral("self_attention_guidance"), entry.selfAttentionGuidance);
    o.insert(QStringLiteral("preferred_resolution"), entry.preferredResolution);
    if (!entry.linkedEditStyle.isEmpty())
        o.insert(QStringLiteral("linked_edit_style"), entry.linkedEditStyle);
    if (!entry.samplerPresetName.isEmpty())
        o.insert(QStringLiteral("sampler"), entry.samplerPresetName);
    o.insert(QStringLiteral("sampler_steps"), entry.samplerSteps);
    o.insert(QStringLiteral("cfg_scale"), entry.cfgScale);
    if (!entry.liveSamplerPresetName.isEmpty())
        o.insert(QStringLiteral("live_sampler"), entry.liveSamplerPresetName);
    o.insert(QStringLiteral("live_sampler_steps"), entry.liveSamplerSteps);
    o.insert(QStringLiteral("live_cfg_scale"), entry.liveCfgScale);
    return o;
}

QString ComfyStyleCollection::saveEntryToUserStyles(const ComfyStyleEntry &entry)
{
    const QString baseName = QFileInfo(entry.filepath).fileName();
    if (baseName.isEmpty())
        return QString();
    const QString dest = userStylesDir() + QLatin1Char('/') + baseName;
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QString();
    f.write(QJsonDocument(entryToJson(entry)).toJson(QJsonDocument::Indented));
    f.close();
    reload();
    return dest;
}

QString ComfyStyleCollection::comboDisplayName(const ComfyStyleEntry &entry)
{
    return QStringLiteral("%1 (%2)").arg(entry.name, entry.styleId);
}

QString ComfyStyleCollection::createStyle(const QString &checkpoint, const QString &copyFromStyleId)
{
    ComfyStyleEntry e;
    if (!copyFromStyleId.trimmed().isEmpty()) {
        if (const ComfyStyleEntry *src = findByStyleId(copyFromStyleId)) {
            e = *src;
            e.name = QStringLiteral("%1 (%2)").arg(src->name, ComfyTr::tr("Copy"));
        }
    } else {
        e.name = ComfyTr::tr("New Style");
        e.stylePrompt = comfyDefaultStylePrompt();
        e.negativePrompt = comfyDefaultNegativeStylePrompt();
    }
    e.isBuiltin = false;
        e.checkpoints = QStringList{checkpoint.trimmed()};

    QDir dir(userStylesDir());
    QString filename = QStringLiteral("style.json");
    int n = 0;
    while (QFile::exists(dir.filePath(filename))) {
        ++n;
        filename = QStringLiteral("style-%1.json").arg(n);
    }
    e.filepath = dir.filePath(filename);
    e.styleId = filename;
    if (saveEntryToUserStyles(e).isEmpty())
        return QString();
    return e.styleId;
}

bool ComfyStyleCollection::deleteUserStyle(const QString &styleId)
{
    const ComfyStyleEntry *st = findByStyleId(styleId);
    if (!st || st->isBuiltin)
        return false;
    const QString path = userStylesDir() + QLatin1Char('/') + QFileInfo(st->filepath).fileName();
    if (!QFile::remove(path))
        return false;
    reload();
    return true;
}

bool ComfyStyleCollection::renameStyle(const QString &styleId, const QString &newName)
{
    const ComfyStyleEntry *st = findByStyleId(styleId);
    if (!st || st->isBuiltin || newName.trimmed().isEmpty())
        return false;
    ComfyStyleEntry e = *st;
    e.name = newName.trimmed();
    return !saveEntryToUserStyles(e).isEmpty();
}

const ComfyStyleEntry *ComfyStyleCollection::findByComboIndex(int comboIndex,
                                                               bool showBuiltin,
                                                               int legacyKConfigCustomCount) const
{
    if (comboIndex <= 0)
        return nullptr;
    const QList<const ComfyStyleEntry *> styles = filtered(showBuiltin);
    const int styleCount = styles.size();
    if (comboIndex <= styleCount)
        return styles.at(comboIndex - 1);
    return nullptr; // legacy KConfig custom handled separately by dock
}
