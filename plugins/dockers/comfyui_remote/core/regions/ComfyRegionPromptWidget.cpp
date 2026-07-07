/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyRegionPromptWidget.h"
#include "ComfyTextArea.h"
#include "ComfyPromptStackWidget.h"
#include "ComfyPromptLayoutMetrics.h"
#include "ComfyLocalization.h"
#include "ComfyRegionLink.h"
#include "ComfyControlLayer.h"
#include "ComfyUIUtils.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUiLayoutDiagnostics.h"

#include <QGuiApplication>
#include <QLoggingCategory>

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QEvent>
#include <QFontMetrics>
#include <QImage>
#include <QJsonObject>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QResizeEvent>
#include <QSet>

#include <KoColorConversionTransformation.h>
#include <kis_paint_device.h>

#include <klocalizedstring.h>
#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_node.h>
#include <kis_icon_utils.h>

Q_DECLARE_LOGGING_CATEGORY(KIS_COMFYUI_REMOTE)

namespace {

class InactiveRegionChip : public QFrame
{
public:
    explicit InactiveRegionChip(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("InactiveRegionWidget"));
        setFrameStyle(QFrame::StyledPanel);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(40);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setStyleSheet(QStringLiteral("QFrame#InactiveRegionWidget { background-color: %1; }")
                          .arg(ComfyUiStyle::colors().secondaryPanel));
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(6);
        m_thumb = new QLabel(this);
        m_thumb->setFixedSize(32, 32);
        m_thumb->setAlignment(Qt::AlignCenter);
        m_prompt = new QLabel(this);
        m_prompt->setWordWrap(false);
        m_icons = new QHBoxLayout();
        m_icons->setSpacing(2);
        row->addWidget(m_thumb, 0, Qt::AlignTop);
        row->addWidget(m_prompt, 1);
        row->addLayout(m_icons);
    }

    void setContent(const QPixmap &thumb, const QString &text, const QStringList &controlModeStems, bool placeholder)
    {
        if (thumb.isNull()) {
            m_thumb->clear();
            m_thumb->hide();
            m_thumb->setFixedSize(0, 0);
        } else {
            m_thumb->show();
            m_thumb->setFixedSize(32, 32);
            m_thumb->setPixmap(thumb.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        m_displayText = text;
        m_placeholder = placeholder;
        while (QLayoutItem *it = m_icons->takeAt(0)) {
            if (QWidget *w = it->widget())
                w->deleteLater();
            delete it;
        }
        const int iconSize = qMax(14, static_cast<int>(fontMetrics().height() * 1.2));
        for (const QString &stem : controlModeStems) {
            auto *ic = new QLabel(this);
            ic->setPixmap(ComfyTheme::icon(stem).pixmap(iconSize, iconSize));
            m_icons->addWidget(ic);
        }
        updateClippedText();
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QFrame::resizeEvent(e);
        updateClippedText();
    }

private:
    void updateClippedText()
    {
        if (m_displayText.isEmpty()) {
            m_prompt->clear();
            return;
        }
        const QFontMetrics fm(m_prompt->font());
        const int w = qMax(20, m_prompt->width() - 4);
        m_prompt->setText(fm.elidedText(m_displayText, Qt::ElideRight, w));
        m_prompt->setToolTip(m_displayText);
        if (m_placeholder) {
            QFont f = m_prompt->font();
            f.setItalic(true);
            m_prompt->setFont(f);
            m_prompt->setStyleSheet(QStringLiteral("color: %1;").arg(ComfyUiStyle::colors().secondaryText));
        } else {
            QFont f = m_prompt->font();
            f.setItalic(false);
            m_prompt->setFont(f);
            m_prompt->setStyleSheet(QStringLiteral("color: %1;").arg(ComfyUiStyle::colors().primaryText));
        }
    }

    QLabel *m_thumb = nullptr;
    QLabel *m_prompt = nullptr;
    QHBoxLayout *m_icons = nullptr;
    QString m_displayText;
    bool m_placeholder = false;
};

QPixmap thumbnailForRegion(const ComfyUIRemoteDock::Private::RegionEntry &entry,
                           KisImageSP image,
                           bool isRoot)
{
    Q_UNUSED(entry);
    Q_UNUSED(image);
    if (isRoot)
        return ComfyTheme::icon(QStringLiteral("root")).pixmap(32, 32);
    return QPixmap();
}

QString inactivePromptText(const ComfyUIRemoteDock::Private::RegionEntry &r, bool isRoot, bool *outPlaceholder = nullptr)
{
    QString prompt = r.prompt;
    prompt.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (prompt.isEmpty()) {
        if (outPlaceholder)
            *outPlaceholder = true;
        if (isRoot)
            return QObject::tr("Common text prompt - click to add content");
        return QObject::tr("%1 - click to add regional text").arg(r.name);
    }
    if (outPlaceholder)
        *outPlaceholder = false;
    return prompt;
}

QStringList controlIconStems(const QList<ComfyControlLayerEntry> &layers)
{
    QStringList stems;
    for (const ComfyControlLayerEntry &c : layers) {
        QString mode = c.mode;
        mode.replace(QLatin1Char('_'), QLatin1Char('-'));
        stems.append(QStringLiteral("control-") + mode);
    }
    return stems;
}

bool regionHasLayerLinks(const ComfyUIRemoteDock::Private::RegionEntry &entry)
{
    return !ComfyRegionLink::parseLayerIds(entry.layerIds).isEmpty();
}

KisLayerSP primaryLinkedLayer(const ComfyUIRemoteDock::Private::RegionEntry &entry, KisImageSP image)
{
    for (const QString &idStr : ComfyRegionLink::parseLayerIds(entry.layerIds)) {
        if (KisLayerSP layer = ComfyRegionLink::findLayerByUuid(image, QUuid(idStr)))
            return ComfyRegionLink::linkTarget(layer);
    }
    return KisLayerSP();
}

KisNodeSP stackAnchorUnderRoot(KisLayerSP layer, KisGroupLayerSP root)
{
    if (!layer || !root)
        return KisNodeSP();
    KisNodeSP rootNode = root;
    KisNodeSP node = layer;
    while (node->parent() && node->parent() != rootNode)
        node = node->parent();
    return node->parent() == rootNode ? node : KisNodeSP();
}

int regionIndexForStackNode(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions,
                            KisImageSP image,
                            KisGroupLayerSP root,
                            KisNodeSP stackNode,
                            int skipIndex = -1)
{
    if (!stackNode)
        return -1;
    for (int i = 0; i < regions.size(); ++i) {
        if (i == skipIndex || !regionHasLayerLinks(regions.at(i)))
            continue;
        if (stackAnchorUnderRoot(primaryLinkedLayer(regions.at(i), image), root) == stackNode)
            return i;
    }
    return -1;
}

QList<int> unlinkedRegionIndices(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions, int skipIndex)
{
    QList<int> out;
    for (int i = 0; i < regions.size(); ++i) {
        if (i != skipIndex && !regionHasLayerLinks(regions.at(i)))
            out.append(i);
    }
    return out;
}

QList<int> linkedRegionsTopToBottom(const QList<ComfyUIRemoteDock::Private::RegionEntry> &regions,
                                    KisImageSP image,
                                    int skipIndex = -1)
{
    QList<int> ordered;
    QSet<int> used;
    if (!image)
        return ordered;
    KisGroupLayerSP root = image->rootLayer();
    if (!root)
        return ordered;
    for (KisNodeSP node = root->lastChild(); node; node = node->prevSibling()) {
        const int idx = regionIndexForStackNode(regions, image, root, node, skipIndex);
        if (idx >= 0 && !used.contains(idx)) {
            ordered.append(idx);
            used.insert(idx);
        }
    }
    return ordered;
}

void layerSiblingNodesBottomToTop(KisNodeSP anchor, QList<KisNodeSP> *below, QList<KisNodeSP> *above)
{
    if (!anchor || !below || !above)
        return;
    for (KisNodeSP s = anchor->prevSibling(); s; s = s->prevSibling())
        below->append(s);
    for (KisNodeSP s = anchor->nextSibling(); s; s = s->nextSibling())
        above->append(s);
}

} // namespace

ComfyRegionPromptWidget::ComfyRegionPromptWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RegionPromptWidget"));
    setFocusPolicy(Qt::NoFocus);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_inactiveAbove = new QVBoxLayout();
    m_inactiveAbove->setSpacing(2);
    rootLayout->addLayout(m_inactiveAbove);

    m_activeFrame = new ComfyTextInputFrame(this);
    m_activeFrame->setObjectName(QStringLiteral("ActiveRegionWidget"));
    m_activeFrame->setFrameShape(QFrame::NoFrame);
    m_activeFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *activeLay = new QVBoxLayout(m_activeFrame);
    activeLay->setContentsMargins(0, 0, 0, 0);
    activeLay->setSpacing(0);
    activeLay->setAlignment(Qt::AlignTop);

    m_headerHost = new QWidget(m_activeFrame);
    auto *headerRow = new QHBoxLayout(m_headerHost);
    headerRow->setContentsMargins(4, 4, 2, 0);
    headerRow->setSpacing(4);
    m_headerIcon = new QLabel(m_headerHost);
    m_headerIcon->setFixedSize(18, 18);
    m_headerIcon->setAlignment(Qt::AlignCenter);
    m_headerLabel = new QLabel(m_headerHost);
    m_headerLabel->setWordWrap(true);
    m_btnLink = new QPushButton(m_headerHost);
    m_btnLink->setFlat(true);
    m_btnLink->setIcon(
        ComfyTheme::icon(QStringLiteral("link")));
    m_btnRemove = new QPushButton(m_headerHost);
    m_btnRemove->setFlat(true);
    m_btnRemove->setToolTip(ComfyTr::tr("Remove this region"));
    m_btnRemove->setIcon(
        ComfyTheme::icon(QStringLiteral("remove")));
    headerRow->addWidget(m_headerIcon);
    headerRow->addWidget(m_headerLabel, 1);
    headerRow->addWidget(m_btnLink);
    headerRow->addWidget(m_btnRemove);

    m_promptStack = new ComfyPromptStackWidget(m_activeFrame);
    m_promptStack->setHeaderWidget(m_headerHost);
    m_editPrompt = m_promptStack->positiveEditor();
    m_editNegative = m_promptStack->negativeEditor();
    activeLay->addWidget(m_promptStack, 0, Qt::AlignTop);
    connect(m_promptStack, &ComfyPromptStackWidget::layoutHeightsChanged, this, [this]() {
        if (!m_promptStack)
            return;
        if (m_promptStack->resizeDragging())
            syncCompactHeightFromLayout();
        else
            applyCompactLayout(m_promptStack->positiveLineCount(), m_showNegativePrompt, m_showResizeHandle, m_liveLineCounts);
        if (m_activeFrame)
            m_activeFrame->updateGeometry();
        updateGeometry();
        Q_EMIT layoutHeightsChanged();
    });

    m_noRegionStrip = new QWidget(m_activeFrame);
    auto *noLay = new QHBoxLayout(m_noRegionStrip);
    noLay->setContentsMargins(0, 0, 0, 0);
    m_noRegionLabel = new QLabel(m_noRegionStrip);
    m_noRegionLabel->setWordWrap(true);
    ComfyUiStyle::styleHint(m_noRegionLabel);
    m_btnNewRegion = new QPushButton(ComfyTr::tr("New region"), m_noRegionStrip);
    m_btnNewRegion->setIcon(
        ComfyTheme::icon(QStringLiteral("region-add")));
    m_btnLinkRegionMenu = new QPushButton(ComfyTr::tr("Link region"), m_noRegionStrip);
    m_btnLinkRegionMenu->setIcon(
        ComfyTheme::icon(QStringLiteral("link")));
    noLay->addWidget(m_noRegionLabel, 1);
    noLay->addWidget(m_btnNewRegion);
    noLay->addWidget(m_btnLinkRegionMenu);
    activeLay->addWidget(m_noRegionStrip);

    m_emptyHint = new QLabel(
        ComfyTr::tr("No regions yet. Use Add below, or link a layer after creating a region."), m_activeFrame);
    m_emptyHint->setWordWrap(true);
    ComfyUiStyle::styleHint(m_emptyHint);
    activeLay->addWidget(m_emptyHint);

    rootLayout->addWidget(m_activeFrame);

    m_inactiveBelow = new QVBoxLayout();
    m_inactiveBelow->setSpacing(2);
    rootLayout->addLayout(m_inactiveBelow);

    connect(m_editPrompt, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_syncingEditor || !m_regions || !m_activeIndex)
            return;
        if (currentMode() == EditorMode::Root) {
            pushRootPromptsToDock();
            return;
        }
        if (currentMode() != EditorMode::Region)
            return;
        (*m_regions)[*m_activeIndex].prompt = m_editPrompt->toPlainText();
        Q_EMIT regionEdited();
    });
    connect(m_editNegative, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_syncingEditor)
            return;
        if (currentMode() == EditorMode::Root)
            pushRootPromptsToDock();
    });
    connect(m_btnLink, &QPushButton::clicked, this, [this]() {
        if (!m_regions || !m_activeIndex || !m_viewManager || currentMode() != EditorMode::Region)
            return;
        KisLayerSP layer = m_viewManager->activeLayer();
        if (!layer)
            return;
        ComfyRegionLink::toggleActiveLayerLink(&(*m_regions)[*m_activeIndex], layer);
        if (m_viewManager->image())
            ComfyRegionLink::syncMaskSourceFromLinks(&(*m_regions)[*m_activeIndex], m_viewManager->image());
        syncActiveEditorFromRegion();
        updateLinkButton();
        Q_EMIT regionEdited();
        rebuildInactiveChips();
    });
    connect(m_btnRemove, &QPushButton::clicked, this, &ComfyRegionPromptWidget::removeRegionRequested);
    connect(m_btnNewRegion, &QPushButton::clicked, this, &ComfyRegionPromptWidget::requestAddRegion);
    connect(m_btnLinkRegionMenu, &QPushButton::clicked, this, [this]() { showLinkMenu(m_btnLinkRegionMenu); });
}

bool ComfyRegionPromptWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const QVariant idxVar = obj->property("regionIndex");
        if (idxVar.isValid()) {
            setActiveIndex(idxVar.toInt());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ComfyRegionPromptWidget::setViewManager(KisViewManager *viewManager)
{
    m_viewManager = viewManager;
    refresh();
}

void ComfyRegionPromptWidget::setPromptHeaderMode(int mode)
{
    m_promptHeaderMode = qBound(0, mode, 2);
    rebuildActiveEditor();
}

void ComfyRegionPromptWidget::setLiveSingleRegionMode(bool liveSingleRegion)
{
    if (m_liveSingleRegionMode == liveSingleRegion)
        return;
    m_liveSingleRegionMode = liveSingleRegion;
    rebuildInactiveChips();
    syncActiveEditorFromRegion();
    Q_EMIT layoutHeightsChanged();
}

void ComfyRegionPromptWidget::setShowNegativePrompt(bool show)
{
    m_showNegativePrompt = show;
    rebuildActiveEditor();
}

void ComfyRegionPromptWidget::setPromptTranslationCode(const QString &code)
{
    m_promptTranslationCode = code.isEmpty() || code == QLatin1String("disabled") ? QStringLiteral("EN") : code;
}

void ComfyRegionPromptWidget::setNegativePromptWarningVisible(bool visible)
{
    if (m_promptStack)
        m_promptStack->setNegativeWarningVisible(visible);
}

void ComfyRegionPromptWidget::setRootPromptEditors(QPlainTextEdit *positive, QPlainTextEdit *negative)
{
    m_dockRootPositive = positive;
    m_dockRootNegative = negative;
}

void ComfyRegionPromptWidget::embedRegionControlPanel(QWidget *panel)
{
    if (!panel || m_controlPanelHost)
        return;
    m_controlPanelHost = panel;
    if (auto *rootLayout = qobject_cast<QVBoxLayout *>(layout())) {
        rootLayout->addSpacing(4);
        rootLayout->addWidget(panel);
    }
    updateGeometry();
}

void ComfyRegionPromptWidget::bind(QList<ComfyUIRemoteDock::Private::RegionEntry> *regions, int *activeIndex)
{
    m_regions = regions;
    m_activeIndex = activeIndex;
    refresh();
}

void ComfyRegionPromptWidget::refresh()
{
    if (!m_regions || !m_activeIndex)
        return;
    if (m_regions->isEmpty()) {
        *m_activeIndex = ComfyRegionLink::kRootRegionIndex;
    } else if (*m_activeIndex >= m_regions->size()) {
        *m_activeIndex = 0;
    } else if (*m_activeIndex < ComfyRegionLink::kUnlinkedRegionIndex
               && *m_activeIndex != ComfyRegionLink::kRootRegionIndex) {
        *m_activeIndex = 0;
    }
    rebuildInactiveChips();
    rebuildActiveEditor();
    Q_EMIT editingModeChanged(*m_activeIndex);
    Q_EMIT layoutHeightsChanged();
}

void ComfyRegionPromptWidget::onActiveLayerChanged()
{
    // FAITHFUL_PORT: upstream ActiveRegionWidget — layer changes refresh link affordances
    // only (_update_links / _update_actions). Region prompt selection stays user-driven.
    if (!m_regions || !m_activeIndex || !m_viewManager)
        return;
    updateLinkButton();
    updateNoRegionStrip();
}

void ComfyRegionPromptWidget::focusPromptEditor()
{
    if (m_editNegative && m_editNegative->isVisible() && m_editNegative->hasFocus())
        return;
    if (m_editPrompt && m_editPrompt->isVisible())
        m_editPrompt->setFocus(Qt::OtherFocusReason);
    else if (m_editNegative && m_editNegative->isVisible())
        m_editNegative->setFocus(Qt::OtherFocusReason);
}

QVariant ComfyRegionPromptWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    // Android IME queries ancestors of the focused editor, not the editor itself.
    return ComfyTextArea::forwardContainerInputMethodQuery(this, query);
}

ComfyRegionPromptWidget::EditorMode ComfyRegionPromptWidget::currentMode() const
{
    if (!m_regions || !m_activeIndex)
        return EditorMode::Empty;
    if (*m_activeIndex == ComfyRegionLink::kRootRegionIndex)
        return EditorMode::Root;
    if (m_regions->isEmpty())
        return EditorMode::Empty;
    if (*m_activeIndex == ComfyRegionLink::kUnlinkedRegionIndex)
        return EditorMode::Unlinked;
    if (*m_activeIndex >= 0 && *m_activeIndex < m_regions->size())
        return EditorMode::Region;
    return EditorMode::Empty;
}

void ComfyRegionPromptWidget::clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }
}

void ComfyRegionPromptWidget::setActiveIndex(int index, bool emitSignal)
{
    if (!m_activeIndex)
        return;
    if (index == *m_activeIndex)
        return;
    commitActiveEditorToRegion();
    *m_activeIndex = index;
    syncActiveEditorFromRegion();
    rebuildInactiveChips();
    applyPromptLineHeights();
    updateLinkButton();
    updateNoRegionStrip();
    if (emitSignal) {
        Q_EMIT activeIndexChanged(index);
        Q_EMIT editingModeChanged(index);
        Q_EMIT layoutHeightsChanged();
    }
}

void ComfyRegionPromptWidget::rebuildInactiveChips()
{
    clearLayout(m_inactiveAbove);
    clearLayout(m_inactiveBelow);
    if (!m_regions || !m_activeIndex)
        return;

    KisImageSP image = m_viewManager ? KisImageSP(m_viewManager->image()) : KisImageSP();
    const int active = *m_activeIndex;

    auto makeRootChip = [this, image]() -> InactiveRegionChip * {
        ComfyUIRemoteDock::Private::RegionEntry pseudo;
        pseudo.name = ComfyTr::tr("Common");
        if (m_dockRootPositive)
            pseudo.prompt = m_dockRootPositive->toPlainText();
        if (pseudo.prompt.isEmpty() && m_editPrompt)
            pseudo.prompt = m_editPrompt->toPlainText();
        bool rootPlaceholder = false;
        const QString rootText = inactivePromptText(pseudo, true, &rootPlaceholder);
        auto *rootChip = new InactiveRegionChip(this);
        rootChip->setContent(thumbnailForRegion(pseudo, image, true), rootText, {}, rootPlaceholder);
        return rootChip;
    };

    auto makeRegionChip = [this, image](const ComfyUIRemoteDock::Private::RegionEntry &r) -> InactiveRegionChip * {
        bool regionPlaceholder = false;
        const QString chipText = inactivePromptText(r, false, &regionPlaceholder);
        auto *chip = new InactiveRegionChip(this);
        chip->setContent(thumbnailForRegion(r, image, false), chipText,
                         controlIconStems(r.controlLayers), regionPlaceholder);
        return chip;
    };

    if (m_liveSingleRegionMode) {
        if (m_regions->isEmpty())
            return;
        if (active >= 0) {
            if (InactiveRegionChip *rootChip = makeRootChip()) {
                rootChip->setProperty("regionIndex", ComfyRegionLink::kRootRegionIndex);
                rootChip->installEventFilter(this);
                m_inactiveBelow->addWidget(rootChip);
            }
            return;
        }
        if (active == ComfyRegionLink::kRootRegionIndex && m_viewManager && m_viewManager->image()) {
            const int linked = ComfyRegionLink::findRegionIndexForLayer(
                *m_regions, m_viewManager->image(), m_viewManager->activeLayer(), ComfyRegionLink::LinkMode::Any);
            if (linked >= 0) {
                if (InactiveRegionChip *chip = makeRegionChip(m_regions->at(linked))) {
                    chip->setProperty("regionIndex", linked);
                    chip->installEventFilter(this);
                    m_inactiveAbove->addWidget(chip);
                }
            }
            return;
        }
        if (active == ComfyRegionLink::kUnlinkedRegionIndex) {
            if (InactiveRegionChip *rootChip = makeRootChip()) {
                rootChip->setProperty("regionIndex", ComfyRegionLink::kRootRegionIndex);
                rootChip->installEventFilter(this);
                m_inactiveBelow->addWidget(rootChip);
            }
        }
        return;
    }

    auto installChip = [this](InactiveRegionChip *chip, int index, QVBoxLayout *layout) {
        if (!chip || !layout)
            return;
        chip->setProperty("regionIndex", index);
        chip->installEventFilter(this);
        layout->addWidget(chip);
    };

    const bool rootActive = active == ComfyRegionLink::kRootRegionIndex || active == ComfyRegionLink::kUnlinkedRegionIndex;

    if (rootActive) {
        for (const int idx : unlinkedRegionIndices(*m_regions, active))
            installChip(makeRegionChip(m_regions->at(idx)), idx, m_inactiveAbove);
        for (const int idx : linkedRegionsTopToBottom(*m_regions, image, active))
            installChip(makeRegionChip(m_regions->at(idx)), idx, m_inactiveAbove);
        return;
    }

    if (active >= 0 && active < m_regions->size()) {
        KisGroupLayerSP root = image ? image->rootLayer() : KisGroupLayerSP();
        KisNodeSP anchor = stackAnchorUnderRoot(primaryLinkedLayer(m_regions->at(active), image), root);

        for (const int idx : unlinkedRegionIndices(*m_regions, active))
            installChip(makeRegionChip(m_regions->at(idx)), idx, m_inactiveAbove);

        if (anchor) {
            QList<KisNodeSP> belowNodes;
            QList<KisNodeSP> aboveNodes;
            layerSiblingNodesBottomToTop(anchor, &belowNodes, &aboveNodes);
            for (auto it = aboveNodes.crbegin(); it != aboveNodes.crend(); ++it) {
                const int idx = regionIndexForStackNode(*m_regions, image, root, *it, active);
                if (idx >= 0)
                    installChip(makeRegionChip(m_regions->at(idx)), idx, m_inactiveAbove);
            }
            for (auto it = belowNodes.crbegin(); it != belowNodes.crend(); ++it) {
                const int idx = regionIndexForStackNode(*m_regions, image, root, *it, active);
                if (idx >= 0)
                    installChip(makeRegionChip(m_regions->at(idx)), idx, m_inactiveBelow);
            }
        } else {
            for (const int idx : linkedRegionsTopToBottom(*m_regions, image, active))
                installChip(makeRegionChip(m_regions->at(idx)), idx, m_inactiveAbove);
        }

        if (!m_regions->isEmpty())
            installChip(makeRootChip(), ComfyRegionLink::kRootRegionIndex, m_inactiveBelow);
    }
}

void ComfyRegionPromptWidget::rebuildActiveEditor()
{
    syncActiveEditorFromRegion();
    applyPromptLineHeights();
}

void ComfyRegionPromptWidget::syncRootPromptsFromDock()
{
    m_syncingEditor = true;
    if (m_dockRootPositive && m_editPrompt)
        ComfyTextArea::setPlainTextPreserveCursor(m_editPrompt, m_dockRootPositive->toPlainText());
    if (m_dockRootNegative && m_editNegative)
        ComfyTextArea::setPlainTextPreserveCursor(m_editNegative, m_dockRootNegative->toPlainText());
    m_syncingEditor = false;
}

void ComfyRegionPromptWidget::refreshRootPromptFromDock()
{
    if (currentMode() == EditorMode::Root)
        syncRootPromptsFromDock();
}

void ComfyRegionPromptWidget::commitRootPromptEditors()
{
    pushRootPromptsToDock();
}

void ComfyRegionPromptWidget::pushRootPromptsToDock()
{
    if (!m_dockRootPositive || !m_dockRootNegative)
        return;
    QSignalBlocker b1(m_dockRootPositive);
    QSignalBlocker b2(m_dockRootNegative);
    m_dockRootPositive->setPlainText(m_editPrompt->toPlainText());
    m_dockRootNegative->setPlainText(m_editNegative->toPlainText());
}

void ComfyRegionPromptWidget::syncActiveEditorFromRegion()
{
    m_syncingEditor = true;
    const EditorMode mode = currentMode();
    const bool hasRegions = m_regions && !m_regions->isEmpty();

    m_activeFrame->setVisible(mode == EditorMode::Root || mode == EditorMode::Region
                              || mode == EditorMode::Unlinked || mode == EditorMode::Empty);
    m_emptyHint->setVisible(!hasRegions && mode == EditorMode::Empty);
    m_noRegionStrip->setVisible(mode == EditorMode::Unlinked);
    m_editPrompt->setVisible(mode == EditorMode::Root || mode == EditorMode::Region);
    const bool showNeg = mode == EditorMode::Root && m_showNegativePrompt;
    if (m_promptStack)
        m_promptStack->setShowNegative(showNeg);
    m_btnLink->setVisible(mode == EditorMode::Region);
    m_btnRemove->setVisible(mode == EditorMode::Region);
    const bool showFullHeader =
        (mode == EditorMode::Root || mode == EditorMode::Region) && m_promptHeaderMode == 0;
    const bool showIconOnlyHeader =
        (mode == EditorMode::Root || mode == EditorMode::Region) && m_promptHeaderMode == 1;
    if (m_headerHost)
        m_headerHost->setVisible(showFullHeader);
    m_headerLabel->setVisible(showFullHeader);
    m_headerIcon->setVisible((showFullHeader || showIconOnlyHeader) && mode == EditorMode::Root);

    const bool editingPrompt = (m_editPrompt && m_editPrompt->hasFocus())
                               || (m_editNegative && m_editNegative->hasFocus());

    if (mode == EditorMode::Root) {
        m_headerLabel->setText(ComfyTr::tr("Text prompt common to all regions"));
        m_headerIcon->setPixmap(ComfyTheme::icon(QStringLiteral("root")).pixmap(16, 16));
        if (!editingPrompt)
            syncRootPromptsFromDock();
    } else if (mode == EditorMode::Region && m_regions && m_activeIndex) {
        const ComfyUIRemoteDock::Private::RegionEntry &r = m_regions->at(*m_activeIndex);
        KisImageSP image = m_viewManager ? KisImageSP(m_viewManager->image()) : KisImageSP();
        if (m_promptHeaderMode == 0)
            m_headerLabel->setText(
                QStringLiteral("%1 - %2").arg(ComfyRegionLink::regionDisplayName(r, image), ComfyTr::tr("Regional text prompt")));
        m_editPrompt->setPlaceholderText(ComfyTr::tr("Prompt for this area"));
        if (!editingPrompt)
            ComfyTextArea::setPlainTextPreserveCursor(m_editPrompt, r.prompt);
    }

    updateLinkButton();
    updateNoRegionStrip();
    if (m_headerLabel && m_editPrompt && m_headerLabel->isVisible()) {
        QFont headerFont = m_editPrompt->font();
        headerFont.setItalic(true);
        m_headerLabel->setFont(headerFont);
        m_headerLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(ComfyUiStyle::colors().secondaryText));
    }
    if (m_promptStack)
        m_promptStack->refreshFrameHeight();
    m_syncingEditor = false;
}

void ComfyRegionPromptWidget::commitActiveEditorToRegion()
{
    if (m_syncingEditor || !m_regions || !m_activeIndex)
        return;
    if (currentMode() == EditorMode::Root) {
        pushRootPromptsToDock();
        return;
    }
    if (currentMode() != EditorMode::Region)
        return;
    (*m_regions)[*m_activeIndex].prompt = m_editPrompt->toPlainText();
    if (m_viewManager && m_viewManager->image())
        ComfyRegionLink::syncMaskSourceFromLinks(&(*m_regions)[*m_activeIndex], m_viewManager->image());
}

void ComfyRegionPromptWidget::updateLinkButton()
{
    if (!m_btnLink || currentMode() != EditorMode::Region || !m_regions || !m_activeIndex || !m_viewManager)
        return;
    const int idx = *m_activeIndex;
    if (idx < 0 || idx >= m_regions->size())
        return;
    const ComfyRegionLink::ActiveLayerLinkUi ui = ComfyRegionLink::linkUiForRegion(
        &m_regions->at(idx), *m_regions, idx, m_viewManager->image(), m_viewManager->activeLayer());
    m_btnLink->setIcon(ComfyTheme::icon(ui.iconStem));
    m_btnLink->setEnabled(ui.canToggleLink);
    m_btnLink->setToolTip(ui.toolTip);
}

void ComfyRegionPromptWidget::updateNoRegionStrip()
{
    if (!m_noRegionLabel || !m_viewManager)
        return;
    KisLayerSP layer = m_viewManager->activeLayer();
    const bool canLink = layer && (dynamic_cast<KisGroupLayer *>(layer.data()) || dynamic_cast<KisPaintLayer *>(layer.data()));
    m_btnNewRegion->setEnabled(canLink);
    m_btnLinkRegionMenu->setEnabled(canLink && m_regions && m_regions->size() > 0);
    m_noRegionLabel->setText(canLink ? ComfyTr::tr("Active layer is not linked to a region")
                                     : ComfyTr::tr("Active layer cannot be linked to a region"));
}

int ComfyRegionPromptWidget::currentPositiveLineCount() const
{
    return m_promptStack ? m_promptStack->positiveLineCount() : 3;
}

void ComfyRegionPromptWidget::syncCompactHeightFromLayout()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    if (m_controlPanelHost) {
        m_controlPanelHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        m_controlPanelHost->updateGeometry();
    }
    if (m_activeFrame)
        m_activeFrame->updateGeometry();
    updateGeometry();
    adjustSize();

    int compactH = 0;
    if (QLayout *root = layout()) {
        root->activate();
        compactH = root->minimumSize().height();
    }
    if (compactH <= 0 && m_promptStack)
        compactH = m_promptStack->layoutSizeHint().height();
    if (compactH > 0) {
        setFixedHeight(compactH);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
}

void ComfyRegionPromptWidget::applyCompactLayout(int positiveLines,
                                                 bool showNegative,
                                                 bool showResizeHandle,
                                                 bool liveLineCounts)
{
    m_showNegativePrompt = showNegative;
    m_showResizeHandle = showResizeHandle;
    m_liveLineCounts = liveLineCounts;
    if (m_promptStack)
        m_promptStack->setLiveLineCounts(liveLineCounts);
    syncActiveEditorFromRegion();
    const bool effectiveShowNeg = currentMode() == EditorMode::Root && m_showNegativePrompt;
    if (m_promptStack)
        m_promptStack->applyLayout(positiveLines, effectiveShowNeg, showResizeHandle);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    if (m_controlPanelHost) {
        m_controlPanelHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        m_controlPanelHost->updateGeometry();
    }
    if (m_activeFrame)
        m_activeFrame->updateGeometry();
    updateGeometry();
    adjustSize();

    int compactH = 0;
    if (QLayout *root = layout()) {
        root->activate();
        compactH = root->minimumSize().height();
    }
    if (compactH <= 0 && m_promptStack) {
        compactH = m_promptStack->layoutSizeHint().height();
        if (compactH <= 0 && m_editPrompt) {
            compactH = ComfyPromptLayoutMetrics::stackFrameHeightForLines(
                m_editPrompt->fontMetrics(), positiveLines, effectiveShowNeg);
        }
    }
    if (compactH <= 0)
        compactH = qMax(minimumSizeHint().height(), sizeHint().height());
    if (compactH > 0) {
        setFixedHeight(compactH);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    qCWarning(KIS_COMFYUI_REMOTE).noquote()
        << QStringLiteral("COMFY_UI_DIAG RegionPrompt.applyCompactLayout posLines=") << positiveLines
        << QStringLiteral("showNeg=") << effectiveShowNeg
        << QStringLiteral("resizeHandle=") << showResizeHandle
        << QStringLiteral("stackH=") << (m_promptStack ? m_promptStack->layoutSizeHint().height() : -1)
        << QStringLiteral("controlH=") << (m_controlPanelHost ? m_controlPanelHost->sizeHint().height() : -1)
        << QStringLiteral("compactH=") << compactH
        << QStringLiteral("editPromptH=") << (m_editPrompt ? m_editPrompt->height() : -1)
        << QStringLiteral("editNegH=") << (m_editNegative ? m_editNegative->height() : -1)
        << QStringLiteral("widgetH=") << height();
}

void ComfyRegionPromptWidget::applyPromptLineHeights()
{
    QJsonObject st = ComfyUIUtils::loadSettingsJson();
    const int lines = qBound(1, st.value(QStringLiteral("prompt_line_count")).toInt(3), 10);
    applyPromptLineHeights(lines);
}

void ComfyRegionPromptWidget::applyPromptLineHeights(int positiveLines)
{
    const bool effectiveShowNeg = currentMode() == EditorMode::Root && m_showNegativePrompt;
    const int lines =
        ComfyPromptLayoutMetrics::positiveLinesForGenerateWorkspace(effectiveShowNeg, positiveLines);
    if (m_promptStack)
        m_promptStack->applyLayout(lines, effectiveShowNeg, m_showResizeHandle);
}

void ComfyRegionPromptWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void ComfyRegionPromptWidget::showLinkMenu(QPushButton *anchor)
{
    if (!m_regions || !m_viewManager || !anchor)
        return;
    KisLayerSP layer = m_viewManager->activeLayer();
    if (!layer)
        return;
    QMenu menu(this);
    for (int i = 0; i < m_regions->size(); ++i) {
        QString label = m_regions->at(i).prompt;
        label.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (label.isEmpty())
            label = ComfyTr::tr("<No text prompt>");
        if (label.size() > 20)
            label = label.left(17) + QLatin1String("...");
        QAction *act = menu.addAction(label);
        connect(act, &QAction::triggered, this, [this, i, layer]() {
            ComfyRegionLink::linkLayer(&(*m_regions)[i], layer);
            if (m_viewManager->image())
                ComfyRegionLink::syncMaskSourceFromLinks(&(*m_regions)[i], m_viewManager->image());
            setActiveIndex(i);
            Q_EMIT regionEdited();
        });
    }
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}
