#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

namespace lightrec {

class CameraWindow : public QWidget
{
    Q_OBJECT
public:
    explicit CameraWindow(QWidget *parent = nullptr);

    void setFrame(const QImage &frame);
    void setMirror(bool mirror);
    void setOpacityPercent(int opacity);
    void setPlaceholder(const QString &text);
    void setCaptureRect(const QRect &rect);
    QRect captureRect() const { return frameGeometry(); }

signals:
    void geometryChanged(const QRect &rect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QImage m_frame;
    QString m_placeholder = QStringLiteral("摄像头");
    bool m_mirror = true;
    int m_opacity = 100;
    bool m_dragging = false;
    bool m_resizing = false;
    QPoint m_dragOffset;
    QRect m_startGeometry;
};

} // namespace lightrec
