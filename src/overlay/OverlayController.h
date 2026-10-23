#pragma once

#include "core/Types.h"
#include "overlay/AnnotationModel.h"
#include "overlay/OverlayCompositor.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QVector>

namespace lightrec {

class AnnotationOverlay;
class AnnotationToolbar;
class KeyDisplayWindow;
class CameraWindow;
class CameraManager;

/**
 * Owns every overlay window and keeps the thread-safe compositor in sync.
 * The recording thread calls compose() directly; all other methods run on the
 * GUI thread.
 */
class OverlayController : public QObject
{
    Q_OBJECT
public:
    explicit OverlayController(QObject *parent = nullptr);
    ~OverlayController() override;

    void startSession(const QRect &captureRect, const QSize &sourceSize, const OverlayConfig &config);
    void stopSession();
    bool isSessionActive() const { return m_active; }

    void setCaptureRect(const QRect &rect);
    QSize outputSize() const { return m_outputSize; }
    QImage compose(const QImage &screenFrame, const QRect &screenRect, qint64 nowMs);

    OverlayConfig config() const { return m_config; }
    void applyConfig(const OverlayConfig &config);

    void setDrawingEnabled(bool enabled);
    bool isDrawingEnabled() const;
    void toggleDrawing();
    void setDrawingTool(AnnotationTool tool);
    void selectAnnotationColor(int index);
    void adjustAnnotationPenWidth(int delta);
    void undoAnnotation();
    void clearAnnotation();

    void setOverlaysVisible(bool visible);
    bool overlaysVisible() const { return m_config.overlaysVisible; }
    void toggleOverlaysVisible();

    AnnotationModel *annotationModel() { return m_model; }

    void setCameraEnabled(bool enabled);
    void setCameraDevice(const QString &deviceId);
    void setSpotlightEnabled(bool enabled);
    void setMagnifierEnabled(bool enabled);
    bool isSpotlightEnabled() const { return m_config.spotlightEnabled; }
    bool isMagnifierEnabled() const { return m_config.magnifierEnabled; }
    void setLayout(LayoutTemplate layout);
    void setPptImagePath(const QString &path);
    void updateCameraRect(const QRect &rect);
    void setFloatingToolbarGeometry(const QRect &geometry);

    void startCameraPreview();
    void stopCameraPreview();

signals:
    void drawingEnabledChanged(bool enabled);
    void overlaysVisibleChanged(bool visible);
    void spotlightEnabledChanged(bool enabled);
    void magnifierEnabledChanged(bool enabled);
    void cameraError(const QString &message);
    void configChanged(const lightrec::OverlayConfig &config);

private:
    void installHooks();
    void uninstallHooks();
    void tick();
    void onKeyPressed(const QString &combination);
    void onMouseClicked(Qt::MouseButton button, const QPoint &position);
    void startCamera();
    void stopCamera();
    void loadPptImage();
    void applyConfigToWindows();
    void refreshCompositor();
    QSize computeOutputSize(const QRect &captureRect, const QSize &sourceSize) const;
    QPoint defaultKeyPosition() const;
    QRect defaultCameraRect() const;

    AnnotationModel *m_model = nullptr;
    OverlayCompositor m_compositor;
    AnnotationOverlay *m_overlay = nullptr;
    AnnotationToolbar *m_toolbar = nullptr;
    KeyDisplayWindow *m_keyWindow = nullptr;
    CameraWindow *m_cameraWindow = nullptr;
    CameraManager *m_camera = nullptr;

    OverlayConfig m_config;
    QRect m_captureRect;
    QRect m_floatingToolbarRect;
    QSize m_outputSize{ 1920, 1080 };
    QImage m_pptImage;
    QVector<MouseRipple> m_ripples;
    QVector<KeyEntry> m_keyEntries;
    QPoint m_cursor;
    bool m_active = false;
    bool m_cameraRunning = false;
    bool m_cameraPreview = false;
    QTimer m_tickTimer;
};

} // namespace lightrec
