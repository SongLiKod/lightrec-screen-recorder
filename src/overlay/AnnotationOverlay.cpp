#include "overlay/AnnotationOverlay.h"
#include "core/WindowUtils.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace lightrec {

AnnotationOverlay::AnnotationOverlay(AnnotationModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("LightRec 画板"));

    connect(m_model, &AnnotationModel::changed, this, QOverload<>::of(&QWidget::update));
    m_animationTimer.setInterval(33);
    connect(&m_animationTimer, &QTimer::timeout, this, [this] {
        if (isVisible())
            update();
    });
    m_animationTimer.start();
}

void AnnotationOverlay::refreshGeometry()
{
    const QRect desktop = WindowUtils::virtualDesktopGeometry();
    if (geometry() != desktop)
        setGeometry(desktop);
}

void AnnotationOverlay::setOverlayVisible(bool visible)
{
    if (visible) {
        refreshGeometry();
        show();
        raise();
        WindowUtils::excludeFromCapture(winId());
        const bool interactive = m_drawingEnabled || m_spotlightEnabled || m_magnifierEnabled;
        setAttribute(Qt::WA_TransparentForMouseEvents, !interactive);
        WindowUtils::setClickThrough(winId(), !interactive);
    } else {
        hide();
    }
    update();
}

void AnnotationOverlay::setDrawingEnabled(bool enabled)
{
    if (m_drawingEnabled == enabled) {
        updateInteractiveState();
        return;
    }
    m_drawingEnabled = enabled;
    if (m_drawing && !enabled)
        emit liveStrokeCleared();
    m_drawing = false;
    updateInteractiveState();
    update();
    emit drawingEnabledChanged(enabled);
}

void AnnotationOverlay::updateInteractiveState()
{
    const bool interactive = m_drawingEnabled || m_spotlightEnabled || m_magnifierEnabled;
    if (interactive) {
        refreshGeometry();
        setAttribute(Qt::WA_ShowWithoutActivating, false);
        show();
        raise();
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        if (winId())
            WindowUtils::setClickThrough(winId(), false);
        setCursor(Qt::CrossCursor);
        activateWindow();
        setFocus(Qt::OtherFocusReason);
    } else {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        if (winId())
            WindowUtils::setClickThrough(winId(), true);
        setCursor(Qt::ArrowCursor);
        clearFocus();
    }
    update();
}

void AnnotationOverlay::setSpotlightEnabled(bool enabled)
{
    m_spotlightEnabled = enabled;
    if (enabled) {
        m_magnifierEnabled = false;
        m_drawingEnabled = false;
    }
    updateInteractiveState();
    update();
}

void AnnotationOverlay::setSpotlightRect(const QRect &rect)
{
    m_spotlightRect = rect;
    update();
}

void AnnotationOverlay::setSpotlightDim(int dim)
{
    m_spotlightDim = qBound(40, dim, 220);
    update();
}

void AnnotationOverlay::setMagnifierEnabled(bool enabled)
{
    m_magnifierEnabled = enabled;
    if (enabled) {
        m_spotlightEnabled = false;
        m_drawingEnabled = false;
    }
    updateInteractiveState();
    update();
}

void AnnotationOverlay::setMagnifierRect(const QRect &rect)
{
    m_magnifierRect = rect;
    update();
}

void AnnotationOverlay::setMagnifierOptions(int width, double zoom)
{
    m_magnifierWidth = qBound(200, width, 800);
    m_magnifierZoom = qBound(1.2, zoom, 4.0);
    update();
}

void AnnotationOverlay::setWhiteboardEnabled(bool enabled)
{
    if (m_whiteboard == enabled)
        return;
    m_whiteboard = enabled;
    update();
}

void AnnotationOverlay::setGridEnabled(bool enabled)
{
    if (m_grid == enabled)
        return;
    m_grid = enabled;
    update();
}

void AnnotationOverlay::setGuidesEnabled(bool enabled)
{
    if (m_guides == enabled)
        return;
    m_guides = enabled;
    update();
}

void AnnotationOverlay::setTool(AnnotationTool tool)
{
    if (m_tool == tool)
        return;
    if (m_drawing)
        emit liveStrokeCleared();
    m_tool = tool;
    m_drawing = false;
    m_erasing = false;
    emit toolChanged(tool);
}

void AnnotationOverlay::setPenColor(const QColor &color)
{
    if (color.isValid())
        m_penColor = color;
}

void AnnotationOverlay::setPenWidth(int width)
{
    m_penWidth = qBound(1, width, 40);
}

void AnnotationOverlay::setMouseEffectsEnabled(bool enabled)
{
    m_mouseEffects = enabled;
    update();
}

void AnnotationOverlay::setMouseStyle(int size, const QColor &leftColor,
                                      const QColor &rightColor, int durationMs)
{
    m_mouseSize = qBound(16, size, 200);
    m_leftClickColor = leftColor;
    m_rightClickColor = rightColor;
    m_clickDurationMs = qBound(120, durationMs, 3000);
    update();
}

void AnnotationOverlay::setKeyDisplayEnabled(bool enabled)
{
    m_keyDisplay = enabled;
    update();
}

void AnnotationOverlay::setKeyStyle(int fontSize, int opacity, const QPoint &globalPosition)
{
    m_keyFontSize = qBound(10, fontSize, 72);
    m_keyOpacity = qBound(0, opacity, 100);
    m_keyPosition = globalPosition;
    update();
}

void AnnotationOverlay::setLiveMouse(const QPoint &globalPosition, bool visible,
                                     const QVector<MouseRipple> &ripples)
{
    m_cursor = globalPosition;
    m_cursorVisible = visible;
    m_ripples = ripples;
}

void AnnotationOverlay::setKeyEntries(const QVector<KeyEntry> &entries)
{
    m_keys = entries;
}

QPointF AnnotationOverlay::toGlobal(const QPointF &local) const
{
    return local + QPointF(geometry().topLeft());
}

void AnnotationOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    emit interactionStarted();

    if (m_spotlightEnabled || m_magnifierEnabled) {
        m_dragStart = event->globalPosition().toPoint();
        m_dragMode = m_spotlightEnabled ? DragMode::Spotlight : DragMode::Magnifier;
        const QRect &current = m_spotlightEnabled ? m_spotlightRect : m_magnifierRect;
        m_dragStartRect = current.contains(m_dragStart) ? current : QRect();
        update();
        return;
    }

    if (!m_drawingEnabled) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (m_tool == AnnotationTool::Eraser) {
        m_erasing = true;
        m_model->eraseAt(toGlobal(event->position()), qMax<qreal>(8.0, m_penWidth * 2.5));
        update();
        return;
    }

    if (m_tool == AnnotationTool::Text) {
        QInputDialog dialog(nullptr);
        dialog.setWindowTitle(QStringLiteral("文字标注"));
        dialog.setLabelText(QStringLiteral("输入标注文字："));
        dialog.setInputMode(QInputDialog::TextInput);
        dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.resize(420, 160);
        dialog.show();
        // Do not put the text-entry dialog into the recording itself.
        WindowUtils::excludeFromCapture(dialog.winId());
        dialog.raise();
        dialog.activateWindow();
        if (dialog.exec() == QDialog::Accepted && !dialog.textValue().isEmpty()) {
            AnnotationStroke stroke;
            stroke.tool = AnnotationTool::Text;
            stroke.color = m_penColor;
            stroke.width = m_penWidth;
            stroke.start = toGlobal(event->position());
            stroke.end = stroke.start;
            stroke.text = dialog.textValue();
            stroke.font = QFont(QStringLiteral("Microsoft YaHei UI"), 12 + m_penWidth * 3,
                                QFont::DemiBold);
            m_model->addStroke(stroke);
        }
        emit interactionStarted();
        return;
    }

    m_drawing = true;
    m_currentStroke = AnnotationStroke();
    m_currentStroke.tool = m_tool;
    m_currentStroke.color = m_penColor;
    m_currentStroke.width = m_penWidth;
    m_currentStroke.start = toGlobal(event->position());
    m_currentStroke.end = m_currentStroke.start;
    if (m_tool == AnnotationTool::Pen)
        m_currentStroke.points.append(m_currentStroke.start);
    emit liveStrokeChanged(m_currentStroke);
    update();
}

void AnnotationOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragMode == DragMode::Spotlight || m_dragMode == DragMode::Magnifier) {
        const QPoint global = event->globalPosition().toPoint();
        const QPoint delta = global - m_dragStart;
        QRect &rect = (m_dragMode == DragMode::Spotlight) ? m_spotlightRect : m_magnifierRect;
        if (m_dragStartRect.isValid()) {
            rect = m_dragStartRect.translated(delta);
        } else {
            if (delta.manhattanLength() < 4)
                return;
            rect = QRect(m_dragStart, global).normalized();
        }
        rect = rect.intersected(geometry());
        if (rect.width() < 24)
            rect.setWidth(24);
        if (rect.height() < 24)
            rect.setHeight(24);
        update();
        return;
    }

    if (m_erasing) {
        m_model->eraseAt(toGlobal(event->position()), qMax<qreal>(8.0, m_penWidth * 2.5));
        update();
        return;
    }

    if (!m_drawing)
        return;
    const QPointF point = toGlobal(event->position());
    if (m_tool == AnnotationTool::Pen) {
        if (m_currentStroke.points.isEmpty()
            || QLineF(m_currentStroke.points.last(), point).length() > 1.5)
            m_currentStroke.points.append(point);
    }
    m_currentStroke.end = point;
    emit liveStrokeChanged(m_currentStroke);
    update();
}

void AnnotationOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_erasing) {
        m_erasing = false;
        update();
        return;
    }

    if (m_dragMode == DragMode::Spotlight) {
        m_dragMode = DragMode::None;
        emit spotlightRectChanged(m_spotlightRect);
        update();
        return;
    }
    if (m_dragMode == DragMode::Magnifier) {
        m_dragMode = DragMode::None;
        emit magnifierRectChanged(m_magnifierRect);
        update();
        return;
    }

    if (!m_drawing)
        return;

    m_drawing = false;
    const QPointF point = toGlobal(event->position());
    m_currentStroke.end = point;
    if (m_tool == AnnotationTool::Pen && m_currentStroke.points.size() < 2
        && QLineF(m_currentStroke.start, point).length() < 3.0) {
        emit liveStrokeCleared();
        update();
        return;
    }
    if (m_currentStroke.tool != AnnotationTool::Pen
        && QLineF(m_currentStroke.start, point).length() < 4.0) {
        emit liveStrokeCleared();
        update();
        return;
    }
    m_model->addStroke(m_currentStroke);
    emit liveStrokeCleared();
    update();
}

void AnnotationOverlay::keyPressEvent(QKeyEvent *event)
{
    if (m_drawingEnabled && !m_drawing) {
        AnnotationTool requested = AnnotationTool::None;
        switch (event->key()) {
        case Qt::Key_1:
        case Qt::Key_P: requested = AnnotationTool::Pen; break;
        case Qt::Key_2:
        case Qt::Key_L: requested = AnnotationTool::Line; break;
        case Qt::Key_3:
        case Qt::Key_A: requested = AnnotationTool::Arrow; break;
        case Qt::Key_4:
        case Qt::Key_R: requested = AnnotationTool::Rect; break;
        case Qt::Key_5:
        case Qt::Key_O: requested = AnnotationTool::Ellipse; break;
        case Qt::Key_6:
        case Qt::Key_T: requested = AnnotationTool::Text; break;
        case Qt::Key_7:
        case Qt::Key_E: requested = AnnotationTool::Eraser; break;
        default: break;
        }
        if (requested != AnnotationTool::None) {
            setTool(requested);
            return;
        }
    }

    if (event->key() == Qt::Key_Escape) {
        if (m_spotlightEnabled) {
            setSpotlightEnabled(false);
            emit spotlightExitRequested();
            return;
        }
        if (m_magnifierEnabled) {
            setMagnifierEnabled(false);
            emit magnifierExitRequested();
            return;
        }
        if (m_drawingEnabled) {
            setDrawingEnabled(false);
            return;
        }
    }
    QWidget::keyPressEvent(event);
}

bool AnnotationOverlay::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_NCHITTEST
            && (m_drawingEnabled || m_spotlightEnabled || m_magnifierEnabled)) {
            if (result)
                *result = HTCLIENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

void AnnotationOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintContent(painter, true);
}

void AnnotationOverlay::paintAnnotationBackground(QPainter &painter) const
{
    if (!m_whiteboard)
        return;

    painter.save();
    painter.fillRect(rect(), QColor(0xF7, 0xF8, 0xFB));

    if (m_grid) {
        painter.setPen(QPen(QColor(0xD8, 0xDE, 0xE8), 1));
        const int step = 48;
        for (int x = step; x < width(); x += step)
            painter.drawLine(x, 0, x, height());
        for (int y = step; y < height(); y += step)
            painter.drawLine(0, y, width(), y);
    }

    if (m_guides) {
        QPen guide(QColor(0x9A, 0xAA, 0xBE), 1, Qt::DashLine);
        guide.setDashPattern({ 6, 5 });
        painter.setPen(guide);
        for (int i = 1; i <= 2; ++i) {
            const int x = width() * i / 3;
            const int y = height() * i / 3;
            painter.drawLine(x, 0, x, height());
            painter.drawLine(0, y, width(), y);
        }
        painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0, 170), 1, Qt::DashLine));
        painter.drawLine(width() / 2, 0, width() / 2, height());
        painter.drawLine(0, height() / 2, width(), height() / 2);
    }
    painter.restore();
}

void AnnotationOverlay::paintContent(QPainter &painter, bool includeMagnifier) const
{
    // Layered windows let mouse input pass through fully transparent pixels on
    // Windows.  While a tool is active, paint an almost invisible base layer
    // (alpha 1) so the whole desktop remains clickable for drawing/selection.
    if (m_drawingEnabled || m_spotlightEnabled || m_magnifierEnabled)
        painter.fillRect(rect(), QColor(0, 0, 0, 1));

    paintAnnotationBackground(painter);

    if (m_spotlightEnabled)
        paintSpotlight(painter);

    painter.save();
    painter.translate(-geometry().topLeft());
    for (const AnnotationStroke &stroke : m_model->strokes()) {
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
            const QPointF left = stroke.end - QPointF(std::cos(angle - M_PI / 7.0) * length,
                                                      -std::sin(angle - M_PI / 7.0) * length);
            const QPointF right = stroke.end - QPointF(std::cos(angle + M_PI / 7.0) * length,
                                                       -std::sin(angle + M_PI / 7.0) * length);
            painter.setBrush(stroke.color);
            painter.drawPolygon(QPolygonF{ stroke.end, left, right });
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

    if (m_drawing) {
        painter.save();
        painter.setPen(QPen(m_currentStroke.color, m_currentStroke.width, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        switch (m_currentStroke.tool) {
        case AnnotationTool::Pen: {
            if (m_currentStroke.points.size() >= 2) {
                QPainterPath path(m_currentStroke.points.first());
                for (int i = 1; i < m_currentStroke.points.size(); ++i)
                    path.lineTo(m_currentStroke.points.at(i));
                painter.drawPath(path);
            }
            break;
        }
        case AnnotationTool::Line:
            painter.drawLine(m_currentStroke.start, m_currentStroke.end);
            break;
        case AnnotationTool::Arrow:
            painter.drawLine(m_currentStroke.start, m_currentStroke.end);
            break;
        case AnnotationTool::Rect:
            painter.drawRect(QRectF(m_currentStroke.start, m_currentStroke.end).normalized());
            break;
        case AnnotationTool::Ellipse:
            painter.drawEllipse(QRectF(m_currentStroke.start, m_currentStroke.end).normalized());
            break;
        default:
            break;
        }
        painter.restore();
    }
    painter.restore();

    if (m_mouseEffects)
        paintMouseEffects(painter);
    if (m_keyDisplay)
        paintKeyPanel(painter);
    if (includeMagnifier && m_magnifierEnabled)
        paintMagnifier(painter);
}

void AnnotationOverlay::paintMouseEffects(QPainter &painter) const
{
    if (!m_cursorVisible)
        return;
    const QPoint local = m_cursor - geometry().topLeft();
    if (!rect().adjusted(-40, -40, 40, 40).contains(local))
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double radius = std::max(6.0, m_mouseSize / 2.0);
    painter.save();
    painter.setBrush(Qt::NoBrush);
    QColor ring = m_leftClickColor;
    ring.setAlpha(235);
    QColor glow = m_leftClickColor;
    glow.setAlpha(110);
    painter.setPen(QPen(glow, 5));
    painter.drawEllipse(QPointF(local), radius + 5, radius + 5);
    painter.setPen(QPen(ring, 2));
    painter.drawEllipse(QPointF(local), radius, radius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ring);
    painter.drawEllipse(QPointF(local), 3, 3);

    for (const MouseRipple &ripple : m_ripples) {
        const double progress = (now - ripple.startMs) / static_cast<double>(m_clickDurationMs);
        if (progress < 0.0 || progress > 1.0)
            continue;
        const QPoint localClick = ripple.position - geometry().topLeft();
        if (!rect().adjusted(-40, -40, 40, 40).contains(localClick))
            continue;
        QColor color = ripple.color;
        color.setAlphaF(static_cast<float>((1.0 - progress) * 0.85));
        painter.setPen(QPen(color, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(localClick), radius + progress * radius * 2.2,
                            radius + progress * radius * 2.2);
    }
    painter.restore();
}

void AnnotationOverlay::paintSpotlight(QPainter &painter) const
{
    if (!m_spotlightRect.isValid() || m_spotlightRect.isEmpty())
        return;
    const QRect local = m_spotlightRect.translated(-geometry().topLeft());
    painter.save();
    painter.setPen(Qt::NoPen);
    const QColor dim(0, 0, 0, m_spotlightDim);
    painter.fillRect(QRect(0, 0, width(), local.top()), dim);
    painter.fillRect(QRect(0, local.bottom() + 1, width(), height() - local.bottom() - 1), dim);
    painter.fillRect(QRect(0, local.top(), local.left(), local.height()), dim);
    painter.fillRect(QRect(local.right() + 1, local.top(),
                           width() - local.right() - 1, local.height()), dim);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 3));
    painter.drawRoundedRect(local.adjusted(1, 1, -1, -1), 8, 8);
    painter.restore();
}

void AnnotationOverlay::paintMagnifier(QPainter &painter) const
{
    if (!m_magnifierRect.isValid() || m_magnifierRect.isEmpty())
        return;
    const QRect source = m_magnifierRect.intersected(geometry());
    if (source.isEmpty())
        return;

    QScreen *screen = QGuiApplication::screenAt(source.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect screenGeometry = screen->geometry();
    const QRect clamped = source.intersected(screenGeometry);
    if (clamped.isEmpty())
        return;
    const QPixmap fullScreen = screen->grabWindow(0);
    QPixmap snapshot = fullScreen.isNull()
                           ? QPixmap()
                           : fullScreen.copy(clamped.x() - screenGeometry.x(),
                                             clamped.y() - screenGeometry.y(),
                                             clamped.width(), clamped.height());

    // Magnify the screen together with the annotations and annotation
    // background. Render this overlay without the magnifier itself to avoid
    // recursion, then crop exactly the selected region.
    if (!snapshot.isNull()) {
        QImage overlayImage(size(), QImage::Format_ARGB32_Premultiplied);
        overlayImage.fill(Qt::transparent);
        QPainter overlayPainter(&overlayImage);
        overlayPainter.setRenderHint(QPainter::Antialiasing, true);
        overlayPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        paintContent(overlayPainter, false);
        overlayPainter.end();

        const QRect localSource = source.translated(-geometry().topLeft());
        const QImage annotation = overlayImage.copy(localSource);
        QPainter snapshotPainter(&snapshot);
        snapshotPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        snapshotPainter.drawImage(snapshot.rect(), annotation);
    }

    const int outputWidth = qBound(200, m_magnifierWidth, width() / 2);
    const int outputHeight = qMax(120, outputWidth * clamped.height() / qMax(1, clamped.width()));
    const QRect output(width() - outputWidth - 24, height() - outputHeight - 24,
                       outputWidth, outputHeight);

    painter.save();
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(source.translated(-geometry().topLeft()));
    painter.setBrush(QColor(15, 20, 27, 245));
    painter.drawRoundedRect(output.adjusted(-6, -6, 6, 6), 10, 10);
    if (!snapshot.isNull()) {
        painter.drawPixmap(output, snapshot);
    } else {
        painter.setPen(QColor(0xB0, 0xBD, 0xCC));
        painter.drawText(output, Qt::AlignCenter, QStringLiteral("放大预览"));
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0x2D, 0x8C, 0xF0), 2));
    painter.drawRoundedRect(output, 8, 8);
    painter.restore();
}

void AnnotationOverlay::paintKeyPanel(QPainter &painter) const
{
    if (m_keys.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QVector<KeyEntry> visible;
    for (const KeyEntry &entry : m_keys) {
        if (now - entry.startMs <= 3000)
            visible.append(entry);
    }
    if (visible.isEmpty())
        return;
    while (visible.size() > 3)
        visible.removeFirst();

    QFont font(QStringLiteral("Microsoft YaHei UI"), m_keyFontSize, QFont::DemiBold);
    painter.save();
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int lineHeight = metrics.height() + 10;
    const int padding = 16;
    int panelWidth = 0;
    for (const KeyEntry &entry : visible)
        panelWidth = std::max(panelWidth, metrics.horizontalAdvance(entry.text));
    panelWidth += padding * 2;
    const int panelHeight = lineHeight * visible.size() + padding * 2;

    int x = 24;
    int y = height() - panelHeight - 24;
    if (m_keyPosition.x() >= 0 && m_keyPosition.y() >= 0) {
        x = m_keyPosition.x() - geometry().x();
        y = m_keyPosition.y() - geometry().y();
    }
    x = qBound(0, x, width() - panelWidth);
    y = qBound(0, y, height() - panelHeight);

    painter.setOpacity(m_keyOpacity / 100.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x0F, 0x14, 0x1C, 210));
    painter.drawRoundedRect(QRect(x, y, panelWidth, panelHeight), 10, 10);
    painter.setPen(QColor(0xF2, 0xF6, 0xFC));
    int textY = y + padding + metrics.ascent();
    for (const KeyEntry &entry : visible) {
        painter.drawText(x + padding, textY, entry.text);
        textY += lineHeight;
    }
    painter.restore();
}

} // namespace lightrec
