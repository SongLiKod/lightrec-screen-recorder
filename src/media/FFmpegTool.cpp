#include "media/FFmpegTool.h"
#include "core/AppPaths.h"
#include "media/MediaProbe.h"

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTimer>

#include <windows.h>

namespace lightrec {

namespace {

QString filterPath(const QString &path)
{
    QString result = QDir::toNativeSeparators(path);
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    result.replace(QLatin1Char(':'), QStringLiteral("\\:"));
    result.replace(QLatin1Char('\''), QStringLiteral("\\'"));
    return result;
}

QString pickAnchor(const QString &start, const QString &center, const QString &end)
{
    switch (QRandomGenerator::global()->bounded(3)) {
    case 0:
        return start;
    case 1:
        return center;
    default:
        return end;
    }
}

QString overlayX(const QString &position, int margin)
{
    if (position == QStringLiteral("random"))
        return pickAnchor(QString::number(margin), QStringLiteral("(W-w)/2"),
                          QStringLiteral("W-w-%1").arg(margin));
    if (position == QStringLiteral("drift")) {
        return QStringLiteral("%1+(W-w-2*%1)*(0.5+0.5*sin(t*0.37+1.0))")
            .arg(margin);
    }
    if (position.contains(QStringLiteral("left")))
        return QString::number(margin);
    if (position.contains(QStringLiteral("center")))
        return QStringLiteral("(W-w)/2");
    return QStringLiteral("W-w-%1").arg(margin);
}

QString overlayY(const QString &position, int margin)
{
    if (position == QStringLiteral("random"))
        return pickAnchor(QString::number(margin), QStringLiteral("(H-h)/2"),
                          QStringLiteral("H-h-%1").arg(margin));
    if (position == QStringLiteral("drift")) {
        return QStringLiteral("%1+(H-h-2*%1)*(0.5+0.5*sin(t*0.53+2.0))")
            .arg(margin);
    }
    if (position.contains(QStringLiteral("top")))
        return QString::number(margin);
    if (position.contains(QStringLiteral("center")))
        return QStringLiteral("(H-h)/2");
    return QStringLiteral("H-h-%1").arg(margin);
}

QString textX(const QString &position, int margin)
{
    if (position == QStringLiteral("random"))
        return pickAnchor(QString::number(margin), QStringLiteral("(W-text_w)/2"),
                          QStringLiteral("W-text_w-%1").arg(margin));
    if (position == QStringLiteral("drift")) {
        return QStringLiteral("%1+(W-text_w-2*%1)*(0.5+0.5*sin(t*0.37+1.0))")
            .arg(margin);
    }
    if (position.contains(QStringLiteral("left")))
        return QString::number(margin);
    if (position.contains(QStringLiteral("center")))
        return QStringLiteral("(W-text_w)/2");
    return QStringLiteral("W-text_w-%1").arg(margin);
}

QString textY(const QString &position, int margin)
{
    if (position == QStringLiteral("random"))
        return pickAnchor(QString::number(margin), QStringLiteral("(H-text_h)/2"),
                          QStringLiteral("H-text_h-%1").arg(margin));
    if (position == QStringLiteral("drift")) {
        return QStringLiteral("%1+(H-text_h-2*%1)*(0.5+0.5*sin(t*0.53+2.0))")
            .arg(margin);
    }
    if (position.contains(QStringLiteral("top")))
        return QString::number(margin);
    if (position.contains(QStringLiteral("center")))
        return QStringLiteral("(H-text_h)/2");
    return QStringLiteral("H-text_h-%1").arg(margin);
}

QString fontFile()
{
    const QString yahei = QStringLiteral("C:/Windows/Fonts/msyh.ttc");
    if (QFileInfo::exists(yahei))
        return yahei;
    const QString arial = QStringLiteral("C:/Windows/Fonts/arial.ttf");
    if (QFileInfo::exists(arial))
        return arial;
    return QString();
}

} // namespace

FFmpegTool::FFmpegTool(QObject *parent)
    : QObject(parent)
{
    m_executable = AppPaths::ffmpegExe();
    if (!QFileInfo::exists(m_executable)) {
        const QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (!found.isEmpty())
            m_executable = found;
    }
}

FFmpegTool::~FFmpegTool()
{
    cancel();
}

bool FFmpegTool::available() const
{
    return QFileInfo::exists(m_executable);
}

QString FFmpegTool::executable() const
{
    return m_executable;
}

bool FFmpegTool::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void FFmpegTool::run(const QStringList &arguments, const QString &jobName, qint64 durationMs)
{
    cancel();

    if (!available()) {
        emit finished(false, QStringLiteral("未找到 ffmpeg.exe，请将其放到程序目录后重试"));
        return;
    }

    m_jobName = jobName;
    m_durationMs = durationMs;
    m_logTail.clear();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });

    connect(m_process, &QProcess::readyReadStandardOutput, this, &FFmpegTool::handleOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &FFmpegTool::handleOutput);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FFmpegTool::handleFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit finished(false, QStringLiteral("无法启动 ffmpeg.exe"));
    });

    QStringList fullArguments;
    fullArguments << QStringLiteral("-hide_banner") << QStringLiteral("-y")
                  << QStringLiteral("-nostdin") << QStringLiteral("-progress")
                  << QStringLiteral("pipe:1") << QStringLiteral("-nostats");
    fullArguments << arguments;

    emit started(jobName);
    emit logLine(QStringLiteral("ffmpeg ") + fullArguments.join(QLatin1Char(' ')));
    m_process->start(m_executable, fullArguments);
}

void FFmpegTool::cancel()
{
    if (!m_process)
        return;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    m_process->deleteLater();
    m_process = nullptr;
}

void FFmpegTool::handleOutput()
{
    if (!m_process)
        return;

    const QByteArray output = m_process->readAllStandardOutput() + m_process->readAllStandardError();
    const QString text = QString::fromLocal8Bit(output);
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QStringLiteral("out_time_us="))
            || line.startsWith(QStringLiteral("out_time_ms="))) {
            const qint64 microseconds = line.section(QLatin1Char('='), 1).toLongLong();
            const qint64 processedMs = microseconds / 1000;
            int percent = -1;
            if (m_durationMs > 0)
                percent = static_cast<int>(qBound<qint64>(0, processedMs * 100 / m_durationMs,
                                                         static_cast<qint64>(100)));
            emit progressChanged(percent, processedMs);
            continue;
        }
        if (line == QStringLiteral("progress=end")) {
            emit progressChanged(100, m_durationMs);
            continue;
        }
        if (line.startsWith(QStringLiteral("frame=")) || line.startsWith(QStringLiteral("bitrate="))
            || line.startsWith(QStringLiteral("speed=")) || line.startsWith(QStringLiteral("progress="))
            || line.startsWith(QStringLiteral("out_time=")) || line.startsWith(QStringLiteral("dup_frames="))
            || line.startsWith(QStringLiteral("drop_frames=")) || line.startsWith(QStringLiteral("fps=")))
            continue;

        m_logTail.append(line);
        while (m_logTail.size() > 40)
            m_logTail.removeFirst();
        emit logLine(line);
    }
}

void FFmpegTool::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;

    handleOutput();
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    QString message;
    if (success) {
        message = QStringLiteral("处理完成");
        emit progressChanged(100, m_durationMs);
    } else {
        message = m_logTail.isEmpty()
                      ? QStringLiteral("处理失败（退出码 %1）").arg(exitCode)
                      : QStringLiteral("处理失败：%1").arg(m_logTail.last());
    }

    m_process->deleteLater();
    m_process = nullptr;
    emit finished(success, message);
}

QStringList FFmpegTool::repairArguments(const QString &input, const QString &output)
{
    return {
        QStringLiteral("-err_detect"), QStringLiteral("ignore_err"),
        QStringLiteral("-fflags"), QStringLiteral("+genpts+discardcorrupt"),
        QStringLiteral("-i"), input,
        QStringLiteral("-map"), QStringLiteral("0:v?"),
        QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-c"), QStringLiteral("copy"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        output
    };
}

QStringList FFmpegTool::speedArguments(const QString &input, const QString &output, double speed,
                                       bool hasAudio)
{
    const double clamped = qBound(0.5, speed, 2.0);
    QStringList arguments;
    arguments << QStringLiteral("-i") << input;

    if (hasAudio) {
        arguments << QStringLiteral("-filter_complex")
                  << QStringLiteral("[0:v]setpts=PTS/%1[v];[0:a]atempo=%1[a]")
                         .arg(clamped, 0, 'f', 3)
                  << QStringLiteral("-map") << QStringLiteral("[v]")
                  << QStringLiteral("-map") << QStringLiteral("[a]")
                  << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("192k");
    } else {
        arguments << QStringLiteral("-vf")
                  << QStringLiteral("setpts=PTS/%1").arg(clamped, 0, 'f', 3)
                  << QStringLiteral("-an");
    }

    arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("veryfast")
              << QStringLiteral("-crf") << QStringLiteral("20")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << output;
    return arguments;
}

QStringList FFmpegTool::trimArguments(const QString &input, const QString &output,
                                      qint64 startMs, qint64 durationMs,
                                      const QRect &crop, bool hasAudio)
{
    QStringList arguments;
    if (startMs > 0)
        arguments << QStringLiteral("-ss") << QString::number(startMs / 1000.0, 'f', 3);
    arguments << QStringLiteral("-i") << input;
    if (durationMs > 0)
        arguments << QStringLiteral("-t") << QString::number(durationMs / 1000.0, 'f', 3);

    if (crop.isValid() && crop.width() >= 2 && crop.height() >= 2) {
        arguments << QStringLiteral("-vf")
                  << QStringLiteral("crop=%1:%2:%3:%4")
                         .arg(crop.width() & ~1)
                         .arg(crop.height() & ~1)
                         .arg(crop.x())
                         .arg(crop.y());
    }

    arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("veryfast")
              << QStringLiteral("-crf") << QStringLiteral("20")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (hasAudio)
        arguments << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("192k");
    else
        arguments << QStringLiteral("-an");

    arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart") << output;
    return arguments;
}

QStringList FFmpegTool::compressArguments(const QString &input, const QString &output,
                                          int crf, const QSize &scale, bool hasAudio)
{
    QStringList arguments;
    arguments << QStringLiteral("-i") << input;

    QStringList filters;
    if (scale.isValid() && scale.width() >= 2)
        filters << QStringLiteral("scale=%1:-2").arg(scale.width() & ~1);
    if (!filters.isEmpty())
        arguments << QStringLiteral("-vf") << filters.join(QLatin1Char(','));

    arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("medium")
              << QStringLiteral("-crf") << QString::number(qBound(18, crf, 34))
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (hasAudio)
        arguments << QStringLiteral("-c:a") << QStringLiteral("aac")
                  << QStringLiteral("-b:a") << QStringLiteral("128k");
    else
        arguments << QStringLiteral("-an");

    arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart") << output;
    return arguments;
}

QStringList FFmpegTool::gifArguments(const QString &input, const QString &output,
                                     qint64 startMs, qint64 durationMs, int fps, int width)
{
    QStringList arguments;
    if (startMs > 0)
        arguments << QStringLiteral("-ss") << QString::number(startMs / 1000.0, 'f', 3);
    arguments << QStringLiteral("-i") << input;
    if (durationMs > 0)
        arguments << QStringLiteral("-t") << QString::number(durationMs / 1000.0, 'f', 3);

    arguments << QStringLiteral("-filter_complex")
              << QStringLiteral("fps=%1,scale=%2:-1:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer")
                     .arg(qBound(5, fps, 30))
                     .arg(qBound(80, width, 1920))
              << QStringLiteral("-loop") << QStringLiteral("0")
              << QStringLiteral("-an")
              << output;
    return arguments;
}

QStringList FFmpegTool::watermarkArguments(const QString &input, const QString &output,
                                           const WatermarkConfig &config,
                                           const QString &textFilePath)
{
    QStringList arguments;
    arguments << QStringLiteral("-i") << input;

    const QString alpha = QString::number(qBound(0, config.opacity, 100) / 100.0, 'f', 2);
    const QString x = overlayX(config.position, config.margin);
    const QString y = overlayY(config.position, config.margin);

    if (!config.imagePath.isEmpty() && QFileInfo::exists(config.imagePath)) {
        arguments << QStringLiteral("-i") << config.imagePath;
        arguments << QStringLiteral("-filter_complex")
                  << QStringLiteral("[1:v]format=rgba,colorchannelmixer=aa=%1,scale=trunc(iw*%2/2)*2:-2[wm];[0:v][wm]overlay=%3:%4[v]")
                         .arg(alpha)
                         .arg(qBound(0.02, config.scale, 1.0), 0, 'f', 3)
                         .arg(x, y)
                  << QStringLiteral("-map") << QStringLiteral("[v]")
                  << QStringLiteral("-map") << QStringLiteral("0:a?");
    } else {
        QString drawText = QStringLiteral("drawtext=textfile='%1'").arg(filterPath(textFilePath));
        const QString font = fontFile();
        if (!font.isEmpty())
            drawText += QStringLiteral(":fontfile='%1'").arg(filterPath(font));
        drawText += QStringLiteral(":fontcolor=0x%1@%2:fontsize=%3:x=%4:y=%5")
                        .arg(config.color.name(QColor::HexRgb).mid(1), alpha)
                        .arg(config.fontSize)
                        .arg(textX(config.position, config.margin),
                             textY(config.position, config.margin));
        arguments << QStringLiteral("-vf") << drawText;
    }

    arguments << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("veryfast")
              << QStringLiteral("-crf") << QStringLiteral("20")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
              << QStringLiteral("-c:a") << QStringLiteral("aac")
              << QStringLiteral("-b:a") << QStringLiteral("192k")
              << QStringLiteral("-movflags") << QStringLiteral("+faststart")
              << output;
    return arguments;
}

} // namespace lightrec
