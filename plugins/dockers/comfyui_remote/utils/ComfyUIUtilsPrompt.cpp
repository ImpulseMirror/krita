/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIUtils.h"
#include "ComfyLocalization.h"
#include "ComfyResources.h"
#include "ComfyTheme.h"

#include <QImageReader>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTextStream>

namespace ComfyUIUtils {

// §13.35: strip_prompt_comments — text after '#' removed unless escaped as '\#'
QString stripPromptComments(QString text)
{
    const QChar hash = QLatin1Char('#');
    const QChar backslash = QLatin1Char('\\');
    QStringList lines = text.split(QLatin1Char('\n'));
    QStringList result;
    for (const QString &line : lines) {
        QString out;
        for (int i = 0; i < line.size(); ++i) {
            if (line[i] == backslash && i + 1 < line.size() && line[i + 1] == hash) {
                out.append(line.mid(i, 2));
                ++i;
                continue;
            }
            if (line[i] == hash) {
                break;
            }
            out.append(line[i]);
        }
        result.append(out);
    }
    return result.join(QLatin1Char('\n'));
}

QString sanitizePrompt(const QString &prompt)
{
    if (prompt.trimmed().isEmpty())
        return QStringLiteral("no prompt");
    QString s = prompt.left(40);
    QString out;
    for (const QChar &c : s) {
        if (c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('_') || c == QLatin1Char('-'))
            out.append(c);
    }
    return out.trimmed().isEmpty() ? QStringLiteral("no prompt") : out.trimmed();
}


QString formatSaveImageFileName(const QString &templateStr, const QString &documentName, const QString &jobTimestamp,
                                int jobIndex1Based, const QString &promptTrimmed)
{
    QString t = templateStr.trimmed();
    if (t.isEmpty())
        t = QStringLiteral("{document_name}-generated-{job_timestamp}-{job_index}-{prompt}");

    auto sanitizeDocName = [](const QString &s) {
        QString o;
        for (const QChar &c : s) {
            if (c != QLatin1Char('/') && c != QLatin1Char('\\') && c != QLatin1Char(':') && c != QLatin1Char('*')
                && c != QLatin1Char('?') && c != QLatin1Char('"') && c != QLatin1Char('<') && c != QLatin1Char('>')
                && c != QLatin1Char('|'))
                o.append(c);
        }
        const QString tr = o.trimmed();
        return tr.isEmpty() ? QStringLiteral("image") : tr;
    };

    const QString promptSeg = sanitizePrompt(promptTrimmed);
    QString out = t;
    out.replace(QStringLiteral("{document_name}"), sanitizeDocName(documentName));
    out.replace(QStringLiteral("{job_timestamp}"), jobTimestamp);
    out.replace(QStringLiteral("{job_index}"), QString::number(jobIndex1Based));
    out.replace(QStringLiteral("{prompt}"), promptSeg);

    QString final;
    for (const QChar &c : out) {
        if (c.unicode() < 32 || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
            || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"') || c == QLatin1Char('<')
            || c == QLatin1Char('>') || c == QLatin1Char('|'))
            final.append(QLatin1Char('_'));
        else
            final.append(c);
    }
    final = final.trimmed();
    return final.isEmpty() ? QStringLiteral("generated") : final;
}

int saveImageQualityJpeg(const QJsonObject &settings)
{
    if (settings.contains(QStringLiteral("save_image_quality_jpeg")))
        return qBound(0, settings.value(QStringLiteral("save_image_quality_jpeg")).toInt(85), 100);
    if (settings.contains(QStringLiteral("save_image_jpeg_quality")))
        return qBound(0, settings.value(QStringLiteral("save_image_jpeg_quality")).toInt(90), 100);
    return 85;
}

int saveImageQualityWebp(const QJsonObject &settings)
{
    return qBound(0, settings.value(QStringLiteral("save_image_quality_webp")).toInt(80), 100);
}

// §13.201: Smallest parenthesis or angle block containing cursor; else (-1, 0)
static std::pair<int, int> selectParenthesisBlock(const QString &text, int cursorPos,
    const QChar &openCh, const QChar &closeCh)
{
    if (cursorPos < 0 || cursorPos > text.size()) return {-1, 0};
    int start = -1;
    int depth = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == openCh) {
            if (depth == 0) start = i;
            ++depth;
        } else if (text[i] == closeCh) {
            --depth;
            if (depth == 0 && start >= 0 && cursorPos >= start && cursorPos <= i + 1)
                return {start, i - start + 1};
        }
    }
    return {-1, 0};
}

// §13.201: Word (alphanumeric/underscore) containing cursor
static std::pair<int, int> selectCurrentWord(const QString &text, int cursorPos)
{
    if (cursorPos < 0 || cursorPos > text.size()) return {-1, 0};
    int start = cursorPos;
    while (start > 0) {
        QChar c = text[start - 1];
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            --start;
        else
            break;
    }
    int end = cursorPos;
    while (end < text.size()) {
        QChar c = text[end];
        if (c.isLetterOrNumber() || c == QLatin1Char('_'))
            ++end;
        else
            break;
    }
    if (start >= end) return {-1, 0};
    return {start, end - start};
}

std::pair<int, int> attentionSegmentRange(const QString &text, int cursorPos)
{
    if (text.isEmpty() || cursorPos < 0) return {-1, 0};
    cursorPos = qBound(0, cursorPos, text.size());
    // §8.5 / §13.35 / §13.201: bracket pairs (), <>, [], {} — same order as inner checks in reference parse path
    auto pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('('), QLatin1Char(')'));
    if (pr.first >= 0) return pr;
    pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('<'), QLatin1Char('>'));
    if (pr.first >= 0) return pr;
    pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('['), QLatin1Char(']'));
    if (pr.first >= 0) return pr;
    pr = selectParenthesisBlock(text, cursorPos, QLatin1Char('{'), QLatin1Char('}'));
    if (pr.first >= 0) return pr;
    return selectCurrentWord(text, cursorPos);
}

// §8.5 / §13.35: Parse (word:weight), <…>, […], {…}; adjust weight by delta; clamp [−2.0, 2.0]
QString editAttentionWeight(const QString &segment, double delta)
{
    if (segment.isEmpty()) return segment;
    QChar openCh = segment[0];
    QChar closeCh;
    if (openCh == QLatin1Char('('))
        closeCh = QLatin1Char(')');
    else if (openCh == QLatin1Char('<'))
        closeCh = QLatin1Char('>');
    else if (openCh == QLatin1Char('['))
        closeCh = QLatin1Char(']');
    else if (openCh == QLatin1Char('{'))
        closeCh = QLatin1Char('}');
    else
        return segment;
    int closeIdx = segment.indexOf(closeCh, 1);
    if (closeIdx < 0) return segment;
    QString inner = segment.mid(1, closeIdx - 1).trimmed();
    double weight = 1.0;
    int colonIdx = inner.indexOf(QLatin1Char(':'));
    if (colonIdx >= 0) {
        bool ok = false;
        weight = inner.mid(colonIdx + 1).trimmed().toDouble(&ok);
        if (!ok) weight = 1.0;
        inner = inner.left(colonIdx).trimmed();
    }
    weight = qBound(-2.0, weight + delta, 2.0);
    QString out;
    out.append(openCh);
    if (!inner.isEmpty()) out.append(inner);
    if (qAbs(weight - 1.0) > 1e-6) {
        out.append(QLatin1Char(':'));
        out.append(QString::number(weight, 'f', 1));
    }
    out.append(closeCh);
    return out;
}
// §13.35: eval_wildcards — {option1|option2|...}, deterministic from seed, evaluated during workflow build
QString evalWildcards(QString text, quint32 seed)
{
    QRandomGenerator rng(seed);
    const int maxIterations = 10;
    for (int iter = 0; iter < maxIterations; ++iter) {
        int start = text.indexOf(QLatin1Char('{'));
        if (start < 0) break;
        int depth = 1;
        int end = start + 1;
        for (; end < text.size(); ++end) {
            QChar c = text[end];
            if (c == QLatin1Char('{')) ++depth;
            else if (c == QLatin1Char('}')) {
                --depth;
                if (depth == 0) break;
            }
        }
        if (end >= text.size()) break;
        QString content = text.mid(start + 1, end - start - 1);
        QStringList options;
        int d = 0;
        int segStart = 0;
        for (int i = 0; i <= content.size(); ++i) {
            if (i == content.size()) {
                options.append(content.mid(segStart).trimmed());
                break;
            }
            QChar c = content[i];
            if (c == QLatin1Char('{')) ++d;
            else if (c == QLatin1Char('}')) --d;
            else if (c == QLatin1Char('|') && d == 0) {
                options.append(content.mid(segStart, i - segStart).trimmed());
                segStart = i + 1;
            }
        }
        if (options.isEmpty()) break;
        int idx = static_cast<int>(rng.bounded(static_cast<quint32>(options.size())));
        QString chosen = options.at(idx);
        text.replace(start, end - start + 1, chosen);
    }
    return text;
}

// §13.35: extract_layers — <layer:name> replaced with arch-specific template, returns ordered layer names
QString layerPlaceholderReplacementForArch(ComfyResources::Arch arch)
{
    if (arch == ComfyResources::Arch::Flux2_4b || arch == ComfyResources::Arch::Flux2_9b)
        return QStringLiteral("image {}");
    if (arch == ComfyResources::Arch::QwenEP)
        return QStringLiteral("Picture {}");
    return QStringLiteral("Picture {}");
}

QStringList extractLayerPlaceholders(QString &prompt, const QString &replacementTemplate)
{
    QStringList layerNames;
    const QString prefix = QStringLiteral("<layer:");
    int idx = 0;
    int n = 1;
    while ((idx = prompt.indexOf(prefix, idx)) >= 0) {
        int nameStart = idx + prefix.size();
        int end = prompt.indexOf(QLatin1Char('>'), nameStart);
        if (end < 0)
            break;
        QString name = prompt.mid(nameStart, end - nameStart).trimmed();
        if (!name.isEmpty()) {
            layerNames.append(name);
            QString replacement = replacementTemplate;
            if (replacement.contains(QStringLiteral("{}")))
                replacement.replace(QStringLiteral("{}"), QString::number(n));
            else
                replacement = replacementTemplate.arg(n);
            prompt.replace(idx, end - idx + 1, replacement);
            n++;
            idx += replacement.size();
        } else {
            idx = end + 1;
        }
    }
    return layerNames;
}
QString buildInpaintPromptInstructions(const QString &mode, const QString &archKey)
{
    const ComfyResources::Arch arch = ComfyResources::archFromKey(archKey);
    if (!ComfyResources::supportsEditInstructions(arch))
        return QString();
    if (mode == QLatin1String("fill") || mode == QLatin1String("expand")) {
        if (arch == ComfyResources::Arch::Flux2_4b)
            return QStringLiteral("Fill the green spaces according to the image.\n");
        return QString();
    }
    if (mode == QLatin1String("add_object"))
        return QStringLiteral("Add the object to the scene.\n");
    if (mode == QLatin1String("remove_object"))
        return QStringLiteral("Remove the object.\n");
    if (mode == QLatin1String("replace_background"))
        return QStringLiteral("Replace the background while keeping the main subject.\n");
    return QString();
}

QString prependInpaintPromptInstructions(const QString &prompt, const QString &mode, const QString &archKey)
{
    const QString instr = buildInpaintPromptInstructions(mode, archKey);
    if (instr.isEmpty())
        return prompt;
    if (prompt.startsWith(instr))
        return prompt;
    return instr + prompt;
}
// §13.215: Tag CSV — columns tag, type, count, aliases; returns tag column for autocomplete
QStringList loadTagCsvTags(const QString &filePath)
{
    QStringList tags;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return tags;
    QTextStream in(&f);
    QString headerLine = in.readLine();
    if (headerLine.isEmpty()) return tags;
    QStringList headers = headerLine.split(QLatin1Char(','));
    int tagCol = -1;
    for (int i = 0; i < headers.size(); ++i) {
        if (headers[i].trimmed() == QLatin1String("tag")) {
            tagCol = i;
            break;
        }
    }
    if (tagCol < 0) return tags;
    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList cols = line.split(QLatin1Char(','));
        if (tagCol < cols.size()) {
            QString tag = cols[tagCol].trimmed();
            if (!tag.isEmpty())
                tags.append(tag);
        }
    }
    return tags;
}

// §13.4: Read A1111 "parameters" or ComfyUI workflow JSON (prompt/workflow text) from image metadata
QPair<QString, QString> readPromptFromImageFile(const QString &filePath)
{
    QPair<QString, QString> out;
    if (filePath.isEmpty()) return out;
    QImageReader reader(filePath);
    QString params = reader.text(QStringLiteral("parameters"));
    if (!params.isEmpty()) {
        const int negIdx = params.indexOf(QStringLiteral("Negative prompt:"));
        if (negIdx < 0) {
            out.first = params.trimmed();
            return out;
        }
        out.first = params.left(negIdx).trimmed();
        QString rest = params.mid(negIdx + 15).trimmed();
        const int stepsIdx = rest.indexOf(QStringLiteral("Steps:"));
        out.second = stepsIdx >= 0 ? rest.left(stepsIdx).trimmed() : rest;
        return out;
    }
    // ComfyUI format: JSON in "prompt" or "workflow" text, CLIPTextEncode nodes
    QString promptJson = reader.text(QStringLiteral("prompt"));
    if (promptJson.isEmpty()) promptJson = reader.text(QStringLiteral("workflow"));
    if (promptJson.isEmpty()) return out;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(promptJson.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return out;
    QJsonObject root = doc.object();
    QStringList clipTexts;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().isObject()) continue;
        QJsonObject node = it.value().toObject();
        if (node.value(QStringLiteral("class_type")).toString() != QLatin1String("CLIPTextEncode")) continue;
        QJsonObject inputs = node.value(QStringLiteral("inputs")).toObject();
        QString text = inputs.value(QStringLiteral("text")).toString();
        if (!text.isEmpty()) clipTexts.append(text);
    }
    if (clipTexts.size() >= 1) out.first = clipTexts.at(0);
    if (clipTexts.size() >= 2) out.second = clipTexts.at(1);
    return out;
}

QString activePromptTranslationLanguage(const QJsonObject &settingsRoot)
{
    QJsonObject s = settingsRoot;
    if (s.isEmpty())
        s = loadSettingsJson();
    if (!s.value(QStringLiteral("translation_enabled")).toBool(false))
        return QString();
    QString code = s.value(QStringLiteral("prompt_translation")).toString().trimmed();
    if (code.isEmpty() || code == QLatin1String("disabled"))
        return QString();
    return code;
}

QString wrapPromptWithTranslationLanguage(const QString &prompt, const QString &languageCode)
{
    const QString p = prompt.trimmed();
    const QString lang = languageCode.trimmed();
    if (lang.isEmpty() || p.isEmpty())
        return prompt;
    return QStringLiteral("lang:%1 %2 lang:en ").arg(lang, prompt);
}

QString mergeStylePromptWithInstruction(const QString &styleTemplate, const QString &userInstruction)
{
    const QString u = userInstruction.trimmed();
    const QString t = styleTemplate.trimmed();
    if (styleTemplate.contains(QLatin1String("{prompt}")))
        return QString(styleTemplate).replace(QLatin1String("{prompt}"), userInstruction);
    if (t.isEmpty())
        return u;
    if (u.isEmpty())
        return t;
    return t + QLatin1String(", ") + u;
}

} // namespace ComfyUIUtils
