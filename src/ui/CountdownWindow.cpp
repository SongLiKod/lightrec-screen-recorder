#include "ui/CountdownWindow.h"

#include "core/WindowUtils.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

namespace lightrec {

CountdownWindow::CountdownWindow(int seconds, QWidget *parent)
    : QWidget(parent)
    , m_remaining(qMax(1, seconds))
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("LightRec 录制倒计时"));
    setFixedSize(460, 250);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        move(available.center().x() - width() / 2,
             available.center().y() - height() / 2);
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, [this] {
        --m_remaining;
        if (m_remaining <= 0) {
            m_timer->stop();
            emit finished();
            close();
            return;
        }
        update();
    });
}

void CountdownWindow::start()
{
    show();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    WindowUtils::excludeFromCapture(winId());
    WindowUtils::setTopmost(winId(), true);
    m_timer->start();
}

void CountdownWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        m_timer->stop();
        emit canceled();
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CountdownWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 180), 2));
    painter.setBrush(QColor(15, 20, 27, 242));
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 18, 18);

    QFont numberFont(QStringLiteral("Microsoft YaHei UI"), 92, QFont::Bold);
    painter.setFont(numberFont);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 20, width(), 130), Qt::AlignCenter,
                     QString::number(m_remaining));

    QFont titleFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(QColor(0xE6, 0xED, 0xF6));
    painter.drawText(QRect(0, 154, width(), 28), Qt::AlignCenter,
                     QStringLiteral("即将开始录制"));

    QFont hintFont(QStringLiteral("Microsoft YaHei UI"), 11);
    painter.setFont(hintFont);
    painter.setPen(QColor(0x8F, 0x9D, 0xB0));
    painter.drawText(QRect(0, 188, width(), 24), Qt::AlignCenter,
                     QStringLiteral("按 Esc 或空格取消"));
}

} // namespace lightrec
