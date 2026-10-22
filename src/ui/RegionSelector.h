#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

namespace lightrec {

class RegionSelector : public QWidget
{
    Q_OBJECT
public:
    explicit RegionSelector(const QRect &initialRegion = QRect(), bool adjusting = false,
                            QWidget *parent = nullptr);

    QRect selectedRegion() const { return m_region; }

signals:
    void regionSelected(const QRect &region);
    void canceled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Handle {
        None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Inside
    };

    Handle handleAt(const QPoint &position) const;
    QRect handleRect(Handle handle) const;
    void updateRegionFromDrag(const QPoint &globalPosition);
    void confirm();

    QRect m_region;
    QRect m_initialRegion;
    QRect m_dragStartRegion;
    QPoint m_dragStart;
    Handle m_activeHandle = Handle::None;
    bool m_adjusting = false;
    bool m_selecting = false;
};

} // namespace lightrec
