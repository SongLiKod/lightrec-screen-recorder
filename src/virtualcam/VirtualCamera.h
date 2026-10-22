#pragma once

#include <QImage>
#include <QObject>
#include <QString>

namespace lightrec {

/**
 * Virtual camera frame bridge.
 *
 * Frames are published to the named shared-memory segment
 * "Local\\LightRecVirtualCamera" with a small header:
 *   uint32 magic, uint32 width, uint32 height, uint32 stride, uint32 sequence
 * followed by BGRA pixels.  A companion virtual camera device (DirectShow /
 * Media Foundation) can consume the segment and expose it to meeting apps.
 *
 * The class also probes for the Windows 11 virtual camera API and reports a
 * friendly status in the UI.
 */
class VirtualCamera : public QObject
{
    Q_OBJECT
public:
    explicit VirtualCamera(QObject *parent = nullptr);
    ~VirtualCamera() override;

    static bool isPlatformSupported();
    static QString backendDescription();

    bool start(QString *error);
    void stop();
    bool isRunning() const { return m_running; }
    void pushFrame(const QImage &frame);

    QString lastError() const { return m_lastError; }

signals:
    void errorOccurred(const QString &message);

private:
    void *m_mapping = nullptr;
    void *m_view = nullptr;
    quint32 m_capacity = 0;
    quint32 m_sequence = 0;
    bool m_running = false;
    QString m_lastError;
};

} // namespace lightrec
