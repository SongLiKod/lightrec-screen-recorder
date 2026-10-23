#pragma once

#include "core/Types.h"

#include <QHash>
#include <QPoint>
#include <QWidget>

class QLabel;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;

namespace lightrec {

class FloatingToolbar : public QWidget
{
    Q_OBJECT
public:
    explicit FloatingToolbar(QWidget *parent = nullptr);

    void setState(RecordState state);
    void setScale(int percent);
    void setStyle(int style);
    void setOpacity(int opacity);
    int scale() const { return m_scale; }
    void showMessage(const QString &message);
    void setHint(const QString &hint);
    void setElapsed(qint64 elapsedMs);
    void setCaptureInVideo(bool include);
    void setDrawEnabled(bool enabled);
    void setOverlaysVisible(bool visible);
    void setCameraEnabled(bool enabled);
    void setSpotlightEnabled(bool enabled);
    void setMagnifierEnabled(bool enabled);
    void setRegionMode(bool regionMode);
    void showToolbar();
    void hideToolbar();
    void refreshShortcutHints();

signals:
    void startRequested();
    void pauseRequested();
    void stopRequested();
    void drawToggled(bool enabled);
    void overlaysToggled(bool visible);
    void cameraToggled(bool enabled);
    void spotlightToggled(bool enabled);
    void magnifierToggled(bool enabled);
    void adjustRegionRequested();
    void showMainRequested();
    void geometryChanged(const QRect &geometry);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    QLabel *m_dragHandle = nullptr;
    QLabel *m_dot = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QTimer *m_messageTimer = nullptr;
    QString m_hintText;
    QVBoxLayout *m_outerLayout = nullptr;
    QHBoxLayout *m_buttonLayout = nullptr;
    QToolButton *m_startButton = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_stopButton = nullptr;
    QToolButton *m_drawButton = nullptr;
    QToolButton *m_overlayButton = nullptr;
    QToolButton *m_cameraButton = nullptr;
    QToolButton *m_spotlightButton = nullptr;
    QToolButton *m_magnifierButton = nullptr;
    QToolButton *m_regionButton = nullptr;
    QToolButton *m_mainButton = nullptr;
    QHash<QToolButton *, QLabel *> m_shortcutLabels;
    QPoint m_dragOffset;
    bool m_dragging = false;
    int m_scale = 150;
    int m_style = 2;      // 0 dark, 1 light, 2 follow theme
    int m_opacity = 92;
    bool m_darkAppearance = true;
    bool m_captureInVideo = false;

    void applyAppearance();
};

} // namespace lightrec
