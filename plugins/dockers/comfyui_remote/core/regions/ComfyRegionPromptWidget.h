/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_REGION_PROMPT_WIDGET_H_
#define COMFY_REGION_PROMPT_WIDGET_H_

#include <QWidget>

class KisViewManager;
class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class QMenu;

#include "ComfyUIRemoteDockPrivate.h"

class ComfyRegionPromptWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ComfyRegionPromptWidget(QWidget *parent = nullptr);

    void setViewManager(KisViewManager *viewManager);
    void setPromptHeaderMode(int mode);
    void setLiveSingleRegionMode(bool liveSingleRegion);
    void setShowNegativePrompt(bool show);
    void setNegativePromptWarningVisible(bool visible);
    void setPromptTranslationCode(const QString &code);
    QPlainTextEdit *positivePromptEditor() const { return m_editPrompt; }
    QPlainTextEdit *negativePromptEditor() const { return m_editNegative; }
    void setRootPromptEditors(QPlainTextEdit *positive, QPlainTextEdit *negative);
    void embedRegionControlPanel(QWidget *panel);
    void setRootControlLayers(QList<ComfyControlLayerEntry> *layers);
    void bind(QList<ComfyUIRemoteDock::Private::RegionEntry> *regions, int *activeIndex);
    void refresh();
    /// Refresh control-mode icons on active header + inactive chips (after control list edits).
    void refreshControlIcons();
    void onActiveLayerChanged();
    void focusPromptEditor();
    void commitRootPromptEditors();
    void refreshRootPromptFromDock();
    /// Compact docker: @p positiveLines from settings; negative is always 1 line (FAITHFUL_PORT).
    void applyCompactLayout(int positiveLines,
                            bool showNegative,
                            bool showResizeHandle,
                            bool liveLineCounts = false);
    void syncCompactHeightFromLayout();
    int currentPositiveLineCount() const;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

Q_SIGNALS:
    void activeIndexChanged(int index);
    void regionEdited();
    void removeRegionRequested();
    void requestAddRegion();
    void translatePromptRequested(bool negative);
    void requestLinkActiveToRegion(int regionIndex);
    void editingModeChanged(int activeIndex);
    void layoutHeightsChanged();

private:
    enum class EditorMode { Root, Region, Unlinked, Empty };

    void clearLayout(QLayout *layout);
    void rebuildInactiveChips();
    void rebuildActiveEditor();
    void setActiveIndex(int index, bool emitSignal = true);
    EditorMode currentMode() const;
    void syncActiveEditorFromRegion();
    void commitActiveEditorToRegion();
    void syncRootPromptsFromDock();
    void pushRootPromptsToDock();
    void updateLinkButton();
    void updateNoRegionStrip();
    void applyPromptLineHeights();
    void applyPromptLineHeights(int positiveLines);
    void showLinkMenu(QPushButton *anchor);

    KisViewManager *m_viewManager = nullptr;
    QList<ComfyUIRemoteDock::Private::RegionEntry> *m_regions = nullptr;
    int *m_activeIndex = nullptr;
    int m_promptHeaderMode = 0;
    bool m_liveSingleRegionMode = false;
    bool m_showNegativePrompt = true;
    QString m_promptTranslationCode;

    QPlainTextEdit *m_dockRootPositive = nullptr;
    QPlainTextEdit *m_dockRootNegative = nullptr;

    QVBoxLayout *m_inactiveAbove = nullptr;
    QFrame *m_activeFrame = nullptr;
    QVBoxLayout *m_inactiveBelow = nullptr;
    QWidget *m_controlPanelHost = nullptr;

    QLabel *m_headerIcon = nullptr;
    QLabel *m_headerLabel = nullptr;
    QWidget *m_headerHost = nullptr;
    QPushButton *m_btnLink = nullptr;
    QPushButton *m_btnRemove = nullptr;
    class ComfyPromptStackWidget *m_promptStack = nullptr;
    QPlainTextEdit *m_editPrompt = nullptr;
    QPlainTextEdit *m_editNegative = nullptr;

    QLabel *m_emptyHint = nullptr;

    QWidget *m_noRegionStrip = nullptr;
    QLabel *m_noRegionLabel = nullptr;
    QPushButton *m_btnNewRegion = nullptr;
    QPushButton *m_btnLinkRegionMenu = nullptr;
    bool m_showResizeHandle = true;
    bool m_liveLineCounts = false;
    QList<ComfyControlLayerEntry> *m_rootControlLayers = nullptr;

    bool m_syncingEditor = false;
};
#endif
