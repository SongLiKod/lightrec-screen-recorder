#include "overlay/AnnotationToolbar.h"
#include "core/WindowUtils.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>

namespace lightrec {

AnnotationToolbar::AnnotationToolbar(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setObjectName(QStringLiteral("annotationToolbar"));
    setWindowTitle(QStringLiteral("画板工具"));
    setMinimumSize(760, 64);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(6);
    setStyleSheet(QStringLiteral("QToolButton { font-size:20px; }"));

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);

    const struct {
        AnnotationTool tool;
        const char *text;
        const char *tip;
    } tools[] = {
        { AnnotationTool::Pen, "✎", "画笔" },
        { AnnotationTool::Line, "／", "直线" },
        { AnnotationTool::Arrow, "↗", "箭头" },
        { AnnotationTool::Rect, "□", "矩形" },
        { AnnotationTool::Ellipse, "○", "圆形" },
        { AnnotationTool::Text, "T", "文字" }
    };

    for (const auto &entry : tools) {
        QToolButton *button = makeToolButton(entry.tool, QString::fromUtf8(entry.text),
                                             QString::fromUtf8(entry.tip));
        layout->addWidget(button);
    }

    auto *separator = new QWidget(this);
    separator->setFixedWidth(1);
    separator->setStyleSheet(QStringLiteral("background: rgba(128,140,160,80);"));
    layout->addWidget(separator);

    m_colorButton = new QToolButton(this);
    m_colorButton->setToolTip(QStringLiteral("画笔颜色"));
    m_colorButton->setAutoRaise(true);
    m_colorButton->setFixedSize(38, 38);
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(m_penColor, this, QStringLiteral("选择画笔颜色"));
        if (color.isValid())
            setPenColor(color);
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

    auto *undoButton = new QToolButton(this);
    undoButton->setText(QStringLiteral("撤销"));
    undoButton->setToolTip(QStringLiteral("撤销上一笔"));
    undoButton->setAutoRaise(true);
    undoButton->setMinimumSize(58, 38);
    connect(undoButton, &QToolButton::clicked, this, &AnnotationToolbar::undoRequested);
    layout->addWidget(undoButton);

    auto *clearButton = new QToolButton(this);
    clearButton->setText(QStringLiteral("清空"));
    clearButton->setToolTip(QStringLiteral("清空画板"));
    clearButton->setAutoRaise(true);
    clearButton->setMinimumSize(58, 38);
    connect(clearButton, &QToolButton::clicked, this, &AnnotationToolbar::clearRequested);
    layout->addWidget(clearButton);

    auto *hideButton = new QToolButton(this);
    hideButton->setText(QStringLiteral("隐藏"));
    hideButton->setToolTip(QStringLiteral("一键隐藏所有 Overlay"));
    hideButton->setAutoRaise(true);
    hideButton->setMinimumSize(58, 38);
    connect(hideButton, &QToolButton::clicked, this, &AnnotationToolbar::hideRequested);
    layout->addWidget(hideButton);

    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("退出"));
    closeButton->setToolTip(QStringLiteral("退出标注模式"));
    closeButton->setAutoRaise(true);
    closeButton->setMinimumSize(58, 38);
    connect(closeButton, &QToolButton::clicked, this, &AnnotationToolbar::closeRequested);
    layout->addWidget(closeButton);

    updateColorButton();
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
    emit penColorChanged(color);
}

void AnnotationToolbar::setPenWidth(int width)
{
    const QSignalBlocker blocker(m_widthSpin);
    m_widthSpin->setValue(qBound(1, width, 24));
    emit penWidthChanged(m_widthSpin->value());
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
    const QSignalBlocker blocker(m_widthSpin);
    m_widthSpin->setValue(qBound(1, config.penWidth, 24));
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

void AnnotationToolbar::showCentered()
{
    if (layout())
        layout()->activate();
    adjustSize();
    const QRect desktop = WindowUtils::virtualDesktopGeometry();
    // Leave room for the floating recording toolbar above the annotation tools.
    move(desktop.center().x() - width() / 2, desktop.top() + 96);
    show();
    raise();
    WindowUtils::excludeFromCapture(winId());
    WindowUtils::setTopmost(winId(), true);
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
