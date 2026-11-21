#include "audiodecoder.h"

#include "packetqueue.h"
#include "mediaqueue.h"

#include "audiodevice.h"
#include "mediaclock.h"
#include "filtergraph.h"

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
}

AudioDecoder::~AudioDecoder()
{
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
    if(mediaClock_){
        mediaClock_->pause();
    }
}

void AudioDecoder::resume()
{
    resumePlayback();
    if(audioDevice_){
        audioDevice_->setPause(false);
    }
    if(mediaClock_){
        mediaClock_->resume();
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
    if (mediaClock_){
        mediaClock_->setAudioEnded(false);
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

void AudioDecoder::decoder()
{
    auto swrContext = createInOutSwrContext();
    if(!swrContext.has_value()){
        qDebug() << "[AudioDecoder] createSwrContext failed";
        return;
    }

    auto [swrCtxIn, swrCtxOut] = swrContext.value();

    FilterGraph filterGraph;

    if (!filterGraph.init(speed_.load(), context_.codec->sample_rate)) {
        qDebug() << "[AudioDecoder] init failed (initial)";
        swr_free(&swrCtxIn);
        swr_free(&swrCtxOut);
        return;
    }

    audioDevice_ = std::make_unique<AudioDevice>(context_.codec->sample_rate);

    Frame frame;
    Frame filteredFrame;
    Frame fltFrame;

    int64_t ptsOffset{};
    bool firstFrameAfterSeek{};
    while (!stopRequested_) {
        waitPaused();

//        if (speedChanged_.exchange(false)) {
//            filterGraph.free();
//            if (!filterGraph.init(speed_.load(), context_.codec->sample_rate)) {
//                qDebug() << "[AudioDecoder] init failed (speed change)";
//            }
//        }

        if (flushRequested_) {
            avcodec_flush_buffers(context_.codec);
            filterGraph.free();
            filterGraph.init(speed_.load(), context_.codec->sample_rate);
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
            continue;
        }

        avcodec_send_packet(context_.codec, packet);
        av_packet_free(&packet);

        while (avcodec_receive_frame(context_.codec, frame.get()) == 0 && !stopRequested_) {
            // 1) декодер -> FLT
            int outSamples = swr_get_out_samples(swrCtxIn, frame->nb_samples);

            av_frame_unref(fltFrame.get());
            fltFrame->format = AV_SAMPLE_FMT_FLT;
            fltFrame->sample_rate = context_.codec->sample_rate;
            fltFrame->channels = context_.codec->channels;
            fltFrame->nb_samples = outSamples;
            av_frame_get_buffer(fltFrame.get(), 0);

            swr_convert(swrCtxIn, fltFrame->data, outSamples,
                        (const uint8_t**)frame->data, frame->nb_samples);
            fltFrame->pts = frame->pts;

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

            filterGraph.avBuffersrc(fltFrame, filteredFrame, context_.codec->sample_rate,
                                    seekTargetSec_, swrCtxOut, mediaClock_, audioDevice_);
            av_frame_unref(frame.get());
        }
    }
    qDebug() << "[AudioDecoder] final";
    swr_free(&swrCtxIn);
    swr_free(&swrCtxOut);
}

