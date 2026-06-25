/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_STYLE_LORA_LIST_WIDGET_H_
#define COMFY_STYLE_LORA_LIST_WIDGET_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QPushButton;
class QToolButton;
class QVBoxLayout;
class ComfyStyleLoraItemWidget;

/// Python ai_diffusion/ui/style.py LoraList — per-style LoRA list (not the global library).
class ComfyStyleLoraListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ComfyStyleLoraListWidget(QWidget *parent = nullptr);

    QJsonArray value() const;
    void setValue(const QJsonArray &arr);

    void setEditingEnabled(bool enabled);
    void setServerLoraFilenames(const QStringList &serverLoras);
    void refreshFilters();

Q_SIGNALS:
    void valueChanged();
    void refreshRequested();

private:
    void addItem(const QJsonObject &initial = QJsonObject());
    void removeItem(ComfyStyleLoraItemWidget *item);
    void applyFilter();
    void notifyChanged();

    QVBoxLayout *m_itemLayout = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QPushButton *m_addButton = nullptr;
    QToolButton *m_refreshButton = nullptr;
    QList<ComfyStyleLoraItemWidget *> m_items;
    QStringList m_serverLoraFilenames;
    bool m_editingEnabled = true;
};

#endif
