#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

class QCamera;
class QMediaCaptureSession;
class QVideoSink;

namespace lightrec {

struct CameraDeviceInfo {
    QString id;
    QString name;
    bool isDefault = false;
};

class CameraManager : public QObject
{
    Q_OBJECT
public:
    explicit CameraManager(QObject *parent = nullptr);
    ~CameraManager() override;

    static QList<CameraDeviceInfo> devices();

    bool start(const QString &deviceId, QString *error);
    void stop();
    bool isRunning() const { return m_running; }
    QString deviceName() const { return m_deviceName; }

signals:
    void frameReady(const QImage &frame);
    void errorOccurred(const QString &message);
    void started(const QString &deviceName);

private:
    QCamera *m_camera = nullptr;
    QMediaCaptureSession *m_session = nullptr;
    QVideoSink *m_sink = nullptr;
    QString m_deviceName;
    bool m_running = false;
    qint64 m_lastFrameMs = 0;
};

} // namespace lightrec
