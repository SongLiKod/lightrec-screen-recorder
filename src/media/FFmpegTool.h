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
    // 按音轨导出：system/mic 表示是否把该音轨写入输出，index 为文件内的音轨序号
    // （0 = 第一条音频流）。同时选择两条时混合成一条（系统 100% + 麦克风 -6 dB），
    // 只选一条时直接挑选该音轨。两种情况视频流都直接复制，不重新编码。
    // faststart=false 供本机预览缓存使用，省去 moov 重排的整文件二次写入。
    static QStringList audioExportArguments(const QString &input, const QString &output,
                                            bool system, bool mic,
                                            int systemIndex, int micIndex,
                                            bool faststart = true);

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
