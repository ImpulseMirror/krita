/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyTextArea.h"

#include "ComfySpinBox.h"

#include <QComboBox>
#include <QSpinBox>
#include <QString>
#include <QUuid>
#include <QVector>
#include <QPointer>
#include <QVector>

#include <kis_node.h>
#include <kis_paint_layer.h>
#include <kis_types.h>

class QCompleter;

namespace ComfyDockShellInternal {

void setComboCurrentItemData(QComboBox *c, const QString &data, int fallbackIndex);
QUuid comfyParseLayerUuidString(const QString &layerId);
KisPaintLayer *findPaintLayerByUuidInTree(KisNodeSP node, const QString &uuidWithoutBraces);
void collectPaintLayerNodes(KisNodeSP node, QVector<QPair<QString, QString>> *out);
void collectInpaintContextMaskLayerNodes(KisNodeSP node, QVector<QPair<QString, QString>> *out);

using ComfyPromptPlainTextEdit = ComfyTextArea;

/// §13.32: Strength spinbox snaps to valid step boundaries (arrow keys / scroll).
class StrengthSpinBox : public ComfySpinBox
{
public:
    explicit StrengthSpinBox(QSpinBox *stepsSpinBox, QWidget *parent = nullptr);
    void stepBy(int step) override;

private:
    QPointer<QSpinBox> m_steps;
};

/// §13.105: compact progress for Live preview row (percent label + 120° arc).
class LiveSpinnerWidget : public QWidget
{
public:
    explicit LiveSpinnerWidget(QWidget *parent = nullptr);
    void setProgress(int progress);
    void startAnimation();
    void stopAnimation();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QTimer *m_timer = nullptr;
    int m_progress = 0;
    int m_angle = 0;
};

void setLiveSpinnerProgress(QWidget *spinner, int percent);
void startLiveSpinnerWidget(QWidget *spinner);
void stopLiveSpinnerWidget(QWidget *spinner);

} // namespace ComfyDockShellInternal
