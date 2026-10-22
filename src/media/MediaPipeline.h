#pragma once

#include "audio/WasapiCapture.h"
#include "core/Types.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

struct AVFormatContext;
struct AVStream;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct AVAudioFifo;
struct SwsContext;
struct SwrContext;

namespace lightrec {

/**
 * Real-time FFmpeg pipeline.
 *
 * Encodes one video stream and up to two independent audio tracks, writes a
 * crash-safe fragmented MP4 and (optionally) mirrors the same packets to an
 * RTMP endpoint.  Hardware encoders are probed in order and the pipeline falls
 * back to software encoding when the GPU encoder cannot be opened.
 */
class MediaPipeline : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString outputPath;      // empty when streaming only
        QSize outputSize;
        VideoConfig video;
        AudioConfig audio;
        StreamConfig stream;
    };

    explicit MediaPipeline(QObject *parent = nullptr);
    ~MediaPipeline() override;

    bool open(const Options &options, QString *error);
    bool encodeVideo(const QImage &frame, qint64 ptsMs, QString *error);
    void pushAudio(int track, const QByteArray &data, const AudioFrameFormat &format, qint64 ptsMs);
    bool finish(QString *error);
    void abort();

    bool isOpen() const { return m_open; }
    QString usedEncoder() const { return m_usedEncoder; }
    QStringList warnings() const { return m_warnings; }
    QString finalPath() const { return m_finalPath; }
    QString partPath() const { return m_partPath; }

signals:
    void warning(const QString &message);

private:
#ifdef LIGHTREC_HAVE_FFMPEG
    bool openFile(QString *error);
    bool openStream(QString *error);
    bool openVideoEncoder(QString *error);
    bool openAudioEncoder(int track, QString *error);
    bool writeHeader(AVFormatContext *context, QString *error);
    bool writePacket(AVFormatContext *context, AVStream *stream,
                     AVCodecContext *encoder, AVPacket *packet);
    bool encodeAudioFrame(int track, AVFrame *frame, QString *error);
    bool drainVideoEncoder(QString *error);
    bool drainAudioEncoder(int track, QString *error);
    bool flushAudioFifo(int track, QString *error);
    bool remuxToFinal(QString *error);
    void closeContexts();
    void addWarning(const QString &message);
    AVStream *addVideoStream(AVFormatContext *context);
    AVStream *addAudioStream(AVFormatContext *context, int track);
    void applyEncoderOptions(AVCodecContext *context, const QString &name);
    QStringList encoderCandidates(bool streaming) const;
#endif

    Options m_options;
    QString m_finalPath;
    QString m_partPath;
    QString m_usedEncoder;
    QStringList m_warnings;
    bool m_open = false;
    bool m_streamActive = false;
    bool m_audioEnabled[2] = { false, false };
    bool m_audioStarted[2] = { false, false };
    qint64 m_audioBasePts[2] = { 0, 0 };
    qint64 m_audioSamples[2] = { 0, 0 };
    qint64 m_lastVideoPts = -1;
    int m_inputRate[2] = { 0, 0 };
    int m_inputChannels[2] = { 0, 0 };
    int m_width = 0;
    int m_height = 0;
    int m_fps = 30;
    bool m_hardwareEncoder = false;
    QString m_requestedEncoder;
    QMutex m_mutex;

#ifdef LIGHTREC_HAVE_FFMPEG
    AVFormatContext *m_fileContext = nullptr;
    AVFormatContext *m_streamContext = nullptr;
    AVStream *m_videoStream = nullptr;
    AVStream *m_streamVideoStream = nullptr;
    AVStream *m_audioStream[2] = { nullptr, nullptr };
    AVStream *m_streamAudioStream[2] = { nullptr, nullptr };
    AVCodecContext *m_videoEncoder = nullptr;
    AVCodecContext *m_audioEncoder[2] = { nullptr, nullptr };
    AVFrame *m_videoFrame = nullptr;
    AVFrame *m_audioFrame[2] = { nullptr, nullptr };
    AVPacket *m_packet = nullptr;
    SwsContext *m_sws = nullptr;
    SwrContext *m_swr[2] = { nullptr, nullptr };
    AVAudioFifo *m_audioFifo[2] = { nullptr, nullptr };
    uint8_t **m_audioConverted[2] = { nullptr, nullptr };
    int m_audioConvertedCapacity[2] = { 0, 0 };
#endif
};

} // namespace lightrec
