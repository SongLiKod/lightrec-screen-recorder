#include "ui/CaptureBorderWindow.h"

#include "core/WindowUtils.h"

#include <QPainter>
#include <QPen>

namespace lightrec {

CaptureBorderWindow::CaptureBorderWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setWindowTitle(QStringLiteral("LightRec 录制范围"));
}

void CaptureBorderWindow::showBorder(const QRect &captureRect)
{
    if (captureRect.width() < 2 || captureRect.height() < 2) {
        hideBorder();
        return;
    }
    m_captureRect = captureRect;
    setGeometry(captureRect);

    // Apply the exclusion/click-through styles before showing so the first
    // captured frame can never contain the indicator.
    WindowUtils::excludeFromCapture(winId());
    WindowUtils::setClickThrough(winId(), true);
    show();
    raise();
    WindowUtils::setTopmost(winId(), true);
    update();
}

void CaptureBorderWindow::setCaptureRect(const QRect &captureRect)
{
    if (captureRect.width() < 2 || captureRect.height() < 2)
        return;
    if (m_captureRect == captureRect && geometry() == captureRect)
        return;
    m_captureRect = captureRect;
    setGeometry(captureRect);
    update();
}

void CaptureBorderWindow::hideBorder()
{
    hide();
}

void CaptureBorderWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (width() < 2 || height() < 2)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // Centre stays fully transparent; the 2 px outline marks the exact area.
    const QColor color(0x2D, 0x8C, 0xF0, 235);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, 2));
    painter.drawRect(rect().adjusted(1, 1, -2, -2));

    // Small corner accents make the range easy to see on bright desktops.
    const int accent = qMin(16, qMin(width(), height()) / 4);
    painter.setPen(QPen(QColor(0x71, 0xB8, 0xFF, 255), 3));
    painter.drawLine(0, 0, accent, 0);
    painter.drawLine(0, 0, 0, accent);
    painter.drawLine(width() - accent - 1, 0, width() - 1, 0);
    painter.drawLine(width() - 1, 0, width() - 1, accent);
    painter.drawLine(0, height() - 1, accent, height() - 1);
    painter.drawLine(0, height() - accent - 1, 0, height() - 1);
    painter.drawLine(width() - accent - 1, height() - 1, width() - 1, height() - 1);
    painter.drawLine(width() - 1, height() - accent - 1, width() - 1, height() - 1);
}

} // namespace lightrec
