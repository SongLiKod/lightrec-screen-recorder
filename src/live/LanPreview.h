#pragma once

#include "capture/ScreenCapture.h"
#include "core/Types.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <atomic>
#include <thread>

namespace lightrec {

class OverlayController;

/**
 * Lightweight capture loop used when the LAN live server runs without a
 * recording session.  It composes the same overlay elements (annotations,
 * mouse effects, camera) as the recorder so viewers see the annotated screen.
 */
class LanPreview : public QObject
{
    Q_OBJECT
public:
    explicit LanPreview(OverlayController *overlay, QObject *parent = nullptr);
    ~LanPreview() override;

    bool start(const VideoConfig &video, const OverlayConfig &overlay, QString *error);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    void frameReady(const QImage &frame);
    void errorOccurred(const QString &message);

private:
    void loop();

    OverlayController *m_overlay = nullptr;
    ScreenCapture m_capture;
    VideoConfig m_video;
    QSize m_outputSize;
    std::atomic<bool> m_stopRequested{ false };
    std::thread m_thread;
    bool m_running = false;
};

} // namespace lightrec
