#include "overlay/CameraWindow.h"
#include "core/WindowUtils.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace lightrec {

CameraWindow::CameraWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setWindowTitle(QStringLiteral("摄像头画中画"));
    setMinimumSize(160, 90);
    resize(360, 202);
    setMouseTracking(true);
}

void CameraWindow::setFrame(const QImage &frame)
{
    m_frame = frame;
    update();
}

void CameraWindow::setMirror(bool mirror)
{
    m_mirror = mirror;
    update();
}

void CameraWindow::setOpacityPercent(int opacity)
{
    m_opacity = qBound(0, opacity, 100);
    update();
}

void CameraWindow::setPlaceholder(const QString &text)
{
    m_placeholder = text;
    update();
}

void CameraWindow::setCaptureRect(const QRect &rect)
{
    if (rect.width() >= 160 && rect.height() >= 90)
        setGeometry(rect);
}

void CameraWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(m_opacity / 100.0);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
    painter.setClipPath(clip);
    painter.fillRect(rect(), QColor(0x14, 0x19, 0x22));

    if (!m_frame.isNull()) {
        const double scale = std::min(static_cast<double>(width()) / m_frame.width(),
                                      static_cast<double>(height()) / m_frame.height());
        const int imageWidth = static_cast<int>(m_frame.width() * scale);
        const int imageHeight = static_cast<int>(m_frame.height() * scale);
        const QRect target((width() - imageWidth) / 2, (height() - imageHeight) / 2,
                           imageWidth, imageHeight);
        if (m_mirror) {
            painter.translate(target.center());
            painter.scale(-1, 1);
            painter.translate(-target.center());
        }
        painter.drawImage(target, m_frame);
    } else {
        painter.setPen(QColor(0x9A, 0xA4, 0xB4));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 12));
        painter.drawText(rect(), Qt::AlignCenter, m_placeholder);
    }

    painter.setClipping(false);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 200), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);

    // resize grip
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2D, 0x8C, 0xF0, 200));
    for (int i = 0; i < 3; ++i) {
        painter.drawRect(width() - 14 + i * 4, height() - 6, 2, 2);
        painter.drawRect(width() - 6, height() - 14 + i * 4, 2, 2);
    }
}

void CameraWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint position = event->position().toPoint();
    m_resizing = position.x() > width() - 20 && position.y() > height() - 20;
    m_dragging = !m_resizing;
    m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    m_startGeometry = frameGeometry();
    event->accept();
}

void CameraWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_resizing) {
        const QPoint delta = event->globalPosition().toPoint() - m_startGeometry.topLeft();
        resize(std::max(160, delta.x()), std::max(90, delta.y()));
        event->accept();
        return;
    }
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    if (event->position().x() > width() - 20 && event->position().y() > height() - 20)
        setCursor(Qt::SizeFDiagCursor);
    else
        setCursor(Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void CameraWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging || m_resizing)
        emit geometryChanged(frameGeometry());
    m_dragging = false;
    m_resizing = false;
    QWidget::mouseReleaseEvent(event);
}

} // namespace lightrec
