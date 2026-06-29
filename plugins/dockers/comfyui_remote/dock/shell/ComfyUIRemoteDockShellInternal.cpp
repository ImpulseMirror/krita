/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyUIRemoteDockShellInternal.h"

#include "ComfyUIUtils.h"

#include <QComboBox>
#include <QCompleter>
#include <QAbstractItemView>
#include <QPainter>
#include <QPointer>
#include <QTimer>

#include <kis_layer.h>
#include <kis_node.h>
#include <kis_paint_layer.h>

namespace ComfyDockShellInternal {

void setComboCurrentItemData(QComboBox *c, const QString &data, int fallbackIndex)
{
    if (!c || c->count() <= 0)
        return;
    for (int i = 0; i < c->count(); ++i) {
        if (c->itemData(i).toString() == data) {
            c->setCurrentIndex(i);
            return;
        }
    }
    c->setCurrentIndex(qBound(0, fallbackIndex, c->count() - 1));
}

QUuid comfyParseLayerUuidString(const QString &layerId)
{
    const QString lid = layerId.trimmed();
    if (lid.isEmpty())
        return QUuid();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QUuid::fromString(QStringView{lid});
#else
    QString s = lid;
    if (s.length() == 32 && !s.contains(QLatin1Char('-'))) {
        s = QStringLiteral("{%1-%2-%3-%4-%5}")
                .arg(s.mid(0, 8), s.mid(8, 4), s.mid(12, 4), s.mid(16, 4), s.mid(20, 12));
    }
    return QUuid(s);
#endif
}

KisPaintLayer *findPaintLayerByUuidInTree(KisNodeSP node, const QString &uuidWithoutBraces)
{
    if (!node || uuidWithoutBraces.isEmpty())
        return nullptr;
    KisPaintLayer *pl = dynamic_cast<KisPaintLayer *>(node.data());
    if (pl && node->uuid().toString(QUuid::WithoutBraces) == uuidWithoutBraces)
        return pl;
    for (quint32 i = 0; i < node->childCount(); ++i) {
        if (KisPaintLayer *found = findPaintLayerByUuidInTree(node->at(i), uuidWithoutBraces))
            return found;
    }
    return nullptr;
}

void collectPaintLayerNodes(KisNodeSP node, QVector<QPair<QString, QString>> *out)
{
    if (!node || !out)
        return;
    if (dynamic_cast<KisPaintLayer *>(node.data())) {
        const QString id = node->uuid().toString(QUuid::WithoutBraces);
        out->append(qMakePair(id, node->name()));
    }
    for (quint32 i = 0; i < node->childCount(); ++i)
        collectPaintLayerNodes(node->at(i), out);
}

void collectInpaintContextMaskLayerNodes(KisNodeSP node, QVector<QPair<QString, QString>> *out)
{
    if (!node || !out)
        return;
    if (ComfyUIUtils::isInpaintContextMaskNode(node)) {
        const QString id = node->uuid().toString(QUuid::WithoutBraces);
        out->append(qMakePair(id, node->name()));
    }
    for (quint32 i = 0; i < node->childCount(); ++i)
        collectInpaintContextMaskLayerNodes(node->at(i), out);
}

ComfyPromptPlainTextEdit::ComfyPromptPlainTextEdit(QCompleter *completer, QWidget *parent)
    : QPlainTextEdit(parent)
    , m_completer(completer)
{
}

bool ComfyPromptPlainTextEdit::focusNextPrevChild(bool next)
{
    if (!m_completer.isNull()) {
        QAbstractItemView *pop = m_completer->popup();
        if (pop && pop->isVisible())
            return false;
    }
    return QPlainTextEdit::focusNextPrevChild(next);
}

StrengthSpinBox::StrengthSpinBox(QSpinBox *stepsSpinBox, QWidget *parent)
    : QSpinBox(parent)
    , m_steps(stepsSpinBox)
{
}

void StrengthSpinBox::stepBy(int step)
{
    const int steps = m_steps ? qMax(1, m_steps->value()) : 20;
    int idx = qRound(value() * steps / 100.0);
    idx = qBound(1, idx, steps);
    const int newIdx = qBound(1, idx + step, steps);
    setValue(qRound(100.0 * newIdx / steps));
}

LiveSpinnerWidget::LiveSpinnerWidget(QWidget *parent)
    : QWidget(parent)
{
    // Upstream LivePreviewArea: percent label left, arc spinner right of preview.
    setFixedSize(56, 18);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + 12) % 360;
        update();
    });
}

void LiveSpinnerWidget::setProgress(int progress)
{
    m_progress = qBound(0, progress, 100);
    update();
}

void LiveSpinnerWidget::startAnimation()
{
    m_timer->start(50);
    show();
}

void LiveSpinnerWidget::stopAnimation()
{
    m_timer->stop();
    hide();
}

void LiveSpinnerWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int w = width();
    const int h = height();
    const QColor fg = palette().color(QPalette::WindowText);
    const int arcSize = qMin(h, w / 2);
    const int textWidth = w - arcSize;
    p.setPen(fg);
    p.drawText(QRect(0, 0, textWidth, h),
               Qt::AlignVCenter | Qt::AlignRight,
               QString::number(m_progress) + QLatin1Char('%'));
    const int span = 120 * 16;
    const int startAngle = (90 - m_angle) * 16;
    p.setPen(Qt::NoPen);
    p.setBrush(fg);
    p.drawPie(w - arcSize + 1, 1, arcSize - 2, arcSize - 2, startAngle, span);
}

void setLiveSpinnerProgress(QWidget *spinner, int percent)
{
    if (auto *w = static_cast<LiveSpinnerWidget *>(spinner))
        w->setProgress(percent);
}

void startLiveSpinnerWidget(QWidget *spinner)
{
    if (auto *w = static_cast<LiveSpinnerWidget *>(spinner))
        w->startAnimation();
}

void stopLiveSpinnerWidget(QWidget *spinner)
{
    if (auto *w = static_cast<LiveSpinnerWidget *>(spinner))
        w->stopAnimation();
}

} // namespace ComfyDockShellInternal
