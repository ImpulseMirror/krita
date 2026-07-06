/*
 * SPDX-FileCopyrightText: 2025 Krita Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ComfyPromptStackWidget.h"
#include "ComfyPromptLayoutMetrics.h"
#include "ComfyLocalization.h"
#include "ComfyPromptResizeHandle.h"
#include "ComfyTheme.h"
#include "ComfyUiStyle.h"
#include "ComfyUIUtils.h"
#include "ComfyTextArea.h"

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
    return ComfyUiStyle::promptStackFrameStyleSheet(false);
}

QString stackFocusStyle()
{
    return ComfyUiStyle::promptStackFrameStyleSheet(true);
}

QString negativeEditorStyle()
{
    return ComfyUiStyle::promptTextAreaStyleSheet(true);
}

QString positiveEditorStyle()
{
    return ComfyUiStyle::promptTextAreaStyleSheet(false);
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

void paintCenteredResizeDots(QPainter &p, const QWidget *widget)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c(ComfyUiStyle::colors().highlight);
    p.setBrush(c);
    p.setPen(Qt::NoPen);

    const int dotR = 1;
    const int spacing = 4;
    const int rows[2] = {3, 4};
    const int baseY = widget->height() - 3;
    for (int row = 0; row < 2; ++row) {
        const int count = rows[row];
        const int rowWidth = (count - 1) * spacing;
        const int x0 = (widget->width() - rowWidth) / 2;
        const int y = baseY - row * spacing;
        for (int i = 0; i < count; ++i)
            p.drawEllipse(QPoint(x0 + i * spacing, y), dotR, dotR);
    }
}

/// FAITHFUL_PORT: upstream ResizeHandle on negative — drag adjusts positive line count.
class NegativePromptDragHandle : public QWidget
{
public:
    using DragFn = std::function<void(int globalY)>;
    using ReleaseFn = std::function<void()>;
    using PressFn = std::function<void(int globalY)>;

    NegativePromptDragHandle(PressFn onPress, DragFn onDrag, ReleaseFn onRelease, QWidget *parent)
        : QWidget(parent)
        , m_onPress(std::move(onPress))
        , m_onDrag(std::move(onDrag))
        , m_onRelease(std::move(onRelease))
    {
        setFixedHeight(12);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::SizeVerCursor);
        setToolTip(ComfyTr::tr("Drag vertically to resize the text area."));
    }

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::UngrabMouse && m_dragging) {
            m_dragging = false;
            if (m_onRelease)
                m_onRelease();
        }
        return QWidget::event(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // Opaque hit target: translucent widgets only receive clicks on painted pixels.
        p.fillRect(rect(), QColor(0, 0, 0, 1));
        paintCenteredResizeDots(p, this);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            if (m_onPress)
                m_onPress(e->globalPos().y());
            grabMouse();
            e->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_dragging) {
            m_dragging = false;
            releaseMouse();
            if (m_onRelease)
                m_onRelease();
        }
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_dragging || !m_onDrag)
            return;
        m_onDrag(e->globalPos().y());
        e->accept();
    }

private:
    PressFn m_onPress;
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

    m_positive = new ComfyTextArea(nullptr, this);
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

    m_negative = new ComfyTextArea(nullptr, this);
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

void ComfyPromptStackWidget::setHeaderWidget(QWidget *header)
{
    if (m_headerWidget == header)
        return;
    auto *lay = qobject_cast<QVBoxLayout *>(layout());
    if (!lay)
        return;
    if (m_headerWidget) {
        lay->removeWidget(m_headerWidget);
        m_headerWidget->setParent(nullptr);
    }
    m_headerWidget = header;
    if (m_headerWidget) {
        m_headerWidget->setParent(this);
        lay->insertWidget(0, m_headerWidget);
    }
    syncFrameToEditors();
    updateGeometry();
}

void ComfyPromptStackWidget::refreshFrameHeight()
{
    syncFrameToEditors();
    updateGeometry();
}

void ComfyPromptStackWidget::applyLayout(int positiveLines, bool showNegative, bool showResizeHandle)
{
    m_positiveLines = qBound(1, positiveLines, 10);
    m_showNegative = showNegative;
    m_showResizeHandle = showResizeHandle;
    m_negative->setVisible(showNegative);
    applyHeights();
    if (!m_resizeDragging)
        ensureResizeHandle();
    else if (m_resizeHandle)
        repositionChrome();
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
    if (m_headerWidget && m_headerWidget->isVisible())
        h += m_headerWidget->sizeHint().height();
    if (QLayout *lay = layout()) {
        const QMargins mg = lay->contentsMargins();
        h += mg.top() + mg.bottom();
    }
    return h;
}

void ComfyPromptStackWidget::syncFrameToEditors()
{
    int h = 0;
    if (m_headerWidget && m_headerWidget->isVisible())
        h += m_headerWidget->height() > 0 ? m_headerWidget->height() : m_headerWidget->sizeHint().height();
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

void ComfyPromptStackWidget::onNegativeHandleDragStarted(int globalY)
{
    m_resizeDragging = true;
    m_dragAnchorGlobalY = globalY;
    m_dragAnchorLines = m_positiveLines;
}

void ComfyPromptStackWidget::onNegativeHandleDragged(int globalY)
{
    if (!m_positive || !m_negative)
        return;
    const QFontMetrics fm = promptFontMetrics(m_positive);
    const int dy = globalY - m_dragAnchorGlobalY;
    const int lineDelta =
        static_cast<int>(qRound(dy / double(qMax(1, fm.lineSpacing()))));
    const int newLines = qBound(1, m_dragAnchorLines + lineDelta, 10);
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
    if (!m_showResizeHandle) {
        if (m_resizeHandle) {
            m_resizeHandle->deleteLater();
            m_resizeHandle = nullptr;
        }
        return;
    }

    const bool wantNegativeHandle = m_showNegative;
    if (m_resizeHandle) {
        const bool hasNegativeHandle = qobject_cast<ComfyPromptResizeHandle *>(m_resizeHandle) == nullptr;
        if (wantNegativeHandle == hasNegativeHandle) {
            repositionChrome();
            return;
        }
        m_resizeHandle->deleteLater();
        m_resizeHandle = nullptr;
    }

    if (m_showNegative) {
        m_resizeHandle = new NegativePromptDragHandle(
            [this](int globalY) { onNegativeHandleDragStarted(globalY); },
            [this](int globalY) { onNegativeHandleDragged(globalY); },
            [this]() {
                m_resizeDragging = false;
                persistPositiveLineCount();
            },
            this);
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
            this);
        m_resizeHandle = handle;
        connect(handle, &ComfyPromptResizeHandle::heightChanged, this, [this]() {
            if (m_positive) {
                const QFontMetrics fm = promptFontMetrics(m_positive);
                m_positiveLines = linesFromHeight(fm, m_positive->height());
            }
            syncFrameToEditors();
            repositionChrome();
            Q_EMIT layoutHeightsChanged();
        });
    }
    if (m_resizeHandle) {
        m_resizeHandle->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        repositionChrome();
    }
}

void ComfyPromptStackWidget::applyEditorChrome()
{
    if (m_positive)
        m_positive->setStyleSheet(positiveEditorStyle());
    if (m_negative)
        m_negative->setStyleSheet(m_showNegative ? negativeEditorStyle() : positiveEditorStyle());
}

QVariant ComfyPromptStackWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return ComfyTextArea::forwardContainerInputMethodQuery(this, query);
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
    if (m_resizeHandle && m_showResizeHandle && m_showNegative && m_negative && m_negative->isVisible()) {
        const QRect r = m_negative->geometry();
        const int hh = m_resizeHandle->height();
        m_resizeHandle->setGeometry(r.x(), r.y() + r.height() - hh, r.width(), hh);
        m_resizeHandle->raise();
    } else if (m_resizeHandle && m_showResizeHandle && m_positive && !m_showNegative) {
        if (auto *handle = qobject_cast<ComfyPromptResizeHandle *>(m_resizeHandle))
            handle->syncGeometry();
    }

    if (m_negativeWarning && m_negativeWarning->isVisible() && m_negative) {
        const QPoint br = m_negative->mapTo(this, QPoint(m_negative->width(), m_negative->height()));
        const int s = 16;
        m_negativeWarning->setGeometry(br.x() - s - 4, br.y() - s - 4, s, s);
        m_negativeWarning->raise();
    }
}
