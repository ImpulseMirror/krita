/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPromptStackWidget.h"
#include "ComfyPromptLayoutMetrics.h"
#include "ComfyLocalization.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfyTheme.h"
#include "ComfyUIUtils.h"

#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <functional>

namespace {

using ComfyPromptLayoutMetrics::heightForLines;
using ComfyPromptLayoutMetrics::kNegativeLineCount;
using ComfyPromptLayoutMetrics::kPromptHeightPadPx;

QString stackBaseStyle()
{
    return QStringLiteral("QFrame#PromptStackWidget { border: 1px solid palette(mid); background: palette(base); }");
}

QString stackFocusStyle()
{
    return QStringLiteral("QFrame#PromptStackWidget { border: 1px solid palette(highlight); background: palette(base); }");
}

QString negativeEditorStyle()
{
    return QStringLiteral(
        "QPlainTextEdit { background-color: rgba(255, 0, 0, 15); border: none; padding: 0px; margin: 0px; }");
}

QString positiveEditorStyle()
{
    return QStringLiteral("QPlainTextEdit { background: transparent; border: none; padding: 0px; margin: 0px; }");
}

QFontMetrics promptFontMetrics(const QPlainTextEdit *editor)
{
    if (!editor || !editor->document())
        return QFontMetrics(editor ? editor->font() : QFont());
    return QFontMetrics(editor->document()->defaultFont());
}

int linesFromHeight(const QFontMetrics &fm, int heightPx)
{
    return qBound(1,
                  static_cast<int>(qRound((heightPx - kPromptHeightPadPx) / double(qMax(1, fm.lineSpacing())))),
                  10);
}

void paintResizeGrip(QPainter &p, const QWidget *widget)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = widget->palette().color(QPalette::PlaceholderText).lighter(100);
    p.setBrush(c);
    p.setPen(Qt::NoPen);

    const int w = widget->width();
    const int h = widget->height();
    for (int i = 0, x = 2; x < w - 1; x += 3, ++i) {
        const int y = (i % 2 == 0) ? 2 * h / 3 : h / 3;
        p.drawEllipse(QPoint(x, y), 1, 1);
    }
}

/// FAITHFUL_PORT: upstream ResizeHandle on negative — drag adjusts positive line count.
class NegativePromptDragHandle : public QWidget
{
public:
    using DragFn = std::function<void(int yPosInNegative)>;
    using ReleaseFn = std::function<void()>;

    NegativePromptDragHandle(DragFn onDrag, ReleaseFn onRelease, QWidget *parent)
        : QWidget(parent)
        , m_onDrag(std::move(onDrag))
        , m_onRelease(std::move(onRelease))
    {
        setFixedSize(22, 8);
        setCursor(Qt::SizeVerCursor);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setToolTip(ComfyTr::tr("Drag vertically to resize the text area."));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        paintResizeGrip(p, this);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_dragging = true;
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        Q_UNUSED(e);
        if (m_dragging && m_onRelease)
            m_onRelease();
        m_dragging = false;
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_dragging || !m_onDrag)
            return;
        m_onDrag(mapToParent(e->pos()).y());
    }

private:
    DragFn m_onDrag;
    ReleaseFn m_onRelease;
    bool m_dragging = false;
};

} // namespace

ComfyPromptStackWidget::ComfyPromptStackWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("PromptStackWidget"));
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_positive = new QPlainTextEdit(this);
    m_positive->setFrameShape(QFrame::NoFrame);
    m_positive->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_positive->setPlaceholderText(
        ComfyTr::tr("Describe the content you want to see, or leave empty."));
    m_positive->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_positive->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_positive->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_positive->document()->setDocumentMargin(0);
    m_positive->setTabChangesFocus(true);
    m_positive->installEventFilter(this);
    lay->addWidget(m_positive);

    m_negative = new QPlainTextEdit(this);
    m_negative->setFrameShape(QFrame::NoFrame);
    m_negative->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_negative->setPlaceholderText(ComfyTr::tr("Describe content you want to avoid."));
    m_negative->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_negative->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_negative->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_negative->document()->setDocumentMargin(0);
    m_negative->setContentsMargins(0, 2, 0, 2);
    m_negative->setTabChangesFocus(true);
    m_negative->installEventFilter(this);
    m_negative->hide();
    lay->addWidget(m_negative);

    m_negativeWarning = new QLabel(this);
    m_negativeWarning->setPixmap(ComfyTheme::icon(QStringLiteral("alert")).pixmap(16, 16));
    m_negativeWarning->setToolTip(
        ComfyTr::tr("The selected Style does not use the negative prompt."));
    m_negativeWarning->hide();
    m_negativeWarning->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    applyEditorChrome();
    applyHeights();
    updateFocusBorder();
}

void ComfyPromptStackWidget::setShowNegative(bool show)
{
    if (m_showNegative == show)
        return;
    m_showNegative = show;
    m_negative->setVisible(show);
    applyEditorChrome();
    applyHeights();
    ensureResizeHandle();
    repositionChrome();
}

void ComfyPromptStackWidget::setNegativeWarningVisible(bool visible)
{
    if (m_negativeWarning)
        m_negativeWarning->setVisible(visible && m_showNegative);
    repositionChrome();
}

void ComfyPromptStackWidget::setFocusChromeEnabled(bool enabled)
{
    m_focusChromeEnabled = enabled;
    updateFocusBorder();
}

void ComfyPromptStackWidget::setLiveLineCounts(bool liveLineCounts)
{
    m_liveLineCounts = liveLineCounts;
}

void ComfyPromptStackWidget::applyLayout(int positiveLines, bool showNegative, bool showResizeHandle)
{
    m_positiveLines = qBound(1, positiveLines, 10);
    m_showNegative = showNegative;
    m_showResizeHandle = showResizeHandle;
    m_negative->setVisible(showNegative);
    applyHeights();
    ensureResizeHandle();
    if (m_resizeHandle)
        m_resizeHandle->setVisible(showResizeHandle);
    applyEditorChrome();
    repositionChrome();
}

void ComfyPromptStackWidget::applyHeights()
{
    if (!m_positive || !m_negative)
        return;
    const QFontMetrics fm = promptFontMetrics(m_positive);
    const int posH = heightForLines(fm, m_positiveLines);
    m_positive->setFixedHeight(posH);
    if (m_showNegative) {
        const int negH = heightForLines(fm, kNegativeLineCount);
        m_negative->setFixedHeight(negH);
    }
    syncFrameToEditors();
    repositionChrome();
}

int ComfyPromptStackWidget::frameHeightForLines() const
{
    if (!m_positive)
        return 20;
    const QFontMetrics fm = promptFontMetrics(m_positive);
    int h = heightForLines(fm, m_positiveLines);
    if (m_showNegative && m_negative)
        h += heightForLines(fm, kNegativeLineCount);
    if (QLayout *lay = layout()) {
        const QMargins mg = lay->contentsMargins();
        h += mg.top() + mg.bottom();
    }
    return h;
}

void ComfyPromptStackWidget::syncFrameToEditors()
{
    int h = 0;
    if (m_positive && m_positive->isVisible())
        h += m_positive->height();
    if (m_showNegative && m_negative && m_negative->isVisible())
        h += m_negative->height();
    if (QLayout *lay = layout()) {
        const QMargins mg = lay->contentsMargins();
        h += mg.top() + mg.bottom();
    }
    m_frameHeightPx = qMax(h, frameHeightForLines());
    setFixedHeight(m_frameHeightPx);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateGeometry();
}

void ComfyPromptStackWidget::onNegativeHandleDragged(int yPosInNegative)
{
    if (!m_positive || !m_negative)
        return;
    const QFontMetrics fm = promptFontMetrics(m_positive);
    const int posH = m_positive->contentsRect().height();
    const int negH = m_negative->contentsRect().height();
    const int newHeight = yPosInNegative - negH + posH - kPromptHeightPadPx;
    const int newLines = qBound(1, static_cast<int>(qRound(newHeight / double(qMax(1, fm.lineSpacing())))), 10);
    if (newLines == m_positiveLines)
        return;
    m_positiveLines = newLines;
    applyHeights();
    Q_EMIT layoutHeightsChanged();
}

void ComfyPromptStackWidget::persistPositiveLineCount()
{
    QJsonObject st = ComfyUIUtils::loadSettingsJson();
    if (m_liveLineCounts) {
        const int liveLines = m_showNegative ? m_positiveLines + kNegativeLineCount : m_positiveLines;
        st.insert(QStringLiteral("prompt_line_count_live"), liveLines);
    } else {
        st.insert(QStringLiteral("prompt_line_count"), m_positiveLines);
    }
    ComfyUIUtils::saveSettingsJson(st);
    Q_EMIT layoutHeightsChanged();
}

void ComfyPromptStackWidget::ensureResizeHandle()
{
    if (m_resizeHandle) {
        m_resizeHandle->deleteLater();
        m_resizeHandle = nullptr;
    }

    if (!m_showResizeHandle)
        return;

    if (m_showNegative) {
        m_resizeHandle = new NegativePromptDragHandle(
            [this](int y) { onNegativeHandleDragged(y); },
            [this]() { persistPositiveLineCount(); },
            m_negative);
        connect(m_resizeHandle, &QObject::destroyed, this, [this]() {
            if (m_resizeHandle == sender())
                m_resizeHandle = nullptr;
        });
    } else {
        auto *handle = new ComfyPromptResizeHandle(
            m_positive,
            [this](int lines) {
                m_positiveLines = qBound(1, lines, 10);
                applyHeights();
                persistPositiveLineCount();
            },
            heightForLines(promptFontMetrics(m_positive), 1),
            m_positive);
        m_resizeHandle = handle;
        connect(handle, &ComfyPromptResizeHandle::heightChanged, this, [this]() {
            syncFrameToEditors();
            repositionChrome();
            Q_EMIT layoutHeightsChanged();
        });
    }
}

void ComfyPromptStackWidget::applyEditorChrome()
{
    if (m_positive)
        m_positive->setStyleSheet(positiveEditorStyle());
    if (m_negative)
        m_negative->setStyleSheet(m_showNegative ? negativeEditorStyle() : positiveEditorStyle());
}

void ComfyPromptStackWidget::updateFocusBorder()
{
    if (!m_focusChromeEnabled) {
        setStyleSheet(QString());
        return;
    }
    const bool focused =
        (m_positive && m_positive->hasFocus()) || (m_negative && m_negative->isVisible() && m_negative->hasFocus());
    setStyleSheet(focused ? stackFocusStyle() : stackBaseStyle());
}

bool ComfyPromptStackWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_positive || obj == m_negative) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            repositionChrome();
        } else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            updateFocusBorder();
        }
    }
    return QFrame::eventFilter(obj, event);
}

void ComfyPromptStackWidget::resizeEvent(QResizeEvent *event)
{
    if (m_frameHeightPx > 0 && height() != m_frameHeightPx)
        setFixedHeight(m_frameHeightPx);
    QFrame::resizeEvent(event);
    repositionChrome();
}

void ComfyPromptStackWidget::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    applyHeights();
}

QSize ComfyPromptStackWidget::sizeHint() const
{
    const int h = m_frameHeightPx > 0 ? m_frameHeightPx : frameHeightForLines();
    // Width 0 + Expanding policy: layout stretches to parent; never hint QWIDGETSIZE_MAX.
    return QSize(0, qMax(h, 20));
}

QSize ComfyPromptStackWidget::layoutSizeHint() const
{
    return sizeHint();
}

QSize ComfyPromptStackWidget::minimumSizeHint() const
{
    return sizeHint();
}

void ComfyPromptStackWidget::repositionChrome()
{
    if (m_resizeHandle && m_resizeHandle->parentWidget() == m_negative) {
        const QRect r = m_negative->rect();
        m_resizeHandle->move((r.width() - m_resizeHandle->width()) / 2, r.height() - m_resizeHandle->height());
        m_resizeHandle->raise();
    }

    if (m_negativeWarning && m_negativeWarning->isVisible() && m_negative) {
        const QPoint br = m_negative->mapTo(this, QPoint(m_negative->width(), m_negative->height()));
        const int s = 16;
        m_negativeWarning->setGeometry(br.x() - s - 4, br.y() - s - 4, s, s);
        m_negativeWarning->raise();
    }
}
