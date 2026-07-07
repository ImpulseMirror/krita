/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDock.h"
#include "ComfyLocalization.h"
#include "ComfyUIRemoteDockPrivate.h"
#include "ComfyUIUtils.h"
#include "ComfyRegionPromptWidget.h"

#include "ComfyGenerateUi.h"

#include <QPlainTextEdit>
#include <QCompleter>
#include <QStringListModel>
#include <KSharedConfig>

#include "ComfyUIRemoteDockShellInternal.h"

using namespace ComfyDockShellInternal;

namespace {

bool tagCompletionTokenChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char(':') || ch == QLatin1Char('-');
}

QString tagCompletionPrefixAtCursor(QPlainTextEdit *e)
{
    if (!e)
        return {};
    QTextCursor c = e->textCursor();
    const QString t = e->toPlainText();
    int pos = c.position();
    int start = pos;
    while (start > 0 && tagCompletionTokenChar(t.at(start - 1)))
        --start;
    return t.mid(start, pos - start);
}

} // namespace


void ComfyUIRemoteDock::refreshPromptTagCompleter()
{
    if (!m_d->tagKeywordModel)
        return;
    m_d->tagKeywordModel->setStringList(ComfyUIUtils::tagKeywordsForAutocomplete());
}
void ComfyUIRemoteDock::refreshQueueResolutionRowVisibility()
{
    if (!m_d->generate.queueResolutionRow)
        return;
    QString preset = ComfyUIUtils::loadSettingsJson().value(QStringLiteral("performance_preset")).toString();
    if (preset.isEmpty())
        preset = QStringLiteral("auto");
    m_d->generate.queueResolutionRow->setVisible(preset == QLatin1String("custom"));
}
void ComfyUIRemoteDock::persistSeedToConfig()
{
    if (!m_d->generate.checkFixedSeed || !m_d->generate.spinSeed)
        return;
    KConfigGroup cfg = KSharedConfig::openConfig()->group("ComfyUIRemote");
    cfg.writeEntry("FixedSeed", m_d->generate.checkFixedSeed->isChecked());
    cfg.writeEntry("SeedValue", static_cast<qint64>(m_d->generate.spinSeed->value()));
}
void ComfyUIRemoteDock::refreshQueuePopupSupportsBatch()
{
    const int ws = m_d->comboWorkspace ? m_d->comboWorkspace->currentIndex() : 0;
    const bool supportsBatch = (ws == 0);
    if (m_d->generate.queueBatchOptionsRow)
        m_d->generate.queueBatchOptionsRow->setVisible(supportsBatch);
    if (m_d->generate.queueEnqueueModeRow)
        m_d->generate.queueEnqueueModeRow->setVisible(supportsBatch);
}
void ComfyUIRemoteDock::showPromptTagCompletion(QPlainTextEdit *editor)
{
    if (!editor)
        return;
    QCompleter *comp = nullptr;
    QPlainTextEdit *positive = m_d->generate.editPrompt;
    QPlainTextEdit *negative = m_d->generate.editNegative;
    if (m_d->generate.regionPromptWidget) {
        if (QPlainTextEdit *rp = m_d->generate.regionPromptWidget->positivePromptEditor())
            positive = rp;
        if (QPlainTextEdit *rn = m_d->generate.regionPromptWidget->negativePromptEditor())
            negative = rn;
    }
    if (editor == positive)
        comp = m_d->promptTagCompleter;
    else if (editor == negative)
        comp = m_d->negativePromptTagCompleter;
    if (!comp || !m_d->tagKeywordModel)
        return;
    if (m_d->tagKeywordModel->stringList().isEmpty()) {
        setStatusMessage(ComfyTr::tr("No tags loaded. Add CSV files (e.g. Danbooru.csv) to the tag folder and enable stems under Settings → Interface."),
                         false,
                         true);
        return;
    }
    const QString prefix = tagCompletionPrefixAtCursor(editor);
    comp->setCompletionPrefix(prefix);
    QRect cr = editor->cursorRect();
    cr.setWidth(qMax(cr.width(), 220));
    comp->complete(cr);
}
void ComfyUIRemoteDock::insertPromptTagCompletion(QPlainTextEdit *editor, const QString &completion)
{
    if (!editor || completion.isEmpty())
        return;
    const QString prefix = tagCompletionPrefixAtCursor(editor);
    QTextCursor tc = editor->textCursor();
    const int n = prefix.length();
    if (n > 0)
        tc.setPosition(tc.position() - n, QTextCursor::KeepAnchor);
    tc.removeSelectedText();
    // §13.138: literal parentheses in tag names must not break attention syntax
    QString escaped = completion;
    escaped.replace(QLatin1Char('('), QStringLiteral("\\("));
    escaped.replace(QLatin1Char(')'), QStringLiteral("\\)"));
    tc.insertText(escaped);
    editor->setTextCursor(tc);
}
