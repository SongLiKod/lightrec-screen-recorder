#pragma once

#include "core/Types.h"

#include <QImage>
#include <QList>
#include <QString>

#include <memory>

namespace lightrec {

class ICaptureBackend
{
public:
    virtual ~ICaptureBackend() = default;
    virtual bool start(const CaptureConfig &config, QString *error) = 0;
    virtual void stop() = 0;
    virtual bool updateConfig(const CaptureConfig &config, QString *error) = 0;
    virtual bool grab(QImage &frame, int timeoutMs, QString *error) = 0;
    virtual QRect captureRect() const = 0;
    virtual QSize sourceSize() const = 0;
    virtual QString name() const = 0;
};

/**
 * Facade over the available capture backends.
 *
 * Preferred backend: Windows Graphics Capture (Windows 10 1903+).
 * Fallback backend: DXGI Desktop Duplication.
 *
 * The facade keeps the capture rectangle up to date so that window capture can
 * follow a moving window and region capture can be adjusted while recording.
 */
class ScreenCapture
{
public:
    ScreenCapture();
    ~ScreenCapture();

    static QList<MonitorInfo> monitors();
    static QList<WindowInfo> windows();

    bool start(const CaptureConfig &config, QString *error);
    void stop();
    bool isRunning() const;

    /** Change region/window while recording. The output canvas size is kept. */
    bool updateConfig(const CaptureConfig &config, QString *error);

    /**
     * Grab the next frame. Returns false on timeout (the screen did not change);
     * callers should re-use the previous frame in that case.
     */
    bool grab(QImage &frame, int timeoutMs, QString *error);

    QRect captureRect() const;
    QSize sourceSize() const;
    QString backendName() const;
    QString lastError() const { return m_lastError; }

private:
    std::unique_ptr<ICaptureBackend> m_backend;
    QString m_lastError;
    bool m_running = false;
};

} // namespace lightrec
