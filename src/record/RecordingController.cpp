#include "record/RecordingController.h"

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "overlay/OverlayController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

namespace lightrec {

RecordingController::RecordingController(OverlayController *overlay, QObject *parent)
    : QObject(parent)
    , m_overlay(overlay)
{
    m_elapsedTimer.setInterval(200);
    connect(&m_elapsedTimer, &QTimer::timeout, this, &RecordingController::updateElapsed);
    connect(this, &RecordingController::workerFinished,
            this, &RecordingController::handleWorkerFinished, Qt::QueuedConnection);

    m_systemAudio.setCallback([this](const QByteArray &data, const AudioFrameFormat &format, qint64 ptsMs) {
        if (m_pipeline)
            m_pipeline->pushAudio(0, data, format, ptsMs);
    });
    m_micAudio.setCallback([this](const QByteArray &data, const AudioFrameFormat &format, qint64 ptsMs) {
        if (m_pipeline)
            m_pipeline->pushAudio(1, data, format, ptsMs);
    });
}

RecordingController::~RecordingController()
{
    m_stopRequested = true;
    m_paused = false;
    m_elapsedTimer.stop();

    m_systemAudio.stop();
    m_micAudio.stop();
    if (m_thread.joinable())
        m_thread.join();
    m_capture.stop();
    if (m_pipeline) {
        m_pipeline->abort();
        m_pipeline.reset();
    }
    m_journal.discard();
    if (m_overlay)
        m_overlay->stopSession();
}

bool RecordingController::isRecording() const
{
    return m_state == RecordState::Recording || m_state == RecordState::Paused
           || m_state == RecordState::Starting;
}

bool RecordingController::start(const RecordOptions &options, QString *error)
{
    if (m_state != RecordState::Idle) {
        if (error)
            *error = QStringLiteral("当前正在录制，请先停止");
        return false;
    }

    m_options = options;
    m_warnings.clear();
    m_usedEncoder.clear();
    m_streamActive = m_options.stream.enabled && !m_options.stream.fullUrl().isEmpty();
    m_options.stream.enabled = m_streamActive;

    m_outputPath = m_options.outputPath;
    if (m_outputPath.isEmpty() && (!m_streamActive || m_options.stream.saveLocal)) {
        m_outputPath = AppPaths::uniqueOutputPath(AppPaths::outputDir());
        m_options.outputPath = m_outputPath;
    }

    if (m_outputPath.isEmpty() && !m_streamActive) {
        if (error)
            *error = QStringLiteral("请先选择保存目录或填写推流地址");
        return false;
    }

    if (!m_outputPath.isEmpty()) {
        QDir().mkpath(QFileInfo(m_outputPath).absolutePath());
        m_partPath = m_outputPath + QStringLiteral(".part");
    } else {
        m_partPath.clear();
    }

    setState(RecordState::Starting);

    QString captureError;
    if (!m_capture.start(m_options.video, &captureError)) {
        setState(RecordState::Idle);
        if (error)
            *error = captureError;
        return false;
    }

    const QRect captureRect = m_capture.captureRect();
    m_overlay->startSession(captureRect, m_capture.sourceSize(), m_options.overlay);
    const QSize outputSize = m_overlay->outputSize();

    MediaPipeline::Options pipelineOptions;
    pipelineOptions.outputPath = m_outputPath;
    pipelineOptions.outputSize = outputSize;
    pipelineOptions.video = m_options.video;
    pipelineOptions.audio = m_options.audio;
    pipelineOptions.stream = m_options.stream;

    m_pipeline = std::make_unique<MediaPipeline>();
    connect(m_pipeline.get(), &MediaPipeline::warning, this, [this](const QString &message) {
        if (!m_warnings.contains(message))
            m_warnings.append(message);
        emit warning(message);
    });

    QString pipelineError;
    if (!m_pipeline->open(pipelineOptions, &pipelineError)) {
        m_pipeline.reset();
        m_capture.stop();
        m_overlay->stopSession();
        setState(RecordState::Idle);
        if (error)
            *error = pipelineError;
        return false;
    }

    m_usedEncoder = m_pipeline->usedEncoder();
    emit encoderSelected(m_usedEncoder);
    if (m_streamActive)
        emit streamStatus(QStringLiteral("正在连接 RTMP 服务器..."));

    if (m_options.audio.systemEnabled) {
        m_systemAudio.setGainDb(m_options.audio.systemGainDb);
        m_systemAudio.setNoiseSuppression(false);
        QString audioError;
        if (!m_systemAudio.start(true, m_options.audio.systemDeviceId, &audioError)) {
            m_warnings.append(QStringLiteral("系统声音采集失败：%1").arg(audioError));
            emit warning(m_warnings.last());
        }
    }
    if (m_options.audio.micEnabled) {
        m_micAudio.setGainDb(m_options.audio.micGainDb);
        m_micAudio.setNoiseSuppression(m_options.audio.noiseSuppression);
        QString audioError;
        if (!m_micAudio.start(false, m_options.audio.micDeviceId, &audioError)) {
            m_warnings.append(QStringLiteral("麦克风采集失败：%1").arg(audioError));
            emit warning(m_warnings.last());
        }
    }

    QString journalError;
    if (!m_journal.begin(m_options, m_partPath, m_outputPath, outputSize, &journalError)) {
        m_warnings.append(journalError);
        emit warning(journalError);
    }

    m_clock.start();
    m_pausedMs = 0;
    m_pauseStarted = 0;
    m_checkpointCounter = 0;
    m_stopRequested = false;
    m_paused = false;

    setState(RecordState::Recording);
    m_elapsedTimer.start();
    m_thread = std::thread([this] { captureLoop(); });
    return true;
}

void RecordingController::pause()
{
    if (m_state != RecordState::Recording)
        return;
    m_paused = true;
    m_pauseStarted = m_clock.elapsed();
    m_systemAudio.setPaused(true);
    m_micAudio.setPaused(true);
    setState(RecordState::Paused);
}

void RecordingController::resume()
{
    if (m_state != RecordState::Paused)
        return;
    m_pausedMs = m_pausedMs.load() + (m_clock.elapsed() - m_pauseStarted);
    m_paused = false;
    m_systemAudio.setPaused(false);
    m_micAudio.setPaused(false);
    setState(RecordState::Recording);
}

void RecordingController::stop()
{
    if (m_state != RecordState::Recording && m_state != RecordState::Paused
        && m_state != RecordState::Starting) {
        return;
    }
    m_stopRequested = true;
    m_paused = false;
    m_systemAudio.setPaused(false);
    m_micAudio.setPaused(false);
    setState(RecordState::Stopping);
}

bool RecordingController::updateRegion(const QRect &region, QString *error)
{
    if (m_state != RecordState::Recording && m_state != RecordState::Paused) {
        if (error)
            *error = QStringLiteral("当前没有正在进行的录制");
        return false;
    }
    if (region.width() < 2 || region.height() < 2) {
        if (error)
            *error = QStringLiteral("选区尺寸无效");
        return false;
    }

    VideoConfig config = m_options.video;
    config.mode = CaptureMode::Region;
    config.region = region;
    if (!m_capture.updateConfig(config, error))
        return false;
    m_options.video = config;
    m_overlay->setCaptureRect(region);
    return true;
}

qint64 RecordingController::elapsedMs() const
{
    if (m_state == RecordState::Idle || m_state == RecordState::Starting)
        return 0;
    qint64 elapsed = m_clock.elapsed() - m_pausedMs.load();
    if (m_state == RecordState::Paused)
        elapsed -= (m_clock.elapsed() - m_pauseStarted);
    return std::max<qint64>(0, elapsed);
}

void RecordingController::captureLoop()
{
#ifdef Q_OS_WIN
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    QImage lastFrame;
    QRect lastRect = m_capture.captureRect();
    QElapsedTimer frameClock;
    frameClock.start();
    const int frameInterval = std::max(1, 1000 / std::max(1, m_options.video.fps));
    bool success = true;
    QString message = QStringLiteral("录制完成");

    while (!m_stopRequested.load()) {
        if (m_paused.load()) {
            QThread::msleep(20);
            continue;
        }

        const qint64 loopStarted = frameClock.elapsed();
        const qint64 ptsMs = std::max<qint64>(0, m_clock.elapsed() - m_pausedMs.load());

        QImage frame;
        QString grabError;
        const bool grabbed = m_capture.grab(frame, std::min(12, frameInterval), &grabError);
        if (grabbed && !frame.isNull()) {
            lastFrame = frame;
            lastRect = m_capture.captureRect();
        } else if (!grabError.isEmpty()) {
            qWarning() << "Capture error:" << grabError;
        }

        if (!lastFrame.isNull()) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const QImage composed = m_overlay->compose(lastFrame, lastRect, now);
            emit frameReady(composed);
            QString encodeError;
            if (!m_pipeline->encodeVideo(composed, ptsMs, &encodeError)) {
                success = false;
                message = encodeError;
                qWarning() << "Encode error:" << encodeError;
                break;
            }
        }

        const qint64 spent = frameClock.elapsed() - loopStarted;
        if (spent < frameInterval)
            QThread::msleep(static_cast<unsigned long>(frameInterval - spent));
    }

    m_systemAudio.stop();
    m_micAudio.stop();
    m_capture.stop();

    QString finalPath = m_outputPath;
    if (m_pipeline) {
        QString finishError;
        if (success && !m_pipeline->finish(&finishError)) {
            success = false;
            message = finishError;
        } else if (!success) {
            m_pipeline->abort();
        }
    }

    if (success) {
        m_journal.complete();
        if (!m_outputPath.isEmpty() && !QFileInfo::exists(m_outputPath)) {
            success = false;
            message = QStringLiteral("录像文件未能生成，请使用视频修复工具尝试恢复缓存");
        }
    } else {
        m_journal.checkpoint(m_clock.elapsed());
    }

    if (m_outputPath.isEmpty())
        finalPath.clear();
    emit workerFinished(success, message, finalPath);
#ifdef Q_OS_WIN
    if (SUCCEEDED(comResult))
        CoUninitialize();
#endif
}

void RecordingController::updateElapsed()
{
    const qint64 elapsed = elapsedMs();
    emit elapsedChanged(elapsed);
    if (m_state == RecordState::Recording || m_state == RecordState::Paused) {
        if (++m_checkpointCounter >= 10) { // ~2 seconds
            m_checkpointCounter = 0;
            m_journal.checkpoint(elapsed);
        }
    }
}

void RecordingController::handleWorkerFinished(bool success, const QString &message, const QString &path)
{
    if (m_thread.joinable())
        m_thread.join();
    m_elapsedTimer.stop();
    m_capture.stop();
    m_systemAudio.stop();
    m_micAudio.stop();
    m_pipeline.reset();
    if (m_overlay)
        m_overlay->stopSession();

    setState(RecordState::Idle);
    emit recordingFinished(path, success, message);
}

void RecordingController::setState(RecordState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

} // namespace lightrec
