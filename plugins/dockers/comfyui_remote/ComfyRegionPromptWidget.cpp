/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyRegionPromptWidget.h"
#include "ComfyLocalization.h"
#include "ComfyRegionLink.h"
#include "ComfyControlLayer.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfyUIUtils.h"

#include <QGuiApplication>

#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QEvent>
#include <QFontMetrics>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QPoint>

#include <KoColorConversionTransformation.h>
#include <kis_paint_device.h>

#include <klocalizedstring.h>
#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_group_layer.h>
#include <kis_paint_layer.h>
#include <kis_node.h>
#include <KisIconUtils.h>

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

    void setContent(const QPixmap &thumb, const QString &text, const QStringList &controlModeStems)
    {
        m_thumb->setPixmap(thumb.isNull() ? QPixmap() : thumb.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_displayText = text;
        while (QLayoutItem *it = m_icons->takeAt(0)) {
            if (QWidget *w = it->widget())
                w->deleteLater();
            delete it;
        }
        const int iconSize = qMax(14, static_cast<int>(fontMetrics().height() * 1.2));
        for (const QString &stem : controlModeStems) {
            auto *ic = new QLabel(this);
            ic->setPixmap(KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(stem)).pixmap(iconSize, iconSize));
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
    }

    QLabel *m_thumb = nullptr;
    QLabel *m_prompt = nullptr;
    QHBoxLayout *m_icons = nullptr;
    QString m_displayText;
};

QPixmap thumbnailForRegion(const ComfyUIRemoteDock::Private::RegionEntry &entry,
                           KisImageSP image,
                           bool isRoot)
{
    if (isRoot) {
        return KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("region-prompt")))
            .pixmap(32, 32);
    }
    if (image && !entry.layerIds.isEmpty()) {
        const QStringList ids = ComfyRegionLink::parseLayerIds(entry.layerIds);
        if (!ids.isEmpty()) {
            if (KisLayerSP layer = ComfyRegionLink::findLayerByUuid(image, QUuid(ids.first()))) {
                const QRect bounds = layer->exactBounds() & image->bounds();
                if (!bounds.isEmpty()) {
                    KisPaintDeviceSP dev = layer->projection();
                    if (dev) {
                        const KoColorProfile *profile =
                            image->colorSpace() ? image->colorSpace()->profile() : nullptr;
                        QImage rgba = dev->convertToQImage(profile, bounds.x(), bounds.y(), bounds.width(),
                                                           bounds.height(),
                                                           KoColorConversionTransformation::internalRenderingIntent(),
                                                           KoColorConversionTransformation::internalConversionFlags());
                        if (!rgba.isNull())
                            return QPixmap::fromImage(
                                rgba.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    }
                }
            }
        }
    }
    return KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("region-prompt")))
        .pixmap(32, 32);
}

QString inactivePromptText(const ComfyUIRemoteDock::Private::RegionEntry &r, bool isRoot)
{
    QString prompt = r.prompt;
    prompt.replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (prompt.isEmpty()) {
        if (isRoot)
            return QObject::tr("Common text prompt - click to add content");
        return QObject::tr("%1 - click to add regional text").arg(r.name);
    }
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

} // namespace

ComfyRegionPromptWidget::ComfyRegionPromptWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RegionPromptWidget"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(2);

    m_inactiveAbove = new QVBoxLayout();
    m_inactiveAbove->setSpacing(2);
    rootLayout->addLayout(m_inactiveAbove);

    m_activeFrame = new QFrame(this);
    m_activeFrame->setObjectName(QStringLiteral("ActiveRegionWidget"));
    m_activeFrame->setFrameStyle(QFrame::StyledPanel);
    auto *activeLay = new QVBoxLayout(m_activeFrame);
    activeLay->setContentsMargins(4, 4, 4, 4);
    activeLay->setSpacing(4);

    auto *headerRow = new QHBoxLayout();
    m_headerIcon = new QLabel(m_activeFrame);
    m_headerIcon->setFixedSize(18, 18);
    m_headerLabel = new QLabel(m_activeFrame);
    m_headerLabel->setWordWrap(true);
    m_btnLink = new QPushButton(m_activeFrame);
    m_btnLink->setFlat(true);
    m_btnLink->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("link"))));
    m_btnRemove = new QPushButton(m_activeFrame);
    m_btnRemove->setFlat(true);
    m_btnRemove->setToolTip(ComfyTr::tr("Remove this region"));
    m_btnRemove->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("remove"))));
    headerRow->addWidget(m_headerIcon);
    headerRow->addWidget(m_headerLabel, 1);
    headerRow->addWidget(m_btnLink);
    headerRow->addWidget(m_btnRemove);
    activeLay->addLayout(headerRow);

    m_editPrompt = new QPlainTextEdit(m_activeFrame);
    m_editPrompt->setPlaceholderText(ComfyTr::tr("Prompt for this area"));
    m_editPrompt->installEventFilter(this);
    {
        auto *col = new QWidget(m_activeFrame);
        auto *colLay = new QVBoxLayout(col);
        colLay->setContentsMargins(0, 0, 0, 0);
        colLay->setSpacing(0);
        colLay->addWidget(m_editPrompt);
        colLay->addWidget(new ComfyPromptResizeHandle(
            m_editPrompt,
            [](int lines) {
                QJsonObject st = ComfyUIUtils::loadSettingsJson();
                st.insert(QStringLiteral("prompt_line_count"), lines);
                ComfyUIUtils::saveSettingsJson(st);
            },
            40,
            col));
        activeLay->addWidget(col);
    }

    m_editNegative = new QPlainTextEdit(m_activeFrame);
    m_editNegative->setPlaceholderText(ComfyTr::tr("Describe content you want to avoid."));
    m_editNegative->installEventFilter(this);
    {
        auto *col = new QWidget(m_activeFrame);
        auto *colLay = new QVBoxLayout(col);
        colLay->setContentsMargins(0, 0, 0, 0);
        colLay->setSpacing(0);
        colLay->addWidget(m_editNegative);
        colLay->addWidget(new ComfyPromptResizeHandle(
            m_editNegative,
            [](int lines) {
                QJsonObject st = ComfyUIUtils::loadSettingsJson();
                st.insert(QStringLiteral("negative_prompt_line_count"), lines);
                ComfyUIUtils::saveSettingsJson(st);
            },
            28,
            col));
        activeLay->addWidget(col);
    }

    m_comboMask = new QComboBox(m_activeFrame);
    m_comboMask->setToolTip(ComfyTr::tr("Mask source: selection or a paint layer"));
    activeLay->addWidget(m_comboMask);

    m_btnTranslation = new QPushButton(m_activeFrame);
    m_btnTranslation->setFlat(true);
    m_btnTranslation->setToolTip(ComfyTr::tr("Toggle prompt translation (configure language in Settings → Interface)"));
    activeLay->addWidget(m_btnTranslation, 0, Qt::AlignRight);

    m_noRegionStrip = new QWidget(m_activeFrame);
    auto *noLay = new QHBoxLayout(m_noRegionStrip);
    noLay->setContentsMargins(0, 0, 0, 0);
    m_noRegionLabel = new QLabel(m_noRegionStrip);
    m_noRegionLabel->setWordWrap(true);
    m_noRegionLabel->setStyleSheet(QStringLiteral("font-style: italic; color: palette(mid);"));
    m_btnNewRegion = new QPushButton(ComfyTr::tr("New region"), m_noRegionStrip);
    m_btnNewRegion->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("region-add"))));
    m_btnLinkRegionMenu = new QPushButton(ComfyTr::tr("Link region"), m_noRegionStrip);
    m_btnLinkRegionMenu->setIcon(
        KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("link"))));
    noLay->addWidget(m_noRegionLabel, 1);
    noLay->addWidget(m_btnNewRegion);
    noLay->addWidget(m_btnLinkRegionMenu);
    activeLay->addWidget(m_noRegionStrip);

    m_emptyHint = new QLabel(
        ComfyTr::tr("No regions yet. Use Add below, or link a layer after creating a region."), m_activeFrame);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setStyleSheet(QStringLiteral("color: palette(mid); font-style: italic;"));
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
            Q_EMIT regionEdited();
            return;
        }
        if (currentMode() != EditorMode::Region)
            return;
        (*m_regions)[*m_activeIndex].prompt = m_editPrompt->toPlainText();
        Q_EMIT regionEdited();
        rebuildInactiveChips();
    });
    connect(m_editNegative, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_syncingEditor || currentMode() != EditorMode::Root)
            return;
        pushRootPromptsToDock();
        Q_EMIT regionEdited();
    });
    connect(m_comboMask, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_syncingEditor || currentMode() != EditorMode::Region || !m_regions || !m_activeIndex)
            return;
        (*m_regions)[*m_activeIndex].maskSource = m_comboMask->currentData().toString();
        Q_EMIT regionEdited();
        rebuildInactiveChips();
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
    connect(m_btnTranslation, &QPushButton::clicked, this, [this]() {
        const bool ctrl = QGuiApplication::keyboardModifiers() & Qt::ControlModifier;
        if (ctrl) {
            const bool neg = currentMode() == EditorMode::Root && m_showNegativePrompt
                             && m_editNegative && m_editNegative->hasFocus();
            Q_EMIT translatePromptRequested(neg);
            return;
        }
        QJsonObject st = ComfyUIUtils::loadSettingsJson();
        const bool en = st.value(QStringLiteral("translation_enabled")).toBool(false);
        st.insert(QStringLiteral("translation_enabled"), !en);
        ComfyUIUtils::saveSettingsJson(st);
        m_btnTranslation->setText(en ? QStringLiteral("EN") : m_promptTranslationCode.toUpper());
        m_btnTranslation->setToolTip(
            en ? ComfyTr::tr("Prompt translation is active. Click to disable. Ctrl+Click to translate text now.")
               : ComfyTr::tr("Translation is disabled. Click to enable. Ctrl+Click to translate text now."));
    });
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
    if (event->type() == QEvent::FocusIn && (obj == m_editPrompt || obj == m_editNegative)) {
        m_activeFrame->setStyleSheet(QStringLiteral("QFrame#ActiveRegionWidget { border: 1px solid palette(highlight); }"));
        Q_EMIT activated();
    } else if (event->type() == QEvent::FocusOut && (obj == m_editPrompt || obj == m_editNegative)) {
        m_activeFrame->setStyleSheet(QString());
    }
    return QWidget::eventFilter(obj, event);
}

void ComfyRegionPromptWidget::setViewManager(KisViewManager *viewManager)
{
    m_viewManager = viewManager;
    m_lastActiveLayerUuid = QUuid();
    populateMaskCombo();
    refresh();
}

void ComfyRegionPromptWidget::setPromptHeaderMode(int mode)
{
    m_promptHeaderMode = qBound(0, mode, 2);
    rebuildActiveEditor();
}

void ComfyRegionPromptWidget::setShowNegativePrompt(bool show)
{
    m_showNegativePrompt = show;
    rebuildActiveEditor();
}

void ComfyRegionPromptWidget::setPromptTranslationCode(const QString &code)
{
    m_promptTranslationCode = code.isEmpty() || code == QLatin1String("disabled") ? QStringLiteral("EN") : code;
    if (m_btnTranslation)
        m_btnTranslation->setText(m_promptTranslationCode.left(2).toUpper());
    m_btnTranslation->setVisible(!code.isEmpty() && code != QLatin1String("disabled"));
}

void ComfyRegionPromptWidget::setRootPromptEditors(QPlainTextEdit *positive, QPlainTextEdit *negative)
{
    m_dockRootPositive = positive;
    m_dockRootNegative = negative;
    if (m_dockRootPositive) {
        connect(m_dockRootPositive, &QPlainTextEdit::textChanged, this, [this]() {
            if (currentMode() == EditorMode::Root && !m_syncingEditor)
                syncRootPromptsFromDock();
        });
    }
    if (m_dockRootNegative) {
        connect(m_dockRootNegative, &QPlainTextEdit::textChanged, this, [this]() {
            if (currentMode() == EditorMode::Root && !m_syncingEditor)
                syncRootPromptsFromDock();
        });
    }
}

void ComfyRegionPromptWidget::embedRegionControlPanel(QWidget *panel)
{
    if (m_controlPanelHost)
        return;
    m_controlPanelHost = panel;
    layout()->addWidget(panel);
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
    } else if (*m_activeIndex >= m_regions->size()
               || (*m_activeIndex < ComfyRegionLink::kUnlinkedRegionIndex
                   && *m_activeIndex != ComfyRegionLink::kRootRegionIndex)) {
        if (m_viewManager && m_viewManager->image()) {
            const int linked = ComfyRegionLink::findRegionIndexForLayer(
                *m_regions, m_viewManager->image(), m_viewManager->activeLayer(), ComfyRegionLink::LinkMode::Any);
            *m_activeIndex = linked >= 0 ? linked : ComfyRegionLink::kUnlinkedRegionIndex;
        } else {
            *m_activeIndex = 0;
        }
    }
    rebuildInactiveChips();
    rebuildActiveEditor();
    Q_EMIT editingModeChanged(*m_activeIndex);
}

void ComfyRegionPromptWidget::onActiveLayerChanged()
{
    if (!m_regions || !m_activeIndex || !m_viewManager)
        return;
    KisLayerSP layer = m_viewManager->activeLayer();
    const QUuid id = layer ? layer->uuid() : QUuid();
    if (id == m_lastActiveLayerUuid)
        return;
    m_lastActiveLayerUuid = id;

    if (m_regions->isEmpty())
        return;

    KisImageSP image = m_viewManager->image();
    const int idx = ComfyRegionLink::findRegionIndexForLayer(*m_regions, image, layer, ComfyRegionLink::LinkMode::Any);
    if (idx >= 0)
        setActiveIndex(idx);
    else if (*m_activeIndex >= 0)
        setActiveIndex(ComfyRegionLink::kUnlinkedRegionIndex);

    updateLinkButton();
    updateNoRegionStrip();
}

ComfyRegionPromptWidget::EditorMode ComfyRegionPromptWidget::currentMode() const
{
    if (!m_regions || !m_activeIndex)
        return EditorMode::Empty;
    if (m_regions->isEmpty())
        return EditorMode::Empty;
    if (*m_activeIndex == ComfyRegionLink::kRootRegionIndex)
        return EditorMode::Root;
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
            w->deleteLater();
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
    updateLinkButton();
    updateNoRegionStrip();
    if (emitSignal) {
        Q_EMIT activeIndexChanged(index);
        Q_EMIT editingModeChanged(index);
    }
}

void ComfyRegionPromptWidget::rebuildInactiveChips()
{
    clearLayout(m_inactiveAbove);
    clearLayout(m_inactiveBelow);
    if (!m_regions || !m_activeIndex)
        return;

    KisImageSP image = m_viewManager ? m_viewManager->image() : KisImageSP();
    const int active = *m_activeIndex;

    auto placeChip = [this, active, image](InactiveRegionChip *chip, int index) {
        chip->setProperty("regionIndex", index);
        chip->installEventFilter(this);
        if (index == active || (index == ComfyRegionLink::kRootRegionIndex && active == ComfyRegionLink::kRootRegionIndex))
            return;
        if (index == ComfyRegionLink::kRootRegionIndex || (active >= 0 && index < active)
            || active == ComfyRegionLink::kUnlinkedRegionIndex || active == ComfyRegionLink::kRootRegionIndex)
            m_inactiveAbove->addWidget(chip);
        else
            m_inactiveBelow->addWidget(chip);
    };

    if (!m_regions->isEmpty()) {
        ComfyUIRemoteDock::Private::RegionEntry pseudo;
        pseudo.name = ComfyTr::tr("Common");
        if (m_dockRootPositive)
            pseudo.prompt = m_dockRootPositive->toPlainText();
        auto *rootChip = new InactiveRegionChip(this);
        rootChip->setContent(thumbnailForRegion(pseudo, image, true), inactivePromptText(pseudo, true), {});
        placeChip(rootChip, ComfyRegionLink::kRootRegionIndex);
    }

    for (int i = 0; i < m_regions->size(); ++i) {
        if (i == active)
            continue;
        const ComfyUIRemoteDock::Private::RegionEntry &r = m_regions->at(i);
        auto *chip = new InactiveRegionChip(this);
        chip->setContent(thumbnailForRegion(r, image, false), inactivePromptText(r, false),
                         controlIconStems(r.controlLayers));
        placeChip(chip, i);
    }
}

void ComfyRegionPromptWidget::rebuildActiveEditor()
{
    syncActiveEditorFromRegion();
    applyPromptLineHeights();
}

void ComfyRegionPromptWidget::populateMaskCombo()
{
    if (!m_comboMask)
        return;
    const QString prev = m_comboMask->currentData().toString();
    m_comboMask->clear();
    m_comboMask->addItem(ComfyTr::tr("Current selection"), QStringLiteral("selection"));
    if (m_viewManager && m_viewManager->image() && m_viewManager->image()->rootLayer()) {
        QList<KisNodeSP> nodes;
        nodes.append(m_viewManager->image()->rootLayer());
        while (!nodes.isEmpty()) {
            KisNodeSP node = nodes.takeFirst();
            if (KisLayerSP layer = dynamic_cast<KisLayer *>(node.data())) {
                if (!layer->name().isEmpty())
                    m_comboMask->addItem(layer->name(), QStringLiteral("layer:") + layer->name());
            }
            for (int c = 0; c < static_cast<int>(node->childCount()); ++c)
                nodes.append(node->at(c));
        }
    }
    const int idx = m_comboMask->findData(prev);
    if (idx >= 0)
        m_comboMask->setCurrentIndex(idx);
}

void ComfyRegionPromptWidget::syncRootPromptsFromDock()
{
    m_syncingEditor = true;
    if (m_dockRootPositive)
        m_editPrompt->setPlainText(m_dockRootPositive->toPlainText());
    if (m_dockRootNegative)
        m_editNegative->setPlainText(m_dockRootNegative->toPlainText());
    m_syncingEditor = false;
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
    populateMaskCombo();
    const EditorMode mode = currentMode();
    const bool hasRegions = m_regions && !m_regions->isEmpty();

    m_activeFrame->setVisible(hasRegions || mode == EditorMode::Empty);
    m_emptyHint->setVisible(!hasRegions);
    m_noRegionStrip->setVisible(mode == EditorMode::Unlinked);
    m_editPrompt->setVisible(mode == EditorMode::Root || mode == EditorMode::Region);
    m_editNegative->setVisible(mode == EditorMode::Root && m_showNegativePrompt);
    m_comboMask->setVisible(mode == EditorMode::Region);
    m_btnLink->setVisible(mode == EditorMode::Region);
    m_btnRemove->setVisible(mode == EditorMode::Region);
    m_headerLabel->setVisible((mode == EditorMode::Root || mode == EditorMode::Region) && m_promptHeaderMode != 2);
    m_headerIcon->setVisible((mode == EditorMode::Root || mode == EditorMode::Region) && m_promptHeaderMode == 1);
    m_btnTranslation->setVisible((mode == EditorMode::Root || mode == EditorMode::Region)
                                 && !m_promptTranslationCode.isEmpty());

    if (mode == EditorMode::Root) {
        m_headerLabel->setText(ComfyTr::tr("Text prompt common to all regions"));
        syncRootPromptsFromDock();
    } else if (mode == EditorMode::Region && m_regions && m_activeIndex) {
        const ComfyUIRemoteDock::Private::RegionEntry &r = m_regions->at(*m_activeIndex);
        KisImageSP image = m_viewManager ? m_viewManager->image() : KisImageSP();
        if (m_promptHeaderMode == 1) {
            m_headerIcon->setPixmap(
                KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(QStringLiteral("region-prompt")))
                    .pixmap(16, 16));
        }
        if (m_promptHeaderMode == 0)
            m_headerLabel->setText(
                QStringLiteral("%1 - %2").arg(ComfyRegionLink::regionDisplayName(r, image), ComfyTr::tr("Regional text prompt")));
        m_editPrompt->setPlainText(r.prompt);
        int maskIdx = m_comboMask->findData(r.maskSource);
        if (maskIdx < 0)
            maskIdx = m_comboMask->findText(r.maskSource);
        if (maskIdx >= 0)
            m_comboMask->setCurrentIndex(maskIdx);
    }

    updateLinkButton();
    updateNoRegionStrip();
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
    (*m_regions)[*m_activeIndex].maskSource = m_comboMask->currentData().toString();
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
    m_btnLink->setIcon(KisIconUtils::loadIcon(ComfyUIUtils::kritaIconNameForThemeStem(ui.iconStem)));
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

void ComfyRegionPromptWidget::applyPromptLineHeights()
{
    QJsonObject st = ComfyUIUtils::loadSettingsJson();
    const int lines = st.value(QStringLiteral("prompt_line_count")).toInt(2);
    const int negLines = st.value(QStringLiteral("negative_prompt_line_count")).toInt(1);
    const int lh = m_editPrompt->fontMetrics().lineSpacing();
    const int posH = qBound(40, lines * lh + 12, 800);
    const int negH = qBound(28, negLines * lh + 12, 800);
    m_editPrompt->setFixedHeight(posH);
    m_editNegative->setFixedHeight(negH);
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
