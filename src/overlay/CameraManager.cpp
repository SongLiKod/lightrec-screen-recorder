#include "overlay/CameraManager.h"

#include <QCamera>
#include <QCameraDevice>
#include <QDateTime>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoFrame>
#include <QVideoSink>

namespace lightrec {

CameraManager::CameraManager(QObject *parent)
    : QObject(parent)
{
}

CameraManager::~CameraManager()
{
    stop();
}

QList<CameraDeviceInfo> CameraManager::devices()
{
    QList<CameraDeviceInfo> result;
    const QCameraDevice defaultDevice = QMediaDevices::defaultVideoInput();
    const auto cameras = QMediaDevices::videoInputs();
    for (const QCameraDevice &device : cameras) {
        CameraDeviceInfo info;
        info.id = QString::fromUtf8(device.id());
        info.name = device.description();
        info.isDefault = !defaultDevice.isNull() && device.id() == defaultDevice.id();
        result.append(info);
    }
    return result;
}

bool CameraManager::start(const QString &deviceId, QString *error)
{
    stop();

    const auto cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        if (error)
            *error = QStringLiteral("未检测到摄像头设备");
        return false;
    }

    QCameraDevice selected = QMediaDevices::defaultVideoInput();
    if (!deviceId.isEmpty()) {
        const QByteArray wanted = deviceId.toUtf8();
        for (const QCameraDevice &device : cameras) {
            if (device.id() == wanted) {
                selected = device;
                break;
            }
        }
    }
    if (selected.isNull())
        selected = cameras.first();

    m_deviceName = selected.description();
    m_camera = new QCamera(selected, this);
    m_session = new QMediaCaptureSession(this);
    m_sink = new QVideoSink(this);
    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!frame.isValid())
            return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastFrameMs < 30)
            return;
        m_lastFrameMs = now;
        const QImage image = frame.toImage();
        if (!image.isNull())
            emit frameReady(image);
    });

    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error error, const QString &message) {
        if (error != QCamera::NoError)
            emit errorOccurred(message.isEmpty() ? QStringLiteral("摄像头采集失败") : message);
    });

    m_camera->start();
    m_running = true;
    emit started(m_deviceName);
    return true;
}

void CameraManager::stop()
{
    if (m_camera) {
        m_camera->stop();
        m_camera->deleteLater();
        m_camera = nullptr;
    }
    if (m_session) {
        m_session->setCamera(nullptr);
        m_session->deleteLater();
        m_session = nullptr;
    }
    if (m_sink) {
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_running = false;
}

} // namespace lightrec
