#include "media/MediaProbe.h"
#include "media/FFmpegHeaders.h"

#include <QFileInfo>
#include <QVector>

namespace lightrec {

#ifdef LIGHTREC_HAVE_FFMPEG
namespace {
QString codecName(AVCodecID id)
{
    const char *name = avcodec_get_name(id);
    return name ? QString::fromUtf8(name) : QString();
}

QString streamTitle(AVStream *stream)
{
    if (!stream)
        return QString();
    const AVDictionaryEntry *entry = av_dict_get(stream->metadata, "title", nullptr, 0);
    return entry && entry->value ? QString::fromUtf8(entry->value) : QString();
}

// 累计左右声道的能量差，用于判断是否为双单声道（麦克风常见）或真立体声（系统环回常见）。
void accumulateChannelDelta(const AVFrame *frame, double &diff, double &total)
{
    if (!frame || frame->nb_samples <= 0 || frame->ch_layout.nb_channels < 2)
        return;
    const int samples = frame->nb_samples;
    switch (frame->format) {
    case AV_SAMPLE_FMT_FLTP: {
        const auto *left = reinterpret_cast<const float *>(frame->data[0]);
        const auto *right = reinterpret_cast<const float *>(frame->data[1]);
        for (int i = 0; i < samples; ++i) {
            const double delta = static_cast<double>(left[i]) - right[i];
            diff += delta * delta;
            total += static_cast<double>(left[i]) * left[i] + static_cast<double>(right[i]) * right[i];
        }
        break;
    }
    case AV_SAMPLE_FMT_FLT: {
        const auto *data = reinterpret_cast<const float *>(frame->data[0]);
        for (int i = 0; i < samples; ++i) {
            const double l = data[i * 2];
            const double r = data[i * 2 + 1];
            const double delta = l - r;
            diff += delta * delta;
            total += l * l + r * r;
        }
        break;
    }
    case AV_SAMPLE_FMT_S16P: {
        const auto *left = reinterpret_cast<const qint16 *>(frame->data[0]);
        const auto *right = reinterpret_cast<const qint16 *>(frame->data[1]);
        for (int i = 0; i < samples; ++i) {
            const double l = left[i] / 32768.0;
            const double r = right[i] / 32768.0;
            const double delta = l - r;
            diff += delta * delta;
            total += l * l + r * r;
        }
        break;
    }
    case AV_SAMPLE_FMT_S16: {
        const auto *data = reinterpret_cast<const qint16 *>(frame->data[0]);
        for (int i = 0; i < samples; ++i) {
            const double l = data[i * 2] / 32768.0;
            const double r = data[i * 2 + 1] / 32768.0;
            const double delta = l - r;
            diff += delta * delta;
            total += l * l + r * r;
        }
        break;
    }
    default:
        break;
    }
}

// 解码开头约 0.5 秒判断音轨是否为双单声道。左右声道几乎完全一致时视为麦克风。
bool looksDualMono(const QString &filePath, int streamIndex)
{
    AVFormatContext *context = nullptr;
    const QByteArray path = filePath.toUtf8();
    if (avformat_open_input(&context, path.constData(), nullptr, nullptr) < 0)
        return false;

    bool dualMono = false;
    if (avformat_find_stream_info(context, nullptr) >= 0 && streamIndex >= 0
        && streamIndex < static_cast<int>(context->nb_streams)) {
        AVStream *stream = context->streams[streamIndex];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *decoder = codec ? avcodec_alloc_context3(codec) : nullptr;
        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();

        if (decoder && packet && frame && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
            && avcodec_parameters_to_context(decoder, stream->codecpar) >= 0
            && avcodec_open2(decoder, codec, nullptr) >= 0) {
            if (decoder->ch_layout.nb_channels < 2) {
                dualMono = true; // 单声道只可能来自麦克风
            } else {
                double diff = 0.0;
                double total = 0.0;
                qint64 samples = 0;
                const qint64 target = decoder->sample_rate > 0 ? decoder->sample_rate / 2 : 24000;
                auto drain = [&] {
                    while (avcodec_receive_frame(decoder, frame) == 0) {
                        accumulateChannelDelta(frame, diff, total);
                        samples += frame->nb_samples;
                        av_frame_unref(frame);
                    }
                };
                while (samples < target && av_read_frame(context, packet) >= 0) {
                    if (packet->stream_index == streamIndex
                        && avcodec_send_packet(decoder, packet) == 0) {
                        drain();
                    }
                    av_packet_unref(packet);
                    if (total > 0 && diff / total > 1e-4)
                        break; // 已经可以判定为真立体声
                }
                avcodec_send_packet(decoder, nullptr);
                drain();
                dualMono = total > 0 && diff / total < 1e-4;
            }
        }

        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
    }
    avformat_close_input(&context);
    return dualMono;
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

    QVector<int> audioStreamIndices; // 全局流下标，按音轨顺序
    QVector<int> unidentifiedAudio;
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
            const int audioIndex = info.audioTracks;
            if (!info.hasAudio) {
                info.hasAudio = true;
                info.audioCodec = codecName(stream->codecpar->codec_id);
            }
            ++info.audioTracks;
            audioStreamIndices.append(static_cast<int>(i));

            const QString title = streamTitle(stream);
            const QString lower = title.toLower();
            if (info.micAudioIndex < 0
                && (lower.contains(QStringLiteral("麦克风")) || lower.contains(QStringLiteral("mic")))) {
                info.micAudioIndex = audioIndex;
            } else if (info.systemAudioIndex < 0
                       && (lower.contains(QStringLiteral("系统"))
                           || lower.contains(QStringLiteral("system")))) {
                info.systemAudioIndex = audioIndex;
            } else {
                unidentifiedAudio.append(audioIndex);
            }
        }
    }

    // LightRec 的写入约定：没有标题时第一条音轨是系统声音，第二条是麦克风。
    for (int audioIndex : unidentifiedAudio) {
        if (info.systemAudioIndex < 0)
            info.systemAudioIndex = audioIndex;
        else if (info.micAudioIndex < 0)
            info.micAudioIndex = audioIndex;
    }

    // 只有一条且没有标题的旧录像：用声道一致性区分麦克风（双单声道）与系统声音（立体声）。
    // 有标题的音轨以标题为准，不参与启发式判断。
    if (info.audioTracks == 1 && audioStreamIndices.size() == 1
        && unidentifiedAudio.size() == 1 && looksDualMono(filePath, audioStreamIndices.first())) {
        info.systemAudioIndex = -1;
        info.micAudioIndex = 0;
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
