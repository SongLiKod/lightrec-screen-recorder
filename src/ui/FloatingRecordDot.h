#pragma once

#include "core/Types.h"

#include <QPoint>
#include <QWidget>

class QTimer;

namespace lightrec {

/**
 * Circular floating record dot.  Always-on-top, draggable, click to start or
 * stop recording, double-click to bring the main window back.
 */
class FloatingRecordDot : public QWidget
{
    Q_OBJECT
public:
    explicit FloatingRecordDot(QWidget *parent = nullptr);

    void setState(RecordState state);
    void setElapsed(qint64 elapsedMs);
    void setDotSize(int size);
    void setOpacity(int opacity);
    void showAtSavedPosition();

signals:
    void toggleRequested();
    void showMainRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    RecordState m_state = RecordState::Idle;
    qint64 m_elapsedSeconds = 0;
    int m_dotSize = 64;
    int m_opacity = 90;
    bool m_dragging = false;
    bool m_moved = false;
    bool m_suppressClick = false;
    QPoint m_pressGlobal;
    QPoint m_dragOffset;
    QTimer *m_clickTimer = nullptr;
};

} // namespace lightrec
