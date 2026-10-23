#include "ui/FloatingRecordDot.h"

#include "core/ConfigStore.h"
#include "core/WindowUtils.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

namespace lightrec {

namespace {

QString formatElapsed(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds / 60) % 60;
    const qint64 remaining = seconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(remaining, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remaining, 2, 10, QLatin1Char('0'));
}

} // namespace

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
    if (state == RecordState::Idle || state == RecordState::Starting)
        m_elapsedSeconds = 0;
    if (state == RecordState::Recording || state == RecordState::Paused)
        setToolTip(QStringLiteral("正在录制 · 点击停止"));
    else
        setToolTip(QStringLiteral("点击开始录制；拖动可移动；双击打开主界面"));
    update();
}

void FloatingRecordDot::setElapsed(qint64 elapsedMs)
{
    const qint64 seconds = qMax<qint64>(0, elapsedMs / 1000);
    if (seconds == m_elapsedSeconds)
        return;
    m_elapsedSeconds = seconds;
    if (m_state == RecordState::Recording || m_state == RecordState::Paused)
        setToolTip(QStringLiteral("正在录制 %1 · 点击停止").arg(formatElapsed(m_elapsedSeconds)));
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

    if (m_state == RecordState::Recording || m_state == RecordState::Paused) {
        const QString elapsed = formatElapsed(m_elapsedSeconds);
        QFont textFont = font();
        textFont.setBold(true);
        int pixelSize = qMax(8, qRound(circle.height() * 0.26));
        textFont.setPixelSize(pixelSize);
        QFontMetrics metrics(textFont);
        const int maxWidth = qRound(circle.width() * 0.82);
        while (pixelSize > 8 && metrics.horizontalAdvance(elapsed) > maxWidth) {
            --pixelSize;
            textFont.setPixelSize(pixelSize);
            metrics = QFontMetrics(textFont);
        }
        painter.setFont(textFont);
        painter.setPen(QColor(255, 255, 255, 245));
        painter.drawText(circle, Qt::AlignCenter, elapsed);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
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
