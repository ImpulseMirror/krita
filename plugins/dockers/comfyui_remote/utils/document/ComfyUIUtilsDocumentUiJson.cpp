/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyControlLayer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QImage>
#include <QImageWriter>
#include <QBuffer>
#include <QHash>
#include <QSet>
#include <QPainter>
#include <QUuid>

#include <KSharedConfig>
#include <KConfigGroup>

#include <kis_image.h>
#include <kis_layer_utils.h>
#include <kis_annotation.h>
#include <kis_node.h>
#include <kis_group_layer.h>
#include <kis_layer.h>
#include <kis_mask.h>
#include <kis_paint_layer.h>
#include <kis_pixel_selection.h>

#include <KoColorConversionTransformation.h>
#include <KoColorProfile.h>


namespace ComfyUIUtils {

qint64 documentEmbeddedHistoryStorageBytes(KisImageSP image)
{
    if (!image)
        return 0;
    qint64 total = 0;
    const QString prefix = documentAnnotationKey(QStringLiteral("result"));
    for (auto it = image->beginAnnotations(); it != image->endAnnotations(); ++it) {
        const KisAnnotationSP ann = *it;
        if (!ann)
            continue;
        const QString t = ann->type();
        if (t.startsWith(prefix))
            total += static_cast<qint64>(ann->annotation().size());
    }
    return total;
}

qint64 documentEmbeddedHistoryLimitBytes(const QJsonObject &settings)
{
    int mb = settings.value(QStringLiteral("history_document_storage_mb")).toInt(0);
    if (mb <= 0)
        mb = settings.value(QStringLiteral("history_storage")).toInt(20);
    mb = qBound(5, mb, 2000);
    return static_cast<qint64>(mb) * 1024ll * 1024ll;
}

static int uiJsonStoredVersion(const QJsonObject &o)
{
    const QJsonValue v = o.value(QStringLiteral("version"));
    if (v.isUndefined() || v.isNull()) {
        return 0;
    }
    if (v.isDouble()) {
        return int(v.toDouble());
    }
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().trimmed().toInt(&ok);
        return ok ? n : 0;
    }
    return 0;
}

DocumentUiJsonLoadOutcome loadDocumentUiJsonWithMeta(KisImageSP image)
{
    QJsonObject def;
    def.insert(QStringLiteral("version"), persistenceFormatVersion);
    DocumentUiJsonLoadOutcome out;
    out.object = def;
    out.rawVersionFromFile = persistenceFormatVersion;
    if (!image) {
        return out;
    }
    const QString key = documentUiJsonAnnotationKey();
    KisAnnotationSP ann = image->annotation(key);
    if (!ann || ann->annotation().isEmpty()) {
        return out;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(ann->annotation(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        out.parseFailed = true;
        out.parseError = (err.error != QJsonParseError::NoError) ? err.errorString() : QStringLiteral("JSON root is not an object");
        return out;
    }
    QJsonObject o = doc.object();
    const int ver = uiJsonStoredVersion(o);
    out.rawVersionFromFile = ver > 0 ? ver : persistenceFormatVersion;
    if (ver > persistenceFormatVersion) {
        qWarning() << "ComfyUI ui.json version" << ver << "is newer than supported" << persistenceFormatVersion
                   << "- loading default embedded state.";
        out.resetToDefaultsDueToFutureVersion = true;
        out.object = def;
        return out;
    }
    if (ver <= 0) {
        o.insert(QStringLiteral("version"), persistenceFormatVersion);
    } else if (!o.contains(QStringLiteral("version"))) {
        o.insert(QStringLiteral("version"), persistenceFormatVersion);
    }
    out.object = o;
    return out;
}

QJsonObject loadDocumentUiJsonObject(KisImageSP image)
{
    return loadDocumentUiJsonWithMeta(image).object;
}

QJsonObject regionUiStateEntryToJson(const ComfyRegionUiStateEntry &entry)
{
    QJsonObject o;
    if (!entry.name.isEmpty())
        o.insert(QStringLiteral("name"), entry.name);
    if (!entry.positive.isEmpty()) {
        o.insert(QStringLiteral("positive"), entry.positive);
        o.insert(QStringLiteral("prompt"), entry.positive);
    }
    if (!entry.layerIds.isEmpty())
        o.insert(QStringLiteral("layer_ids"), entry.layerIds);
    if (!entry.maskSource.isEmpty())
        o.insert(QStringLiteral("mask_source"), entry.maskSource);
    const QJsonArray ctrl = ComfyControlLayer::toJsonArray(entry.controlLayers);
    if (!ctrl.isEmpty())
        o.insert(QStringLiteral("control"), ctrl);
    return o;
}

ComfyRegionUiStateEntry regionUiStateEntryFromJson(const QJsonObject &o)
{
    ComfyRegionUiStateEntry e;
    e.name = o.value(QStringLiteral("name")).toString();
    e.positive = o.value(QStringLiteral("positive")).toString();
    if (e.positive.isEmpty())
        e.positive = o.value(QStringLiteral("prompt")).toString();
    e.layerIds = o.value(QStringLiteral("layer_ids")).toString();
    QString ms = o.value(QStringLiteral("mask_source")).toString();
    if (ms.isEmpty())
        ms = o.value(QStringLiteral("maskSource")).toString();
    e.maskSource = ms.isEmpty() ? QStringLiteral("selection") : ms;
    e.controlLayers = ComfyControlLayer::fromJsonArray(o.value(QStringLiteral("control")).toArray());
    if (e.name.isEmpty() && !e.positive.isEmpty())
        e.name = e.positive.left(40);
    return e;
}

QJsonArray regionUiStateEntriesToJsonArray(const QList<ComfyRegionUiStateEntry> &entries)
{
    QJsonArray arr;
    for (const ComfyRegionUiStateEntry &e : entries)
        arr.append(regionUiStateEntryToJson(e));
    return arr;
}

QList<ComfyRegionUiStateEntry> regionUiStateEntriesFromJsonArray(const QJsonArray &arr)
{
    QList<ComfyRegionUiStateEntry> out;
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        const ComfyRegionUiStateEntry e = regionUiStateEntryFromJson(v.toObject());
        if (!e.name.isEmpty() || !e.positive.isEmpty())
            out.append(e);
    }
    return out;
}

QJsonObject rootRegionUiWrapToJson(const QString &positive,
                                   const QString &negative,
                                   const QList<ComfyRegionUiStateEntry> &regions)
{
    QJsonObject wrap;
    wrap.insert(QStringLiteral("positive"), positive);
    wrap.insert(QStringLiteral("negative"), negative);
    wrap.insert(QStringLiteral("regions"), regionUiStateEntriesToJsonArray(regions));
    return wrap;
}

bool rootRegionUiWrapFromJson(const QJsonObject &wrap,
                              QString *positive,
                              QString *negative,
                              QList<ComfyRegionUiStateEntry> *regions)
{
    if (!wrap.isEmpty()) {
        if (positive)
            *positive = wrap.value(QStringLiteral("positive")).toString();
        if (negative)
            *negative = wrap.value(QStringLiteral("negative")).toString();
        if (regions)
            *regions = regionUiStateEntriesFromJsonArray(wrap.value(QStringLiteral("regions")).toArray());
        return true;
    }
    return false;
}

QJsonArray readRegionUiArrayFromDocumentUi(const QJsonObject &ui, bool *foundInDocument)
{
    if (foundInDocument)
        *foundInDocument = false;
    const QJsonObject rootObj = ui.value(QStringLiteral("root")).toObject();
    if (rootObj.contains(QStringLiteral("regions"))) {
        if (foundInDocument)
            *foundInDocument = true;
        return rootObj.value(QStringLiteral("regions")).toArray();
    }
    if (ui.contains(QStringLiteral("regions"))) {
        if (foundInDocument)
            *foundInDocument = true;
        return ui.value(QStringLiteral("regions")).toArray();
    }
    return QJsonArray();
}

bool documentHasStoredUiJsonPayload(KisImageSP image)
{
    if (!image)
        return false;
    const KisAnnotationSP ann = image->annotation(documentUiJsonAnnotationKey());
    return ann && !ann->annotation().isEmpty();
}

QJsonObject documentDefaultsFromSettingsRoot(const QJsonObject &settingsRoot)
{
    return settingsRoot.value(QStringLiteral("document_defaults")).toObject();
}

QString inpaintContextForFreshDocumentDefaults(const QString &storedContext)
{
    QString n = storedContext.trimmed().toLower();
    n.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (n == QLatin1String("layer_bounds"))
        return QStringLiteral("automatic");
    if (n.isEmpty())
        return QStringLiteral("automatic");
    return storedContext.trimmed();
}

QString inpaintModeKeyFromJson(const QJsonValue &value)
{
    if (value.isDouble()) {
        switch (value.toInt()) {
        case 1:
            return QStringLiteral("fill");
        case 2:
            return QStringLiteral("expand");
        case 3:
            return QStringLiteral("add_object");
        case 4:
            return QStringLiteral("remove_object");
        case 5:
            return QStringLiteral("replace_background");
        case 6:
            return QStringLiteral("custom");
        case 0:
        default:
            return QStringLiteral("automatic");
        }
    }
    QString s = value.toString().trimmed();
    if (s.isEmpty())
        return QStringLiteral("automatic");
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (s == QLatin1String("automatic") || s == QLatin1String("fill") || s == QLatin1String("expand")
        || s == QLatin1String("add_object") || s == QLatin1String("remove_object")
        || s == QLatin1String("replace_background") || s == QLatin1String("custom"))
        return s;
    bool ok = false;
    const int asInt = s.toInt(&ok);
    if (ok)
        return inpaintModeKeyFromJson(QJsonValue(asInt));
    return QStringLiteral("automatic");
}

QString fillModeKeyFromJson(const QJsonValue &value)
{
    if (value.isDouble()) {
        switch (value.toInt()) {
        case 1:
            return QStringLiteral("neutral");
        case 2:
            return QStringLiteral("blur");
        case 3:
            return QStringLiteral("border");
        case 4:
            return QStringLiteral("replace");
        case 5:
            return QStringLiteral("inpaint");
        case 6:
            return QStringLiteral("green");
        case 0:
        default:
            return QStringLiteral("none");
        }
    }
    QString s = value.toString().trimmed();
    if (s.isEmpty())
        return QStringLiteral("blur");
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (s == QLatin1String("none") || s == QLatin1String("neutral") || s == QLatin1String("blur")
        || s == QLatin1String("border") || s == QLatin1String("replace") || s == QLatin1String("inpaint")
        || s == QLatin1String("green"))
        return s;
    bool ok = false;
    const int asInt = s.toInt(&ok);
    if (ok)
        return fillModeKeyFromJson(QJsonValue(asInt));
    return QStringLiteral("blur");
}

namespace {

QString legacyFillModeIndexToKey(int idx)
{
    static const char *keys[] = {"none", "neutral", "blur", "border", "inpaint"};
    return QString::fromLatin1(keys[qBound(0, idx, 4)]);
}

} // namespace

RecentlyUsedSyncSnapshot recentlyUsedSyncFromSettings(bool *migratedOut)
{
    if (migratedOut)
        *migratedOut = false;

    QJsonObject settings = loadSettingsJson();
    QJsonObject dd = documentDefaultsFromSettingsRoot(settings);
    bool migrated = false;
    const KConfigGroup cfg = KSharedConfig::openConfig()->group(QStringLiteral("ComfyUIRemote"));

    if (!dd.contains(QStringLiteral("inpaint_mode"))
        || dd.value(QStringLiteral("inpaint_mode")).toString().trimmed().isEmpty()) {
        QString mode = cfg.readEntry(QStringLiteral("InpaintModeKey"), QString());
        if (mode.isEmpty()) {
            static const char *legacyModes[] = {"automatic", "fill", "expand"};
            mode = QString::fromUtf8(legacyModes[qBound(0, cfg.readEntry(QStringLiteral("InpaintMode"), 0), 2)]);
        }
        dd.insert(QStringLiteral("inpaint_mode"), mode);
        migrated = true;
    }
    if (!dd.contains(QStringLiteral("inpaint_fill"))
        || dd.value(QStringLiteral("inpaint_fill")).toString().trimmed().isEmpty()) {
        dd.insert(QStringLiteral("inpaint_fill"), legacyFillModeIndexToKey(cfg.readEntry(QStringLiteral("FillMode"), 2)));
        migrated = true;
    }
    if (!dd.contains(QStringLiteral("inpaint_context"))
        || dd.value(QStringLiteral("inpaint_context")).toString().trimmed().isEmpty()) {
        const QString ctx = cfg.readEntry(QStringLiteral("InpaintContext"), QStringLiteral("automatic"));
        dd.insert(QStringLiteral("inpaint_context"), inpaintContextForFreshDocumentDefaults(ctx));
        migrated = true;
    }
    if (!dd.contains(QStringLiteral("inpaint_use_model"))) {
        dd.insert(QStringLiteral("inpaint_use_model"), cfg.readEntry(QStringLiteral("InpaintUseModel"), true));
        migrated = true;
    }
    if (!dd.contains(QStringLiteral("inpaint_use_prompt_focus"))) {
        dd.insert(QStringLiteral("inpaint_use_prompt_focus"), cfg.readEntry(QStringLiteral("InpaintUsePromptFocus"), false));
        migrated = true;
    }
    if (!dd.contains(QStringLiteral("batch_count"))) {
        dd.insert(QStringLiteral("batch_count"), cfg.readEntry(QStringLiteral("BatchCount"), 1));
        migrated = true;
    }

    if (migrated) {
        settings.insert(QStringLiteral("document_defaults"), dd);
        saveSettingsJson(settings);
        if (migratedOut)
            *migratedOut = true;
    }

    RecentlyUsedSyncSnapshot snap;
    snap.inpaintMode = inpaintModeKeyFromJson(dd.value(QStringLiteral("inpaint_mode")));
    snap.inpaintFill = fillModeKeyFromJson(dd.value(QStringLiteral("inpaint_fill")));
    snap.inpaintUseModel = dd.value(QStringLiteral("inpaint_use_model")).toBool(true);
    snap.inpaintUsePromptFocus = dd.value(QStringLiteral("inpaint_use_prompt_focus")).toBool(false);
    snap.inpaintContext =
        inpaintContextForFreshDocumentDefaults(dd.value(QStringLiteral("inpaint_context")).toString());
    snap.batchCount = qMax(1, dd.value(QStringLiteral("batch_count")).toInt(1));
    return snap;
}

static int inpaintModeToJsonValue(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == QLatin1String("fill"))
        return 1;
    if (m == QLatin1String("expand"))
        return 2;
    if (m == QLatin1String("add_object"))
        return 3;
    if (m == QLatin1String("remove_object"))
        return 4;
    if (m == QLatin1String("replace_background"))
        return 5;
    if (m == QLatin1String("custom"))
        return 6;
    return 0;
}

static int fillModeToJsonValue(const QString &fill)
{
    const QString f = fill.trimmed().toLower();
    if (f == QLatin1String("neutral"))
        return 1;
    if (f == QLatin1String("blur"))
        return 2;
    if (f == QLatin1String("border"))
        return 3;
    if (f == QLatin1String("replace"))
        return 4;
    if (f == QLatin1String("inpaint"))
        return 5;
    if (f == QLatin1String("green"))
        return 6;
    return 0;
}

static int inpaintContextToJsonValue(const QString &contextKey)
{
    const QString c = contextKey.trimmed().toLower();
    if (c == QLatin1String("mask_bounds"))
        return 1;
    if (c == QLatin1String("entire_image"))
        return 2;
    if (c == QLatin1String("layer_bounds"))
        return 3;
    return 0;
}

QString inpaintContextKeyFromJson(const QJsonValue &value)
{
    if (value.isDouble()) {
        switch (value.toInt()) {
        case 1:
            return QStringLiteral("mask_bounds");
        case 2:
            return QStringLiteral("entire_image");
        case 3:
            return QStringLiteral("layer_bounds");
        case 0:
        default:
            return QStringLiteral("automatic");
        }
    }
    QString s = value.toString().trimmed();
    if (s.isEmpty())
        return QStringLiteral("automatic");
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    const QUuid legacyUuid = QUuid::fromString(s);
    if (!legacyUuid.isNull())
        return QStringLiteral("layer_bounds");
    if (s == QLatin1String("automatic") || s == QLatin1String("mask_bounds") || s == QLatin1String("entire_image")
        || s == QLatin1String("layer_bounds"))
        return s;
    bool ok = false;
    const int asInt = s.toInt(&ok);
    if (ok)
        return inpaintContextKeyFromJson(QJsonValue(asInt));
    return QStringLiteral("automatic");
}

QString inpaintContextLayerIdFromJson(const QJsonValue &value)
{
    const QString raw = value.toString().trimmed();
    if (raw.isEmpty())
        return QString();
    const QUuid uid = QUuid::fromString(raw);
    return uid.isNull() ? raw : uid.toString(QUuid::WithoutBraces);
}

QJsonObject inpaintWorkspaceToJson(const InpaintWorkspaceSnapshot &state)
{
    QJsonObject o;
    o.insert(QStringLiteral("mode"), inpaintModeToJsonValue(state.mode));
    o.insert(QStringLiteral("fill"), fillModeToJsonValue(state.fill));
    o.insert(QStringLiteral("use_inpaint"), state.useInpaint);
    o.insert(QStringLiteral("use_prompt_focus"), state.usePromptFocus);
    o.insert(QStringLiteral("context"), inpaintContextToJsonValue(state.context));
    if (!state.contextLayerId.isEmpty()) {
        const QUuid uid = QUuid::fromString(state.contextLayerId);
        o.insert(QStringLiteral("context_layer_id"),
                 uid.isNull() ? state.contextLayerId : uid.toString(QUuid::WithBraces));
    } else {
        o.insert(QStringLiteral("context_layer_id"), QString());
    }
    return o;
}

bool inpaintWorkspaceFromJson(const QJsonObject &object, InpaintWorkspaceSnapshot *out)
{
    if (!out || object.isEmpty())
        return false;
    out->mode = inpaintModeKeyFromJson(object.value(QStringLiteral("mode")));
    out->fill = fillModeKeyFromJson(object.value(QStringLiteral("fill")));
    out->useInpaint = object.value(QStringLiteral("use_inpaint")).toBool(out->useInpaint);
    out->usePromptFocus = object.value(QStringLiteral("use_prompt_focus")).toBool(out->usePromptFocus);
    out->context = inpaintContextKeyFromJson(object.value(QStringLiteral("context")));
    const QString rawContext = object.value(QStringLiteral("context")).toString().trimmed();
    out->contextLayerId = inpaintContextLayerIdFromJson(object.value(QStringLiteral("context_layer_id")));
    if (out->contextLayerId.isEmpty() && out->context == QLatin1String("layer_bounds")) {
        const QUuid legacy = QUuid::fromString(rawContext);
        if (!legacy.isNull())
            out->contextLayerId = legacy.toString(QUuid::WithoutBraces);
    }
    if (out->context != QLatin1String("layer_bounds"))
        out->contextLayerId.clear();
    return true;
}

QString historyResultLogicalKey(int slot)
{
    return QStringLiteral("result%1.webp").arg(slot);
}

static int parseResultSlotFromAnnotationType(const QString &fullType)
{
    const QString pfx = documentAnnotationKey(QString());
    if (!fullType.startsWith(pfx))
        return -1;
    const QString logical = fullType.mid(pfx.length());
    static const QRegularExpression re(QStringLiteral("^result(\\d+)(\\.webp)?$"));
    const QRegularExpressionMatch m = re.match(logical);
    if (!m.hasMatch())
        return -1;
    return m.captured(1).toInt();
}

int maxHistorySlotFromDocument(KisImageSP image, const QJsonObject &uiRoot)
{
    int maxS = -1;
    const QJsonArray h = uiRoot.value(QStringLiteral("history")).toArray();
    for (const QJsonValue &v : h) {
        const int s = v.toObject().value(QStringLiteral("slot")).toInt(-1);
        if (s > maxS)
            maxS = s;
    }
    if (image) {
        for (auto it = image->beginAnnotations(); it != image->endAnnotations(); ++it) {
            const KisAnnotationSP ann = *it;
            if (!ann)
                continue;
            const int s = parseResultSlotFromAnnotationType(ann->type());
            if (s > maxS)
                maxS = s;
        }
    }
    return maxS;
}

static QByteArray encodeSingleHistoryImage(const QImage &src, const QString &fmtNorm, const QJsonObject &settings)
{
    QImage im = src;
    if (im.format() != QImage::Format_RGBA8888 && im.format() != QImage::Format_ARGB32)
        im = im.convertToFormat(QImage::Format_ARGB32);
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QByteArray formatBa;
    int quality = -1;
    if (fmtNorm == QLatin1String("jpeg") || fmtNorm == QLatin1String("jpg")) {
        formatBa = QByteArrayLiteral("jpeg");
        quality = saveImageQualityJpeg(settings);
    } else if (fmtNorm == QLatin1String("png") || fmtNorm == QLatin1String("png_small")) {
        formatBa = QByteArrayLiteral("png");
        if (fmtNorm == QLatin1String("png_small"))
            quality = 75;
    } else {
        formatBa = QByteArrayLiteral("webp");
        quality = saveImageQualityWebp(settings);
    }
    QImageWriter w(&buf, formatBa);
    if (quality >= 0)
        w.setQuality(quality);
    if (!w.write(im))
        return QByteArray();
    return buf.data();
}

HistoryImageEncodeResult encodeHistoryImagesFromPaths(const QStringList &paths, const QJsonObject &settings)
{
    HistoryImageEncodeResult r;
    QString fmt = settings.value(QStringLiteral("history_format")).toString().trimmed().toLower();
    if (fmt.isEmpty())
        fmt = QStringLiteral("webp");
    int pos = 0;
    for (const QString &path : paths) {
        QImage img(path);
        if (img.isNull())
            return HistoryImageEncodeResult();
        const QByteArray chunk = encodeSingleHistoryImage(img, fmt, settings);
        if (chunk.isEmpty())
            return HistoryImageEncodeResult();
        r.data.append(chunk);
        pos += chunk.size();
        r.offsets.append(pos);
    }
    return r;
}

} // namespace ComfyUIUtils
