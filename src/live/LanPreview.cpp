#include "live/LanPreview.h"
#include "overlay/OverlayController.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>

namespace lightrec {

LanPreview::LanPreview(OverlayController *overlay, QObject *parent)
    : QObject(parent)
    , m_overlay(overlay)
{
}

LanPreview::~LanPreview()
{
    stop();
}

bool LanPreview::start(const VideoConfig &video, const OverlayConfig &overlay, QString *error)
{
    stop();
    if (!m_overlay) {
        if (error)
            *error = QStringLiteral("叠加模块不可用");
        return false;
    }
    m_video = video;

    QString captureError;
    if (!m_capture.start(m_video, &captureError)) {
        if (error)
            *error = captureError;
        return false;
    }

    m_overlay->startSession(m_capture.captureRect(), m_capture.sourceSize(), overlay);
    m_outputSize = m_overlay->outputSize();

    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread([this] { loop(); });
    return true;
}

void LanPreview::stop()
{
    m_stopRequested = true;
    if (m_thread.joinable())
        m_thread.join();
    if (m_running) {
        m_capture.stop();
        if (m_overlay)
            m_overlay->stopSession();
        m_running = false;
    }
}

void LanPreview::loop()
{
    QImage lastFrame;
    QRect lastRect = m_capture.captureRect();
    QElapsedTimer clock;
    clock.start();
    const int interval = 80; // 12 fps is plenty for the MJPEG preview

    while (!m_stopRequested.load()) {
        const qint64 started = clock.elapsed();
        QImage frame;
        QString error;
        if (m_capture.grab(frame, 30, &error) && !frame.isNull()) {
            lastFrame = frame;
            lastRect = m_capture.captureRect();
        }
        if (!lastFrame.isNull()) {
            if (!m_overlay)
                break;
            const QImage composed = m_overlay->compose(lastFrame, lastRect,
                                                       QDateTime::currentMSecsSinceEpoch());
            emit frameReady(composed);
        }
        const qint64 spent = clock.elapsed() - started;
        if (spent < interval)
            QThread::msleep(static_cast<unsigned long>(interval - spent));
    }
}

} // namespace lightrec
