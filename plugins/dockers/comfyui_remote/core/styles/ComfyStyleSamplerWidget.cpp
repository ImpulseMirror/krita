/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyComboBox.h"
#include "ComfyStyleSamplerWidget.h"
#include "ComfyFormUi.h"
#include "ComfyLocalization.h"
#include "ComfyUIUtils.h"
#include "ComfyUiStyle.h"

#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractSlider>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

ComfyStyleSamplerWidget::ComfyStyleSamplerWidget(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
{
    const QString title = (kind == Kind::Quality)
        ? ComfyTr::tr("Quality Preset (generate and upscale)")
        : ComfyTr::tr("Performance Preset (live mode)");

    m_expander = new QToolButton(this);
    m_expander->setCheckable(true);
    m_expander->setChecked(false);
    m_expander->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_expander->setArrowType(Qt::RightArrow);
    m_expander->setText(title);
    ComfyUiStyle::applyExpanderButton(m_expander);
    connect(m_expander, &QToolButton::toggled, this, [this](bool on) {
        m_expander->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        if (m_extended)
            m_extended->setVisible(on);
    });

    m_preset = new ComfyComboBox(this);
    m_preset->setMinimumWidth(230);
    ComfyUiStyle::applyComboBox(m_preset);
    connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComfyStyleSamplerWidget::onPresetChanged);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 4, 0, 0);
    headerLayout->addWidget(m_expander);
    headerLayout->addStretch();
    headerLayout->addWidget(m_preset);

    m_samplerInfo = new QLabel(this);
    m_samplerInfo->setWordWrap(true);

    const auto stepsSetting = ComfyFormUi::addSliderRow(
        this,
        ComfyTr::tr("Sampler Steps"),
        ComfyTr::tr("Higher values can produce more refined results but take longer"),
        1,
        100,
        QStringLiteral("1"));
    m_steps = stepsSetting.qtSlider();
    m_stepsValue = stepsSetting.valueLabel();
    m_steps->setToolTip(ComfyTr::tr("Higher values can produce more refined results but take longer"));
    connect(m_steps, &QAbstractSlider::valueChanged, this, [this](int v) {
        m_stepsValue->setText(QString::number(v));
        if (!m_loading)
            emit valueChanged();
    });

    const auto cfgSetting = ComfyFormUi::addSliderRow(
        this,
        ComfyTr::tr("Guidance Strength (CFG Scale)"),
        ComfyTr::tr("Value which indicates how closely image generation follows the text prompt"),
        10,
        200,
        QStringLiteral("1.0"));
    m_cfg = cfgSetting.qtSlider();
    m_cfgValue = cfgSetting.valueLabel();
    m_cfg->setToolTip(ComfyTr::tr("Value which indicates how closely image generation follows the text prompt"));
    connect(m_cfg, &QAbstractSlider::valueChanged, this, [this](int v) {
        m_cfgValue->setText(QString::number(v / 10.0, 'f', 1));
        if (!m_loading)
            emit valueChanged();
    });

    auto *infoLayout = new QHBoxLayout();
    infoLayout->addWidget(m_samplerInfo);
    infoLayout->addStretch();

    auto *extendedLayout = new QVBoxLayout();
    extendedLayout->setContentsMargins(16, 2, 0, 2);
    extendedLayout->addLayout(infoLayout);
    extendedLayout->addWidget(stepsSetting.row);
    extendedLayout->addWidget(cfgSetting.row);

    m_extended = new QWidget(this);
    m_extended->setLayout(extendedLayout);
    m_extended->setVisible(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(headerLayout);
    layout->addWidget(m_extended);

    refillPresetCombo(QString());
}

void ComfyStyleSamplerWidget::refillPresetCombo(const QString &selectName)
{
    m_preset->blockSignals(true);
    const QString prev = selectName.isEmpty() ? m_preset->currentText() : selectName;
    m_preset->clear();
    for (const QString &name : ComfyUIUtils::visibleSamplerPresetNames(prev))
        m_preset->addItem(name, name);
    int ix = m_preset->findData(prev);
    if (ix < 0)
        ix = m_preset->findText(prev);
    if (ix >= 0)
        m_preset->setCurrentIndex(ix);
    else if (m_preset->count() > 0)
        m_preset->setCurrentIndex(0);
    m_preset->blockSignals(false);
    updateInfoLabel();
}

void ComfyStyleSamplerWidget::updateInfoLabel()
{
    const QString presetName = m_preset->currentData().toString();
    if (presetName.isEmpty()) {
        m_samplerInfo->clear();
        return;
    }
    const QJsonObject root = ComfyUIUtils::builtinSamplerPresetsRoot();
    QString sam, sch, lora;
    int steps = 20;
    int minSteps = 1;
    double cfg = 7.0;
    if (!ComfyUIUtils::samplerPresetLookup(root, presetName, &sam, &sch, &steps, &minSteps, &cfg, &lora)) {
        m_samplerInfo->clear();
        return;
    }
    QString text = QStringLiteral(" ") + ComfyTr::tr("Sampler") + QStringLiteral(": %1 / %2").arg(sam, sch);
    if (!lora.isEmpty())
        text += QStringLiteral(" +LoRA '%1'").arg(lora);
    m_samplerInfo->setText(text);
}

void ComfyStyleSamplerWidget::onPresetChanged(int)
{
    const QString presetName = m_preset->currentData().toString();
    if (presetName.isEmpty()) {
        updateInfoLabel();
        if (!m_loading)
            emit valueChanged();
        return;
    }
    const QJsonObject root = ComfyUIUtils::builtinSamplerPresetsRoot();
    QString sam, sch, lora;
    int steps = 20;
    int minSteps = 1;
    double cfg = 7.0;
    if (ComfyUIUtils::samplerPresetLookup(root, presetName, &sam, &sch, &steps, &minSteps, &cfg, &lora)) {
        m_loading = true;
        m_steps->setValue(qBound(1, steps, 100));
        m_cfg->setValue(qBound(10, qRound(cfg * 10.0), 200));
        m_loading = false;
    }
    updateInfoLabel();
    if (!m_loading)
        emit valueChanged();
}

void ComfyStyleSamplerWidget::readFromStyle(const ComfyStyleEntry &style)
{
    m_loading = true;
    if (m_kind == Kind::Quality) {
        refillPresetCombo(style.samplerPresetName);
        m_steps->setValue(qBound(1, style.samplerSteps, 100));
        m_cfg->setValue(qBound(10, qRound(style.cfgScale * 10.0), 200));
    } else {
        refillPresetCombo(style.liveSamplerPresetName);
        m_steps->setValue(qBound(1, style.liveSamplerSteps, 100));
        m_cfg->setValue(qBound(10, qRound(style.liveCfgScale * 10.0), 200));
    }
    m_stepsValue->setText(QString::number(m_steps->value()));
    m_cfgValue->setText(QString::number(m_cfg->value() / 10.0, 'f', 1));
    m_loading = false;
    updateInfoLabel();
}

void ComfyStyleSamplerWidget::writeToStyle(ComfyStyleEntry *style) const
{
    if (!style)
        return;
    const QString preset = m_preset->currentData().toString();
    const int steps = m_steps->value();
    const double cfg = m_cfg->value() / 10.0;
    if (m_kind == Kind::Quality) {
        style->samplerPresetName = preset;
        style->samplerSteps = steps;
        style->cfgScale = cfg;
    } else {
        style->liveSamplerPresetName = preset;
        style->liveSamplerSteps = steps;
        style->liveCfgScale = cfg;
    }
}

void ComfyStyleSamplerWidget::setEditingEnabled(bool enabled)
{
    m_expander->setEnabled(enabled);
    m_preset->setEnabled(enabled);
    m_steps->setEnabled(enabled);
    m_cfg->setEnabled(enabled);
}
