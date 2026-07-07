/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfySlider.h"
#include "ComfyTrackSlider.h"
#include "ComfyUiLayoutDiagnostics.h"
#include "ComfyUiStyle.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QTimer>

ComfySlider::ComfySlider(int min, int max, const QString &initialValueText, Layout layout, QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(ComfyUiStyle::Spacing::rowGap);
    m_layout->setAlignment(Qt::AlignVCenter);

    m_slider = new ComfyTrackSlider(Qt::Horizontal, this);
    m_slider->setRange(min, max);

    if (layout == Layout::Settings) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(ComfyUiStyle::Spacing::rowHeight);
    } else {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(ComfyUiStyle::Spacing::sliderWidgetHeight);
    }

    m_valueLabel = new QLabel(this);
    m_valueLabel->setText(initialValueText);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    if (layout == Layout::Settings) {
        m_slider->setMinimumWidth(200);
        m_slider->setMaximumWidth(300);
        const QFontMetrics fm(m_valueLabel->font());
        m_valueLabel->setMinimumWidth(fm.horizontalAdvance(QStringLiteral("555 px")));
    } else {
        m_slider->setMinimumWidth(0);
        m_slider->setMaximumWidth(QWIDGETSIZE_MAX);
        m_layout->addWidget(m_slider, 1);
        m_layout->addWidget(m_valueLabel);
        QTimer::singleShot(0, this, [this]() {
            ComfyUiLayoutDiagnostics::logSliderMetrics("comfySlider.expanding", this);
            if (m_slider)
                ComfyUiLayoutDiagnostics::logSliderMetrics("comfySlider.track", m_slider);
        });
        return;
    }

    m_layout->addWidget(m_slider);
    m_layout->addWidget(m_valueLabel);
}

void ComfySlider::setValueText(const QString &text)
{
    if (m_valueLabel)
        m_valueLabel->setText(text);
}

void ComfySlider::setValueLabelVisible(bool visible)
{
    if (m_valueLabel)
        m_valueLabel->setVisible(visible);
    if (m_layout && m_layout->count() > 1) {
        setFixedHeight(visible ? ComfyUiStyle::Spacing::rowHeight
                               : ComfyUiStyle::Spacing::sliderWidgetHeight);
    }
}

void ComfySlider::setTrailingWidget(QWidget *widget)
{
    if (!widget || !m_layout)
        return;
    m_layout->addWidget(widget);
}

QSize ComfySlider::sizeHint() const
{
    const bool tallRow = m_valueLabel && m_valueLabel->isVisible() && m_layout && m_layout->count() > 1;
    const int h = tallRow ? ComfyUiStyle::Spacing::rowHeight : ComfyUiStyle::Spacing::sliderWidgetHeight;
    int w = 200;
    if (m_slider && m_slider->minimumWidth() > 0)
        w = m_slider->minimumWidth();
    return QSize(w, h);
}

QSize ComfySlider::minimumSizeHint() const
{
    return QSize(60, ComfyUiStyle::Spacing::sliderWidgetHeight);
}
