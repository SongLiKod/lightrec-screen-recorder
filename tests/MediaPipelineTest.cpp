#include "media/MediaPipeline.h"
#include "media/MediaProbe.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace lightrec;

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    QGuiApplication app(argc, argv);

    if (argc > 1) {
        const MediaInfo probe = MediaProbe::probe(QString::fromLocal8Bit(argv[1]));
        qWarning() << "probe valid:" << probe.valid << "duration:" << probe.durationMs
                   << "size:" << QFileInfo(QString::fromLocal8Bit(argv[1])).size()
                   << "video:" << probe.videoCodec << probe.width << "x" << probe.height
                   << "audio:" << probe.hasAudio << "tracks:" << probe.audioTracks;
        return probe.valid ? 0 : 1;
    }

    const QString path = QDir::temp().filePath(QStringLiteral("lightrec_media_test.mp4"));
    QFile::remove(path);

    MediaPipeline::Options options;
    options.outputPath = path;
    options.outputSize = QSize(640, 360);
    options.video.codec = QStringLiteral("h264");
    options.video.fps = 30;
    options.video.bitrateKbps = 2000;
    options.audio.systemEnabled = true;
    options.audio.micEnabled = true;

    MediaPipeline pipeline;
    QObject::connect(&pipeline, &MediaPipeline::warning, [](const QString &message) {
        qWarning() << "pipeline warning:" << message;
    });

    QString error;
    if (!pipeline.open(options, &error)) {
        qCritical() << "open failed:" << error;
        return 1;
    }

    const AudioFrameFormat format{ 48000, 2, true };
    const int frames = 90;
    const int samplesPerFrame = 48000 / 30;
    for (int i = 0; i < frames; ++i) {
        QImage image(640, 360, QImage::Format_ARGB32);
        image.fill(QColor(0x18, 0x20, 0x2C));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(QColor(0x2D, 0x8C, 0xF0));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRect(40 + i * 4, 120, 160, 120), 12, 12);
        painter.setPen(Qt::white);
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 24, QFont::Bold));
        painter.drawText(image.rect(), Qt::AlignCenter, QStringLiteral("LightRec 自检"));
        painter.end();

        if (!pipeline.encodeVideo(image, static_cast<qint64>(i) * 1000 / 30, &error)) {
            qCritical() << "encode video failed:" << error;
            return 2;
        }

        QByteArray audio(samplesPerFrame * 2 * static_cast<int>(sizeof(float)), Qt::Uninitialized);
        auto *samples = reinterpret_cast<float *>(audio.data());
        for (int frame = 0; frame < samplesPerFrame; ++frame) {
            const double t = (i * samplesPerFrame + frame) / 48000.0;
            const float value = static_cast<float>(std::sin(2.0 * M_PI * 440.0 * t) * 0.15);
            samples[frame * 2] = value;
            samples[frame * 2 + 1] = value;
        }
        const qint64 pts = static_cast<qint64>(i) * 1000 / 30;
        pipeline.pushAudio(0, audio, format, pts);
        pipeline.pushAudio(1, audio, format, pts);
    }

    if (!pipeline.finish(&error)) {
        qCritical() << "finish failed:" << error;
        return 3;
    }

    const MediaInfo info = MediaProbe::probe(path);
    qWarning() << "file:" << path;
    qWarning() << "valid:" << info.valid << "duration:" << info.durationMs
               << "size:" << QFileInfo(path).size() << "video:" << info.videoCodec
               << info.width << "x" << info.height << "audio:" << info.hasAudio
               << "tracks:" << info.audioTracks;

    const bool ok = info.valid && info.durationMs >= 2500 && info.width == 640
                    && info.height == 360 && info.hasAudio;
    return ok ? 0 : 4;
}
