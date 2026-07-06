/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_STYLE_LORA_ITEM_WIDGET_H_
#define COMFY_STYLE_LORA_ITEM_WIDGET_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class ComfySwitchWidget;

/// Python ai_diffusion/ui/style.py LoraItem — one LoRA row in a style preset.
class ComfyStyleLoraItemWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ComfyStyleLoraItemWidget(QWidget *parent = nullptr);

    QJsonObject value() const;
    void setValue(const QJsonObject &obj);
    void reset();

    void setFilterPrefix(const QString &prefix);
    void refreshLoraNames();
    void setServerLoraFilenames(const QStringList &serverLoras);

    bool isActive() const { return m_active; }
    void setActive(bool active);

protected:
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

Q_SIGNALS:
    void changed();
    void removed(ComfyStyleLoraItemWidget *self);

private:
    void expandAdvanced(bool on);
    void notifyChanged();
    void selectLoraById(const QString &id);
    void updateMetaPanel();
    void showLoraWarnings();

    bool m_active = true;
    QString m_currentId;
    QString m_filterPrefix;
    QStringList m_serverLoraFilenames;

    QToolButton *m_advancedButton = nullptr;
    QComboBox *m_select = nullptr;
    QLabel *m_warningIcon = nullptr;
    ComfySwitchWidget *m_enabled = nullptr;
    QSpinBox *m_strength = nullptr;
    QToolButton *m_remove = nullptr;
    QWidget *m_advanced = nullptr;
    QLabel *m_warningText = nullptr;
    QLineEdit *m_triggerEdit = nullptr;
    QLabel *m_defaultStrengthValue = nullptr;
    QPushButton *m_defaultStrengthButton = nullptr;
    QLabel *m_fileIdLabel = nullptr;
    QLabel *m_filePathLabel = nullptr;
};

#endif
