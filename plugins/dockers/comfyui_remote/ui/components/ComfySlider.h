/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ComfyTrackSlider.h"

#include <QWidget>

class QLabel;
class QHBoxLayout;

/// Horizontal slider with optional value label (settings + docker).
class ComfySlider : public QWidget
{
    Q_OBJECT
public:
    enum class Layout {
        Settings,   ///< 200–300px track, reserved value label width
        Expanding,  ///< track grows; value label fits content
    };

    explicit ComfySlider(int min,
                         int max,
                         const QString &initialValueText = QString(),
                         Layout layout = Layout::Settings,
                         QWidget *parent = nullptr);

    QAbstractSlider *slider() const { return m_slider; }

    QLabel *valueLabel() const { return m_valueLabel; }

    void setValueText(const QString &text);
    void setValueLabelVisible(bool visible);
    void setTrailingWidget(QWidget *widget);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    ComfyTrackSlider *m_slider = nullptr;
    QLabel *m_valueLabel = nullptr;
    QHBoxLayout *m_layout = nullptr;
};
