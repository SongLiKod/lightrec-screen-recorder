#include "overlay/OverlayController.h"

#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/WindowUtils.h"
#include "overlay/AnnotationOverlay.h"
#include "overlay/AnnotationToolbar.h"
#include "overlay/CameraManager.h"
#include "overlay/CameraWindow.h"
#include "overlay/InputHooks.h"
#include "overlay/KeyDisplayWindow.h"

#include <QApplication>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace lightrec {

OverlayController::OverlayController(QObject *parent)
    : QObject(parent)
    , m_model(new AnnotationModel(this))
    , m_camera(new CameraManager(this))
{
    m_overlay = new AnnotationOverlay(m_model);
    m_toolbar = new AnnotationToolbar();
    m_keyWindow = new KeyDisplayWindow();
    m_cameraWindow = new CameraWindow();

    m_config = ConfigStore::instance().overlayDefaults();
    m_compositor.setConfig(m_config);
    m_compositor.setOutputSize(m_outputSize);

    m_tickTimer.setInterval(16);
    connect(&m_tickTimer, &QTimer::timeout, this, &OverlayController::tick);

    connect(m_model, &AnnotationModel::changed, this, &OverlayController::refreshCompositor);

    connect(m_toolbar, &AnnotationToolbar::toolChanged, this, [this](AnnotationTool tool) {
        m_overlay->setTool(tool);
    });
    connect(m_toolbar, &AnnotationToolbar::penColorChanged, this, [this](const QColor &color) {
        m_config.penColor = color;
        m_overlay->setPenColor(color);
    });
    connect(m_toolbar, &AnnotationToolbar::penWidthChanged, this, [this](int width) {
        m_config.penWidth = width;
        m_overlay->setPenWidth(width);
    });
    connect(m_toolbar, &AnnotationToolbar::undoRequested, m_model, &AnnotationModel::undo);
    connect(m_toolbar, &AnnotationToolbar::clearRequested, m_model, &AnnotationModel::clear);
    connect(m_toolbar, &AnnotationToolbar::hideRequested, this, [this] {
        setOverlaysVisible(false);
    });
    connect(m_toolbar, &AnnotationToolbar::closeRequested, this, [this] {
        setDrawingEnabled(false);
    });

    connect(m_overlay, &AnnotationOverlay::drawingEnabledChanged,
            this, &OverlayController::drawingEnabledChanged);
    connect(m_overlay, &AnnotationOverlay::spotlightRectChanged, this, [this](const QRect &rect) {
        m_config.spotlightRect = rect;
        refreshCompositor();
        emit configChanged(m_config);
    });
    connect(m_overlay, &AnnotationOverlay::magnifierRectChanged, this, [this](const QRect &rect) {
        m_config.magnifierRect = rect;
        refreshCompositor();
        emit configChanged(m_config);
    });

    connect(m_camera, &CameraManager::frameReady, this, [this](const QImage &frame) {
        m_compositor.setCameraFrame(frame);
        m_cameraWindow->setFrame(frame);
    });
    connect(m_camera, &CameraManager::errorOccurred, this, [this](const QString &message) {
        qWarning() << "Camera error:" << message;
        emit cameraError(message);
    });
    connect(m_camera, &CameraManager::started, this, [this](const QString &name) {
        m_cameraRunning = true;
        m_cameraWindow->setPlaceholder(name);
    });

    connect(m_cameraWindow, &CameraWindow::geometryChanged, this, [this](const QRect &rect) {
        m_config.cameraRect = rect;
        refreshCompositor();
        emit configChanged(m_config);
    });
    connect(m_keyWindow, &KeyDisplayWindow::positionChanged, this, [this](const QPoint &position) {
        m_config.keyPosition = position;
        refreshCompositor();
        emit configChanged(m_config);
    });
}

OverlayController::~OverlayController()
{
    stopSession();
    delete m_overlay;
    delete m_toolbar;
    delete m_keyWindow;
    delete m_cameraWindow;
}

void OverlayController::startSession(const QRect &captureRect, const QSize &sourceSize,
                                     const OverlayConfig &config)
{
    m_active = true;
    m_captureRect = captureRect;
    m_config = config;
    m_config.overlaysVisible = true;
    m_outputSize = computeOutputSize(captureRect, sourceSize);
    m_compositor.setOutputSize(m_outputSize);
    m_compositor.setConfig(m_config);
    m_model->clear();
    m_ripples.clear();
    m_keyEntries.clear();

    m_overlay->refreshGeometry();
    m_overlay->setPenColor(m_config.penColor);
    m_overlay->setPenWidth(m_config.penWidth);
    m_overlay->setTool(m_toolbar->tool());
    m_overlay->setMouseEffectsEnabled(m_config.showMouseHighlight);
    m_overlay->setMouseStyle(m_config.mouseSize, m_config.leftClickColor,
                             m_config.rightClickColor, m_config.clickDurationMs);
    m_overlay->setKeyDisplayEnabled(m_config.showKeys);
    m_overlay->setKeyStyle(m_config.keyFontSize, m_config.keyOpacity, m_config.keyPosition);
    m_overlay->setDrawingEnabled(false);
    m_config.spotlightEnabled = false;
    m_config.magnifierEnabled = false;
    m_config.spotlightRect = defaultSpotlightRect();
    m_config.magnifierRect = defaultMagnifierRect();
    m_overlay->setSpotlightEnabled(false);
    m_overlay->setMagnifierEnabled(false);
    m_overlay->setSpotlightRect(m_config.spotlightRect);
    m_overlay->setMagnifierRect(m_config.magnifierRect);
    m_overlay->setSpotlightDim(m_config.spotlightDim);
    m_overlay->setMagnifierOptions(m_config.magnifierWidth, m_config.magnifierZoom);
    m_overlay->setOverlayVisible(m_config.overlaysVisible);

    m_keyWindow->setStyle(m_config.keyFontSize, m_config.keyOpacity);
    m_keyWindow->setGlobalPosition(m_config.keyPosition.x() >= 0 ? m_config.keyPosition
                                                                 : defaultKeyPosition());
    m_keyWindow->setVisible(m_config.showKeys && m_config.overlaysVisible);
    if (m_keyWindow->isVisible())
        WindowUtils::excludeFromCapture(m_keyWindow->winId());

    loadPptImage();
    m_cameraWindow->setMirror(m_config.cameraMirror);
    m_cameraWindow->setOpacityPercent(m_config.cameraOpacity);
    m_cameraWindow->setCaptureRect(m_config.cameraRect.isValid() ? m_config.cameraRect
                                                                 : defaultCameraRect());

    if (m_config.showCamera && m_config.layout == LayoutTemplate::Single) {
        m_cameraWindow->show();
        m_cameraWindow->raise();
        WindowUtils::excludeFromCapture(m_cameraWindow->winId());
    } else {
        m_cameraWindow->hide();
    }

    installHooks();
    m_tickTimer.start();
    if (m_config.showCamera)
        startCamera();

    refreshCompositor();
    emit overlaysVisibleChanged(m_config.overlaysVisible);
}

void OverlayController::stopSession()
{
    m_active = false;
    m_tickTimer.stop();
    uninstallHooks();
    m_cameraPreview = false;
    stopCamera();
    m_overlay->setDrawingEnabled(false);
    m_overlay->hide();
    m_toolbar->hide();
    m_keyWindow->hide();
    m_cameraWindow->hide();
    m_ripples.clear();
    m_keyEntries.clear();
    m_compositor.setCameraFrame(QImage());
    m_compositor.setKeyEntries({});
}

void OverlayController::setCaptureRect(const QRect &rect)
{
    if (rect.width() < 2 || rect.height() < 2)
        return;
    m_captureRect = rect;
    refreshCompositor();
}

QImage OverlayController::compose(const QImage &screenFrame, const QRect &screenRect, qint64 nowMs)
{
    return m_compositor.compose(screenFrame, screenRect, nowMs);
}

void OverlayController::applyConfig(const OverlayConfig &config)
{
    m_config = config;
    if (m_active && m_config.layout != LayoutTemplate::Single) {
        m_cameraWindow->hide();
    }
    applyConfigToWindows();
    refreshCompositor();
    emit configChanged(m_config);
}

void OverlayController::applyConfigToWindows()
{
    m_compositor.setConfig(m_config);
    m_overlay->setPenColor(m_config.penColor);
    m_overlay->setPenWidth(m_config.penWidth);
    m_overlay->setMouseEffectsEnabled(m_config.showMouseHighlight);
    m_overlay->setMouseStyle(m_config.mouseSize, m_config.leftClickColor,
                             m_config.rightClickColor, m_config.clickDurationMs);
    m_overlay->setKeyDisplayEnabled(m_config.showKeys);
    m_overlay->setKeyStyle(m_config.keyFontSize, m_config.keyOpacity, m_config.keyPosition);
    m_keyWindow->setStyle(m_config.keyFontSize, m_config.keyOpacity);
    if (m_config.keyPosition.x() >= 0)
        m_keyWindow->setGlobalPosition(m_config.keyPosition);
    m_cameraWindow->setMirror(m_config.cameraMirror);
    m_cameraWindow->setOpacityPercent(m_config.cameraOpacity);

    if (m_active) {
        m_keyWindow->setVisible(m_config.showKeys && m_config.overlaysVisible);
        if (m_keyWindow->isVisible())
            WindowUtils::excludeFromCapture(m_keyWindow->winId());
        const bool showCamera = m_config.showCamera && m_config.overlaysVisible
                                && m_config.layout == LayoutTemplate::Single;
        m_cameraWindow->setVisible(showCamera);
        if (showCamera) {
            m_cameraWindow->raise();
            WindowUtils::excludeFromCapture(m_cameraWindow->winId());
        }
        if (m_config.showCamera) {
            if (!m_cameraRunning)
                startCamera();
        } else {
            stopCamera();
        }
    }
    loadPptImage();
}

void OverlayController::setDrawingEnabled(bool enabled)
{
    if (!m_active && enabled)
        return;
    if (enabled && !m_config.overlaysVisible)
        setOverlaysVisible(true);
    if (enabled) {
        if (m_config.spotlightEnabled)
            setSpotlightEnabled(false);
        if (m_config.magnifierEnabled)
            setMagnifierEnabled(false);
    }

    m_overlay->setDrawingEnabled(enabled);
    if (enabled) {
        // Keep the overlay below its own toolbar, otherwise the full-screen
        // drawing surface would swallow every toolbar click.
        m_overlay->raise();
        WindowUtils::setTopmost(m_overlay->winId(), true);
        m_toolbar->syncFromConfig(m_config);
        m_toolbar->showCentered();
        m_toolbar->raise();
        WindowUtils::setTopmost(m_toolbar->winId(), true);
    } else {
        m_toolbar->hide();
    }
}

bool OverlayController::isDrawingEnabled() const
{
    return m_overlay->isDrawingEnabled();
}

void OverlayController::toggleDrawing()
{
    setDrawingEnabled(!isDrawingEnabled());
}

void OverlayController::setOverlaysVisible(bool visible)
{
    if (!m_active && visible)
        return;
    if (m_config.overlaysVisible == visible && m_overlay->isVisible() == visible)
        return;
    m_config.overlaysVisible = visible;
    m_compositor.setConfig(m_config);
    m_overlay->setOverlayVisible(visible);
    m_keyWindow->setVisible(visible && m_config.showKeys && m_active);
    m_cameraWindow->setVisible(visible && m_config.showCamera && m_active
                               && m_config.layout == LayoutTemplate::Single);
    if (!visible) {
        m_overlay->setDrawingEnabled(false);
        m_overlay->setSpotlightEnabled(false);
        m_overlay->setMagnifierEnabled(false);
        m_config.spotlightEnabled = false;
        m_config.magnifierEnabled = false;
        emit spotlightEnabledChanged(false);
        emit magnifierEnabledChanged(false);
        m_toolbar->hide();
    }
    if (visible) {
        if (m_keyWindow->isVisible())
            WindowUtils::excludeFromCapture(m_keyWindow->winId());
        if (m_cameraRunning) {
            m_cameraWindow->raise();
            WindowUtils::excludeFromCapture(m_cameraWindow->winId());
        }
    }
    emit overlaysVisibleChanged(visible);
    emit configChanged(m_config);
}

void OverlayController::toggleOverlaysVisible()
{
    setOverlaysVisible(!m_config.overlaysVisible);
}

void OverlayController::setCameraEnabled(bool enabled)
{
    m_config.showCamera = enabled;
    applyConfigToWindows();
    refreshCompositor();
    emit configChanged(m_config);
}

void OverlayController::setSpotlightEnabled(bool enabled)
{
    if (!m_active && enabled)
        return;
    if (enabled) {
        if (!m_config.overlaysVisible)
            setOverlaysVisible(true);
        if (m_overlay->isDrawingEnabled())
            setDrawingEnabled(false);
        if (m_config.magnifierEnabled)
            setMagnifierEnabled(false);
        if (!m_config.spotlightRect.isValid() || m_config.spotlightRect.isEmpty())
            m_config.spotlightRect = defaultSpotlightRect();
        m_config.spotlightEnabled = true;
        m_overlay->setSpotlightRect(m_config.spotlightRect);
        m_overlay->setSpotlightDim(m_config.spotlightDim);
        m_overlay->setSpotlightEnabled(true);
    } else {
        m_config.spotlightEnabled = false;
        m_overlay->setSpotlightEnabled(false);
    }
    refreshCompositor();
    emit spotlightEnabledChanged(m_config.spotlightEnabled);
    emit configChanged(m_config);
}

void OverlayController::setMagnifierEnabled(bool enabled)
{
    if (!m_active && enabled)
        return;
    if (enabled) {
        if (!m_config.overlaysVisible)
            setOverlaysVisible(true);
        if (m_overlay->isDrawingEnabled())
            setDrawingEnabled(false);
        if (m_config.spotlightEnabled)
            setSpotlightEnabled(false);
        if (!m_config.magnifierRect.isValid() || m_config.magnifierRect.isEmpty())
            m_config.magnifierRect = defaultMagnifierRect();
        m_config.magnifierEnabled = true;
        m_overlay->setMagnifierRect(m_config.magnifierRect);
        m_overlay->setMagnifierOptions(m_config.magnifierWidth, m_config.magnifierZoom);
        m_overlay->setMagnifierEnabled(true);
    } else {
        m_config.magnifierEnabled = false;
        m_overlay->setMagnifierEnabled(false);
    }
    refreshCompositor();
    emit magnifierEnabledChanged(m_config.magnifierEnabled);
    emit configChanged(m_config);
}

void OverlayController::setCameraDevice(const QString &deviceId)
{
    m_config.cameraDeviceId = deviceId;
    if (m_cameraRunning) {
        stopCamera();
        startCamera();
    }
    emit configChanged(m_config);
}

void OverlayController::setLayout(LayoutTemplate layout)
{
    m_config.layout = layout;
    applyConfigToWindows();
    refreshCompositor();
    emit configChanged(m_config);
}

void OverlayController::setPptImagePath(const QString &path)
{
    m_config.pptImagePath = path;
    loadPptImage();
    emit configChanged(m_config);
}

void OverlayController::updateCameraRect(const QRect &rect)
{
    if (!rect.isValid())
        return;
    m_config.cameraRect = rect;
    m_cameraWindow->setCaptureRect(rect);
    refreshCompositor();
    emit configChanged(m_config);
}

void OverlayController::startCameraPreview()
{
    m_cameraPreview = true;
    startCamera();
}

void OverlayController::stopCameraPreview()
{
    m_cameraPreview = false;
    if (!m_active)
        stopCamera();
}

void OverlayController::startCamera()
{
    if (m_cameraRunning)
        return;
    QString error;
    if (!m_camera->start(m_config.cameraDeviceId, &error)) {
        emit cameraError(error);
        return;
    }
    m_cameraRunning = true;
}

void OverlayController::stopCamera()
{
    if (!m_cameraRunning)
        return;
    m_camera->stop();
    m_cameraRunning = false;
    m_compositor.setCameraFrame(QImage());
    m_cameraWindow->setFrame(QImage());
}

void OverlayController::loadPptImage()
{
    if (m_config.pptImagePath.isEmpty()) {
        m_pptImage = QImage();
    } else {
        QImage image(m_config.pptImagePath);
        if (!image.isNull())
            m_pptImage = image;
    }
    m_compositor.setPptImage(m_pptImage);
}

void OverlayController::refreshCompositor()
{
    m_compositor.setConfig(m_config);
    m_compositor.setStrokes(m_model->strokes());
    m_compositor.setMouseState(m_cursor, true, m_ripples);
    m_compositor.setKeyEntries(m_keyEntries);
    m_compositor.setPptImage(m_pptImage);
}

void OverlayController::installHooks()
{
    if (!InputHooks::instance().isInstalled()) {
        if (!InputHooks::instance().install())
            qWarning() << "Input hooks unavailable; key/mouse overlay disabled";
    }
    disconnect(&InputHooks::instance(), nullptr, this, nullptr);
    connect(&InputHooks::instance(), &InputHooks::keyPressed,
            this, &OverlayController::onKeyPressed);
    connect(&InputHooks::instance(), &InputHooks::mouseClicked,
            this, &OverlayController::onMouseClicked);
}

void OverlayController::uninstallHooks()
{
    disconnect(&InputHooks::instance(), nullptr, this, nullptr);
}

void OverlayController::tick()
{
    if (!m_active)
        return;

    m_cursor = QCursor::pos();
    m_overlay->setLiveMouse(m_cursor, true, m_ripples);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_ripples.erase(std::remove_if(m_ripples.begin(), m_ripples.end(),
                                   [this, now](const MouseRipple &ripple) {
                                       return now - ripple.startMs > m_config.clickDurationMs + 100;
                                   }),
                    m_ripples.end());

    m_keyEntries.erase(std::remove_if(m_keyEntries.begin(), m_keyEntries.end(),
                                      [now](const KeyEntry &entry) {
                                          return now - entry.startMs > 3200;
                                      }),
                       m_keyEntries.end());

    m_compositor.setMouseState(m_cursor, true, m_ripples);
    m_compositor.setKeyEntries(m_keyEntries);
    m_compositor.setStrokes(m_model->strokes());
    m_overlay->setKeyEntries(m_keyEntries);
    m_keyWindow->setEntries(m_keyEntries);
}

void OverlayController::onKeyPressed(const QString &combination)
{
    if (!m_active || !m_config.showKeys)
        return;
    KeyEntry entry;
    entry.text = combination;
    entry.startMs = QDateTime::currentMSecsSinceEpoch();
    m_keyEntries.append(entry);
    while (m_keyEntries.size() > 8)
        m_keyEntries.removeFirst();
    m_compositor.setKeyEntries(m_keyEntries);
    m_keyWindow->setEntries(m_keyEntries);
    m_overlay->setKeyEntries(m_keyEntries);
}

void OverlayController::onMouseClicked(Qt::MouseButton button, const QPoint &position)
{
    if (!m_active || !m_config.showMouseHighlight)
        return;
    MouseRipple ripple;
    ripple.position = position;
    ripple.rightButton = button == Qt::RightButton;
    ripple.color = ripple.rightButton ? m_config.rightClickColor : m_config.leftClickColor;
    ripple.startMs = QDateTime::currentMSecsSinceEpoch();
    m_ripples.append(ripple);
    while (m_ripples.size() > 12)
        m_ripples.removeFirst();
    m_compositor.setMouseState(m_cursor, true, m_ripples);
}

QSize OverlayController::computeOutputSize(const QRect &captureRect, const QSize &sourceSize) const
{
    QSize size = sourceSize.isValid() && sourceSize.width() > 1 && sourceSize.height() > 1
                     ? sourceSize
                     : captureRect.size();
    if (m_config.layout != LayoutTemplate::Single) {
        size.setWidth(std::max(1280, size.width()));
        size.setHeight(std::max(720, size.width() * 9 / 16));
    }
    if (size.width() < 2 || size.height() < 2)
        size = QSize(1920, 1080);
    return QSize(size.width() & ~1, size.height() & ~1);
}

QPoint OverlayController::defaultKeyPosition() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return QPoint(24, 24);
    const QRect geometry = screen->availableGeometry();
    return QPoint(geometry.left() + 24, geometry.bottom() - 160);
}

QRect OverlayController::defaultCameraRect() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return QRect(40, 40, 360, 202);
    const QRect geometry = screen->availableGeometry();
    return QRect(geometry.right() - 380, geometry.bottom() - 242, 360, 202);
}

QRect OverlayController::defaultSpotlightRect() const
{
    const QRect desktop = WindowUtils::virtualDesktopGeometry();
    const int width = qMax(240, desktop.width() / 3);
    const int height = qMax(160, desktop.height() / 3);
    return QRect(desktop.center().x() - width / 2, desktop.center().y() - height / 2,
                 width, height);
}

QRect OverlayController::defaultMagnifierRect() const
{
    const QRect desktop = WindowUtils::virtualDesktopGeometry();
    return QRect(desktop.center().x() - 160, desktop.center().y() - 100, 320, 200);
}

} // namespace lightrec
