#pragma once

#include "core/Types.h"

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QStringList>

namespace lightrec {

/**
 * Thin asynchronous wrapper around the bundled ffmpeg.exe used by the
 * post-processing toolbox (repair, trim, crop, speed, compress, GIF, watermark).
 * The FFmpeg libraries are used for real-time recording; the CLI is used for
 * offline jobs so that every filter stays available.
 */
class FFmpegTool : public QObject
{
    Q_OBJECT
public:
    explicit FFmpegTool(QObject *parent = nullptr);
    ~FFmpegTool() override;

    bool available() const;
    QString executable() const;
    bool isRunning() const;

    void run(const QStringList &arguments, const QString &jobName = QString(),
             qint64 durationMs = 0);
    void cancel();

    static QStringList repairArguments(const QString &input, const QString &output);
    static QStringList speedArguments(const QString &input, const QString &output, double speed,
                                      bool hasAudio);
    static QStringList trimArguments(const QString &input, const QString &output,
                                     qint64 startMs, qint64 durationMs,
                                     const QRect &crop, bool hasAudio);
    static QStringList compressArguments(const QString &input, const QString &output,
                                         int crf, const QSize &scale, bool hasAudio);
    static QStringList gifArguments(const QString &input, const QString &output,
                                    qint64 startMs, qint64 durationMs, int fps, int width);
    static QStringList watermarkArguments(const QString &input, const QString &output,
                                          const WatermarkConfig &config,
                                          const QString &textFilePath);

signals:
    void started(const QString &jobName);
    void logLine(const QString &line);
    void progressChanged(int percent, qint64 processedMs);
    void finished(bool success, const QString &message);

private:
    void handleOutput();
    void handleFinished(int exitCode, QProcess::ExitStatus status);

    QProcess *m_process = nullptr;
    QString m_executable;
    QString m_jobName;
    qint64 m_durationMs = 0;
    QStringList m_logTail;
};

} // namespace lightrec
