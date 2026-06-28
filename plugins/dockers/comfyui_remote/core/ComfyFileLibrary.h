/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_FILE_LIBRARY_H_
#define COMFY_FILE_LIBRARY_H_

#include <QFlags>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

enum class ComfyFileFormat {
    Unknown = 0,
    Checkpoint = 1,
    Diffusion = 2,
    Lora = 3,
};

enum ComfyFileSourceFlag {
    ComfyFileSourceUnavailable = 0,
    ComfyFileSourceLocal = 1,
    ComfyFileSourceRemote = 2,
};
Q_DECLARE_FLAGS(ComfyFileSources, ComfyFileSourceFlag)

/// Python ai_diffusion/files.py — File + FileCollection + FileLibrary.
struct ComfyFileRecord {
    QString id;
    QString name;
    ComfyFileSources source = ComfyFileSourceUnavailable;
    ComfyFileFormat format = ComfyFileFormat::Unknown;
    QString hash;
    QString path;
    qint64 size = 0;
    QJsonObject metadata;

    static ComfyFileRecord remote(const QString &id, ComfyFileFormat format = ComfyFileFormat::Unknown);
    static ComfyFileRecord local(const QString &filePath, ComfyFileFormat format, bool computeHash = false);

    static ComfyFileRecord fromJson(const QJsonObject &o);
    QJsonObject toJson() const;

    bool mergeFrom(const ComfyFileRecord &other);
    QString computeHash();
    QJsonValue meta(const QString &key) const;
    void setMeta(const QString &key, const QJsonValue &value);
};

class ComfyFileCollection
{
public:
    explicit ComfyFileCollection(const QString &databasePath = QString());

    const QList<ComfyFileRecord> &files() const { return m_files; }
    QString databasePath() const { return m_databasePath; }

    void load();
    void save() const;

    void extend(const QList<ComfyFileRecord> &input);
    void update(const QList<ComfyFileRecord> &newFiles, ComfyFileSourceFlag sourceFlag);
    void add(const ComfyFileRecord &file);
    void replaceAll(const QList<ComfyFileRecord> &files);

    const ComfyFileRecord *find(const QString &id) const;
    const ComfyFileRecord *findLocal(const QString &id) const;
    int findIndex(const QString &id) const;

    void setMeta(ComfyFileRecord *file, const QString &key, const QJsonValue &value);
    bool setMetaById(const QString &id, const QString &key, const QJsonValue &value);

private:
    void removeMissingLocalFiles();

    QString m_databasePath;
    QList<ComfyFileRecord> m_files;
};

class ComfyFileLibrary
{
public:
    static ComfyFileLibrary &instance();
    void init();

    ComfyFileCollection &checkpoints() { return m_checkpoints; }
    const ComfyFileCollection &checkpoints() const { return m_checkpoints; }
    ComfyFileCollection &loras() { return m_loras; }
    const ComfyFileCollection &loras() const { return m_loras; }

    void updateRemoteCheckpoints(const QStringList &serverCheckpointFilenames);
    void updateRemoteLoras(const QStringList &serverLoraFilenames);

    /// Style JSON checkpoint list → first name present on server (Python Style.preferred_checkpoint).
    static QString preferredCheckpoint(const QStringList &styleCheckpoints, const QStringList &availableOnServer);

    /// Enabled library LoRAs missing on server but present locally (path + hash).
    QList<const ComfyFileRecord *> localLorasMissingOnServer(const QStringList &serverLoraFilenames) const;

#ifdef COMFYUI_ENABLE_TEST_HOOKS
    void resetLorasDatabaseForTests(const QString &databasePath);
#endif

private:
    ComfyFileLibrary();

    ComfyFileCollection m_checkpoints;
    ComfyFileCollection m_loras;
    bool m_initialized = false;
};

namespace ComfyFileLibraryUtil {
/// SHA-256 digest, base64-encoded (Python File.compute_hash).
QString sha256Base64OfFile(const QString &filePath);
} // namespace ComfyFileLibraryUtil

#endif
