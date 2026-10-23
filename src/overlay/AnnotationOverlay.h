#pragma once

#include "core/Types.h"
#include "overlay/AnnotationModel.h"
#include "overlay/OverlayCompositor.h"

#include <QColor>
#include <QPoint>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace lightrec {

/**
 * Full-virtual-desktop, click-through annotation window.  While drawing is
 * enabled it accepts the mouse and records strokes; otherwise it is transparent
 * to input and only mirrors the live overlay effects.
 */
class AnnotationOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit AnnotationOverlay(AnnotationModel *model, QWidget *parent = nullptr);

    void setDrawingEnabled(bool enabled);
    bool isDrawingEnabled() const { return m_drawingEnabled; }

    void setTool(AnnotationTool tool);
    AnnotationTool tool() const { return m_tool; }
    void setPenColor(const QColor &color);
    void setPenWidth(int width);

    void setMouseEffectsEnabled(bool enabled);
    void setMouseStyle(int size, const QColor &leftColor, const QColor &rightColor, int durationMs);
    void setKeyDisplayEnabled(bool enabled);
    void setKeyStyle(int fontSize, int opacity, const QPoint &globalPosition);

    void setSpotlightEnabled(bool enabled);
    void setSpotlightRect(const QRect &rect);
    void setSpotlightDim(int dim);
    bool isSpotlightEnabled() const { return m_spotlightEnabled; }
    QRect spotlightRect() const { return m_spotlightRect; }

    void setMagnifierEnabled(bool enabled);
    void setMagnifierRect(const QRect &rect);
    void setMagnifierOptions(int width, double zoom);
    bool isMagnifierEnabled() const { return m_magnifierEnabled; }
    QRect magnifierRect() const { return m_magnifierRect; }
    void setLiveMouse(const QPoint &globalPosition, bool visible,
                      const QVector<MouseRipple> &ripples);
    void setKeyEntries(const QVector<KeyEntry> &entries);

    void refreshGeometry();
    void setOverlayVisible(bool visible);

signals:
    void drawingEnabledChanged(bool enabled);
    void overlayVisibilityRequested(bool visible);
    void toolChanged(lightrec::AnnotationTool tool);
    void interactionStarted();
    void spotlightRectChanged(const QRect &rect);
    void magnifierRectChanged(const QRect &rect);
    void spotlightExitRequested();
    void magnifierExitRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QPointF toGlobal(const QPointF &local) const;
    void updateInteractiveState();
    void paintKeyPanel(QPainter &painter) const;
    void paintMouseEffects(QPainter &painter) const;
    void paintSpotlight(QPainter &painter) const;
    void paintMagnifier(QPainter &painter) const;

    AnnotationModel *m_model = nullptr;
    AnnotationTool m_tool = AnnotationTool::Pen;
    QColor m_penColor = Qt::red;
    int m_penWidth = 4;
    bool m_drawingEnabled = false;
    bool m_drawing = false;
    bool m_mouseEffects = true;
    bool m_keyDisplay = true;
    int m_mouseSize = 48;
    QColor m_leftClickColor = QColor(0x2D, 0x8C, 0xF0);
    QColor m_rightClickColor = QColor(0xFF, 0x95, 0x00);
    int m_clickDurationMs = 600;
    int m_keyFontSize = 22;
    int m_keyOpacity = 85;
    QPoint m_keyPosition = QPoint(-1, -1);
    AnnotationStroke m_currentStroke;

    enum class DragMode { None, Draw, Spotlight, Magnifier };
    DragMode m_dragMode = DragMode::None;
    bool m_spotlightEnabled = false;
    QRect m_spotlightRect;
    int m_spotlightDim = 140;
    bool m_magnifierEnabled = false;
    QRect m_magnifierRect;
    int m_magnifierWidth = 420;
    double m_magnifierZoom = 2.0;
    QPoint m_dragStart;
    QRect m_dragStartRect;

    QPoint m_cursor;
    bool m_cursorVisible = false;
    QVector<MouseRipple> m_ripples;
    QVector<KeyEntry> m_keys;

    QTimer m_animationTimer;
};

} // namespace lightrec
