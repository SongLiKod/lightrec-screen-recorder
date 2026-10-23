#pragma once

#include "core/Types.h"
#include "overlay/AnnotationModel.h"

#include <QImage>
#include <QMutex>
#include <QPoint>
#include <QVector>

class QPainter;

namespace lightrec {

struct MouseRipple {
    QPoint position;
    QColor color;
    qint64 startMs = 0;
    bool rightButton = false;
};

struct KeyEntry {
    QString text;
    qint64 startMs = 0;
};

/**
 * Thread-safe compositor.  The UI thread pushes overlay state; the recording
 * thread calls compose() for every captured frame.  All shared state is guarded
 * by a mutex and never touches QWidget objects.
 */
class OverlayCompositor
{
public:
    OverlayCompositor();

    void setConfig(const OverlayConfig &config);
    OverlayConfig config() const;

    void setOutputSize(const QSize &size);
    QSize outputSize() const;

    void setStrokes(const QVector<AnnotationStroke> &strokes);
    void setCameraFrame(const QImage &frame);
    void setPptImage(const QImage &image);
    void setMouseState(const QPoint &cursor, bool cursorVisible,
                       const QVector<MouseRipple> &ripples);
    void setKeyEntries(const QVector<KeyEntry> &entries);

    QImage compose(const QImage &screenFrame, const QRect &screenRect, qint64 nowMs);
    QRect screenArea() const;

    static QRect fitRect(const QImage &image, const QRect &target);

private:
    void drawStroke(QPainter &painter, const AnnotationStroke &stroke) const;
    void drawCamera(QPainter &painter, const QRect &target, const QImage &frame,
                    bool mirror, int opacity) const;
    void drawMouse(QPainter &painter, const QRect &screenArea, const QRect &screenRect,
                   qint64 nowMs, const OverlayConfig &config, const QPoint &cursor,
                   bool cursorVisible, const QVector<MouseRipple> &ripples) const;
    void drawKeys(QPainter &painter, const QRect &screenArea, const QRect &screenRect,
                  const OverlayConfig &config, const QVector<KeyEntry> &keys) const;
    void drawSpotlight(QPainter &painter, const QRect &screenArea, const QRect &screenRect,
                       const OverlayConfig &config) const;
    void drawWhiteboard(QPainter &painter, const QRect &screenArea,
                        const OverlayConfig &config) const;
    void drawMagnifier(QPainter &painter, const QRect &screenArea, const QRect &screenRect,
                       const QImage &composedCanvas, const OverlayConfig &config) const;
    QRect screenAreaFor(const QSize &canvas) const;

    mutable QMutex m_mutex;
    OverlayConfig m_config;
    QSize m_outputSize{ 1920, 1080 };
    QVector<AnnotationStroke> m_strokes;
    QImage m_cameraFrame;
    QImage m_pptImage;
    QPoint m_cursor;
    bool m_cursorVisible = false;
    QVector<MouseRipple> m_ripples;
    QVector<KeyEntry> m_keys;
};

} // namespace lightrec
