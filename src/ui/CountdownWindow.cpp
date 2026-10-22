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
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Keep the countdown transparent: only the text is drawn, with a soft
    // dark outline so it stays readable on any desktop background.
    auto drawShadowed = [&painter](const QRect &rect, const QString &text,
                                   const QColor &color, const QFont &font, int blur) {
        painter.setFont(font);
        painter.setPen(QColor(0, 0, 0, 150));
        for (int dx = -blur; dx <= blur; ++dx) {
            for (int dy = -blur; dy <= blur; ++dy) {
                if (dx == 0 && dy == 0)
                    continue;
                painter.drawText(rect.translated(dx, dy), Qt::AlignCenter, text);
            }
        }
        painter.setPen(color);
        painter.drawText(rect, Qt::AlignCenter, text);
    };

    QFont numberFont(QStringLiteral("Microsoft YaHei UI"), 92, QFont::Bold);
    drawShadowed(QRect(0, 10, width(), 150), QString::number(m_remaining),
                 Qt::white, numberFont, 2);

    QFont titleFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::DemiBold);
    drawShadowed(QRect(0, 164, width(), 28), QStringLiteral("即将开始录制"),
                 QColor(0xE6, 0xED, 0xF6), titleFont, 1);

    QFont hintFont(QStringLiteral("Microsoft YaHei UI"), 11);
    drawShadowed(QRect(0, 198, width(), 24), QStringLiteral("按 Esc 或空格取消"),
                 QColor(0xB8, 0xC6, 0xD8), hintFont, 1);
}

} // namespace lightrec
