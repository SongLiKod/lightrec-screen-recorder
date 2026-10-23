#include "ui/FloatingRecordDot.h"

#include "core/ConfigStore.h"
#include "core/WindowUtils.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

namespace lightrec {

FloatingRecordDot::FloatingRecordDot(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setWindowTitle(QStringLiteral("LightRec 快捷录制"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("点击开始录制；拖动可移动；双击打开主界面"));
    setDotSize(ConfigStore::instance().floatingDotSize());
    setOpacity(ConfigStore::instance().toolbarOpacity());

    // Delay the single click briefly so a double click opens the main window
    // without also toggling a recording as a side effect.
    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(qMax(180, QApplication::doubleClickInterval()));
    connect(m_clickTimer, &QTimer::timeout, this, [this] { emit toggleRequested(); });
}

void FloatingRecordDot::setState(RecordState state)
{
    m_state = state;
    if (state == RecordState::Recording || state == RecordState::Paused)
        setToolTip(QStringLiteral("正在录制 · 点击停止"));
    else
        setToolTip(QStringLiteral("点击开始录制；拖动可移动；双击打开主界面"));
    update();
}

void FloatingRecordDot::setDotSize(int size)
{
    m_dotSize = qBound(44, size, 120);
    setFixedSize(m_dotSize, m_dotSize);
    update();
}

void FloatingRecordDot::setOpacity(int opacity)
{
    m_opacity = qBound(40, opacity, 100);
    update();
}

void FloatingRecordDot::showAtSavedPosition()
{
    const QString saved = ConfigStore::instance().value(QStringLiteral("ui/dotPos")).toString();
    QPoint position;
    if (!saved.isEmpty()) {
        const QStringList parts = saved.split(QLatin1Char(','));
        if (parts.size() == 2)
            position = QPoint(parts.at(0).toInt(), parts.at(1).toInt());
    }
    if (saved.isEmpty()) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect available = screen->availableGeometry();
            position = QPoint(available.right() - width() - 24,
                              available.center().y() - height() / 2);
        }
    }
    QScreen *screen = QGuiApplication::screenAt(position);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        position.setX(qBound(available.left() + 4, position.x(), available.right() - width() - 4));
        position.setY(qBound(available.top() + 4, position.y(), available.bottom() - height() - 4));
    }
    move(position);
    show();
    raise();
    WindowUtils::excludeFromCapture(winId());
    WindowUtils::setTopmost(winId(), true);
}

void FloatingRecordDot::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(m_opacity / 100.0);

    const QRectF circle = QRectF(rect()).adjusted(6, 6, -6, -6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(circle.translated(0, 3));

    QColor color = QColor(0x2D, 0x8C, 0xF0);
    if (m_state == RecordState::Recording)
        color = QColor(0xE5, 0x48, 0x4D);
    else if (m_state == RecordState::Paused)
        color = QColor(0xF0, 0xA0, 0x20);

    painter.setBrush(color);
    painter.setPen(QPen(QColor(255, 255, 255, 210), 2));
    painter.drawEllipse(circle);

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    if (m_state == RecordState::Recording) {
        const QRectF square(circle.center().x() - circle.width() * 0.16,
                            circle.center().y() - circle.height() * 0.16,
                            circle.width() * 0.32, circle.height() * 0.32);
        painter.drawRoundedRect(square, 3, 3);
    } else if (m_state == RecordState::Paused) {
        const double barWidth = circle.width() * 0.10;
        const double barHeight = circle.height() * 0.30;
        painter.drawRoundedRect(QRectF(circle.center().x() - barWidth * 2.2,
                                       circle.center().y() - barHeight / 2,
                                       barWidth, barHeight), 2, 2);
        painter.drawRoundedRect(QRectF(circle.center().x() + barWidth * 1.2,
                                       circle.center().y() - barHeight / 2,
                                       barWidth, barHeight), 2, 2);
    } else {
        painter.drawEllipse(circle.center(), circle.width() * 0.18, circle.height() * 0.18);
    }
}

void FloatingRecordDot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_moved = false;
    m_pressGlobal = event->globalPosition().toPoint();
    m_dragOffset = m_pressGlobal - frameGeometry().topLeft();
    event->accept();
}

void FloatingRecordDot::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    const QPoint global = event->globalPosition().toPoint();
    if ((global - m_pressGlobal).manhattanLength() > 4)
        m_moved = true;
    if (m_moved) {
        QPoint target = global - m_dragOffset;
        if (QScreen *screen = QGuiApplication::screenAt(global)) {
            const QRect available = screen->availableGeometry();
            target.setX(qBound(available.left() + 2, target.x(),
                               available.right() - width() - 2));
            target.setY(qBound(available.top() + 2, target.y(),
                               available.bottom() - height() - 2));
        }
        move(target);
    }
    event->accept();
}

void FloatingRecordDot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    if (m_suppressClick) {
        // The second release of a double click; the double-click handler has
        // already requested the main window.
        m_suppressClick = false;
        event->accept();
        return;
    }
    if (m_moved) {
        ConfigStore::instance().setValue(QStringLiteral("ui/dotPos"),
                                         QStringLiteral("%1,%2").arg(pos().x()).arg(pos().y()));
    } else {
        m_clickTimer->start();
    }
    event->accept();
}

void FloatingRecordDot::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_clickTimer)
            m_clickTimer->stop();
        m_suppressClick = true;
        emit showMainRequested();
    }
}

} // namespace lightrec
