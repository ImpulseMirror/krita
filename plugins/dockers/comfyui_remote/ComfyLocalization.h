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
inline QString tr(const char *text)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text));
}
inline QString tr(const char *text, const QString &a1)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), a1);
}
inline QString tr(const char *text, const QString &a1, const QString &a2)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), a1, a2);
}
inline QString tr(const char *text, const QString &a1, const QString &a2, const QString &a3)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), a1, a2, a3);
}
inline QString tr(const char *text, int a1)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), QString::number(a1));
}
inline QString tr(const char *text, int a1, int a2)
{
    return ComfyLocalization::instance().translate(QString::fromUtf8(text), QString::number(a1), QString::number(a2));
}
/// i18nc(context, text) — context ignored; JSON keys match English \a text.
inline QString trc(const char * /*context*/, const char *text)
{
    return tr(text);
}
} // namespace ComfyTr

#endif
