#include "overlay/AnnotationToolbar.h"
#include "core/WindowUtils.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QPen>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSpinBox>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

namespace lightrec {

const QList<QColor> &AnnotationToolbar::defaultColors()
{
    static const QList<QColor> colors = {
        QColor(0xE8, 0x3B, 0x3B), // red
        QColor(0xF5, 0xC5, 0x42), // yellow
        QColor(0x2E, 0xCC, 0x71), // green
        QColor(0x2D, 0x8C, 0xF0), // blue
        QColor(0xFF, 0xFF, 0xFF), // white
        QColor(0x10, 0x13, 0x18)  // black
    };
    return colors;
}

AnnotationToolbar::AnnotationToolbar(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setObjectName(QStringLiteral("annotationToolbar"));
    setWindowTitle(QStringLiteral("画板工具"));
    setMinimumSize(980, 86);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 8, 14, 6);
    outer->setSpacing(2);
    auto *layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    outer->addLayout(layout);
    setStyleSheet(QStringLiteral("QToolButton { font-size:20px; }"));

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    const struct {
        AnnotationTool tool;
        const char *text;
        const char *tip;
    } tools[] = {
        { AnnotationTool::Pen, "✎", "画笔（快捷键 1 / P）" },
        { AnnotationTool::Line, "／", "直线（快捷键 2 / L）" },
        { AnnotationTool::Arrow, "↗", "箭头（快捷键 3 / A）" },
        { AnnotationTool::Rect, "□", "矩形（快捷键 4 / R）" },
        { AnnotationTool::Ellipse, "○", "圆形（快捷键 5 / O）" },
        { AnnotationTool::Text, "T", "文字（快捷键 6 / T）" },
        { AnnotationTool::Eraser, "橡皮", "橡皮擦（快捷键 7 / E）" }
    };

    for (const auto &entry : tools) {
        QToolButton *button = makeToolButton(entry.tool, QString::fromUtf8(entry.text),
                                             QString::fromUtf8(entry.tip));
        layout->addWidget(button);
    }

    // Single-letter shortcuts work while the annotation toolbar (or one of its
    // buttons) has focus.  The overlay itself uses 1-6 and the same letters, so
    // quick tool switching works whether the pointer is over the canvas or the
    // toolbar.  Digits are deliberately not registered on the toolbar because
    // the width spin box needs them for editing.
    const QList<QPair<int, AnnotationTool>> toolShortcuts = {
        { Qt::Key_P, AnnotationTool::Pen },
        { Qt::Key_L, AnnotationTool::Line },
        { Qt::Key_A, AnnotationTool::Arrow },
        { Qt::Key_R, AnnotationTool::Rect },
        { Qt::Key_O, AnnotationTool::Ellipse },
        { Qt::Key_T, AnnotationTool::Text },
        { Qt::Key_E, AnnotationTool::Eraser }
    };
    for (const auto &entry : toolShortcuts) {
        auto *shortcut = new QShortcut(QKeySequence(entry.first), this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, tool = entry.second] {
            setTool(tool);
            emit toolChanged(tool);
        });
    }

    auto *separator = new QWidget(this);
    separator->setFixedWidth(1);
    separator->setStyleSheet(QStringLiteral("background: rgba(128,140,160,80);"));
    layout->addWidget(separator);

    const QStringList colorNames = {
        QStringLiteral("红色"), QStringLiteral("黄色"), QStringLiteral("绿色"),
        QStringLiteral("蓝色"), QStringLiteral("白色"), QStringLiteral("黑色")
    };
    const QStringList colorKeys = {
        QStringLiteral("Z"), QStringLiteral("X"), QStringLiteral("C"),
        QStringLiteral("V"), QStringLiteral("B"), QStringLiteral("N")
    };
    const QList<QColor> &colors = defaultColors();
    for (int i = 0; i < colors.size(); ++i) {
        auto *swatch = new QToolButton(this);
        swatch->setText(colorKeys.value(i));
        swatch->setToolTip(QStringLiteral("%1（快捷键 %2）")
                               .arg(colorNames.value(i), colorKeys.value(i)));
        swatch->setFixedSize(30, 30);
        swatch->setCursor(Qt::PointingHandCursor);
        const QColor color = colors.at(i);
        m_paletteButtons.insert(swatch, color);
        connect(swatch, &QToolButton::clicked, this, [this, color] {
            setPenColor(color);
        });
        layout->addWidget(swatch);
    }

    m_colorButton = new QToolButton(this);
    m_colorButton->setToolTip(QStringLiteral("自定义颜色（点击选择）"));
    m_colorButton->setAutoRaise(true);
    m_colorButton->setFixedSize(38, 38);
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        QColorDialog dialog(m_penColor, this);
        dialog.setWindowTitle(QStringLiteral("选择画笔颜色"));
        dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        dialog.show();
        WindowUtils::excludeFromCapture(dialog.winId());
        dialog.raise();
        dialog.activateWindow();
        if (dialog.exec() == QDialog::Accepted && dialog.currentColor().isValid())
            setPenColor(dialog.currentColor());
    });
    layout->addWidget(m_colorButton);

    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(1, 24);
    m_widthSpin->setValue(4);
    m_widthSpin->setToolTip(QStringLiteral("画笔粗细"));
    m_widthSpin->setFixedWidth(72);
    m_widthSpin->setMinimumHeight(34);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        emit penWidthChanged(value);
    });
    layout->addWidget(m_widthSpin);

    m_whiteboardButton = new QToolButton(this);
    m_whiteboardButton->setText(QStringLiteral("白板"));
    m_whiteboardButton->setToolTip(QStringLiteral("切换白板背景（隐藏屏幕内容）"));
    m_whiteboardButton->setCheckable(true);
    m_whiteboardButton->setAutoRaise(true);
    m_whiteboardButton->setMinimumSize(58, 38);
    layout->addWidget(m_whiteboardButton);

    m_gridButton = new QToolButton(this);
    m_gridButton->setText(QStringLiteral("网格"));
    m_gridButton->setToolTip(QStringLiteral("白板背景显示网格"));
    m_gridButton->setCheckable(true);
    m_gridButton->setAutoRaise(true);
    m_gridButton->setMinimumSize(58, 38);
    m_gridButton->setEnabled(false);
    layout->addWidget(m_gridButton);

    m_guidesButton = new QToolButton(this);
    m_guidesButton->setText(QStringLiteral("参考线"));
    m_guidesButton->setToolTip(QStringLiteral("白板背景显示三分参考线"));
    m_guidesButton->setCheckable(true);
    m_guidesButton->setAutoRaise(true);
    m_guidesButton->setMinimumSize(68, 38);
    m_guidesButton->setEnabled(false);
    layout->addWidget(m_guidesButton);

    connect(m_whiteboardButton, &QToolButton::toggled, this, [this](bool enabled) {
        m_gridButton->setEnabled(enabled);
        m_guidesButton->setEnabled(enabled);
        emit whiteboardToggled(enabled);
    });
    connect(m_gridButton, &QToolButton::toggled, this, &AnnotationToolbar::gridToggled);
    connect(m_guidesButton, &QToolButton::toggled, this, &AnnotationToolbar::guidesToggled);

    auto *undoButton = new QToolButton(this);
    undoButton->setText(QStringLiteral("撤销"));
    undoButton->setToolTip(QStringLiteral("撤销上一笔（Backspace / Ctrl+Z）"));
    undoButton->setAutoRaise(true);
    undoButton->setMinimumSize(58, 38);
    connect(undoButton, &QToolButton::clicked, this, &AnnotationToolbar::undoRequested);
    layout->addWidget(undoButton);

    auto *clearButton = new QToolButton(this);
    clearButton->setText(QStringLiteral("清空"));
    clearButton->setToolTip(QStringLiteral("清空画板（Delete）"));
    clearButton->setAutoRaise(true);
    clearButton->setMinimumSize(58, 38);
    connect(clearButton, &QToolButton::clicked, this, &AnnotationToolbar::clearRequested);
    layout->addWidget(clearButton);

    auto *hideButton = new QToolButton(this);
    hideButton->setText(QStringLiteral("隐藏"));
    hideButton->setToolTip(QStringLiteral("一键隐藏所有 Overlay（H；Ctrl+Alt+H 恢复）"));
    hideButton->setAutoRaise(true);
    hideButton->setMinimumSize(58, 38);
    connect(hideButton, &QToolButton::clicked, this, &AnnotationToolbar::hideRequested);
    layout->addWidget(hideButton);

    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("退出"));
    closeButton->setToolTip(QStringLiteral("退出标注模式（Esc）"));
    closeButton->setAutoRaise(true);
    closeButton->setMinimumSize(58, 38);
    connect(closeButton, &QToolButton::clicked, this, &AnnotationToolbar::closeRequested);
    layout->addWidget(closeButton);

    auto *shortcutHint = new QLabel(
        QStringLiteral("工具 1–7（含橡皮 E）  ·  颜色 Z X C V B N  ·  粗细 [ ] / - =  ·  "
                       "撤销 Backspace / Ctrl+Z  ·  清空 Delete  ·  隐藏 H  ·  退出 Esc"),
        this);
    shortcutHint->setWordWrap(true);
    shortcutHint->setStyleSheet(QStringLiteral("color:#9AA8BA; font-size:11px; padding:0 2px;"));
    outer->addWidget(shortcutHint);

    updateColorButton();
    updatePaletteSelection();
    adjustSize();
}

QToolButton *AnnotationToolbar::makeToolButton(AnnotationTool tool, const QString &text,
                                               const QString &tip)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setToolTip(tip);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setFixedSize(46, 46);
    m_toolGroup->addButton(button, static_cast<int>(tool));
    if (tool == m_tool)
        button->setChecked(true);

    connect(button, &QToolButton::clicked, this, [this, tool] {
        setTool(tool);
        emit toolChanged(tool);
    });
    return button;
}

void AnnotationToolbar::setTool(AnnotationTool tool)
{
    m_tool = tool;
    if (QAbstractButton *button = m_toolGroup->button(static_cast<int>(tool)))
        button->setChecked(true);
}

void AnnotationToolbar::setPenColor(const QColor &color)
{
    if (!color.isValid())
        return;
    m_penColor = color;
    updateColorButton();
    updatePaletteSelection();
    emit penColorChanged(color);
}

void AnnotationToolbar::setPenColorIndex(int index)
{
    const QList<QColor> &colors = defaultColors();
    if (index >= 0 && index < colors.size())
        setPenColor(colors.at(index));
}

void AnnotationToolbar::setPenWidth(int width)
{
    const QSignalBlocker blocker(m_widthSpin);
    m_widthSpin->setValue(qBound(1, width, 24));
    emit penWidthChanged(m_widthSpin->value());
}

void AnnotationToolbar::adjustPenWidth(int delta)
{
    if (delta != 0)
        setPenWidth(penWidth() + delta);
}

int AnnotationToolbar::penWidth() const
{
    return m_widthSpin->value();
}

void AnnotationToolbar::syncFromConfig(const OverlayConfig &config)
{
    {
        const QSignalBlocker blocker(m_toolGroup);
        setTool(AnnotationTool::Pen);
    }
    m_penColor = config.penColor;
    updateColorButton();
    updatePaletteSelection();
    {
        const QSignalBlocker blocker(m_widthSpin);
        m_widthSpin->setValue(qBound(1, config.penWidth, 24));
    }
    {
        const QSignalBlocker blocker(m_whiteboardButton);
        m_whiteboardButton->setChecked(config.annotationWhiteboard);
        m_gridButton->setEnabled(config.annotationWhiteboard);
        m_guidesButton->setEnabled(config.annotationWhiteboard);
    }
    {
        const QSignalBlocker blocker(m_gridButton);
        m_gridButton->setChecked(config.annotationGrid);
    }
    {
        const QSignalBlocker blocker(m_guidesButton);
        m_guidesButton->setChecked(config.annotationGuides);
    }
}

void AnnotationToolbar::updatePaletteSelection()
{
    for (auto it = m_paletteButtons.constBegin(); it != m_paletteButtons.constEnd(); ++it) {
        QToolButton *button = it.key();
        const QColor color = it.value();
        const bool selected = color == m_penColor;
        const QString textColor = color.lightness() > 170 ? QStringLiteral("#111820")
                                                          : QStringLiteral("#ffffff");
        const QString border = selected ? QStringLiteral("3px solid #ffffff")
                                        : QStringLiteral("1px solid rgba(255,255,255,80)");
        button->setStyleSheet(
            QStringLiteral("QToolButton { background:%1; color:%2; border:%3;"
                           " border-radius:15px; font-size:11px; font-weight:700; }")
                .arg(color.name(), textColor, border));
    }
}

void AnnotationToolbar::updateColorButton()
{
    QPixmap pixmap(22, 22);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0xCC, 0xD4, 0xE0), 1));
    painter.setBrush(m_penColor);
    painter.drawEllipse(2, 2, 18, 18);
    painter.end();
    m_colorButton->setIcon(QIcon(pixmap));
    m_colorButton->setIconSize(QSize(22, 22));
}

void AnnotationToolbar::showCentered(const QRect &area, const QRect &anchor)
{
    if (layout())
        layout()->activate();
    adjustSize();
    followAnchor(area, anchor);
    show();
    raise();
    WindowUtils::excludeFromCapture(winId());
    WindowUtils::setTopmost(winId(), true);
}

void AnnotationToolbar::followAnchor(const QRect &area, const QRect &anchor)
{
    const QRect desktop = WindowUtils::virtualDesktopGeometry();
    const QRect defaultArea = area.isValid() && area.width() > 1 ? area : desktop;
    int x = defaultArea.center().x() - width() / 2;
    int y = defaultArea.top() + 96;

    if (anchor.isValid() && !anchor.isEmpty()) {
        x = anchor.center().x() - width() / 2;
        y = anchor.bottom() + 8;
        if (y + height() > desktop.bottom() - 8)
            y = anchor.top() - height() - 8;
    }

    x = qBound(desktop.left() + 8, x,
               qMax(desktop.left() + 8, desktop.right() - width() - 8));
    y = qBound(desktop.top() + 8, y,
               qMax(desktop.top() + 8, desktop.bottom() - height() - 8));
    move(x, y);
}

void AnnotationToolbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1));
    painter.setBrush(QColor(15, 20, 27, 245));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
}

void AnnotationToolbar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AnnotationToolbar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void AnnotationToolbar::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

} // namespace lightrec
