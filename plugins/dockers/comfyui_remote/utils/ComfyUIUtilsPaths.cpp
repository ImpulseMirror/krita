/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyFileLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QLoggingCategory>

#include <KSharedConfig>
#include <KConfigGroup>

#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace ComfyUIUtils {

QString historyCacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) base = QDir::tempPath();
    QString path = base + QStringLiteral("/krita/comfyui_remote");
    QDir().mkpath(path);
    return path;
}

namespace {

QString g_pluginUserDataDirTestOverride;

QString resolvePluginUserDataDir()
{
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString settingsFile = QStringLiteral("/settings.json");

    QString resolved;
    if (!appData.isEmpty() && appData.contains(QLatin1String("krita"), Qt::CaseInsensitive)) {
        resolved = appData + QStringLiteral("/comfyui_remote");
    } else {
        QString gen = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (gen.isEmpty())
            gen = QDir::homePath();
        resolved = gen + QStringLiteral("/comfyui_remote");
    }

    const QString newSettings = resolved + settingsFile;
    QStringList migrationCandidates;
    if (!appData.isEmpty()) {
        migrationCandidates << appData + QStringLiteral("/ai_diffusion");
        migrationCandidates << appData + QStringLiteral("/krita/comfyui_remote");
    } else {
        migrationCandidates << QDir::tempPath() + QStringLiteral("/krita/comfyui_remote");
    }
    QString gen = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (gen.isEmpty())
        gen = QDir::homePath();
    if (!QFile::exists(newSettings)) {
        for (const QString &oldPath : qAsConst(migrationCandidates)) {
            if (oldPath == resolved || !QFile::exists(oldPath + settingsFile))
                continue;
            QFileInfo ri(resolved);
            if (ri.exists()) {
                QDir rd(resolved);
                if (rd.exists() && rd.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
                    rd.rmdir(resolved);
            }
            QDir().mkpath(QFileInfo(resolved).path());
            if (!QFile::rename(oldPath, resolved)) {
                QDir().mkpath(oldPath);
                return oldPath;
            }
            break;
        }
    }

    QDir().mkpath(resolved);
    return resolved;
}

} // namespace

#ifdef COMFYUI_ENABLE_TEST_HOOKS
namespace ComfyUITestHooks {

void setPluginUserDataDirOverride(const QString &path)
{
    g_pluginUserDataDirTestOverride = path;
}

void clearPluginUserDataDirOverride()
{
    g_pluginUserDataDirTestOverride.clear();
}

} // namespace ComfyUITestHooks
#endif

// §13.66: user_data_dir — AppDataLocation/GenericDataLocation + "comfyui_remote".
// Migrates previous storage dirs when only the previous location has settings.json.
QString pluginUserDataDir()
{
#ifdef COMFYUI_ENABLE_TEST_HOOKS
    if (!g_pluginUserDataDirTestOverride.isEmpty()) {
        QDir().mkpath(g_pluginUserDataDirTestOverride);
        return g_pluginUserDataDirTestOverride;
    }
#endif
    return resolvePluginUserDataDir();
}

namespace {

// Directory containing this plugin's shared library.
QString pluginBinaryDirectory()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    typedef QString (*Symbol)();
    Symbol sym = pluginVersion;
    void *addr = reinterpret_cast<void *>(sym);
    Dl_info info;
    if (dladdr(addr, &info) == 0 || !info.dli_fname)
        return QString();
    return QFileInfo(QFile::decodeName(info.dli_fname)).absolutePath();
#elif defined(Q_OS_WIN)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&pluginVersion), &module)
        || !module) {
        return QString();
    }
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(module, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return QString();
    return QFileInfo(QString::fromWCharArray(buf)).absolutePath();
#else
    return QString();
#endif
}

// §13.66: Legacy folder `.logs` → log_dir (user_data_dir/logs). Python v1.14 used plugin_dir/.logs;
// also migrate user_data_dir/.logs if present (e.g. older C++ layouts).
void migrateLegacyDotLogsPath(const QString &legacyDotLogs, const QString &logDir)
{
    const QFileInfo legInfo(legacyDotLogs);
    if (!legInfo.isDir())
        return;

    if (!QFileInfo::exists(logDir) && QFile::rename(legacyDotLogs, logDir))
        return;

    QDir().mkpath(logDir);
    QDir leg(legacyDotLogs);
    const QFileInfoList top = leg.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : top) {
        const QString dest = logDir + QLatin1Char('/') + fi.fileName();
        if (QFile::exists(dest))
            continue;
        if (!QFile::rename(fi.absoluteFilePath(), dest)) {
            if (fi.isDir()) {
                QDir srcDir(fi.absoluteFilePath());
                QDir().mkpath(dest);
                const QFileInfoList inner = srcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo &inFi : inner) {
                    const QString d2 = dest + QLatin1Char('/') + inFi.fileName();
                    if (!QFile::exists(d2))
                        QFile::rename(inFi.absoluteFilePath(), d2);
                }
                if (srcDir.entryList(QDir::NoDotAndDotDot).isEmpty())
                    srcDir.rmdir(fi.absoluteFilePath());
            }
        }
    }
    leg.refresh();
    if (leg.entryList(QDir::NoDotAndDotDot).isEmpty())
        leg.rmdir(legacyDotLogs);
}

} // namespace

QString pluginInstallDataDir()
{
    const QString binDir = pluginBinaryDirectory();
    if (!binDir.isEmpty()) {
        const QString nextToModule = binDir + QStringLiteral("/data");
        if (QDir(nextToModule).exists())
            return nextToModule;
        const QString nested = binDir + QStringLiteral("/comfyui_remote/data");
        if (QDir(nested).exists())
            return nested;
    }
#ifdef COMFYUI_PLUGIN_SOURCE_DATA_DIR
    {
        const QString devPath = QStringLiteral(COMFYUI_PLUGIN_SOURCE_DATA_DIR);
        if (QDir(devPath).exists())
            return devPath;
    }
#endif
    return QString();
}

static bool copyFileIfMissing(const QString &src, const QString &dest)
{
    if (src.isEmpty() || dest.isEmpty() || QFileInfo::exists(dest))
        return false;
    QDir().mkpath(QFileInfo(dest).absolutePath());
    return QFile::copy(src, dest);
}

static QStringList pluginIconSearchBases()
{
    QStringList bases;
    bases << pluginUserDataDir() + QStringLiteral("/icons/");
    const QString install = pluginInstallDataDir();
    if (!install.isEmpty())
        bases << install + QStringLiteral("/icons/");
    const QString binDir = pluginBinaryDirectory();
    if (!binDir.isEmpty()) {
        bases << binDir + QStringLiteral("/data/icons/");
        bases << binDir + QStringLiteral("/comfyui_remote/data/icons/");
    }
#ifdef COMFYUI_PLUGIN_SOURCE_DATA_DIR
    bases << QStringLiteral(COMFYUI_PLUGIN_SOURCE_DATA_DIR) + QStringLiteral("/icons/");
#endif
    return bases;
}

static void copyBundledIconsIfNeeded(const QString &install, const QString &user)
{
    const QDir src(install + QStringLiteral("/icons"));
    if (!src.exists())
        return;
    const QString dstPath = user + QStringLiteral("/icons");
    QDir().mkpath(dstPath);
    const QDir dst(dstPath);

    int srcCount = 0;
    const QStringList files = src.entryList(QDir::Files);
    for (const QString &fn : files) {
        if (fn.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
            || fn.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            ++srcCount;
    }

    const QString marker = dst.absoluteFilePath(QStringLiteral(".bundled_icon_count"));
    if (QFile::exists(marker)) {
        QFile mf(marker);
        if (mf.open(QIODevice::ReadOnly)) {
            if (mf.readAll().trimmed() == QByteArray::number(srcCount))
                return;
        }
    }

    for (const QString &fn : files) {
        if (!(fn.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
              || fn.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)))
            continue;
        const QString dest = dst.absoluteFilePath(fn);
        if (QFile::exists(dest))
            QFile::remove(dest);
        QFile::copy(src.absoluteFilePath(fn), dest);
    }
    QFile mf(marker);
    if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        mf.write(QByteArray::number(srcCount));
}

QString findBundledThemeIconFile(const QString &stem, const QString &themeSuffix)
{
    if (stem.isEmpty())
        return QString();
    const QString suffix = themeSuffix.isEmpty() ? QStringLiteral("light") : themeSuffix;

    const auto qrcPath = [&](const QString &name) -> QString {
        const QString p = QStringLiteral(":/comfyicons/") + name;
        return QFile::exists(p) ? p : QString();
    };
    {
        const QString p = qrcPath(stem + QLatin1Char('-') + suffix + QStringLiteral(".svg"));
        if (!p.isEmpty())
            return p;
    }
    {
        const QString p = qrcPath(stem + QLatin1Char('-') + suffix + QStringLiteral(".png"));
        if (!p.isEmpty())
            return p;
    }
    {
        const QString p = qrcPath(stem + QStringLiteral(".svg"));
        if (!p.isEmpty())
            return p;
    }
    {
        const QString p = qrcPath(stem + QStringLiteral(".png"));
        if (!p.isEmpty())
            return p;
    }

    ensureBundledPluginDataInstalled();
    for (const QString &base : pluginIconSearchBases()) {
        if (base.isEmpty())
            continue;
        const QString svg = base + stem + QLatin1Char('-') + suffix + QStringLiteral(".svg");
        if (QFileInfo::exists(svg))
            return svg;
        const QString png = base + stem + QLatin1Char('-') + suffix + QStringLiteral(".png");
        if (QFileInfo::exists(png))
            return png;
    }
    for (const QString &base : pluginIconSearchBases()) {
        if (base.isEmpty())
            continue;
        const QString svgPlain = base + stem + QStringLiteral(".svg");
        if (QFileInfo::exists(svgPlain))
            return svgPlain;
        const QString pngPlain = base + stem + QStringLiteral(".png");
        if (QFileInfo::exists(pngPlain))
            return pngPlain;
    }
    return QString();
}

void ensureBundledPluginDataInstalled()
{
    QString install = pluginInstallDataDir();
#ifdef COMFYUI_PLUGIN_SOURCE_DATA_DIR
    if (install.isEmpty())
        install = QStringLiteral(COMFYUI_PLUGIN_SOURCE_DATA_DIR);
#endif
    if (install.isEmpty())
        return;
    const QString user = pluginUserDataDir();
    copyFileIfMissing(install + QStringLiteral("/presets/samplers.json"),
                      user + QStringLiteral("/presets/samplers.json"));
    copyFileIfMissing(install + QStringLiteral("/presets/control.json"),
                      user + QStringLiteral("/presets/control.json"));
    const QDir tagSrc(install + QStringLiteral("/tags"));
    if (tagSrc.exists()) {
        const QString tagDst = tagsStorageDir();
        for (const QString &fn : tagSrc.entryList(QStringList() << QStringLiteral("*.csv"), QDir::Files)) {
            copyFileIfMissing(tagSrc.absoluteFilePath(fn), tagDst + QLatin1Char('/') + fn);
        }
    }
    copyBundledIconsIfNeeded(install, user);
}

QString pluginLogDir()
{
    const QString userDataDir = pluginUserDataDir();
    const QString path = userDataDir + QStringLiteral("/logs");
    migrateLegacyDotLogsPath(userDataDir + QStringLiteral("/.logs"), path);
    const QString binDir = pluginBinaryDirectory();
    if (!binDir.isEmpty())
        migrateLegacyDotLogsPath(binDir + QStringLiteral("/.logs"), path);
    QDir().mkpath(path);
    return path;
}

// §13.148: database_dir for LoRA persistence (user_data_dir/database); checkpoints stay in-memory from server
QString pluginDatabaseDir()
{
    QString path = pluginUserDataDir() + QStringLiteral("/database");
    QDir().mkpath(path);
    return path;
}

QString workflowsStorageDir()
{
    QString path = pluginUserDataDir() + QStringLiteral("/workflows");
    QDir().mkpath(path);
    return path;
}

QString tagsStorageDir()
{
    QString path = pluginUserDataDir() + QStringLiteral("/tags");
    QDir().mkpath(path);
    return path;
}

QStringList discoverTagFileStems()
{
    QSet<QString> stems;
    const auto collectCsvStems = [&stems](const QString &dirPath) {
        const QDir dir(dirPath);
        if (!dir.exists())
            return;
        const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.csv"), QDir::Files);
        for (const QString &fn : files) {
            const QString stem = QFileInfo(fn).completeBaseName().trimmed();
            if (!stem.isEmpty())
                stems.insert(stem);
        }
    };
    collectCsvStems(pluginInstallDataDir() + QStringLiteral("/tags"));
    collectCsvStems(tagsStorageDir());
    QStringList out(stems.begin(), stems.end());
    out.sort(Qt::CaseInsensitive);
    return out;
}

QStringList tagKeywordsForAutocomplete(const QJsonObject &settingsIn)
{
    const QJsonObject settings = settingsIn.isEmpty() ? loadSettingsJson() : settingsIn;
    QString tagDir = settings.value(QStringLiteral("tag_directory")).toString().trimmed();
    if (tagDir.isEmpty())
        tagDir = tagsStorageDir();

    QJsonArray arr = settings.value(QStringLiteral("tag_files")).toArray();
    QStringList stems;
    if (arr.isEmpty()) {
        stems << QStringLiteral("Danbooru") << QStringLiteral("e621");
    } else {
        for (const QJsonValue &v : arr) {
            const QString t = v.toString().trimmed();
            if (!t.isEmpty())
                stems.append(t);
        }
    }

    QSet<QString> seen;
    QStringList out;
    for (const QString &stem : stems) {
        const QString primary = tagDir + QLatin1Char('/') + stem + QStringLiteral(".csv");
        QString path = primary;
        if (!QFile::exists(path))
            path = tagsStorageDir() + QLatin1Char('/') + stem + QStringLiteral(".csv");
        const QStringList rowTags = loadTagCsvTags(path);
        for (const QString &tag : rowTags) {
            if (tag.isEmpty() || seen.contains(tag))
                continue;
            seen.insert(tag);
            out.append(tag);
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QStringList listLocalWorkflowJsonFilenames()
{
    QStringList names;
    QDir dir(workflowsStorageDir());
    if (!dir.exists())
        return names;
    const QStringList fl = dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files, QDir::Name);
    for (const QString &f : fl)
        names.append(f);
    return names;
}

QString lorasJsonPath()
{
    return pluginDatabaseDir() + QStringLiteral("/loras.json");
}

QJsonArray loadLorasJsonArray()
{
    ComfyFileLibrary::instance().init();
    QJsonArray arr;
    for (const ComfyFileRecord &f : ComfyFileLibrary::instance().loras().files())
        arr.append(f.toJson());
    return arr;
}

bool saveLorasJsonArray(const QJsonArray &arr)
{
    ComfyFileLibrary::instance().init();
    QList<ComfyFileRecord> parsed;
    parsed.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        if (v.isObject())
            parsed.append(ComfyFileRecord::fromJson(v.toObject()));
    }
    ComfyFileLibrary::instance().loras().replaceAll(parsed);
    return true;
}

QString mergeLibraryLoraTagsIntoPositivePrompt(const QString &positivePrompt)
{
    ComfyFileLibrary::instance().init();
    QStringList tags;
    for (const ComfyFileRecord &f : ComfyFileLibrary::instance().loras().files()) {
        // FAITHFUL_PORT/BUG: defaulting `enabled` to true here caused every LoRA
        // the server advertised to be auto-injected into the prompt on a fresh
        // install. With Wan 2.1/2.2 video LoRAs present, ComfyUI rejected the
        // /prompt with `prompt_outputs_failed_validation` because the wrong
        // architecture's LoRA was attached. Spec (and the Python plugin) treat
        // LoRAs as opt-in — the user must enable them explicitly.
        if (!f.meta(QStringLiteral("enabled")).toBool(false))
            continue;
        const QString fn = f.id.trimmed();
        if (fn.isEmpty())
            continue;
        const int pct = f.meta(QStringLiteral("strength_percent")).toInt(100);
        if (pct <= 0)
            continue;
        const double w = qBound(0.01, pct / 100.0, 4.0);
        // FAITHFUL_PORT/BUG: ComfyUI's LoraLoader.lora_name input is the path
        // relative to the loras directory, e.g. "Video Loras/wan 2.1/foo.safetensors",
        // not just the basename. Stripping to QFileInfo::fileName() made the
        // server reject "value_not_in_list" when the file lived in a subfolder.
        // Use the full id (already normalised by ComfyFileLibrary) so the tag
        // round-trips through workflow build correctly.
        if (fn.isEmpty())
            continue;
        tags.append(QStringLiteral("<lora:%1:%2>").arg(fn, QString::number(w, 'f', 2)));
    }
    if (tags.isEmpty())
        return positivePrompt;
    const QString suffix = tags.join(QLatin1Char(' '));
    const QString t = positivePrompt.trimmed();
    if (t.isEmpty())
        return suffix;
    return t + QStringLiteral(", ") + suffix;
}

QString mergeStyleLoraTriggersIntoPositivePrompt(const QString &positivePrompt, const QJsonArray &styleLoras)
{
    QStringList triggers;
    ComfyFileLibrary::instance().init();
    for (const QJsonValue &v : styleLoras) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        if (!o.value(QStringLiteral("enabled")).toBool(true))
            continue;
        const QString name = o.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            continue;
        const ComfyFileRecord *rec = ComfyFileLibrary::instance().loras().find(name);
        const QString t = rec ? rec->meta(QStringLiteral("lora_triggers")).toString().trimmed() : QString();
        if (!t.isEmpty())
            triggers.append(t);
    }
    if (triggers.isEmpty())
        return positivePrompt;
    const QString suffix = triggers.join(QStringLiteral(", "));
    const QString t = positivePrompt.trimmed();
    if (t.isEmpty())
        return suffix;
    return t + QStringLiteral(", ") + suffix;
}

// §13.165: Plugin installation path check — warn if not under expected location (dockers/ or .git)
void checkPluginInstallationPath()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    // Resolve path of the current shared library (plugin .so/.dylib)
    typedef QString (*Symbol)();
    Symbol sym = pluginVersion;
    void *addr = reinterpret_cast<void *>(sym);
    Dl_info info;
    if (dladdr(addr, &info) == 0 || !info.dli_fname)
        return;
    QString libPath = QFile::decodeName(info.dli_fname);
    QFileInfo fi(libPath);
    QDir pluginDir = fi.absoluteDir();
    QString parentName = pluginDir.dirName();
    pluginDir.cdUp();
    QString grandParentName = pluginDir.dirName();
    // Expected: we are in .../dockers/comfyui_remote/ (parent of plugin dir = "dockers") or under a .git tree
    bool underDockers = (parentName == QLatin1String("comfyui_remote") && grandParentName == QLatin1String("dockers"));
    bool underGit = false;
    for (QDir d(fi.absoluteDir().absolutePath()); d.exists(); ) {
        if (QFileInfo(d, QStringLiteral(".git")).exists()) {
            underGit = true;
            break;
        }
        if (!d.cdUp())
            break;
    }
    if (!underDockers && !underGit) {
        qWarning("ComfyUI Remote: Plugin is not installed in a 'dockers' directory, this may break user files and settings. Detected installation path is: %s", qPrintable(fi.absoluteFilePath()));
    }
#endif
}

void migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion()
{
    migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(KSharedConfig::openConfig());
}

void migrateMainWindowDockLayoutComfyUIRemoteToImageDiffusion(const KSharedConfigPtr &cfg)
{
    if (!cfg) {
        return;
    }
    KConfigGroup mainWin(cfg, QStringLiteral("MainWindow"));
    KConfigGroup legacy = mainWin.group(QStringLiteral("DockWidget ComfyUIRemote"));
    if (legacy.keyList().isEmpty())
        return;
    KConfigGroup current = mainWin.group(QStringLiteral("DockWidget imageDiffusion"));
    if (current.hasKey(QStringLiteral("DockArea")))
        return;
    if (legacy.hasKey(QStringLiteral("Locked")))
        current.writeEntry(QStringLiteral("Locked"), legacy.readEntry(QStringLiteral("Locked"), false));
    if (legacy.hasKey(QStringLiteral("DockArea")))
        current.writeEntry(QStringLiteral("DockArea"), legacy.readEntry(QStringLiteral("DockArea"), 0));
    if (legacy.hasKey(QStringLiteral("xPosition")))
        current.writeEntry(QStringLiteral("xPosition"), legacy.readEntry(QStringLiteral("xPosition"), 0));
    if (legacy.hasKey(QStringLiteral("yPosition")))
        current.writeEntry(QStringLiteral("yPosition"), legacy.readEntry(QStringLiteral("yPosition"), 0));
    if (legacy.hasKey(QStringLiteral("width")))
        current.writeEntry(QStringLiteral("width"), legacy.readEntry(QStringLiteral("width"), 0));
    if (legacy.hasKey(QStringLiteral("height")))
        current.writeEntry(QStringLiteral("height"), legacy.readEntry(QStringLiteral("height"), 0));
    legacy.deleteGroup();
    cfg->sync();
}
QString liveFramesDirectory(const QString &documentPath)
{
    if (documentPath.isEmpty()) return QString();
    QFileInfo info(documentPath);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".live-frames");
}

QString animationFramesDirectory(const QString &documentPath)
{
    if (documentPath.isEmpty()) return QString();
    QFileInfo info(documentPath);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".animation");
}

QString liveFramePath(const QString &documentPath, int frameIndex)
{
    return liveFramesDirectory(documentPath) + QStringLiteral("/frame-%1.webp").arg(frameIndex);
}

QString animationFramePath(const QString &documentPath, int frameIndex)
{
    return animationFramesDirectory(documentPath) + QStringLiteral("/frame-%1.png").arg(frameIndex);
}
} // namespace ComfyUIUtils
