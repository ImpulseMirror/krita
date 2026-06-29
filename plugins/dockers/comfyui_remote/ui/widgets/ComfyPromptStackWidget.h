/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_PROMPT_STACK_WIDGET_H_
#define COMFY_PROMPT_STACK_WIDGET_H_

#include <QFrame>

class QLabel;
class QPlainTextEdit;

/// FAITHFUL_PORT: upstream TextPromptWidget pair inside ActiveRegionWidget —
/// positive + optional negative flush in one bordered stack (ai_diffusion/ui/region.py).
class ComfyPromptStackWidget : public QFrame
{
    Q_OBJECT
public:
    explicit ComfyPromptStackWidget(QWidget *parent = nullptr);

    QPlainTextEdit *positiveEditor() const { return m_positive; }
    QPlainTextEdit *negativeEditor() const { return m_negative; }

    void setShowNegative(bool show);
    void setNegativeWarningVisible(bool visible);
    void setFocusChromeEnabled(bool enabled);
    /// Positive line count from settings; negative is always 1 line when visible (upstream).
    void applyLayout(int positiveLines, bool showNegative, bool showResizeHandle);
    /// When @p liveLineCounts is true, resize persists `prompt_line_count_live` (Live workspace).
    void setLiveLineCounts(bool liveLineCounts);
    QSize layoutSizeHint() const;

Q_SIGNALS:
    void layoutHeightsChanged();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    int frameHeightForLines() const;
    void applyEditorChrome();
    void applyHeights();
    void syncFrameToEditors();
    void ensureResizeHandle();
    void updateFocusBorder();
    void repositionChrome();
    void persistPositiveLineCount();
    void onNegativeHandleDragged(int yPosInNegative);

    QPlainTextEdit *m_positive = nullptr;
    QPlainTextEdit *m_negative = nullptr;
    QLabel *m_negativeWarning = nullptr;
    QWidget *m_resizeHandle = nullptr;
    bool m_showNegative = false;
    bool m_focusChromeEnabled = true;
    bool m_liveLineCounts = false;
    bool m_showResizeHandle = true;
    int m_positiveLines = 3;
    int m_frameHeightPx = 0;
};

#endif
