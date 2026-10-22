#pragma once

#include "audio/WasapiCapture.h"
#include "capture/ScreenCapture.h"
#include "core/Types.h"
#include "media/MediaPipeline.h"
#include "record/RecordingJournal.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <memory>
#include <thread>

namespace lightrec {

class OverlayController;

class RecordingController : public QObject
{
    Q_OBJECT
public:
    explicit RecordingController(OverlayController *overlay, QObject *parent = nullptr);
    ~RecordingController() override;

    bool start(const RecordOptions &options, QString *error);
    void pause();
    void resume();
    void stop();
    bool updateRegion(const QRect &region, QString *error);

    RecordState state() const { return m_state; }
    bool isRecording() const;
    qint64 elapsedMs() const;
    QString currentOutputPath() const { return m_outputPath; }
    QRect captureRect() const { return m_capture.captureRect(); }
    QString usedEncoder() const { return m_usedEncoder; }
    QStringList warnings() const { return m_warnings; }
    bool streamActive() const { return m_streamActive; }

signals:
    void stateChanged(lightrec::RecordState state);
    void elapsedChanged(qint64 elapsedMs);
    void warning(const QString &message);
    void errorOccurred(const QString &message);
    void recordingFinished(const QString &path, bool success, const QString &message);
    void encoderSelected(const QString &encoderName);
    void streamStatus(const QString &status);
    void frameReady(const QImage &frame);
    void workerFinished(bool success, const QString &message, const QString &path);

private:
    void setState(RecordState state);
    void captureLoop();
    void handleWorkerFinished(bool success, const QString &message, const QString &path);
    void updateElapsed();
    void cleanupAfterStop();

    OverlayController *m_overlay = nullptr;
    ScreenCapture m_capture;
    WasapiCapture m_systemAudio;
    WasapiCapture m_micAudio;
    std::unique_ptr<MediaPipeline> m_pipeline;
    RecordingJournal m_journal;

    RecordOptions m_options;
    RecordState m_state = RecordState::Idle;
    QString m_outputPath;
    QString m_partPath;
    QString m_usedEncoder;
    QStringList m_warnings;
    bool m_streamActive = false;

    std::thread m_thread;
    std::atomic<bool> m_stopRequested{ false };
    std::atomic<bool> m_paused{ false };
    std::atomic<qint64> m_pausedMs{ 0 };

    QElapsedTimer m_clock;
    qint64 m_pauseStarted = 0;
    QTimer m_elapsedTimer;
    int m_checkpointCounter = 0;
};

} // namespace lightrec
