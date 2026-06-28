/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_LOCALIZATION_H_
#define COMFY_LOCALIZATION_H_

#include <QHash>
#include <QList>
#include <QString>

/// Python ai_diffusion/localization.py — bundled data/language/*.json string tables.
struct ComfyLanguageInfo {
    QString id;
    QString name;
    QString filePath;
};

class ComfyLocalization
{
public:
    static ComfyLocalization &instance();

    void init();

    QString languageId() const { return m_id; }
    QString languageName() const { return m_name; }
    const QList<ComfyLanguageInfo> &availableLanguages() const { return m_available; }

    /// English key lookup; missing/null/empty JSON values keep \a key. Supports %1… via .arg after lookup.
    QString translate(const QString &key) const;
    QString translate(const QString &key, const QString &arg1) const;
    QString translate(const QString &key, const QString &arg1, const QString &arg2) const;
    QString translate(const QString &key, const QString &arg1, const QString &arg2, const QString &arg3) const;

    /// Test hook: load without reading settings.json.
    void loadLanguageForTest(const QString &id, const QString &name, const QHash<QString, QString> &translations);

private:
    ComfyLocalization();
    void scanAvailable();
    bool loadFromFile(const QString &id, const QString &filePath);
    static QString readLanguageSettingId();

    QString m_id = QStringLiteral("en");
    QString m_name = QStringLiteral("English");
    QHash<QString, QString> m_translations;
    QList<ComfyLanguageInfo> m_available;
};

/// Drop-in for Krita i18n() using JSON catalogs (English source strings as keys).
namespace ComfyTr {

namespace detail {
inline QString toStr(const QString &s) { return s; }
inline QString toStr(QLatin1String s) { return QString(s); }
inline QString toStr(const char *s) { return QString::fromUtf8(s); }
inline QString toStr(int v) { return QString::number(v); }
inline QString toStr(unsigned int v) { return QString::number(v); }
inline QString toStr(long v) { return QString::number(static_cast<qlonglong>(v)); }
inline QString toStr(long long v) { return QString::number(static_cast<qlonglong>(v)); }
inline QString toStr(unsigned long v) { return QString::number(static_cast<qulonglong>(v)); }
inline QString toStr(unsigned long long v) { return QString::number(static_cast<qulonglong>(v)); }
inline QString toStr(double v) { return QString::number(v); }
inline QString toStr(float v) { return QString::number(static_cast<double>(v)); }
} // namespace detail

inline QString tr(const char *text)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text));
}

template <typename A1>
inline QString tr(const char *text, const A1 &a1)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), detail::toStr(a1));
}

template <typename A1, typename A2>
inline QString tr(const char *text, const A1 &a1, const A2 &a2)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), detail::toStr(a1), detail::toStr(a2));
}

template <typename A1, typename A2, typename A3>
inline QString tr(const char *text, const A1 &a1, const A2 &a2, const A3 &a3)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text),
                                                   detail::toStr(a1),
                                                   detail::toStr(a2),
                                                   detail::toStr(a3));
}

/// 4+ args: fall back to manual .arg() chaining since translate() only handles up to 3.
template <typename A1, typename A2, typename A3, typename A4, typename... Rest>
inline QString tr(const char *text, const A1 &a1, const A2 &a2, const A3 &a3, const A4 &a4, const Rest &...rest)
{
    QString s = ComfyLocalization::instance().translate(QString::fromUtf8(text));
    const QString args[] = {detail::toStr(a1), detail::toStr(a2), detail::toStr(a3), detail::toStr(a4), detail::toStr(rest)...};
    for (const QString &a : args)
        s = s.arg(a);
    return s;
}

/// i18nc(context, text) — context ignored; JSON keys match English \a text.
inline QString trc(const char * /*context*/, const char *text)
{
    return tr(text);
}
} // namespace ComfyTr

#endif
