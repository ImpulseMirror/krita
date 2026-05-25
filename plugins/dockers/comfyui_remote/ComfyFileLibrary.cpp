/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyFileLibrary.h"
#include "ComfyUIUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

Q_DECLARE_OPERATORS_FOR_FLAGS(ComfyFileSources)

namespace {

QString displayNameFromId(const QString &id)
{
    const int dot = id.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? id : id.left(dot);
}

int sourceToInt(ComfyFileSources s)
{
    return static_cast<int>(s);
}

ComfyFileSources sourceFromInt(int v)
{
    return static_cast<ComfyFileSources>(v);
}

QString normalizePathKey(const QString &p)
{
    return p.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/')).toLower();
}

} // namespace

namespace ComfyFileLibraryUtil {

QString sha256Base64OfFile(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash sha(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(4096);
        if (chunk.isEmpty() && f.atEnd())
            break;
        sha.addData(chunk);
    }
    return QString::fromLatin1(sha.result().toBase64());
}

} // namespace ComfyFileLibraryUtil

ComfyFileRecord ComfyFileRecord::remote(const QString &id, ComfyFileFormat format)
{
    ComfyFileRecord f;
    f.id = id;
    f.name = displayNameFromId(id.replace(QLatin1Char('\\'), QLatin1Char('/')));
    f.source = ComfyFileSourceRemote;
    f.format = format;
    return f;
}

ComfyFileRecord ComfyFileRecord::local(const QString &filePath, ComfyFileFormat format, bool computeHash)
{
    const QFileInfo info(filePath);
    ComfyFileRecord f;
    f.id = info.fileName();
    f.name = info.completeBaseName();
    f.source = ComfyFileSourceLocal;
    f.format = format;
    f.path = info.absoluteFilePath();
    if (info.exists())
        f.size = info.size();
    if (computeHash)
        f.computeHash();
    return f;
}

ComfyFileRecord ComfyFileRecord::fromJson(const QJsonObject &o)
{
    ComfyFileRecord f;
    f.id = o.value(QStringLiteral("id")).toString();
    if (f.id.isEmpty())
        f.id = o.value(QStringLiteral("filename")).toString();
    f.name = o.value(QStringLiteral("name")).toString();
    if (f.name.isEmpty())
        f.name = displayNameFromId(f.id);
    f.source = sourceFromInt(o.value(QStringLiteral("source")).toInt(0));
    f.format = static_cast<ComfyFileFormat>(o.value(QStringLiteral("format")).toInt(0));
    f.hash = o.value(QStringLiteral("hash")).toString();
    f.path = o.value(QStringLiteral("path")).toString();
    f.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(0));
    if (o.contains(QStringLiteral("metadata")) && o.value(QStringLiteral("metadata")).isObject())
        f.metadata = o.value(QStringLiteral("metadata")).toObject();
    if (o.contains(QStringLiteral("strength_percent")))
        f.setMeta(QStringLiteral("strength_percent"), o.value(QStringLiteral("strength_percent")).toInt(100));
    if (o.contains(QStringLiteral("enabled")))
        f.setMeta(QStringLiteral("enabled"), o.value(QStringLiteral("enabled")).toBool(true));
    return f;
}

QJsonObject ComfyFileRecord::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("filename"), id);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("source"), sourceToInt(source));
    o.insert(QStringLiteral("format"), static_cast<int>(format));
    if (!hash.isEmpty())
        o.insert(QStringLiteral("hash"), hash);
    if (!path.isEmpty())
        o.insert(QStringLiteral("path"), path);
    if (size > 0)
        o.insert(QStringLiteral("size"), static_cast<double>(size));
    if (!metadata.isEmpty())
        o.insert(QStringLiteral("metadata"), metadata);
    const QJsonValue strength = meta(QStringLiteral("strength_percent"));
    if (strength.isDouble() || strength.isString())
        o.insert(QStringLiteral("strength_percent"), strength.toInt(100));
    const QJsonValue enabled = meta(QStringLiteral("enabled"));
    if (!enabled.isUndefined())
        o.insert(QStringLiteral("enabled"), enabled.toBool(true));
    return o;
}

bool ComfyFileRecord::mergeFrom(const ComfyFileRecord &other)
{
    if (id != other.id)
        return false;
    const ComfyFileSources oldSource = source;
    const QString oldHash = hash;
    const QString oldPath = path;
    source = source | other.source;
    if (!other.hash.isEmpty())
        hash = other.hash;
    if (!other.path.isEmpty())
        path = other.path;
    if (other.size > 0)
        size = other.size;
    if (!other.name.isEmpty())
        name = other.name;
    if (other.format != ComfyFileFormat::Unknown)
        format = other.format;
    return source != oldSource || hash != oldHash || path != oldPath;
}

QString ComfyFileRecord::computeHash()
{
    if (!hash.isEmpty())
        return hash;
    if (path.isEmpty())
        return QString();
    hash = ComfyFileLibraryUtil::sha256Base64OfFile(path);
    return hash;
}

QJsonValue ComfyFileRecord::meta(const QString &key) const
{
    return metadata.value(key);
}

void ComfyFileRecord::setMeta(const QString &key, const QJsonValue &value)
{
    metadata.insert(key, value);
}

ComfyFileCollection::ComfyFileCollection(const QString &databasePath)
    : m_databasePath(databasePath)
{
}

void ComfyFileCollection::load()
{
    if (m_databasePath.isEmpty() || !QFile::exists(m_databasePath))
        return;
    QFile f(m_databasePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QByteArray data = ComfyUIUtils::stripJsonLineComments(f.readAll());
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
        return;
    QList<ComfyFileRecord> parsed;
    if (doc.isArray()) {
        for (const QJsonValue &v : doc.array()) {
            if (v.isObject())
                parsed.append(ComfyFileRecord::fromJson(v.toObject()));
        }
    } else if (doc.isObject()) {
        const QJsonArray nested = doc.object().value(QStringLiteral("loras")).toArray();
        for (const QJsonValue &v : nested) {
            if (v.isObject())
                parsed.append(ComfyFileRecord::fromJson(v.toObject()));
        }
    }
    extend(parsed);
    removeMissingLocalFiles();
}

void ComfyFileCollection::save() const
{
    if (m_databasePath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_databasePath).absolutePath());
    QJsonArray arr;
    for (const ComfyFileRecord &file : m_files)
        arr.append(file.toJson());
    QSaveFile f(m_databasePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.commit();
}

void ComfyFileCollection::extend(const QList<ComfyFileRecord> &input)
{
    if (input.isEmpty())
        return;
    QHash<QString, int> indexById;
    for (int i = 0; i < m_files.size(); ++i)
        indexById.insert(m_files.at(i).id, i);
    for (const ComfyFileRecord &f : input) {
        const auto it = indexById.constFind(f.id);
        if (it != indexById.constEnd())
            m_files[it.value()].mergeFrom(f);
        else {
            indexById.insert(f.id, m_files.size());
            m_files.append(f);
        }
    }
    save();
}

void ComfyFileCollection::update(const QList<ComfyFileRecord> &newFiles, ComfyFileSourceFlag sourceFlag)
{
    QSet<QString> newIds;
    for (const ComfyFileRecord &f : newFiles)
        newIds.insert(f.id);
    for (ComfyFileRecord &f : m_files) {
        if ((f.source & sourceFlag) && !newIds.contains(f.id))
            f.source = f.source & ~ComfyFileSourceFlag(sourceFlag);
    }
    extend(newFiles);
}

void ComfyFileCollection::add(const ComfyFileRecord &file)
{
    extend({file});
}

void ComfyFileCollection::replaceAll(const QList<ComfyFileRecord> &files)
{
    m_files = files;
    save();
}

const ComfyFileRecord *ComfyFileCollection::find(const QString &id) const
{
    for (const ComfyFileRecord &f : m_files) {
        if (f.id == id)
            return &f;
    }
    return nullptr;
}

const ComfyFileRecord *ComfyFileCollection::findLocal(const QString &id) const
{
    for (const ComfyFileRecord &f : m_files) {
        if (f.id == id && (f.source & ComfyFileSourceLocal))
            return &f;
    }
    return nullptr;
}

int ComfyFileCollection::findIndex(const QString &id) const
{
    for (int i = 0; i < m_files.size(); ++i) {
        if (m_files.at(i).id == id)
            return i;
    }
    return -1;
}

void ComfyFileCollection::setMeta(ComfyFileRecord *file, const QString &key, const QJsonValue &value)
{
    if (!file)
        return;
    file->setMeta(key, value);
    save();
}

void ComfyFileCollection::removeMissingLocalFiles()
{
    for (int i = m_files.size() - 1; i >= 0; --i) {
        const ComfyFileRecord &f = m_files.at(i);
        if ((f.source & ComfyFileSourceLocal) && !f.path.isEmpty() && !QFile::exists(f.path))
            m_files.removeAt(i);
    }
}

ComfyFileLibrary &ComfyFileLibrary::instance()
{
    static ComfyFileLibrary s;
    return s;
}

ComfyFileLibrary::ComfyFileLibrary()
    : m_loras(ComfyUIUtils::lorasJsonPath())
{
}

void ComfyFileLibrary::init()
{
    if (m_initialized)
        return;
    m_loras.load();
    m_initialized = true;
}

void ComfyFileLibrary::updateRemoteCheckpoints(const QStringList &serverCheckpointFilenames)
{
    QList<ComfyFileRecord> remote;
    remote.reserve(serverCheckpointFilenames.size());
    for (const QString &name : serverCheckpointFilenames) {
        const QString t = name.trimmed();
        if (!t.isEmpty())
            remote.append(ComfyFileRecord::remote(t, ComfyFileFormat::Checkpoint));
    }
    m_checkpoints.update(remote, ComfyFileSourceRemote);
}

void ComfyFileLibrary::updateRemoteLoras(const QStringList &serverLoraFilenames)
{
    QList<ComfyFileRecord> remote;
    remote.reserve(serverLoraFilenames.size());
    for (const QString &name : serverLoraFilenames) {
        const QString t = name.trimmed();
        if (!t.isEmpty())
            remote.append(ComfyFileRecord::remote(t, ComfyFileFormat::Lora));
    }
    m_loras.update(remote, ComfyFileSourceRemote);
}

QString ComfyFileLibrary::preferredCheckpoint(const QStringList &styleCheckpoints,
                                              const QStringList &availableOnServer)
{
    QHash<QString, QString> available;
    for (const QString &cp : availableOnServer) {
        const QString key = normalizePathKey(cp);
        if (!key.isEmpty())
            available.insert(key, cp);
    }
    for (const QString &cp : styleCheckpoints) {
        const auto it = available.constFind(normalizePathKey(cp));
        if (it != available.constEnd())
            return *it;
    }
    return QStringLiteral("not-found");
}

QList<const ComfyFileRecord *> ComfyFileLibrary::localLorasMissingOnServer(const QStringList &serverLoraFilenames) const
{
    QList<const ComfyFileRecord *> out;
    for (const ComfyFileRecord &f : m_loras.files()) {
        if (!(f.source & ComfyFileSourceLocal) || f.path.isEmpty())
            continue;
        if (!f.meta(QStringLiteral("enabled")).toBool(true))
            continue;
        if (!ComfyUIUtils::loraFilenameKnownOnServer(f.id, serverLoraFilenames))
            out.append(&f);
    }
    return out;
}

#ifdef COMFYUI_ENABLE_TEST_HOOKS
void ComfyFileLibrary::resetLorasDatabaseForTests(const QString &databasePath)
{
    m_initialized = false;
    m_loras = ComfyFileCollection(databasePath);
}
#endif
