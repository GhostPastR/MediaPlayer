#include "audiodecoder.h"

#include "packetqueue.h"
#include "mediaqueue.h"

#include "audiodevice.h"
#include "mediaclock.h"

#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

AudioDecoder::AudioDecoder(AVFormatContext *formatContext, std::shared_ptr<PacketQueue> queue,
                           std::shared_ptr<MediaClock> mediaClock, QObject *parent) : QThread(parent)
{
    formatContext_ = formatContext;
    queue_ = queue;
    mediaClock_ = mediaClock;
    frame_ = av_frame_alloc();
}

AudioDecoder::~AudioDecoder()
{
    freeFilterGraph();

    if(frame_){
        av_frame_free(&frame_);
    }
    if(context_.codec){
        avcodec_free_context(&context_.codec);
    }
}

void AudioDecoder::pause()
{
    pausePlayback();
    if(audioDevice_){
        audioDevice_->setPause(true);
    }
}

void AudioDecoder::resume()
{
    resumePlayback();
    if(audioDevice_){
        audioDevice_->setPause(false);
    }
}

float AudioDecoder::volume() const
{
    if(audioDevice_){
        return audioDevice_->volume();
    }
    return {};
}

void AudioDecoder::setVolume(float volume)
{
    if(audioDevice_){
        audioDevice_->setVolume(volume);
    }
}

void AudioDecoder::stop()
{
    stopPlayback();
    queue_->abort();
}

void AudioDecoder::seekFlush(double targetSec)
{
    flushRequested_ = true;
    queue_->clear();
    if(audioDevice_){
        audioDevice_->clearPcmRb();
    }
    seekTargetSec_ = targetSec;
}

void AudioDecoder::setSpeed(double speed)
{
    if (speed < 1.0){
        speed = 1.0;
    }
    qDebug() << "[AudioDecoder] setSpeed:" << speed;
    speed_ = speed;
    speedChanged_ = true;
}

void AudioDecoder::run()
{
    decoder();
}

std::optional<CodecFormat> AudioDecoder::createContext(int audioIndex)
{
    if(formatContext_){
        AVStream *stream = formatContext_->streams[audioIndex];
        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            qDebug() << "Video decoder not found";
            return {};
        }
        AVCodecContext *codec = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(codec, stream->codecpar);
        if (avcodec_open2(codec, decoder, nullptr) < 0) {
            avcodec_free_context(&codec);
            return {};
        }
        context_ = {codec, stream};
        CodecFormat codecFormat;
        codecFormat.rate = codec->sample_rate;
        codecFormat.channels = codec->channel_layout;
        codecFormat.sample_size = 16;
        return codecFormat;
    }
    return {};
}

std::optional<std::pair<SwrContext*, SwrContext*>> AudioDecoder::createInOutSwrContext()
{
    // декодер -> FLT (для atempo)
    auto swrCtxIn = swr_alloc_set_opts(nullptr,
                                        AV_CH_LAYOUT_STEREO,       // out layout (маска)
                                        AV_SAMPLE_FMT_FLT,         // out format
                                        context_.codec->sample_rate,
                                        context_.codec->channel_layout, // in layout (маска)
                                        context_.codec->sample_fmt,
                                        context_.codec->sample_rate,
                                        0, nullptr);
    if (swr_init(swrCtxIn) < 0) {
        qDebug() << "[AudioDecoder] swr_init(in) failed";
        return {};
    }
    // FLT -> S16
    auto swrCtxOut = swr_alloc_set_opts(nullptr,
                                        AV_CH_LAYOUT_STEREO,
                                        AV_SAMPLE_FMT_S16,
                                        context_.codec->sample_rate,
                                        AV_CH_LAYOUT_STEREO,       // FLT стерео
                                        AV_SAMPLE_FMT_FLT,
                                        context_.codec->sample_rate,
                                        0, nullptr);
    if (swr_init(swrCtxOut) < 0) {
        qDebug() << "[AudioDecoder] swr_init(out) failed";
        swr_free(&swrCtxIn);
        return {};
    }
    return {{swrCtxIn, swrCtxOut}};
}

void AudioDecoder::freeFilterGraph()
{
    if (filterGraph_) {
        avfilter_graph_free(&filterGraph_);
        filterGraph_ = nullptr;
        bufferSrcCtx_ = nullptr;
        bufferSinkCtx_ = nullptr;
    }
}

bool AudioDecoder::initFilterGraph(double speed)
{
    freeFilterGraph();

    filterGraph_ = avfilter_graph_alloc();
    if (!filterGraph_) return false;

    // abuffer: вход должен быть FLT (interleaved float)
    const AVFilter *bufferSrc = avfilter_get_by_name("abuffer");
    char args[512];
    snprintf(args, sizeof(args),
             "time_base=1/%d:sample_rate=%d:sample_fmt=flt:channel_layout=stereo",
             context_.codec->sample_rate, context_.codec->sample_rate);

//    snprintf(args, sizeof(args),
//             "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=stereo",
//             context_.codec->sample_rate, context_.codec->sample_rate,
//             av_get_sample_fmt_name(context_.codec->sample_fmt));

    if (avfilter_graph_create_filter(&bufferSrcCtx_, bufferSrc, "in",
                                     args, nullptr, filterGraph_) < 0) {
        qDebug() << "[AudioDecoder] create abuffer failed";
        return false;
    }

    // abuffersink
    const AVFilter *bufferSink = avfilter_get_by_name("abuffersink");
    if (avfilter_graph_create_filter(&bufferSinkCtx_, bufferSink, "out",
                                     nullptr, nullptr, filterGraph_) < 0) {
        qDebug() << "[AudioDecoder] create abuffersink failed";
        return false;
    }

    // Цепочка atempo: для >2x — несколько фильтров
    QString filterDesc;
    double s = speed;
    if (s <= 2.0) {
        filterDesc = QString("atempo=%1").arg(s);
    } else {
        QStringList parts;
        while (s > 2.0) {
            parts << "atempo=2.0";
            s /= 2.0;
        }
        if (s > 1.0){
            parts << QString("atempo=%1").arg(s);
        }
        filterDesc = parts.join(",");
    }
    qDebug() << "[AudioDecoder] filter graph:" << filterDesc;

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = bufferSrcCtx_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = bufferSinkCtx_;
    inputs->pad_idx    = 0;
    inputs->next       = nullptr;

    if (avfilter_graph_parse_ptr(filterGraph_,
                                 filterDesc.toUtf8().constData(),
                                 &inputs, &outputs, nullptr) < 0) {
        qDebug() << "[AudioDecoder] parse filter graph failed";
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return false;
    }

    if (avfilter_graph_config(filterGraph_, nullptr) < 0) {
        qDebug() << "[AudioDecoder] config filter graph failed";
        return false;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    return true;
}

void AudioDecoder::decoder()
{
//    if (!createSwrContext()) {
//        qDebug() << "[AudioDecoder] createSwrContext failed";
//        return;
//    }
    auto swrContext = createInOutSwrContext();
    if(!swrContext.has_value()){
        qDebug() << "[AudioDecoder] createSwrContext failed";
        return;
    }

    auto [swrCtxIn, swrCtxOut] = swrContext.value();

    if (!initFilterGraph(speed_.load())) {
        qDebug() << "[AudioDecoder] initFilterGraph failed (initial)";
        swr_free(&swrCtxIn);
        swr_free(&swrCtxOut);
        return;
    }

    audioDevice_ = std::make_unique<AudioDevice>(context_.codec->sample_rate);
    AVFrame *filteredFrame = av_frame_alloc();
    AVFrame *fltFrame = av_frame_alloc();

    int64_t ptsOffset{};
    bool firstFrameAfterSeek{};
    while (!stopRequested_) {
        waitPaused();

        if (speedChanged_.exchange(false)) {
            if (!initFilterGraph(speed_.load())) {
                qDebug() << "[AudioDecoder] initFilterGraph failed (speed change)";
            }
        }

        if (flushRequested_) {
            avcodec_flush_buffers(context_.codec);
            freeFilterGraph();
            initFilterGraph(speed_.load());
            ptsOffset = 0;
            firstFrameAfterSeek = true;
            flushRequested_ = false;
            continue;
        }

        AVPacket *packet = queue_->pop();
        if (!packet){
            if (mediaClock_){
                mediaClock_->setAudioEnded(true);
            }
            while (audioDevice_->notEmptyPcmRb() && !stopRequested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            break;
        }

        avcodec_send_packet(context_.codec, packet);
        av_packet_free(&packet);

        while (avcodec_receive_frame(context_.codec, frame_) == 0 && !stopRequested_) {
            // 1) декодер -> FLT
            int outSamples = swr_get_out_samples(swrCtxIn, frame_->nb_samples);

            // НОВОЕ: переиспользуем fltFrame
            av_frame_unref(fltFrame);
            fltFrame->format = AV_SAMPLE_FMT_FLT;
            fltFrame->sample_rate = context_.codec->sample_rate;
            fltFrame->channels = context_.codec->channels;
            fltFrame->nb_samples = outSamples;
            av_frame_get_buffer(fltFrame, 0);

            swr_convert(swrCtxIn, fltFrame->data, outSamples,
                        (const uint8_t**)frame_->data, frame_->nb_samples);
            fltFrame->pts = frame_->pts;

            int64_t offsetToApply = 0;
            if (firstFrameAfterSeek) {
                AVRational inTb{1, context_.codec->sample_rate};
                auto targetPts = av_rescale_q((int64_t)(seekTargetSec_ * AV_TIME_BASE),
                                              AV_TIME_BASE_Q, inTb);
                ptsOffset = targetPts - fltFrame->pts;
                firstFrameAfterSeek = false;
            }
            offsetToApply = ptsOffset;
            if (fltFrame->pts != AV_NOPTS_VALUE) {
                fltFrame->pts += offsetToApply;
            }

            // 2) отправка в atempo
            if (av_buffersrc_add_frame(bufferSrcCtx_, fltFrame) < 0) {
                qDebug() << "EAGAIN";
            }
            // НЕ освобождаем fltFrame, переиспользуем

            // 3) забор из atempo
            while (av_buffersink_get_frame(bufferSinkCtx_, filteredFrame) == 0) {
                AVRational outTb = av_buffersink_get_time_base(bufferSinkCtx_);
                double bufferedSec = audioDevice_->bufferedFrames() / (double)context_.codec->sample_rate;
                double outPtsSec = filteredFrame->pts * av_q2d(outTb) - bufferedSec;
                if(mediaClock_){
                    mediaClock_->setAudioTime(outPtsSec * speed_.load());
                }

                // 4) FLT -> S16
                int outSamples2 = swr_get_out_samples(swrCtxOut, filteredFrame->nb_samples);
                int bufferSize = av_samples_get_buffer_size(nullptr, 2, outSamples2,
                                                             AV_SAMPLE_FMT_S16, 1);
                QByteArray pcm(bufferSize, 0);
                uint8_t *outBuf = reinterpret_cast<uint8_t*>(pcm.data());

                int converted = swr_convert(swrCtxOut, &outBuf, outSamples2,
                                            (const uint8_t**)filteredFrame->data,
                                            filteredFrame->nb_samples);
                int actual = av_samples_get_buffer_size(nullptr, 2, converted, AV_SAMPLE_FMT_S16, 1);
                audioDevice_->readData(pcm, actual);

                av_frame_unref(filteredFrame);
            }
            av_frame_unref(frame_);
        }
    }
    qDebug() << "[AudioDecoder] final";
    swr_free(&swrCtxIn);
    swr_free(&swrCtxOut);
    av_frame_free(&fltFrame); // НОВОЕ: освобождаем переиспользуемый фрейм
    av_frame_free(&filteredFrame);
    freeFilterGraph();
}

