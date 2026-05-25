/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_STYLE_COLLECTION_H_
#define COMFY_STYLE_COLLECTION_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

struct ComfyStyleEntry {
    QString filepath;
    QString styleId; ///< built-in/foo.json or user-relative path (Python Style.filename)
    QString name;
    QString architecture;
    QStringList checkpoints;
    QJsonArray loras;
    QString stylePrompt;
    QString negativePrompt;
    QString vae;
    int clipSkip = 0;
    bool vPredictionZsnr = false;
    double rescaleCfg = 0.7;
    bool selfAttentionGuidance = false;
    int preferredResolution = 0;
    QString linkedEditStyle;
    QString samplerPresetName;
    int samplerSteps = 20;
    double cfgScale = 7.0;
    QString liveSamplerPresetName;
    int liveSamplerSteps = 6;
    double liveCfgScale = 1.8;

    bool isBuiltin = false;
    bool usesNegativePrompt() const { return !negativePrompt.trimmed().isEmpty(); }
};

class ComfyStyleCollection
{
public:
    static ComfyStyleCollection &instance();

    void reload();
    const QList<ComfyStyleEntry> &all() const { return m_styles; }

    QList<const ComfyStyleEntry *> filtered(bool showBuiltin) const;
    const ComfyStyleEntry *findByStyleId(const QString &styleId) const;
    const ComfyStyleEntry *findByComboIndex(int comboIndex, bool showBuiltin, int legacyKConfigCustomCount) const;

    QString builtinStylesDir() const;
    QString userStylesDir() const;

    QJsonObject entryToJson(const ComfyStyleEntry &entry) const;
    /// Writes JSON to user styles dir (built-in → user override copy). Returns path or empty on failure.
    QString saveEntryToUserStyles(const ComfyStyleEntry &entry);

private:
    ComfyStyleCollection();
    ComfyStyleEntry loadStyleFile(const QString &path, bool isBuiltin) const;

    QList<ComfyStyleEntry> m_styles;
};

#endif
