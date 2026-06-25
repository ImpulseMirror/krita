/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyStyleLoraListWidget.h"
#include "ComfyStyleLoraItemWidget.h"
#include "ComfyFileLibrary.h"
#include "ComfyLocalization.h"
#include "ComfyTheme.h"
#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QSet>
#include <QVBoxLayout>

#include <kis_icon_utils.h>

#include <algorithm>

namespace {

QString s_lastLoraFilterPrefix;

} // namespace

ComfyStyleLoraListWidget::ComfyStyleLoraListWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto *headerTextLayout = new QVBoxLayout();
    QLabel *titleLabel = new QLabel(ComfyTr::tr("LoRA"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    QLabel *descLabel = new QLabel(
        ComfyTr::tr("Extensions to the checkpoint which expand its range based on additional training."), this);
    descLabel->setWordWrap(true);
    headerTextLayout->addWidget(titleLabel);
    headerTextLayout->addWidget(descLabel);
    headerLayout->addLayout(headerTextLayout, 5);

    m_addButton = new QPushButton(ComfyTr::tr("Add"), this);
    m_addButton->setMinimumWidth(100);
    connect(m_addButton, &QPushButton::clicked, this, [this]() { addItem(); });

    m_filterCombo = new QComboBox(this);
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyFilter(); });

    m_refreshButton = new QToolButton(this);
    m_refreshButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_refreshButton->setIcon(ComfyTheme::icon(QStringLiteral("reset")));
    m_refreshButton->setToolTip(ComfyTr::tr("Look for new LoRA files"));
    connect(m_refreshButton, &QToolButton::clicked, this, [this]() {
        ComfyFileLibrary::instance().init();
        ComfyFileLibrary::instance().loras().load();
        refreshFilters();
        for (ComfyStyleLoraItemWidget *item : m_items)
            item->refreshLoraNames();
        emit refreshRequested();
    });

    headerLayout->addWidget(m_addButton, 1);
    headerLayout->addWidget(m_filterCombo, 2);
    headerLayout->addWidget(m_refreshButton, 0);
    mainLayout->addLayout(headerLayout);

    m_itemLayout = new QVBoxLayout();
    m_itemLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addLayout(m_itemLayout);

    refreshFilters();
}

QJsonArray ComfyStyleLoraListWidget::value() const
{
    QJsonArray arr;
    for (ComfyStyleLoraItemWidget *item : m_items) {
        if (!item->isActive())
            continue;
        const QJsonObject o = item->value();
        if (o.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            continue;
        arr.append(o);
    }
    return arr;
}

void ComfyStyleLoraListWidget::setValue(const QJsonArray &arr)
{
    for (ComfyStyleLoraItemWidget *item : m_items)
        removeItem(item);
    for (const QJsonValue &v : arr) {
        if (!v.isObject())
            continue;
        addItem(v.toObject());
    }
}

void ComfyStyleLoraListWidget::setEditingEnabled(bool enabled)
{
    m_editingEnabled = enabled;
    m_addButton->setEnabled(enabled);
    m_filterCombo->setEnabled(enabled);
    m_refreshButton->setEnabled(enabled);
    for (ComfyStyleLoraItemWidget *item : m_items)
        item->setEnabled(enabled);
}

void ComfyStyleLoraListWidget::setServerLoraFilenames(const QStringList &serverLoras)
{
    m_serverLoraFilenames = serverLoras;
    for (ComfyStyleLoraItemWidget *item : m_items)
        item->setServerLoraFilenames(serverLoras);
}

void ComfyStyleLoraListWidget::refreshFilters()
{
    m_filterCombo->blockSignals(true);
    m_filterCombo->clear();
    m_filterCombo->addItem(ComfyTheme::icon(QStringLiteral("filter")), ComfyTr::tr("All"), QString());
    QSet<QString> folders;
    ComfyFileLibrary::instance().init();
    for (const ComfyFileRecord &lora : ComfyFileLibrary::instance().loras().files()) {
        if (lora.source == ComfyFileSourceUnavailable)
            continue;
        const QStringList parts = lora.id.split(QLatin1Char('/'));
        for (int i = 1; i < parts.size(); ++i)
            folders.insert(parts.mid(0, i).join(QLatin1Char('/')));
    }
    const QIcon folderIcon = ComfyTheme::icon(QStringLiteral("root"));
    QStringList folderList = folders.values();
    std::sort(folderList.begin(), folderList.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for (const QString &folder : folderList)
        m_filterCombo->addItem(folderIcon, folder, folder);
    int ix = 0;
    if (!s_lastLoraFilterPrefix.isNull()) {
        const int found = m_filterCombo->findData(s_lastLoraFilterPrefix);
        if (found >= 0)
            ix = found;
    }
    m_filterCombo->setCurrentIndex(ix);
    m_filterCombo->blockSignals(false);

    ComfyFileLibrary::instance().init();
    m_addButton->setEnabled(m_editingEnabled && !ComfyFileLibrary::instance().loras().files().isEmpty());
    applyFilter();
}

void ComfyStyleLoraListWidget::addItem(const QJsonObject &initial)
{
    ComfyStyleLoraItemWidget *item = nullptr;
    for (ComfyStyleLoraItemWidget *candidate : m_items) {
        if (!candidate->isActive()) {
            item = candidate;
            item->setActive(true);
            break;
        }
    }
    if (!item) {
        item = new ComfyStyleLoraItemWidget(this);
        connect(item, &ComfyStyleLoraItemWidget::changed, this, &ComfyStyleLoraListWidget::notifyChanged);
        connect(item, &ComfyStyleLoraItemWidget::removed, this, &ComfyStyleLoraListWidget::removeItem);
        m_items.append(item);
    }
    item->setServerLoraFilenames(m_serverLoraFilenames);
    item->setFilterPrefix(m_filterCombo->currentData().toString());
    item->refreshLoraNames();
    if (initial.isEmpty())
        item->reset();
    else
        item->setValue(initial);
    m_itemLayout->addWidget(item);
    notifyChanged();
}

void ComfyStyleLoraListWidget::removeItem(ComfyStyleLoraItemWidget *item)
{
    if (!item)
        return;
    item->setActive(false);
    m_itemLayout->removeWidget(item);
    notifyChanged();
}

void ComfyStyleLoraListWidget::applyFilter()
{
    const QString prefix = m_filterCombo->currentData().toString();
    s_lastLoraFilterPrefix = prefix;
    for (ComfyStyleLoraItemWidget *item : m_items) {
        if (!item->isActive())
            continue;
        item->setFilterPrefix(prefix);
    }
}

void ComfyStyleLoraListWidget::notifyChanged()
{
    emit valueChanged();
}
