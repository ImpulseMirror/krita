/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyCheckBox.h"
#include "ComfyComboBox.h"
#include "ComfyControlLayerRowWidget.h"
#include "ComfyLocalization.h"

#include "ComfyControlLayer.h"
#include "ComfyGrid.h"
#include "ComfyResources.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUIIntervalSlider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include "ComfyTrackSlider.h"
#include <QToolButton>
#include <QVBoxLayout>

#include <KisViewManager.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_node.h>
#include <kis_layer_utils.h>
#include <kis_paint_layer.h>
#include <kis_group_layer.h>
#include <kis_shape_layer.h>

#include <kis_icon_utils.h>
#include <klocalizedstring.h>

namespace {

void collectPickerLayers(KisNodeSP node, QVector<QPair<QString, QString>> *out)
{
    if (!node || !out)
        return;
    if (dynamic_cast<KisPaintLayer *>(node.data()) || dynamic_cast<KisShapeLayer *>(node.data())) {
        out->append(qMakePair(node->uuid().toString(QUuid::WithoutBraces), node->name()));
    }
    for (quint32 i = 0; i < node->childCount(); ++i)
        collectPickerLayers(node->at(i), out);
}

} // namespace

ComfyControlLayerRowWidget::ComfyControlLayerRowWidget(ComfyControlLayerEntry *entry,
                                                       int index,
                                                       KisViewManager *viewManager,
                                                       std::function<QString()> archKeyProvider,
                                                       QWidget *parent)
    : QWidget(parent)
    , m_entry(entry)
    , m_index(index)
    , m_viewManager(viewManager)
    , m_archKeyProvider(std::move(archKeyProvider))
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    m_modeCombo = new ComfyComboBox(this);
    for (const QString &key : ComfyControlLayer::uiModeKeys()) {
        const QString iconStem = QStringLiteral("control-") + key;
        m_modeCombo->addItem(ComfyTheme::icon(iconStem), ComfyControlLayer::modeLabel(key), key);
    }
    const int modeIx = m_modeCombo->findData(m_entry->mode);
    if (modeIx >= 0)
        m_modeCombo->setCurrentIndex(modeIx);

    m_layerCombo = new ComfyComboBox(this);
    m_layerCombo->setMinimumContentsLength(16);
    m_layerCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);

    m_presetSlider = new ComfyTrackSlider(Qt::Horizontal, this);
    m_presetSlider->setRange(0, ComfyControlLayer::maxPresetValue);
    m_presetSlider->setValue(m_entry->presetValue);
    m_presetSlider->setTickInterval(2);
    m_presetSlider->setTicksVisible(true);
    m_presetSlider->setToolTip(ComfyTr::tr("Control strength: how much the layer affects the image"));

    m_btnGenerate = new QToolButton(this);
    m_btnGenerate->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnGenerate->setIcon(ComfyTheme::icon(QStringLiteral("control-generate")));
    m_btnGenerate->setToolTip(ComfyTr::tr("Generate control layer from current image"));

    m_btnAddPose = new QToolButton(this);
    m_btnAddPose->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnAddPose->setIcon(ComfyTheme::icon(QStringLiteral("add-pose")));
    m_btnAddPose->setToolTip(ComfyTr::tr("Add new character pose to selected layer"));

    m_btnExpand = new QToolButton(this);
    m_btnExpand->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnExpand->setIcon(ComfyTheme::icon(QStringLiteral("more")));
    m_btnExpand->setCheckable(true);
    m_btnExpand->setAutoRaise(true);
    m_btnExpand->setToolTip(ComfyTr::tr("Show/hide advanced settings"));

    m_btnRemove = new QToolButton(this);
    m_btnRemove->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_btnRemove->setIcon(ComfyTheme::icon(QStringLiteral("remove")));
    m_btnRemove->setAutoRaise(true);
    m_btnRemove->setToolTip(ComfyTr::tr("Remove control layer"));

    m_middleControls = new QWidget(this);
    auto *middleLay = new QHBoxLayout(m_middleControls);
    ComfyUiStyle::applyTightRowLayout(middleLay);
    middleLay->addWidget(m_layerCombo, 1);
    middleLay->addWidget(m_btnGenerate);
    middleLay->addWidget(m_btnAddPose);
    middleLay->addWidget(m_presetSlider, 1);
    middleLay->addWidget(m_btnExpand);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    ComfyUiStyle::styleStatusLabel(m_errorLabel, ComfyUiStyle::StatusTone::Warning);
    m_errorLabel->hide();

    auto *middleHost = new QWidget(this);
    auto *middleHostLay = new QVBoxLayout(middleHost);
    middleHostLay->setContentsMargins(0, 0, 0, 0);
    middleHostLay->setSpacing(0);
    middleHostLay->addWidget(m_middleControls);
    middleHostLay->addWidget(m_errorLabel);

    auto *bar = new ComfyGridRow(this);
    bar->setMinimumHeight(ComfyUiStyle::Spacing::rowHeight);
    bar->addWidget(m_modeCombo, 3);
    bar->addWidget(middleHost, 8, 1);
    bar->addWidget(m_btnRemove, 1, Qt::AlignRight | Qt::AlignVCenter);
    mainLay->addWidget(bar);

    m_extended = new QWidget(this);
    auto *padLay = new QHBoxLayout(m_extended);
    padLay->setContentsMargins(10, 0, 0, 0);
    auto *line = new QFrame(m_extended);
    line->setFrameShape(QFrame::VLine);
    line->setLineWidth(1);
    padLay->addWidget(line);
    auto *extInner = new QVBoxLayout();
    extInner->setContentsMargins(8, 2, 4, 6);
    padLay->addLayout(extInner);

    m_customStrength = new ComfyCheckBox(ComfyTr::tr("Use custom values"), m_extended);
    m_customStrength->setChecked(m_entry->useCustomStrength);
    auto *actRow = new QHBoxLayout();
    actRow->addWidget(m_customStrength, 1);
    extInner->addLayout(actRow);

    auto *grid = new QGridLayout();
    grid->setSpacing(8);
    m_strengthSlider = new ComfyTrackSlider(Qt::Horizontal, m_extended);
    m_strengthSlider->setRange(0, 75);
    m_strengthSlider->setValue(m_entry->strength);
    m_strengthLabel = new QLabel(m_extended);
    m_rangeLabel = new QLabel(ComfyTr::tr("Range:"), m_extended);
    m_rangeSlider = new ComfyUIIntervalSlider(m_extended);
    m_rangeSlider->setRange(0, 20);
    m_rangeSlider->setInterval(qRound(m_entry->start * 20.0), qRound(m_entry->end * 20.0));
    m_rangeStartLabel = new QLabel(m_extended);
    m_rangeEndLabel = new QLabel(m_extended);
    grid->addWidget(new QLabel(ComfyTr::tr("Strength:")), 0, 0);
    grid->addWidget(m_strengthSlider, 0, 2);
    grid->addWidget(m_strengthLabel, 0, 3);
    grid->addWidget(m_rangeLabel, 1, 0);
    grid->addWidget(m_rangeStartLabel, 1, 1);
    grid->addWidget(m_rangeSlider, 1, 2);
    grid->addWidget(m_rangeEndLabel, 1, 3);
    extInner->addLayout(grid);

    m_extended->setVisible(false);
    mainLay->addWidget(m_extended);

    ComfyUiStyle::applyComboBox(m_modeCombo);
    ComfyUiStyle::applyComboBox(m_layerCombo);
    ComfyUiStyle::applyCheckbox(m_customStrength);
    ComfyUiStyle::applyIconToolButton(m_btnGenerate);
    ComfyUiStyle::applyIconToolButton(m_btnAddPose);
    ComfyUiStyle::applyIconToolButton(m_btnExpand);
    ComfyUiStyle::applyIconToolButton(m_btnRemove);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ComfyControlLayerRowWidget::onModeChanged);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ComfyControlLayerRowWidget::onLayerChanged);
    connect(m_presetSlider, &QAbstractSlider::valueChanged, this, [this](int v) {
        if (!m_entry)
            return;
        m_entry->presetValue = v;
        if (!m_entry->useCustomStrength)
            applyPresetFromSlider();
        Q_EMIT entryEdited();
    });
    connect(m_customStrength, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_entry)
            return;
        m_entry->useCustomStrength = on;
        m_presetSlider->setEnabled(!on);
        m_strengthSlider->setEnabled(on);
        m_rangeSlider->setEnabled(on);
        if (!on)
            applyPresetFromSlider();
        Q_EMIT entryEdited();
    });
    connect(m_strengthSlider, &QAbstractSlider::valueChanged, this, [this](int v) {
        if (!m_entry)
            return;
        m_entry->strength = v;
        m_strengthLabel->setText(
            QString::number(v / static_cast<double>(ComfyControlLayer::strengthMultiplier), 'f', 2));
        Q_EMIT entryEdited();
    });
    connect(m_rangeSlider, &ComfyUIIntervalSlider::intervalChanged, this, [this](int lo, int hi) {
        if (!m_entry)
            return;
        m_entry->start = lo / 20.0;
        m_entry->end = hi / 20.0;
        syncRangeLabels();
        Q_EMIT entryEdited();
    });
    connect(m_btnExpand, &QToolButton::toggled, m_extended, &QWidget::setVisible);
    connect(m_btnGenerate, &QToolButton::clicked, this, [this]() { Q_EMIT generateRequested(m_index); });
    connect(m_btnAddPose, &QToolButton::clicked, this, [this]() { Q_EMIT addPoseCharacterRequested(m_index); });
    connect(m_btnRemove, &QToolButton::clicked, this, [this]() { Q_EMIT removeRequested(m_index); });

    refreshLayerCombo();
    syncRangeLabels();
    m_strengthLabel->setText(
        QString::number(m_entry->strength / static_cast<double>(ComfyControlLayer::strengthMultiplier), 'f', 2));
    m_presetSlider->setEnabled(!m_entry->useCustomStrength);
    m_strengthSlider->setEnabled(m_entry->useCustomStrength);
    m_rangeSlider->setEnabled(m_entry->useCustomStrength);
    updateSupportState();
    updateVisibility();
}

void ComfyControlLayerRowWidget::updateSupportState()
{
    if (!m_entry || !m_errorLabel || !m_middleControls)
        return;
    const QString archKey = m_archKeyProvider ? m_archKeyProvider() : QString();
    const bool supported = ComfyControlLayer::isModeSupported(m_entry->mode, archKey);
    m_errorLabel->setText(ComfyControlLayer::unsupportedModeMessage(m_entry->mode, archKey));
    m_errorLabel->setVisible(!supported);
    m_middleControls->setVisible(supported);
    if (!supported && m_btnExpand)
        m_btnExpand->setChecked(false);
}

void ComfyControlLayerRowWidget::applyPresetFromSlider()
{
    if (!m_entry)
        return;
    const QString archKey = m_archKeyProvider ? m_archKeyProvider() : QString();
    ComfyControlLayer::applyPresetDefaults(m_entry, archKey);
    QSignalBlocker b1(m_strengthSlider);
    QSignalBlocker b2(m_rangeSlider);
    m_strengthSlider->setValue(m_entry->strength);
    m_rangeSlider->setInterval(qRound(m_entry->start * 20.0), qRound(m_entry->end * 20.0));
    m_strengthLabel->setText(
        QString::number(m_entry->strength / static_cast<double>(ComfyControlLayer::strengthMultiplier), 'f', 2));
    syncRangeLabels();
}

void ComfyControlLayerRowWidget::syncRangeLabels()
{
    if (!m_entry)
        return;
    m_rangeStartLabel->setText(QString::number(m_entry->start, 'f', 2));
    m_rangeEndLabel->setText(QString::number(m_entry->end, 'f', 2));
}

void ComfyControlLayerRowWidget::onModeChanged(int comboIndex)
{
    if (!m_entry || comboIndex < 0)
        return;
    m_entry->mode = m_modeCombo->itemData(comboIndex).toString();
    if (!m_entry->useCustomStrength)
        applyPresetFromSlider();
    updateSupportState();
    updateVisibility();
    Q_EMIT entryEdited();
}

void ComfyControlLayerRowWidget::onLayerChanged(int comboIndex)
{
    if (!m_entry || comboIndex < 0)
        return;
    m_entry->layerId = m_layerCombo->itemData(comboIndex).toString();
    m_entry->layerName = m_layerCombo->itemText(comboIndex);
    updateVisibility();
    Q_EMIT entryEdited();
}

bool ComfyControlLayerRowWidget::isPoseVectorLayer() const
{
    if (!m_entry || !m_viewManager || !m_viewManager->image())
        return false;
    if (m_entry->mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) != 0)
        return false;
    if (m_entry->layerId.isEmpty())
        return false;
    KisNodeSP root = m_viewManager->image()->rootLayer();
    if (!root)
        return false;
    const QUuid uid = QUuid::fromString(m_entry->layerId);
    KisNodeSP node = uid.isNull() ? KisNodeSP() : KisLayerUtils::findNodeByUuid(root, uid);
    return node && qobject_cast<KisShapeLayer *>(node.data());
}

void ComfyControlLayerRowWidget::refreshLayerCombo()
{
    if (!m_layerCombo || !m_entry)
        return;
    QVector<QPair<QString, QString>> items;
    if (m_viewManager && m_viewManager->image() && m_viewManager->image()->rootLayer())
        collectPickerLayers(m_viewManager->image()->rootLayer(), &items);
    QSignalBlocker b(m_layerCombo);
    m_layerCombo->clear();
    int sel = -1;
    for (int i = items.size() - 1; i >= 0; --i) {
        m_layerCombo->addItem(items.at(i).second, items.at(i).first);
        if (items.at(i).first == m_entry->layerId || items.at(i).second == m_entry->layerName)
            sel = m_layerCombo->count() - 1;
    }
    if (sel >= 0)
        m_layerCombo->setCurrentIndex(sel);
    else if (!m_entry->layerName.isEmpty()) {
        m_layerCombo->addItem(m_entry->layerName, m_entry->layerId);
        m_layerCombo->setCurrentIndex(m_layerCombo->count() - 1);
    }
}

void ComfyControlLayerRowWidget::updateVisibility()
{
    if (!m_entry)
        return;
    updateSupportState();
    const QString archKey = m_archKeyProvider ? m_archKeyProvider() : QString();
    const ComfyResources::Arch arch = ComfyResources::archFromKey(archKey);
    const bool isEdit = ComfyResources::isEditArch(arch);
    const bool isPose = m_entry->mode.compare(QStringLiteral("pose"), Qt::CaseInsensitive) == 0;
    const bool canGen = ComfyControlLayer::canGenerateJob(*m_entry);
    const bool hasRange = ComfyControlLayer::modeHasRange(m_entry->mode);

    m_btnGenerate->setVisible(canGen);
    m_btnGenerate->setEnabled(canGen);
    m_btnAddPose->setVisible(isPose);
    m_btnAddPose->setEnabled(isPoseVectorLayer());
    m_btnAddPose->setToolTip(isPoseVectorLayer()
                                 ? ComfyTr::tr("Add new character pose to selected layer")
                                 : ComfyTr::tr("Disabled: selected layer must be a vector layer to add a pose"));
    m_presetSlider->setVisible(!isEdit);
    m_btnExpand->setVisible(!isEdit);
    m_rangeLabel->setVisible(hasRange);
    m_rangeSlider->setVisible(hasRange);
    m_rangeStartLabel->setVisible(hasRange);
    m_rangeEndLabel->setVisible(hasRange);
    if (isEdit)
        m_btnExpand->setChecked(false);
}

void ComfyControlLayerRowWidget::setGenerateEnabled(bool enabled)
{
    if (m_btnGenerate)
        m_btnGenerate->setEnabled(enabled && m_entry && ComfyControlLayer::canGenerateJob(*m_entry));
}
