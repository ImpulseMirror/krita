/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMFY_CONTROL_LAYER_LIST_WIDGET_H_
#define COMFY_CONTROL_LAYER_LIST_WIDGET_H_

#include "ComfyControlLayer.h"

#include <QList>
#include <QPushButton>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

class KisViewManager;
class ComfyControlLayerRowWidget;

/// Python ui/control.py ControlListWidget + ControlWidget — dynamic control layer rows.
class ComfyControlLayerListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComfyControlLayerListWidget(QWidget *parent = nullptr);

    void setViewManager(KisViewManager *viewManager);
    void setArchKeyProvider(std::function<QString()> provider);
    void setLayers(QList<ComfyControlLayerEntry> *layers);
    void refresh();
    void refreshLayerCombos();
    void setGenerateEnabled(bool enabled);

Q_SIGNALS:
    void entryEdited();
    void addLayerRequested();
    void generateRequested(int index);
    void addPoseCharacterRequested(int index);
    void removeRequested(int index);

private:
    void rebuildRows();

    KisViewManager *m_viewManager = nullptr;
    std::function<QString()> m_archKeyProvider;
    QList<ComfyControlLayerEntry> *m_layers = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QVector<ComfyControlLayerRowWidget *> m_rows;
};

#endif
