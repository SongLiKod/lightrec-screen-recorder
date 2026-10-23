#include "overlay/OverlayCompositor.h"

#include <QDateTime>
#include <QDebug>
#include <QFontMetrics>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace lightrec {

OverlayCompositor::OverlayCompositor() = default;

void OverlayCompositor::setConfig(const OverlayConfig &config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
}

OverlayConfig OverlayCompositor::config() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

void OverlayCompositor::setOutputSize(const QSize &size)
{
    if (size.width() < 2 || size.height() < 2)
        return;
    QMutexLocker locker(&m_mutex);
    m_outputSize = QSize(size.width() & ~1, size.height() & ~1);
}

QSize OverlayCompositor::outputSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_outputSize;
}

void OverlayCompositor::setStrokes(const QVector<AnnotationStroke> &strokes)
{
    QMutexLocker locker(&m_mutex);
    m_strokes = strokes;
}

void OverlayCompositor::setCameraFrame(const QImage &frame)
{
    QMutexLocker locker(&m_mutex);
    m_cameraFrame = frame;
}

void OverlayCompositor::setPptImage(const QImage &image)
{
    QMutexLocker locker(&m_mutex);
    m_pptImage = image;
}

void OverlayCompositor::setMouseState(const QPoint &cursor, bool cursorVisible,
                                      const QVector<MouseRipple> &ripples)
{
    QMutexLocker locker(&m_mutex);
    m_cursor = cursor;
    m_cursorVisible = cursorVisible;
    m_ripples = ripples;
}

void OverlayCompositor::setKeyEntries(const QVector<KeyEntry> &entries)
{
    QMutexLocker locker(&m_mutex);
    m_keys = entries;
}

QRect OverlayCompositor::fitRect(const QImage &image, const QRect &target)
{
    if (image.isNull() || target.isEmpty())
        return target;
    const double scale = std::min(static_cast<double>(target.width()) / image.width(),
                                  static_cast<double>(target.height()) / image.height());
    const int width = std::max(2, static_cast<int>(image.width() * scale));
    const int height = std::max(2, static_cast<int>(image.height() * scale));
    return QRect(target.x() + (target.width() - width) / 2,
                 target.y() + (target.height() - height) / 2, width, height);
}

QRect OverlayCompositor::screenAreaFor(const QSize &canvas) const
{
    switch (m_config.layout) {
    case LayoutTemplate::Single:
        return QRect(QPoint(0, 0), canvas);
    case LayoutTemplate::Split:
        return QRect(0, 0, static_cast<int>(canvas.width() * 0.70), canvas.height());
    case LayoutTemplate::Triple:
        return QRect(0, 0, static_cast<int>(canvas.width() * 0.68), canvas.height());
    }
    return QRect(QPoint(0, 0), canvas);
}

QRect OverlayCompositor::screenArea() const
{
    QMutexLocker locker(&m_mutex);
    return screenAreaFor(m_outputSize);
}

QImage OverlayCompositor::compose(const QImage &screenFrame, const QRect &screenRect, qint64 nowMs)
{
    OverlayConfig config;
    QSize canvasSize;
    QVector<AnnotationStroke> strokes;
    QImage camera;
    QImage ppt;
    QPoint cursor;
    bool cursorVisible = false;
    QVector<MouseRipple> ripples;
    QVector<KeyEntry> keys;
    {
        QMutexLocker locker(&m_mutex);
        config = m_config;
        canvasSize = m_outputSize;
        strokes = m_strokes;
        camera = m_cameraFrame;
        ppt = m_pptImage;
        cursor = m_cursor;
        cursorVisible = m_cursorVisible;
        ripples = m_ripples;
        keys = m_keys;
    }

    if (canvasSize.width() < 2 || canvasSize.height() < 2)
        canvasSize = screenFrame.size();
    if (canvasSize.width() < 2 || canvasSize.height() < 2)
        canvasSize = QSize(1920, 1080);
    canvasSize = QSize(canvasSize.width() & ~1, canvasSize.height() & ~1);

    QImage canvas(canvasSize, QImage::Format_ARGB32);
    canvas.fill(QColor(0x10, 0x13, 0x18));

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect screenArea = screenAreaFor(canvasSize);
    const QRect fitted = fitRect(screenFrame, screenArea);
    if (!screenFrame.isNull())
        painter.drawImage(fitted, screenFrame);

    if (config.overlaysVisible && config.annotationWhiteboard)
        drawWhiteboard(painter, fitted, config);

    QRect cameraArea;
    QRect pptArea;
    if (config.layout == LayoutTemplate::Split) {
        cameraArea = QRect(screenArea.right() + 1, 0,
                           canvasSize.width() - screenArea.width() - 1, canvasSize.height());
        painter.fillRect(cameraArea, QColor(0x0B, 0x0E, 0x13));
        painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
        painter.drawLine(screenArea.topRight(), screenArea.bottomRight());
    } else if (config.layout == LayoutTemplate::Triple) {
        cameraArea = QRect(screenArea.right() + 1, 0,
                           canvasSize.width() - screenArea.width() - 1, canvasSize.height() / 2);
        pptArea = QRect(screenArea.right() + 1, canvasSize.height() / 2,
                        canvasSize.width() - screenArea.width() - 1,
                        canvasSize.height() - canvasSize.height() / 2);
        painter.fillRect(cameraArea, QColor(0x0B, 0x0E, 0x13));
        painter.fillRect(pptArea, QColor(0x1A, 0x21, 0x2C));
        painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
        painter.drawLine(screenArea.topRight(), screenArea.bottomRight());
    }

    if (config.layout != LayoutTemplate::Single) {
        if (config.showCamera && config.overlaysVisible && !camera.isNull())
            drawCamera(painter, cameraArea, camera, config.cameraMirror, config.cameraOpacity);
        else if (config.layout == LayoutTemplate::Triple || config.layout == LayoutTemplate::Split) {
            painter.setPen(QColor(0x8A, 0x94, 0xA6));
            painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 18));
            painter.drawText(cameraArea, Qt::AlignCenter, QStringLiteral("摄像头"));
        }

        if (config.layout == LayoutTemplate::Triple) {
            if (!ppt.isNull()) {
                painter.drawImage(fitRect(ppt, pptArea.adjusted(8, 8, -8, -8)), ppt);
            } else {
                painter.setPen(QColor(0x8A, 0x94, 0xA6));
                painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 20));
                painter.drawText(pptArea, Qt::AlignCenter, QStringLiteral("PPT 辅助区域"));
            }
        }
    }

    if (config.overlaysVisible && config.spotlightEnabled)
        drawSpotlight(painter, fitted, screenRect, config);

    if (config.overlaysVisible) {
        // Annotations live in virtual-screen coordinates.
        if (!strokes.isEmpty() && screenRect.width() > 0 && screenRect.height() > 0) {
            painter.save();
            const double sx = static_cast<double>(fitted.width()) / screenRect.width();
            const double sy = static_cast<double>(fitted.height()) / screenRect.height();
            painter.translate(fitted.x() - screenRect.x() * sx,
                              fitted.y() - screenRect.y() * sy);
            painter.scale(sx, sy);
            for (const AnnotationStroke &stroke : strokes)
                drawStroke(painter, stroke);
            painter.restore();
        }

        if (config.layout == LayoutTemplate::Single && config.showCamera
            && !camera.isNull() && !config.cameraRect.isEmpty()) {
            const double sx = screenRect.width() > 0
                                  ? static_cast<double>(fitted.width()) / screenRect.width() : 1.0;
            const double sy = screenRect.height() > 0
                                  ? static_cast<double>(fitted.height()) / screenRect.height() : 1.0;
            const QRect target(fitted.x() + static_cast<int>((config.cameraRect.x() - screenRect.x()) * sx),
                               fitted.y() + static_cast<int>((config.cameraRect.y() - screenRect.y()) * sy),
                               static_cast<int>(config.cameraRect.width() * sx),
                               static_cast<int>(config.cameraRect.height() * sy));
            drawCamera(painter, target, camera, config.cameraMirror, config.cameraOpacity);
        }

        if (config.showMouseHighlight)
            drawMouse(painter, fitted, screenRect, nowMs, config, cursor, cursorVisible, ripples);
        if (config.showKeys)
            drawKeys(painter, fitted, screenRect, config, keys);
        if (config.magnifierEnabled) {
            const QImage composed = canvas;
            drawMagnifier(painter, fitted, screenRect, composed, config);
        }
    }

    painter.end();
    return canvas;
}

void OverlayCompositor::drawStroke(QPainter &painter, const AnnotationStroke &stroke) const
{
    painter.save();
    QPen pen(stroke.color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (stroke.tool) {
    case AnnotationTool::Pen: {
        if (stroke.points.size() >= 2) {
            QPainterPath path(stroke.points.first());
            for (int i = 1; i < stroke.points.size(); ++i)
                path.lineTo(stroke.points.at(i));
            painter.drawPath(path);
        } else if (stroke.points.size() == 1) {
            painter.drawPoint(stroke.points.first());
        }
        break;
    }
    case AnnotationTool::Line:
        painter.drawLine(stroke.start, stroke.end);
        break;
    case AnnotationTool::Arrow: {
        painter.drawLine(stroke.start, stroke.end);
        const QLineF line(stroke.start, stroke.end);
        const double angle = std::atan2(-line.dy(), line.dx());
        const double length = std::max(12.0, stroke.width * 3.5);
        const QPointF tip = stroke.end;
        const QPointF left = tip - QPointF(std::cos(angle - M_PI / 7.0) * length,
                                           -std::sin(angle - M_PI / 7.0) * length);
        const QPointF right = tip - QPointF(std::cos(angle + M_PI / 7.0) * length,
                                            -std::sin(angle + M_PI / 7.0) * length);
        painter.setBrush(stroke.color);
        painter.drawPolygon(QPolygonF{ tip, left, right });
        break;
    }
    case AnnotationTool::Rect:
        painter.drawRect(QRectF(stroke.start, stroke.end).normalized());
        break;
    case AnnotationTool::Ellipse:
        painter.drawEllipse(QRectF(stroke.start, stroke.end).normalized());
        break;
    case AnnotationTool::Text:
        painter.setFont(stroke.font);
        painter.drawText(stroke.start, stroke.text);
        break;
    case AnnotationTool::Eraser:
    case AnnotationTool::None:
        break;
    }
    painter.restore();
}

void OverlayCompositor::drawCamera(QPainter &painter, const QRect &target, const QImage &frame,
                                   bool mirror, int opacity) const
{
    if (frame.isNull() || target.isEmpty())
        return;

    const QRect fitted = fitRect(frame, target);
    painter.save();
    painter.setOpacity(qBound(0, opacity, 100) / 100.0);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(fitted.adjusted(-2, -2, 2, 2)), 10, 10);
    painter.setClipPath(clip);
    painter.fillRect(fitted, QColor(0, 0, 0, 120));
    if (mirror) {
        painter.translate(fitted.center());
        painter.scale(-1, 1);
        painter.translate(-fitted.center());
    }
    painter.drawImage(fitted, frame);
    painter.restore();

    painter.save();
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 200), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(fitted.adjusted(-1, -1, 1, 1)), 10, 10);
    painter.restore();
}

void OverlayCompositor::drawMouse(QPainter &painter, const QRect &screenArea,
                                  const QRect &screenRect, qint64 nowMs,
                                  const OverlayConfig &config, const QPoint &cursorPoint,
                                  bool cursorVisible,
                                  const QVector<MouseRipple> &ripples) const
{
    if (!cursorVisible || screenRect.width() <= 0 || screenRect.height() <= 0)
        return;

    const double sx = static_cast<double>(screenArea.width()) / screenRect.width();
    const double sy = static_cast<double>(screenArea.height()) / screenRect.height();
    const QPointF cursor(screenArea.x() + (cursorPoint.x() - screenRect.x()) * sx,
                         screenArea.y() + (cursorPoint.y() - screenRect.y()) * sy);
    if (!screenArea.adjusted(-40, -40, 40, 40).contains(cursor.toPoint()))
        return;

    const double scale = (sx + sy) / 2.0;
    const double radius = std::max(6.0, config.mouseSize * scale / 2.0);

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 110), 5));
    painter.drawEllipse(cursor, radius + 5, radius + 5);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 235), 2));
    painter.drawEllipse(cursor, radius, radius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x2D, 0x8C, 0xF0, 235));
    painter.drawEllipse(cursor, 3, 3);

    for (const MouseRipple &ripple : ripples) {
        const qint64 duration = std::max(120, config.clickDurationMs);
        const double progress = (nowMs - ripple.startMs) / static_cast<double>(duration);
        if (progress < 0.0 || progress > 1.0)
            continue;
        // A click ripple belongs at the click position, not at the current
        // cursor position (the user may already have moved the mouse).
        const QPointF rippleCenter(screenArea.x() + (ripple.position.x() - screenRect.x()) * sx,
                                   screenArea.y() + (ripple.position.y() - screenRect.y()) * sy);
        if (!screenArea.adjusted(-40, -40, 40, 40).contains(rippleCenter.toPoint()))
            continue;
        const double rippleRadius = radius + progress * radius * 2.2;
        QColor color = ripple.color;
        color.setAlphaF(static_cast<float>((1.0 - progress) * 0.85));
        painter.setPen(QPen(color, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(rippleCenter, rippleRadius, rippleRadius);

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(rippleCenter, 5 + 4 * (1.0 - progress),
                            5 + 4 * (1.0 - progress));
    }
    painter.restore();
}

void OverlayCompositor::drawKeys(QPainter &painter, const QRect &screenArea,
                                 const QRect &screenRect, const OverlayConfig &config,
                                 const QVector<KeyEntry> &keys) const
{
    if (keys.isEmpty() || screenRect.width() <= 0 || screenRect.height() <= 0)
        return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QVector<KeyEntry> visible;
    for (const KeyEntry &entry : keys) {
        if (nowMs - entry.startMs <= 3000)
            visible.append(entry);
    }
    if (visible.isEmpty())
        return;
    while (visible.size() > 3)
        visible.removeFirst();

    QFont font(QStringLiteral("Microsoft YaHei UI"), config.keyFontSize, QFont::DemiBold);
    painter.save();
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int lineHeight = metrics.height() + 10;
    const int padding = 16;
    int width = 0;
    for (const KeyEntry &entry : visible)
        width = std::max(width, metrics.horizontalAdvance(entry.text));
    width += padding * 2;
    const int height = lineHeight * visible.size() + padding * 2;

    int x = screenArea.x() + 24;
    int y = screenArea.y() + screenArea.height() - height - 24;
    if (config.keyPosition.x() >= 0 && config.keyPosition.y() >= 0) {
        const double sx = static_cast<double>(screenArea.width()) / screenRect.width();
        const double sy = static_cast<double>(screenArea.height()) / screenRect.height();
        x = screenArea.x() + static_cast<int>((config.keyPosition.x() - screenRect.x()) * sx);
        y = screenArea.y() + static_cast<int>((config.keyPosition.y() - screenRect.y()) * sy);
    }
    x = qBound(screenArea.x(), x, screenArea.right() - width);
    y = qBound(screenArea.y(), y, screenArea.bottom() - height);

    painter.setOpacity(qBound(0, config.keyOpacity, 100) / 100.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x0F, 0x14, 0x1C, 210));
    painter.drawRoundedRect(QRect(x, y, width, height), 10, 10);

    painter.setPen(QColor(0xF2, 0xF6, 0xFC));
    int textY = y + padding + metrics.ascent();
    for (const KeyEntry &entry : visible) {
        painter.drawText(x + padding, textY, entry.text);
        textY += lineHeight;
    }
    painter.restore();
}

void OverlayCompositor::drawSpotlight(QPainter &painter, const QRect &screenArea,
                                      const QRect &screenRect,
                                      const OverlayConfig &config) const
{
    if (!config.spotlightRect.isValid() || config.spotlightRect.isEmpty()
        || screenRect.width() <= 0 || screenRect.height() <= 0) {
        return;
    }
    const double sx = static_cast<double>(screenArea.width()) / screenRect.width();
    const double sy = static_cast<double>(screenArea.height()) / screenRect.height();
    const QRect local(screenArea.x() + static_cast<int>((config.spotlightRect.x() - screenRect.x()) * sx),
                      screenArea.y() + static_cast<int>((config.spotlightRect.y() - screenRect.y()) * sy),
                      static_cast<int>(config.spotlightRect.width() * sx),
                      static_cast<int>(config.spotlightRect.height() * sy));
    const QRect clipped = local.intersected(screenArea);
    if (clipped.isEmpty())
        return;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, qBound(40, config.spotlightDim, 220)));
    painter.drawRect(QRect(screenArea.left(), screenArea.top(),
                           screenArea.width(), clipped.top() - screenArea.top()));
    painter.drawRect(QRect(screenArea.left(), clipped.bottom() + 1,
                           screenArea.width(), screenArea.bottom() - clipped.bottom()));
    painter.drawRect(QRect(screenArea.left(), clipped.top(),
                           clipped.left() - screenArea.left(), clipped.height()));
    painter.drawRect(QRect(clipped.right() + 1, clipped.top(),
                           screenArea.right() - clipped.right(), clipped.height()));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 3));
    painter.drawRoundedRect(clipped.adjusted(1, 1, -1, -1), 8, 8);
    painter.restore();
}

void OverlayCompositor::drawWhiteboard(QPainter &painter, const QRect &screenArea,
                                       const OverlayConfig &config) const
{
    painter.save();
    painter.fillRect(screenArea, QColor(0xF7, 0xF8, 0xFB));

    if (config.annotationGrid) {
        painter.setPen(QPen(QColor(0xD8, 0xDE, 0xE8), 1));
        const int step = qMax(24, screenArea.width() / 32);
        for (int x = screenArea.left() + step; x < screenArea.right(); x += step)
            painter.drawLine(x, screenArea.top(), x, screenArea.bottom());
        for (int y = screenArea.top() + step; y < screenArea.bottom(); y += step)
            painter.drawLine(screenArea.left(), y, screenArea.right(), y);
    }

    if (config.annotationGuides) {
        QPen guide(QColor(0x9A, 0xAA, 0xBE), 1, Qt::DashLine);
        guide.setDashPattern({ 6, 5 });
        painter.setPen(guide);
        for (int i = 1; i <= 2; ++i) {
            const int x = screenArea.left() + screenArea.width() * i / 3;
            const int y = screenArea.top() + screenArea.height() * i / 3;
            painter.drawLine(x, screenArea.top(), x, screenArea.bottom());
            painter.drawLine(screenArea.left(), y, screenArea.right(), y);
        }
        painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 170), 1, Qt::DashLine));
        painter.drawLine(screenArea.center().x(), screenArea.top(),
                         screenArea.center().x(), screenArea.bottom());
        painter.drawLine(screenArea.left(), screenArea.center().y(),
                         screenArea.right(), screenArea.center().y());
    }
    painter.restore();
}

void OverlayCompositor::drawMagnifier(QPainter &painter, const QRect &screenArea,
                                      const QRect &screenRect, const QImage &composedCanvas,
                                      const OverlayConfig &config) const
{
    if (!config.magnifierRect.isValid() || config.magnifierRect.isEmpty()
        || composedCanvas.isNull() || screenRect.width() <= 0 || screenRect.height() <= 0) {
        return;
    }
    const QRect source = config.magnifierRect.intersected(screenRect);
    if (source.isEmpty())
        return;

    const QRect sourceCanvas(screenArea.x()
                                 + static_cast<int>((source.x() - screenRect.x())
                                                    * static_cast<double>(screenArea.width())
                                                    / qMax(1, screenRect.width())),
                             screenArea.y()
                                 + static_cast<int>((source.y() - screenRect.y())
                                                    * static_cast<double>(screenArea.height())
                                                    / qMax(1, screenRect.height())),
                             static_cast<int>(source.width()
                                              * static_cast<double>(screenArea.width())
                                              / qMax(1, screenRect.width())),
                             static_cast<int>(source.height()
                                              * static_cast<double>(screenArea.height())
                                              / qMax(1, screenRect.height())));
    const QRect cropped = sourceCanvas.intersected(composedCanvas.rect());
    if (cropped.isEmpty())
        return;

    const int outputWidth = qBound(200, config.magnifierWidth, screenArea.width() / 2);
    const int outputHeight = qMax(120, outputWidth * cropped.height() / qMax(1, cropped.width()));
    const QRect output(screenArea.right() - outputWidth - 24,
                       screenArea.bottom() - outputHeight - 24,
                       outputWidth, outputHeight);

    painter.save();
    painter.setBrush(QColor(15, 20, 27, 245));
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
    painter.drawRoundedRect(output.adjusted(-6, -6, 6, 6), 10, 10);
    const QImage magnified = composedCanvas.copy(cropped).convertToFormat(QImage::Format_RGB32);
    painter.setOpacity(1.0);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(output, magnified);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(output, 8, 8);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(sourceCanvas);
    painter.restore();
}

} // namespace lightrec
