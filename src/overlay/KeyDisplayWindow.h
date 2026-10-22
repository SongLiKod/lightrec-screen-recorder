#pragma once

#include "overlay/OverlayCompositor.h"

#include <QPoint>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace lightrec {

class KeyDisplayWindow : public QWidget
{
    Q_OBJECT
public:
    explicit KeyDisplayWindow(QWidget *parent = nullptr);

    void setEntries(const QVector<KeyEntry> &entries);
    void setStyle(int fontSize, int opacity);
    void setGlobalPosition(const QPoint &position);
    QPoint globalPosition() const { return frameGeometry().topLeft(); }

signals:
    void positionChanged(const QPoint &position);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateSize();

    QVector<KeyEntry> m_entries;
    int m_fontSize = 22;
    int m_opacity = 85;
    bool m_dragging = false;
    QPoint m_dragOffset;
    QTimer m_cleanupTimer;
};

} // namespace lightrec
