#include "media/MediaPipeline.h"

#include "core/Logger.h"
#include "media/FFmpegHeaders.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QVector>

#include <algorithm>
#include <mutex>

namespace lightrec {

namespace {

#ifdef LIGHTREC_HAVE_FFMPEG
QString ffmpegError(int error)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(error, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

int evenDimension(int value)
{
    return std::max(2, value & ~1);
}
#endif

} // namespace

MediaPipeline::MediaPipeline(QObject *parent)
    : QObject(parent)
{
}

MediaPipeline::~MediaPipeline()
{
    abort();
}

#ifdef LIGHTREC_HAVE_FFMPEG

bool MediaPipeline::open(const Options &options, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (m_open) {
        if (error)
            *error = QStringLiteral("编码管线已经启动");
        return false;
    }

    m_options = options;
    m_finalPath = options.outputPath;
    m_partPath = m_finalPath.isEmpty() ? QString() : m_finalPath + QStringLiteral(".part");
    m_width = evenDimension(options.outputSize.width() > 0 ? options.outputSize.width() : 1920);
    m_height = evenDimension(options.outputSize.height() > 0 ? options.outputSize.height() : 1080);
    m_fps = qBound(1, options.video.fps, 120);
    m_streamActive = options.stream.enabled && !options.stream.fullUrl().isEmpty();
    if (m_streamActive) {
        static std::once_flag networkInit;
        std::call_once(networkInit, [] { avformat_network_init(); });
    }
    m_audioEnabled[0] = options.audio.systemEnabled;
    m_audioEnabled[1] = options.audio.micEnabled;

    if (m_finalPath.isEmpty() && !m_streamActive) {
        if (error)
            *error = QStringLiteral("未指定录像文件或推流地址");
        return false;
    }

    if (!m_partPath.isEmpty()) {
        QDir().mkpath(QFileInfo(m_partPath).absolutePath());
        QFile::remove(m_partPath);
    }

    if (m_streamActive && options.video.codec.compare(QStringLiteral("h265"), Qt::CaseInsensitive) == 0) {
        addWarning(QStringLiteral("RTMP/FLV 不支持 H.265，本次录制与推流统一使用 H.264"));
    }

    if (!openVideoEncoder(error)) {
        closeContexts();
        return false;
    }

    for (int track = 0; track < 2; ++track) {
        if (!m_audioEnabled[track])
            continue;
        if (!openAudioEncoder(track, error)) {
            addWarning(QStringLiteral("%1音频轨道初始化失败，已仅保留其他轨道")
                           .arg(track == 0 ? QStringLiteral("系统") : QStringLiteral("麦克风")));
            m_audioEnabled[track] = false;
        }
    }

    if (!openFile(error)) {
        closeContexts();
        return false;
    }

    if (!openStream(error)) {
        // Streaming failure must not prevent local recording.
        addWarning(QStringLiteral("推流连接失败：%1，本地录像继续").arg(error ? *error : QString()));
        if (m_streamContext) {
            if (m_streamContext->pb)
                avio_closep(&m_streamContext->pb);
            avformat_free_context(m_streamContext);
            m_streamContext = nullptr;
            m_streamVideoStream = nullptr;
            m_streamAudioStream[0] = m_streamAudioStream[1] = nullptr;
        }
        m_streamActive = false;
        if (error)
            error->clear();
    }

    if (!m_fileContext && !m_streamContext) {
        if (error)
            *error = QStringLiteral("无法创建录像文件或推流输出");
        closeContexts();
        return false;
    }

    m_open = true;
    qInfo() << "MediaPipeline opened:" << m_partPath << "encoder:" << m_usedEncoder
            << "size:" << m_width << "x" << m_height << "stream:" << m_streamActive;
    return true;
}

bool MediaPipeline::openVideoEncoder(QString *error)
{
    const QStringList candidates = encoderCandidates(m_streamActive);
    QStringList failures;

    for (const QString &name : candidates) {
        const AVCodec *codec = avcodec_find_encoder_by_name(name.toUtf8().constData());
        if (!codec) {
            failures << QStringLiteral("%1 (未编译)").arg(name);
            continue;
        }

        AVCodecContext *context = avcodec_alloc_context3(codec);
        if (!context)
            continue;

        const bool hardware = name.contains(QStringLiteral("nvenc"))
                              || name.contains(QStringLiteral("amf"))
                              || name.contains(QStringLiteral("qsv"))
                              || name.contains(QStringLiteral("_mf"));
        context->width = m_width;
        context->height = m_height;
        context->time_base = AVRational{ 1, 1000 };
        context->framerate = AVRational{ m_fps, 1 };
        context->pix_fmt = hardware ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        context->bit_rate = static_cast<int64_t>(m_options.video.bitrateKbps) * 1000;
        context->rc_max_rate = context->bit_rate;
        context->rc_buffer_size = static_cast<int>(context->bit_rate);
        context->gop_size = m_fps * 2;
        context->max_b_frames = 0;
        context->thread_count = 0;
        context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        applyEncoderOptions(context, name);

        const int result = avcodec_open2(context, codec, nullptr);
        if (result < 0) {
            failures << QStringLiteral("%1 (%2)").arg(name, ffmpegError(result));
            avcodec_free_context(&context);
            continue;
        }

        m_videoEncoder = context;
        m_usedEncoder = name;
        m_hardwareEncoder = hardware;
        if (!hardware && !candidates.isEmpty()
            && (candidates.first().contains(QStringLiteral("nvenc"))
                || candidates.first().contains(QStringLiteral("amf"))
                || candidates.first().contains(QStringLiteral("qsv")))) {
            addWarning(QStringLiteral("硬件编码不可用，已自动切换为软件编码（%1）").arg(name));
        }
        qInfo() << "Video encoder selected:" << name;
        return true;
    }

    if (error) {
        *error = QStringLiteral("没有可用的视频编码器：%1").arg(failures.join(QStringLiteral("; ")));
    }
    return false;
}

void MediaPipeline::applyEncoderOptions(AVCodecContext *context, const QString &name)
{
    if (!context->priv_data)
        return;
    if (name.contains(QStringLiteral("nvenc"))) {
        av_opt_set(context->priv_data, "preset", "p5", 0);
        av_opt_set(context->priv_data, "tune", "ull", 0);
        av_opt_set(context->priv_data, "rc", "cbr", 0);
    } else if (name.contains(QStringLiteral("amf"))) {
        av_opt_set(context->priv_data, "quality", "balanced", 0);
        av_opt_set(context->priv_data, "rc", "cbr", 0);
    } else if (name.contains(QStringLiteral("qsv"))) {
        av_opt_set(context->priv_data, "preset", "medium", 0);
    } else if (name == QStringLiteral("libx264")) {
        av_opt_set(context->priv_data, "preset", "veryfast", 0);
        av_opt_set(context->priv_data, "tune", "zerolatency", 0);
    } else if (name == QStringLiteral("libx265")) {
        av_opt_set(context->priv_data, "preset", "veryfast", 0);
        av_opt_set(context->priv_data, "x265-params", "log-level=error", 0);
    }
}

QStringList MediaPipeline::encoderCandidates(bool streaming) const
{
    QString codec = m_options.video.codec.toLower();
    if (codec == QStringLiteral("h265") || codec == QStringLiteral("hevc")) {
        if (streaming)
            return { QStringLiteral("libx264") };
        return { QStringLiteral("hevc_nvenc"), QStringLiteral("hevc_amf"),
                 QStringLiteral("hevc_qsv"), QStringLiteral("libx265"),
                 QStringLiteral("hevc_mf") };
    }
    return { QStringLiteral("h264_nvenc"), QStringLiteral("h264_amf"),
             QStringLiteral("h264_qsv"), QStringLiteral("libx264"),
             QStringLiteral("h264_mf") };
}

bool MediaPipeline::openAudioEncoder(int track, QString *error)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        if (error)
            *error = QStringLiteral("FFmpeg 未包含 AAC 编码器");
        return false;
    }

    AVCodecContext *context = avcodec_alloc_context3(codec);
    if (!context)
        return false;

    context->sample_rate = 48000;
    context->sample_fmt = AV_SAMPLE_FMT_FLTP;
    context->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    context->bit_rate = 128000;
    context->time_base = AVRational{ 1, 1000 };
    context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    const int result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("AAC 编码器初始化失败：%1").arg(ffmpegError(result));
        avcodec_free_context(&context);
        return false;
    }

    m_audioEncoder[track] = context;

    const int frameSize = context->frame_size > 0 ? context->frame_size : 1024;
    m_audioFifo[track] = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, frameSize);
    if (!m_audioFifo[track]) {
        if (error)
            *error = QStringLiteral("无法创建音频缓冲");
        avcodec_free_context(&m_audioEncoder[track]);
        return false;
    }

    m_audioFrame[track] = av_frame_alloc();
    if (!m_audioFrame[track])
        return false;
    m_audioFrame[track]->format = AV_SAMPLE_FMT_FLTP;
    m_audioFrame[track]->sample_rate = 48000;
    m_audioFrame[track]->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    m_audioFrame[track]->nb_samples = frameSize;
    if (av_frame_get_buffer(m_audioFrame[track], 0) < 0)
        return false;

    return true;
}

bool MediaPipeline::openFile(QString *error)
{
    if (m_partPath.isEmpty())
        return true;

    AVFormatContext *context = nullptr;
    int result = avformat_alloc_output_context2(&context, nullptr, "mp4",
                                                m_partPath.toUtf8().constData());
    if (result < 0 || !context) {
        if (error)
            *error = QStringLiteral("无法创建 MP4 输出");
        return false;
    }
    m_fileContext = context;

    m_videoStream = addVideoStream(context);
    if (m_audioEnabled[0]) {
        m_audioStream[0] = addAudioStream(context, 0);
        if (!m_audioStream[0]) {
            m_audioEnabled[0] = false;
            addWarning(QStringLiteral("系统声音轨道创建失败，已跳过该轨道"));
        }
    }
    if (m_audioEnabled[1]) {
        m_audioStream[1] = addAudioStream(context, 1);
        if (!m_audioStream[1]) {
            m_audioEnabled[1] = false;
            addWarning(QStringLiteral("麦克风轨道创建失败，已跳过该轨道"));
        }
    }

    if (!m_videoStream) {
        if (error)
            *error = QStringLiteral("无法创建视频流");
        return false;
    }

    result = avio_open(&context->pb, m_partPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("无法写入录像文件：%1").arg(ffmpegError(result));
        return false;
    }

    AVDictionary *options = nullptr;
    av_dict_set(&options, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    result = avformat_write_header(context, &options);
    av_dict_free(&options);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("写入 MP4 头失败：%1").arg(ffmpegError(result));
        return false;
    }
    return true;
}

bool MediaPipeline::openStream(QString *error)
{
    if (!m_streamActive)
        return true;

    const QByteArray url = m_options.stream.fullUrl().toUtf8();
    AVFormatContext *context = nullptr;
    int result = avformat_alloc_output_context2(&context, nullptr, "flv", url.constData());
    if (result < 0 || !context) {
        if (error)
            *error = QStringLiteral("无法创建 RTMP 输出");
        return false;
    }
    m_streamContext = context;

    m_streamVideoStream = addVideoStream(context);
    if (!m_streamVideoStream) {
        if (error)
            *error = QStringLiteral("无法创建 RTMP 视频流");
        return false;
    }
    // FLV/RTMP only carries one audio track.  Prefer the microphone (speech),
    // fall back to system audio; local recording keeps both tracks.
    const int streamAudioTrack = m_audioEnabled[1] ? 1 : (m_audioEnabled[0] ? 0 : -1);
    if (streamAudioTrack >= 0)
        m_streamAudioStream[streamAudioTrack] = addAudioStream(context, streamAudioTrack);
    if (m_audioEnabled[0] && m_audioEnabled[1])
        addWarning(QStringLiteral("RTMP 推流使用麦克风音轨，本地录像保留系统声音 + 麦克风双音轨"));

    result = avio_open(&context->pb, url.constData(), AVIO_FLAG_WRITE);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("无法连接 RTMP 服务器：%1").arg(ffmpegError(result));
        return false;
    }

    AVDictionary *options = nullptr;
    av_dict_set(&options, "flvflags", "no_duration_filesize", 0);
    result = avformat_write_header(context, &options);
    av_dict_free(&options);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("RTMP 握手失败：%1").arg(ffmpegError(result));
        return false;
    }
    return true;
}

AVStream *MediaPipeline::addVideoStream(AVFormatContext *context)
{
    if (!context || !m_videoEncoder)
        return nullptr;
    AVStream *stream = avformat_new_stream(context, nullptr);
    if (!stream)
        return nullptr;
    avcodec_parameters_from_context(stream->codecpar, m_videoEncoder);
    stream->time_base = AVRational{ 1, 1000 };
    stream->avg_frame_rate = AVRational{ m_fps, 1 };
    return stream;
}

AVStream *MediaPipeline::addAudioStream(AVFormatContext *context, int track)
{
    if (!context || !m_audioEncoder[track])
        return nullptr;
    AVStream *stream = avformat_new_stream(context, nullptr);
    if (!stream)
        return nullptr;
    avcodec_parameters_from_context(stream->codecpar, m_audioEncoder[track]);
    stream->time_base = AVRational{ 1, 1000 };
    return stream;
}

bool MediaPipeline::encodeVideo(const QImage &frame, qint64 ptsMs, QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_open || !m_videoEncoder) {
        if (error)
            *error = QStringLiteral("编码管线尚未启动");
        return false;
    }
    if (frame.isNull())
        return true;

    QImage input = frame;
    if (input.width() != m_width || input.height() != m_height) {
        input = input.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (input.format() != QImage::Format_ARGB32)
        input = input.convertToFormat(QImage::Format_ARGB32);

    m_sws = sws_getCachedContext(m_sws,
                                 input.width(), input.height(), AV_PIX_FMT_BGRA,
                                 m_width, m_height, m_videoEncoder->pix_fmt,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) {
        if (error)
            *error = QStringLiteral("无法创建图像转换器");
        return false;
    }

    if (!m_videoFrame) {
        m_videoFrame = av_frame_alloc();
        if (!m_videoFrame) {
            if (error)
                *error = QStringLiteral("无法分配视频帧");
            return false;
        }
        m_videoFrame->format = m_videoEncoder->pix_fmt;
        m_videoFrame->width = m_width;
        m_videoFrame->height = m_height;
        if (av_frame_get_buffer(m_videoFrame, 32) < 0) {
            if (error)
                *error = QStringLiteral("无法分配视频帧缓冲");
            return false;
        }
    }

    if (av_frame_make_writable(m_videoFrame) < 0) {
        if (error)
            *error = QStringLiteral("视频帧不可写");
        return false;
    }

    const uint8_t *sourceData[4] = { input.constBits(), nullptr, nullptr, nullptr };
    const int sourceStride[4] = { static_cast<int>(input.bytesPerLine()), 0, 0, 0 };
    sws_scale(m_sws, sourceData, sourceStride, 0, input.height(),
              m_videoFrame->data, m_videoFrame->linesize);

    if (ptsMs <= m_lastVideoPts)
        ptsMs = m_lastVideoPts + 1;
    m_videoFrame->pts = ptsMs;
    m_lastVideoPts = ptsMs;

    int result = avcodec_send_frame(m_videoEncoder, m_videoFrame);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("提交视频帧失败：%1").arg(ffmpegError(result));
        return false;
    }

    if (!m_packet)
        m_packet = av_packet_alloc();

    while ((result = avcodec_receive_packet(m_videoEncoder, m_packet)) >= 0) {
        if (!writePacket(m_fileContext, m_videoStream, m_videoEncoder, m_packet)) {
            av_packet_unref(m_packet);
            if (error)
                *error = QStringLiteral("写入视频数据失败");
            return false;
        }
        if (!writePacket(m_streamContext, m_streamVideoStream, m_videoEncoder, m_packet))
            disableStream(QStringLiteral("RTMP 推流中断，已继续本地录像"));
        av_packet_unref(m_packet);
    }

    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        if (error)
            *error = QStringLiteral("视频编码失败：%1").arg(ffmpegError(result));
        return false;
    }
    return true;
}

void MediaPipeline::pushAudio(int track, const QByteArray &data, const AudioFrameFormat &format, qint64 ptsMs)
{
    QMutexLocker locker(&m_mutex);
    if (!m_open || track < 0 || track > 1 || !m_audioEnabled[track] || !m_audioEncoder[track])
        return;
    if (data.isEmpty() || format.sampleRate <= 0 || format.channels <= 0)
        return;

    const int frames = static_cast<int>(data.size() / (sizeof(float) * format.channels));
    if (frames <= 0)
        return;

    if (!m_swr[track] || m_inputRate[track] != format.sampleRate || m_inputChannels[track] != format.channels) {
        if (m_swr[track])
            swr_free(&m_swr[track]);
        AVChannelLayout inLayout;
        av_channel_layout_default(&inLayout, format.channels);
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        const int result = swr_alloc_set_opts2(&m_swr[track], &outLayout, AV_SAMPLE_FMT_FLTP, 48000,
                                               &inLayout, AV_SAMPLE_FMT_FLT, format.sampleRate,
                                               0, nullptr);
        av_channel_layout_uninit(&inLayout);
        if (result < 0 || !m_swr[track] || swr_init(m_swr[track]) < 0) {
            addWarning(QStringLiteral("音频重采样器初始化失败"));
            return;
        }
        m_inputRate[track] = format.sampleRate;
        m_inputChannels[track] = format.channels;
        av_audio_fifo_reset(m_audioFifo[track]);
        m_audioStarted[track] = false;
        m_audioSamples[track] = 0;
    }

    const int outSamples = swr_get_out_samples(m_swr[track], frames);
    if (outSamples <= 0)
        return;

    if (m_audioConvertedCapacity[track] < outSamples) {
        if (m_audioConverted[track]) {
            av_freep(&m_audioConverted[track][0]);
            av_freep(&m_audioConverted[track]);
        }
        if (av_samples_alloc_array_and_samples(&m_audioConverted[track], nullptr, 2,
                                               outSamples, AV_SAMPLE_FMT_FLTP, 0) < 0) {
            m_audioConvertedCapacity[track] = 0;
            return;
        }
        m_audioConvertedCapacity[track] = outSamples;
    }

    const uint8_t *inputPlanes[1] = { reinterpret_cast<const uint8_t *>(data.constData()) };
    const int converted = swr_convert(m_swr[track], m_audioConverted[track], outSamples,
                                      inputPlanes, frames);
    if (converted <= 0)
        return;

    const bool fifoWasEmpty = av_audio_fifo_size(m_audioFifo[track]) == 0;
    if (fifoWasEmpty) {
        // QPC timestamps from some capture endpoints are not always in the
        // expected unit.  Keep the track monotonic and ignore absurd jumps.
        const qint64 continuation = m_audioBasePts[track]
                                    + av_rescale(m_audioSamples[track], 1000, 48000);
        if (ptsMs < 0 || ptsMs > 24LL * 3600 * 1000)
            ptsMs = continuation;
        if (m_audioStarted[track])
            ptsMs = std::max(ptsMs, continuation);
        m_audioBasePts[track] = std::max<qint64>(0, ptsMs);
        m_audioSamples[track] = 0;
        m_audioStarted[track] = true;
    }

    if (av_audio_fifo_write(m_audioFifo[track],
                            reinterpret_cast<void **>(m_audioConverted[track]), converted) < 0) {
        addWarning(QStringLiteral("音频缓冲写入失败"));
        return;
    }

    const int frameSize = m_audioEncoder[track]->frame_size > 0
                              ? m_audioEncoder[track]->frame_size : 1024;
    while (av_audio_fifo_size(m_audioFifo[track]) >= frameSize) {
        AVFrame *audioFrame = m_audioFrame[track];
        if (av_audio_fifo_read(m_audioFifo[track], reinterpret_cast<void **>(audioFrame->data),
                               frameSize) < frameSize) {
            break;
        }
        audioFrame->nb_samples = frameSize;
        audioFrame->pts = m_audioBasePts[track]
                          + av_rescale(m_audioSamples[track], 1000, 48000);
        m_audioSamples[track] += frameSize;
        QString ignored;
        encodeAudioFrame(track, audioFrame, &ignored);
    }
}

bool MediaPipeline::encodeAudioFrame(int track, AVFrame *frame, QString *error)
{
    if (!m_audioEncoder[track])
        return false;

    int result = avcodec_send_frame(m_audioEncoder[track], frame);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("提交音频帧失败：%1").arg(ffmpegError(result));
        return false;
    }

    if (!m_packet)
        m_packet = av_packet_alloc();

    while ((result = avcodec_receive_packet(m_audioEncoder[track], m_packet)) >= 0) {
        if (!writePacket(m_fileContext, m_audioStream[track], m_audioEncoder[track], m_packet)) {
            av_packet_unref(m_packet);
            if (error)
                *error = QStringLiteral("写入音频数据失败");
            return false;
        }
        if (!writePacket(m_streamContext, m_streamAudioStream[track], m_audioEncoder[track], m_packet))
            disableStream(QStringLiteral("RTMP 推流中断，已继续本地录像"));
        av_packet_unref(m_packet);
    }

    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        if (error)
            *error = QStringLiteral("音频编码失败：%1").arg(ffmpegError(result));
        return false;
    }
    return true;
}

bool MediaPipeline::writePacket(AVFormatContext *context, AVStream *stream,
                                AVCodecContext *encoder, AVPacket *packet)
{
    if (!context || !stream || !packet)
        return true;

    AVPacket *copy = av_packet_alloc();
    if (!copy)
        return false;
    if (av_packet_ref(copy, packet) < 0) {
        av_packet_free(&copy);
        return false;
    }

    av_packet_rescale_ts(copy, encoder->time_base, stream->time_base);
    copy->stream_index = stream->index;

    const int result = av_interleaved_write_frame(context, copy);
    av_packet_free(&copy);
    if (result < 0) {
        static int failures = 0;
        if (failures++ < 5) {
            qWarning() << "av_interleaved_write_frame failed" << result << ffmpegError(result)
                       << "stream" << stream->index << "pts" << packet->pts
                       << "dts" << packet->dts << "encTimeBase" << encoder->time_base.num
                       << encoder->time_base.den << "streamTimeBase" << stream->time_base.num
                       << stream->time_base.den << "size" << packet->size
                       << "flags" << packet->flags;
        }
        return false;
    }
    return true;
}

bool MediaPipeline::drainVideoEncoder(QString *error)
{
    if (!m_videoEncoder)
        return true;

    int result = avcodec_send_frame(m_videoEncoder, nullptr);
    if (result < 0 && result != AVERROR_EOF) {
        if (error)
            *error = QStringLiteral("刷新视频编码器失败：%1").arg(ffmpegError(result));
        return false;
    }

    if (!m_packet)
        m_packet = av_packet_alloc();

    while ((result = avcodec_receive_packet(m_videoEncoder, m_packet)) >= 0) {
        if (!writePacket(m_fileContext, m_videoStream, m_videoEncoder, m_packet)) {
            av_packet_unref(m_packet);
            if (error)
                *error = QStringLiteral("刷新视频编码器失败");
            return false;
        }
        if (!writePacket(m_streamContext, m_streamVideoStream, m_videoEncoder, m_packet))
            disableStream(QStringLiteral("RTMP 推流中断，已继续本地录像"));
        av_packet_unref(m_packet);
    }
    return true;
}

bool MediaPipeline::flushAudioFifo(int track, QString *error)
{
    if (!m_audioEncoder[track] || !m_audioFifo[track])
        return true;

    const int frameSize = m_audioEncoder[track]->frame_size > 0
                              ? m_audioEncoder[track]->frame_size : 1024;
    AVFrame *frame = m_audioFrame[track];

    while (av_audio_fifo_size(m_audioFifo[track]) > 0) {
        int available = av_audio_fifo_size(m_audioFifo[track]);
        if (available < frameSize) {
            const int padding = frameSize - available;
            uint8_t **silence = nullptr;
            int linesize = 0;
            if (av_samples_alloc_array_and_samples(&silence, &linesize, 2, padding,
                                                   AV_SAMPLE_FMT_FLTP, 0) < 0)
                break;
            av_samples_set_silence(silence, 0, padding, 2, AV_SAMPLE_FMT_FLTP);
            av_audio_fifo_write(m_audioFifo[track], reinterpret_cast<void **>(silence), padding);
            av_freep(&silence[0]);
            av_freep(&silence);
            available = frameSize;
        }

        const int read = av_audio_fifo_read(m_audioFifo[track],
                                            reinterpret_cast<void **>(frame->data),
                                            qMin(available, frameSize));
        if (read <= 0)
            break;
        frame->nb_samples = read;
        frame->pts = m_audioBasePts[track] + av_rescale(m_audioSamples[track], 1000, 48000);
        m_audioSamples[track] += read;
        if (!encodeAudioFrame(track, frame, error))
            return false;
    }
    return true;
}

bool MediaPipeline::drainAudioEncoder(int track, QString *error)
{
    if (!m_audioEncoder[track])
        return true;

    const int result = avcodec_send_frame(m_audioEncoder[track], nullptr);
    if (result < 0 && result != AVERROR_EOF)
        return true; // nothing left to flush

    if (!m_packet)
        m_packet = av_packet_alloc();
    int receive = 0;
    while ((receive = avcodec_receive_packet(m_audioEncoder[track], m_packet)) >= 0) {
        if (!writePacket(m_fileContext, m_audioStream[track], m_audioEncoder[track], m_packet)) {
            av_packet_unref(m_packet);
            if (error)
                *error = QStringLiteral("刷新音频编码器失败");
            return false;
        }
        if (!writePacket(m_streamContext, m_streamAudioStream[track], m_audioEncoder[track], m_packet))
            disableStream(QStringLiteral("RTMP 推流中断，已继续本地录像"));
        av_packet_unref(m_packet);
    }
    return true;
}

bool MediaPipeline::finish(QString *error)
{
    QMutexLocker locker(&m_mutex);
    if (!m_open) {
        if (error)
            *error = QStringLiteral("编码管线未启动");
        return false;
    }

    bool ok = true;
    if (!drainVideoEncoder(error))
        ok = false;

    for (int track = 0; track < 2; ++track) {
        if (!m_audioEnabled[track])
            continue;
        if (!flushAudioFifo(track, error))
            ok = false;
        if (!drainAudioEncoder(track, error))
            ok = false;
    }

    if (m_fileContext) {
        const int result = av_write_trailer(m_fileContext);
        if (result < 0 && ok) {
            if (error)
                *error = QStringLiteral("写入 MP4 尾部失败：%1").arg(ffmpegError(result));
            ok = false;
        }
        if (m_fileContext->pb)
            avio_closep(&m_fileContext->pb);
    }

    if (m_streamContext) {
        av_write_trailer(m_streamContext);
        if (m_streamContext->pb)
            avio_closep(&m_streamContext->pb);
    }

    closeContexts();
    m_open = false;

    if (ok && !m_partPath.isEmpty()) {
        if (!remuxToFinal(error))
            ok = false;
    }
    if (ok && m_finalPath.isEmpty() && !m_streamActive) {
        if (error)
            *error = QStringLiteral("推流已中断，且本次未生成本地录像");
        ok = false;
    }

    return ok;
}

bool MediaPipeline::remuxToFinal(QString *error)
{
    if (m_partPath.isEmpty() || m_finalPath.isEmpty())
        return true;
    if (!QFileInfo::exists(m_partPath)) {
        if (error)
            *error = QStringLiteral("录像缓存文件不存在");
        return false;
    }

    AVFormatContext *input = nullptr;
    int result = avformat_open_input(&input, m_partPath.toUtf8().constData(), nullptr, nullptr);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("无法打开录像缓存：%1").arg(ffmpegError(result));
        return false;
    }
    result = avformat_find_stream_info(input, nullptr);
    if (result < 0) {
        avformat_close_input(&input);
        if (error)
            *error = QStringLiteral("无法读取录像信息：%1").arg(ffmpegError(result));
        return false;
    }

    AVFormatContext *output = nullptr;
    result = avformat_alloc_output_context2(&output, nullptr, "mp4", m_finalPath.toUtf8().constData());
    if (result < 0 || !output) {
        avformat_close_input(&input);
        if (error)
            *error = QStringLiteral("无法创建最终 MP4 文件");
        return false;
    }

    QVector<int> streamMap(input->nb_streams, -1);
    for (unsigned int i = 0; i < input->nb_streams; ++i) {
        AVStream *inStream = input->streams[i];
        if (inStream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO
            && inStream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        AVStream *outStream = avformat_new_stream(output, nullptr);
        if (!outStream)
            continue;
        avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        outStream->codecpar->codec_tag = 0;
        outStream->time_base = inStream->time_base;
        streamMap[i] = outStream->index;
    }

    result = avio_open(&output->pb, m_finalPath.toUtf8().constData(), AVIO_FLAG_WRITE);
    if (result < 0) {
        avformat_free_context(output);
        avformat_close_input(&input);
        if (error)
            *error = QStringLiteral("无法写入最终文件：%1").arg(ffmpegError(result));
        return false;
    }

    AVDictionary *options = nullptr;
    av_dict_set(&options, "movflags", "+faststart", 0);
    result = avformat_write_header(output, &options);
    av_dict_free(&options);
    if (result < 0) {
        avio_closep(&output->pb);
        avformat_free_context(output);
        avformat_close_input(&input);
        if (error)
            *error = QStringLiteral("写入最终文件头失败：%1").arg(ffmpegError(result));
        return false;
    }

    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        av_write_trailer(output);
        avio_closep(&output->pb);
        avformat_free_context(output);
        avformat_close_input(&input);
        if (error)
            *error = QStringLiteral("无法分配转封装数据包");
        return false;
    }

    bool writeOk = true;
    while (av_read_frame(input, packet) >= 0) {
        if (packet->stream_index < 0
            || packet->stream_index >= static_cast<int>(streamMap.size())
            || streamMap[packet->stream_index] < 0) {
            av_packet_unref(packet);
            continue;
        }
        AVStream *inStream = input->streams[packet->stream_index];
        AVStream *outStream = output->streams[streamMap[packet->stream_index]];
        av_packet_rescale_ts(packet, inStream->time_base, outStream->time_base);
        packet->stream_index = outStream->index;
        if (av_interleaved_write_frame(output, packet) < 0)
            writeOk = false;
        av_packet_unref(packet);
        if (!writeOk)
            break;
    }
    av_packet_free(&packet);

    const int trailerResult = av_write_trailer(output);
    avio_closep(&output->pb);
    avformat_free_context(output);
    avformat_close_input(&input);

    if (!writeOk || trailerResult < 0
        || !QFileInfo::exists(m_finalPath) || QFileInfo(m_finalPath).size() <= 0) {
        QFile::remove(m_finalPath);
        if (error)
            *error = QStringLiteral("最终 MP4 写入不完整，已保留缓存文件");
        return false;
    }

    QFile::remove(m_partPath);
    qInfo() << "Recording finalized:" << m_finalPath;
    return true;
}

void MediaPipeline::closeContexts()
{
    if (m_videoEncoder)
        avcodec_free_context(&m_videoEncoder);
    for (int track = 0; track < 2; ++track) {
        if (m_audioEncoder[track])
            avcodec_free_context(&m_audioEncoder[track]);
        if (m_audioFrame[track])
            av_frame_free(&m_audioFrame[track]);
        if (m_audioFifo[track])
            av_audio_fifo_free(m_audioFifo[track]);
        m_audioFifo[track] = nullptr;
        if (m_swr[track])
            swr_free(&m_swr[track]);
        if (m_audioConverted[track]) {
            av_freep(&m_audioConverted[track][0]);
            av_freep(&m_audioConverted[track]);
        }
        m_audioConvertedCapacity[track] = 0;
    }

    if (m_videoFrame)
        av_frame_free(&m_videoFrame);
    if (m_packet)
        av_packet_free(&m_packet);
    if (m_sws)
        sws_freeContext(m_sws);
    m_sws = nullptr;

    if (m_fileContext) {
        if (m_fileContext->pb)
            avio_closep(&m_fileContext->pb);
        avformat_free_context(m_fileContext);
        m_fileContext = nullptr;
    }
    if (m_streamContext) {
        if (m_streamContext->pb)
            avio_closep(&m_streamContext->pb);
        avformat_free_context(m_streamContext);
        m_streamContext = nullptr;
    }

    m_videoStream = nullptr;
    m_streamVideoStream = nullptr;
    m_audioStream[0] = m_audioStream[1] = nullptr;
    m_streamAudioStream[0] = m_streamAudioStream[1] = nullptr;
    m_lastVideoPts = -1;
    m_audioStarted[0] = m_audioStarted[1] = false;
    m_audioSamples[0] = m_audioSamples[1] = 0;
    m_inputRate[0] = m_inputRate[1] = 0;
    m_inputChannels[0] = m_inputChannels[1] = 0;
}

void MediaPipeline::abort()
{
    QMutexLocker locker(&m_mutex);
    if (!m_open && !m_fileContext && !m_streamContext)
        return;

    if (m_open) {
        if (m_fileContext)
            av_write_trailer(m_fileContext);
        if (m_streamContext)
            av_write_trailer(m_streamContext);
    }
    closeContexts();
    m_open = false;
}

void MediaPipeline::addWarning(const QString &message)
{
    if (!m_warnings.contains(message)) {
        m_warnings.append(message);
        qWarning() << "MediaPipeline:" << message;
        emit warning(message);
    }
}

void MediaPipeline::disableStream(const QString &reason)
{
    if (!m_streamContext && !m_streamActive)
        return;
    m_streamActive = false;
    if (m_streamContext) {
        if (m_streamContext->pb)
            avio_closep(&m_streamContext->pb);
        avformat_free_context(m_streamContext);
        m_streamContext = nullptr;
    }
    m_streamVideoStream = nullptr;
    m_streamAudioStream[0] = m_streamAudioStream[1] = nullptr;
    addWarning(reason);
    emit streamInterrupted(reason);
}

#else // !LIGHTREC_HAVE_FFMPEG

bool MediaPipeline::open(const Options &, QString *error)
{
    if (error)
        *error = QStringLiteral("当前版本未启用 FFmpeg，无法录制。请参考 README 安装 FFmpeg SDK 后重新编译。");
    return false;
}

bool MediaPipeline::encodeVideo(const QImage &, qint64, QString *error)
{
    if (error)
        *error = QStringLiteral("FFmpeg 未启用");
    return false;
}

void MediaPipeline::pushAudio(int, const QByteArray &, const AudioFrameFormat &, qint64)
{
}

bool MediaPipeline::finish(QString *error)
{
    if (error)
        *error = QStringLiteral("FFmpeg 未启用");
    return false;
}

void MediaPipeline::abort()
{
}

#endif

} // namespace lightrec
