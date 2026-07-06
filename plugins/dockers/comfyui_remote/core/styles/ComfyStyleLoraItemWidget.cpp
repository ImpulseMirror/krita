/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyComboBox.h"
#include "ComfyStyleLoraItemWidget.h"
#include "ComfyTextArea.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfySwitchWidget.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include "ComfySpinBox.h"
#include <QToolButton>
#include <QVBoxLayout>

#include <kis_icon_utils.h>

namespace {

QString displayNameFromLoraId(const QString &id)
{
    const int dot = id.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? id : id.left(dot);
}

} // namespace

QVariant ComfyStyleLoraItemWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return ComfyTextArea::forwardContainerInputMethodQuery(this, query);
}

ComfyStyleLoraItemWidget::ComfyStyleLoraItemWidget(QWidget *parent)
    : QWidget(parent)
{
    setContentsMargins(0, 0, 0, 0);

    m_advancedButton = new QToolButton(this);
    m_advancedButton->setCheckable(true);
    m_advancedButton->setArrowType(Qt::RightArrow);
    ComfyUiStyle::applyExpanderButton(m_advancedButton);
    m_advancedButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    connect(m_advancedButton, &QToolButton::toggled, this, &ComfyStyleLoraItemWidget::expandAdvanced);

    m_select = new ComfyComboBox(this);
    m_select->setEditable(true);
    ComfyUiStyle::applyComboBox(m_select);
    m_select->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_select, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const QString id = m_select->currentData().toString();
        if (!id.isEmpty())
            selectLoraById(id);
    });
    connect(m_select, &QComboBox::editTextChanged, this, [this](const QString &) { notifyChanged(); });

    m_warningIcon = new QLabel(this);
    m_warningIcon->setPixmap(ComfyTheme::icon(QStringLiteral("warning")).pixmap(16, 16));
    m_warningIcon->setToolTip(QString());
    m_warningIcon->hide();

    m_enabled = new ComfySwitchWidget(this);
    m_enabled->setChecked(true);
    connect(m_enabled, &QAbstractButton::toggled, this, &ComfyStyleLoraItemWidget::notifyChanged);

    m_strength = new ComfySpinBox(this);
    m_strength->setRange(-400, 400);
    m_strength->setSingleStep(5);
    m_strength->setValue(100);
    m_strength->setPrefix(ComfyTr::tr("Strength") + QStringLiteral(": "));
    m_strength->setSuffix(QStringLiteral("%"));
    connect(m_strength, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComfyStyleLoraItemWidget::notifyChanged);

    m_remove = new QToolButton(this);
    m_remove->setIcon(ComfyTheme::icon(QStringLiteral("discard")));
    m_remove->setAutoRaise(true);
    ComfyUiStyle::applyIconToolButton(m_remove);
    connect(m_remove, &QToolButton::clicked, this, [this]() { emit removed(this); });

    auto *expanderLayout = new QHBoxLayout();
    expanderLayout->setContentsMargins(0, 0, 0, 0);
    expanderLayout->setSpacing(0);
    expanderLayout->addWidget(m_advancedButton);
    expanderLayout->addWidget(m_select, 1);

    auto *itemLayout = new QHBoxLayout();
    itemLayout->setContentsMargins(0, 0, 0, 0);
    itemLayout->addLayout(expanderLayout, 3);
    itemLayout->addWidget(m_enabled);
    itemLayout->addWidget(m_strength, 1);
    itemLayout->addWidget(m_warningIcon);
    itemLayout->addWidget(m_remove);

    m_advanced = new QWidget(this);
    m_advanced->setVisible(false);

    m_warningText = new QLabel(m_advanced);
    ComfyUiStyle::styleWarning(m_warningText);
    m_warningText->setWordWrap(true);
    m_warningText->hide();

    auto *triggerLabel = new QLabel(ComfyTr::tr("Trigger words"), m_advanced);
    m_triggerEdit = new QLineEdit(m_advanced);
    m_triggerEdit->setPlaceholderText(
        ComfyTr::tr("Optional text which is added to the prompt when the LoRA is used"));
    ComfyUiStyle::applyLineEdit(m_triggerEdit);
    connect(m_triggerEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_currentId.isEmpty())
            return;
        ComfyFileLibrary::instance().init();
        if (const ComfyFileRecord *rec = ComfyFileLibrary::instance().loras().find(m_currentId)) {
            if (rec->meta(QStringLiteral("lora_triggers")).toString() != text)
                ComfyFileLibrary::instance().loras().setMetaById(m_currentId, QStringLiteral("lora_triggers"), text);
        }
    });

    auto *triggerLayout = new QVBoxLayout();
    triggerLayout->setContentsMargins(0, 0, 0, 0);
    triggerLayout->addWidget(triggerLabel);
    triggerLayout->addWidget(m_triggerEdit);

    auto *defaultLabel = new QLabel(ComfyTr::tr("Default Strength"), m_advanced);
    auto *defaultFrame = new QFrame(m_advanced);
    defaultFrame->setFrameShape(QFrame::StyledPanel);
    auto *defaultFrameLayout = new QHBoxLayout(defaultFrame);
    defaultFrameLayout->setContentsMargins(6, 2, 6, 2);
    m_defaultStrengthValue = new QLabel(QStringLiteral("100%"), defaultFrame);
    defaultFrameLayout->addWidget(m_defaultStrengthValue);

    m_defaultStrengthButton = new QPushButton(ComfyTr::tr("Set Default"), m_advanced);
    connect(m_defaultStrengthButton, &QPushButton::clicked, this, [this]() {
        if (m_currentId.isEmpty())
            return;
        ComfyFileLibrary::instance().init();
        const ComfyFileRecord *rec = ComfyFileLibrary::instance().loras().find(m_currentId);
        if (!rec)
            return;
        const double strength = m_strength->value() / 100.0;
        if (qAbs(rec->meta(QStringLiteral("lora_strength")).toDouble(1.0) - strength) > 0.0001) {
            ComfyFileLibrary::instance().loras().setMetaById(m_currentId, QStringLiteral("lora_strength"), strength);
            updateMetaPanel();
        }
    });

    auto *defaultStrengthFrameLayout = new QHBoxLayout();
    defaultStrengthFrameLayout->setContentsMargins(0, 0, 0, 0);
    defaultStrengthFrameLayout->addWidget(defaultFrame);
    defaultStrengthFrameLayout->addWidget(m_defaultStrengthButton);
    auto *defaultStrengthLayout = new QVBoxLayout();
    defaultStrengthLayout->setContentsMargins(0, 0, 0, 0);
    defaultStrengthLayout->addWidget(defaultLabel);
    defaultStrengthLayout->addLayout(defaultStrengthFrameLayout);

    auto *metaLayout = new QHBoxLayout();
    metaLayout->addLayout(triggerLayout, 3);
    metaLayout->addLayout(defaultStrengthLayout, 1);

    m_fileIdLabel = new QLabel(m_advanced);
    m_filePathLabel = new QLabel(m_advanced);
    QFont smallFont = m_fileIdLabel->font();
    smallFont.setPointSize(qMax(6, smallFont.pointSize() - 1));
    m_fileIdLabel->setFont(smallFont);
    m_filePathLabel->setFont(smallFont);
    ComfyUiStyle::styleCaption(m_fileIdLabel);
    ComfyUiStyle::styleCaption(m_filePathLabel);

    auto *advancedLayout = new QVBoxLayout();
    advancedLayout->setContentsMargins(3, 2, 0, 2);
    advancedLayout->addWidget(m_warningText);
    advancedLayout->addLayout(metaLayout);
    advancedLayout->addWidget(m_fileIdLabel);
    advancedLayout->addWidget(m_filePathLabel);

    auto *line = new QFrame(m_advanced);
    line->setFrameShape(QFrame::VLine);
    line->setLineWidth(1);

    auto *padLayout = new QHBoxLayout();
    padLayout->setContentsMargins(7, 0, 34, 10);
    padLayout->addWidget(line);
    padLayout->addLayout(advancedLayout);
    m_advanced->setLayout(padLayout);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(itemLayout);
    layout->addWidget(m_advanced);

    refreshLoraNames();
}

void ComfyStyleLoraItemWidget::expandAdvanced(bool on)
{
    m_advancedButton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    m_advanced->setVisible(on);
}

void ComfyStyleLoraItemWidget::notifyChanged()
{
    updateMetaPanel();
    emit changed();
}

QJsonObject ComfyStyleLoraItemWidget::value() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), m_currentId);
    o.insert(QStringLiteral("strength"), m_strength->value() / 100.0);
    o.insert(QStringLiteral("enabled"), m_enabled->isChecked());
    return o;
}

void ComfyStyleLoraItemWidget::setValue(const QJsonObject &obj)
{
    const QString id = obj.value(QStringLiteral("name")).toString().trimmed();
    const double strength = obj.value(QStringLiteral("strength")).toDouble(1.0);
    const bool enabled = obj.value(QStringLiteral("enabled")).toBool(true);

    m_enabled->blockSignals(true);
    m_strength->blockSignals(true);
    m_enabled->setChecked(enabled);
    m_strength->setValue(qRound(strength * 100.0));
    m_enabled->blockSignals(false);
    m_strength->blockSignals(false);

    if (id.isEmpty()) {
        m_currentId.clear();
        m_select->blockSignals(true);
        m_select->setCurrentIndex(-1);
        m_select->clearEditText();
        m_select->blockSignals(false);
    } else {
        selectLoraById(id);
    }
    updateMetaPanel();
}

void ComfyStyleLoraItemWidget::reset()
{
    m_currentId.clear();
    m_enabled->blockSignals(true);
    m_strength->blockSignals(true);
    m_enabled->setChecked(true);
    m_strength->setValue(100);
    m_enabled->blockSignals(false);
    m_strength->blockSignals(false);
    refreshLoraNames();
    if (m_select->count() > 0)
        selectLoraById(m_select->itemData(0).toString());
    updateMetaPanel();
}

void ComfyStyleLoraItemWidget::setFilterPrefix(const QString &prefix)
{
    m_filterPrefix = prefix;
    refreshLoraNames();
}

void ComfyStyleLoraItemWidget::refreshLoraNames()
{
    const QString prevId = m_currentId.isEmpty() ? m_select->currentData().toString() : m_currentId;
    m_select->blockSignals(true);
    m_select->clear();
    ComfyFileLibrary::instance().init();
    for (const ComfyFileRecord &rec : ComfyFileLibrary::instance().loras().files()) {
        if (rec.source == ComfyFileSourceUnavailable)
            continue;
        if (!m_filterPrefix.isEmpty() && !rec.id.startsWith(m_filterPrefix))
            continue;
        m_select->addItem(rec.name, rec.id);
    }
    if (!prevId.isEmpty()) {
        const int ix = m_select->findData(prevId);
        if (ix >= 0)
            m_select->setCurrentIndex(ix);
        else
            m_select->setEditText(displayNameFromLoraId(prevId));
    }
    m_select->blockSignals(false);
}

void ComfyStyleLoraItemWidget::setServerLoraFilenames(const QStringList &serverLoras)
{
    m_serverLoraFilenames = serverLoras;
    showLoraWarnings();
}

void ComfyStyleLoraItemWidget::setActive(bool active)
{
    m_active = active;
    setVisible(active);
}

void ComfyStyleLoraItemWidget::selectLoraById(const QString &id)
{
    if (id.isEmpty())
        return;
    ComfyFileLibrary::instance().init();
    const ComfyFileRecord *file = ComfyFileLibrary::instance().loras().find(id);
    if (!file) {
        ComfyFileRecord remote = ComfyFileRecord::remote(id, ComfyFileFormat::Lora);
        m_currentId = id;
        m_select->blockSignals(true);
        const int ix = m_select->findData(id);
        if (ix >= 0)
            m_select->setCurrentIndex(ix);
        else
            m_select->setEditText(remote.name);
        m_select->blockSignals(false);
        updateMetaPanel();
        return;
    }
    if (file->id == m_currentId)
        return;
    m_currentId = file->id;
    m_select->blockSignals(true);
    const int ix = m_select->findData(file->id);
    if (ix >= 0)
        m_select->setCurrentIndex(ix);
    else
        m_select->setEditText(file->name);
    m_select->blockSignals(false);

    const int defaultPct = qRound(file->meta(QStringLiteral("lora_strength")).toDouble(1.0) * 100.0);
    if (defaultPct != m_strength->value()) {
        m_strength->blockSignals(true);
        m_strength->setValue(defaultPct);
        m_strength->blockSignals(false);
    }
    const QString triggers = file->meta(QStringLiteral("lora_triggers")).toString();
    if (!triggers.isEmpty()) {
        m_triggerEdit->blockSignals(true);
        m_triggerEdit->setText(triggers);
        m_triggerEdit->blockSignals(false);
    }
    updateMetaPanel();
    notifyChanged();
}

void ComfyStyleLoraItemWidget::updateMetaPanel()
{
    if (m_currentId.isEmpty()) {
        m_fileIdLabel->clear();
        m_filePathLabel->hide();
        m_warningIcon->hide();
        m_warningText->hide();
        m_defaultStrengthValue->setText(QStringLiteral("100%"));
        m_defaultStrengthButton->setEnabled(m_strength->value() != 100);
        return;
    }

    ComfyFileLibrary::instance().init();
    const ComfyFileRecord *file = ComfyFileLibrary::instance().loras().find(m_currentId);
    m_fileIdLabel->setText(QStringLiteral("ID: %1").arg(m_currentId));
    if (file && (file->source & ComfyFileSourceLocal) && !file->path.isEmpty()) {
        QString path = file->path;
        if (path.size() > 80)
            path = QStringLiteral("...") + path.right(80);
        m_filePathLabel->setText(ComfyTr::tr("Local file") + QStringLiteral(": %1").arg(path));
        m_filePathLabel->show();
    } else {
        m_filePathLabel->hide();
    }

    if (file && file->meta(QStringLiteral("lora_strength")).isDouble()) {
        const int istrength = qRound(file->meta(QStringLiteral("lora_strength")).toDouble() * 100.0);
        m_defaultStrengthValue->setText(QStringLiteral("%1%").arg(istrength));
        m_defaultStrengthButton->setEnabled(istrength != m_strength->value());
    } else {
        m_defaultStrengthValue->setText(QStringLiteral("100%"));
        m_defaultStrengthButton->setEnabled(m_strength->value() != 100);
    }

    if (file)
        m_triggerEdit->setText(file->meta(QStringLiteral("lora_triggers")).toString());

    showLoraWarnings();
}

void ComfyStyleLoraItemWidget::showLoraWarnings()
{
    if (m_currentId.isEmpty()) {
        m_warningIcon->hide();
        m_warningText->hide();
        return;
    }

    const QString notInstalled = ComfyTr::tr("The LoRA file is not installed on the server.");
    const QString special = ComfyTr::tr(
        "This LoRA is usually added automatically by a Sampler or Control Layer when needed.\n"
        "It is not required to add it manually here.");

    ComfyFileLibrary::instance().init();
    const ComfyFileRecord *file = ComfyFileLibrary::instance().loras().find(m_currentId);
    const bool onServer = !m_serverLoraFilenames.isEmpty()
                          && ComfyUIUtils::loraFilenameKnownOnServer(m_currentId, m_serverLoraFilenames);

    if (!file || file->source == ComfyFileSourceUnavailable || (!m_serverLoraFilenames.isEmpty() && !onServer)) {
        m_warningIcon->setToolTip(notInstalled);
        m_warningIcon->show();
        m_warningText->setText(notInstalled);
        m_warningText->show();
    } else if (m_currentId.startsWith(QStringLiteral("lora-"), Qt::CaseInsensitive)) {
        m_warningIcon->setToolTip(special);
        m_warningIcon->show();
        m_warningText->setText(special);
        m_warningText->show();
    } else {
        m_warningIcon->hide();
        m_warningText->hide();
    }
}
