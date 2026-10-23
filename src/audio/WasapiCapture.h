#pragma once

#include "core/Types.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

namespace lightrec {

struct AudioFrameFormat {
    int sampleRate = 48000;
    int channels = 2;
    bool isFloat = true;
};

using AudioFrameCallback =
    std::function<void(const QByteArray &data, const AudioFrameFormat &format, qint64 ptsMs)>;

/**
 * WASAPI shared-mode capture.  One instance captures the system loopback,
 * a second instance captures the microphone, producing two independent tracks.
 */
class WasapiCapture
{
public:
    WasapiCapture();
    ~WasapiCapture();

    static QList<AudioDeviceInfo> renderDevices();
    static QList<AudioDeviceInfo> captureDevices();

    bool start(bool loopback, const QString &deviceId, QString *error);
    void stop();
    void setPaused(bool paused);
    bool isRunning() const;

    void setCallback(AudioFrameCallback callback);
    void setGainDb(int gainDb) { m_gainDb = gainDb; }
    void setNoiseSuppression(bool enabled) { m_noiseSuppression = enabled; }

    QString deviceName() const { return m_deviceName; }
    AudioFrameFormat format() const { return m_format; }
    bool isLoopback() const { return m_loopback; }
    qint64 startEpochMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    AudioFrameCallback m_callback;
    QString m_deviceName;
    AudioFrameFormat m_format;
    bool m_loopback = false;
    int m_gainDb = 0;
    bool m_noiseSuppression = false;
};

} // namespace lightrec
