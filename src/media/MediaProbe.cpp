#include "media/MediaProbe.h"
#include "media/FFmpegHeaders.h"

#include <QFileInfo>

namespace lightrec {

#ifdef LIGHTREC_HAVE_FFMPEG
namespace {
QString codecName(AVCodecID id)
{
    const char *name = avcodec_get_name(id);
    return name ? QString::fromUtf8(name) : QString();
}
} // namespace
#endif

MediaInfo MediaProbe::probe(const QString &filePath)
{
    MediaInfo info;
#ifdef LIGHTREC_HAVE_FFMPEG
    AVFormatContext *context = nullptr;
    const QByteArray path = filePath.toUtf8();
    int result = avformat_open_input(&context, path.constData(), nullptr, nullptr);
    if (result < 0) {
        char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, buffer, sizeof(buffer));
        info.error = QString::fromUtf8(buffer);
        return info;
    }

    result = avformat_find_stream_info(context, nullptr);
    if (result < 0) {
        info.error = QStringLiteral("无法读取媒体流信息");
        avformat_close_input(&context);
        return info;
    }

    info.valid = true;
    if (context->duration != AV_NOPTS_VALUE)
        info.durationMs = context->duration / (AV_TIME_BASE / 1000);

    for (unsigned int i = 0; i < context->nb_streams; ++i) {
        AVStream *stream = context->streams[i];
        if (!stream || !stream->codecpar)
            continue;
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && info.width == 0) {
            info.width = stream->codecpar->width;
            info.height = stream->codecpar->height;
            info.videoCodec = codecName(stream->codecpar->codec_id);
            if (stream->duration != AV_NOPTS_VALUE && info.durationMs == 0) {
                info.durationMs = av_rescale_q(stream->duration, stream->time_base,
                                               AVRational{ 1, 1000 });
            }
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (!info.hasAudio) {
                info.hasAudio = true;
                info.audioCodec = codecName(stream->codecpar->codec_id);
            }
            ++info.audioTracks;
        }
    }

    avformat_close_input(&context);
#else
    info.error = QStringLiteral("FFmpeg 未启用");
    Q_UNUSED(filePath)
#endif
    return info;
}

bool MediaProbe::isPlayable(const QString &filePath)
{
    return probe(filePath).valid;
}

RecordingFileInfo MediaProbe::fileInfo(const QString &filePath)
{
    RecordingFileInfo info;
    const QFileInfo file(filePath);
    info.path = file.absoluteFilePath();
    info.fileName = file.fileName();
    info.sizeBytes = file.size();
    info.created = file.lastModified();

    const MediaInfo media = probe(filePath);
    info.broken = !media.valid;
    info.durationMs = media.durationMs;
    info.width = media.width;
    info.height = media.height;
    info.codec = media.videoCodec;
    return info;
}

} // namespace lightrec
