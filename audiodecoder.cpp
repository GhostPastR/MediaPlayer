#include "audiodecoder.h"

#include "packetqueue.h"
#include "mediaqueue.h"

#include "audiodevice.h"
#include "mediaclock.h"
#include "rubberbandprocessor.h"

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

std::optional<SwrContext *> AudioDecoder::createOutSwrContext()
{
    auto swrCtxOut = swr_alloc_set_opts(nullptr,
                                        AV_CH_LAYOUT_STEREO,
                                        AV_SAMPLE_FMT_S16,
                                        context_.codec->sample_rate,
                                        AV_CH_LAYOUT_STEREO,       // FLTP стерео
                                        AV_SAMPLE_FMT_FLTP,        // Внимание: planar float
                                        context_.codec->sample_rate,
                                        0, nullptr);
        if (swr_init(swrCtxOut) < 0) {
            qDebug() << "[AudioDecoder] swr_init(out) failed";
            return {};
        }
        return swrCtxOut;
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

void AudioDecoder::decoder()
{
    // 1) Входной SwrContext: декодер -> FLTP (planar float, 2 канала)
    SwrContext* swrCtxIn = swr_alloc_set_opts(nullptr,
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, context_.codec->sample_rate,
        context_.codec->channel_layout, context_.codec->sample_fmt,
        context_.codec->sample_rate, 0, nullptr);
    if (swr_init(swrCtxIn) < 0) {
        qDebug() << "[AudioDecoder] swr_init(in) failed";
        return;
    }

    // 2) Выходной SwrContext: FLTP -> S16
    SwrContext* swrCtxOut = swr_alloc_set_opts(nullptr,
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, context_.codec->sample_rate,
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, context_.codec->sample_rate,
        0, nullptr);
    if (swr_init(swrCtxOut) < 0) {
        qDebug() << "[AudioDecoder] swr_init(out) failed";
        swr_free(&swrCtxIn);
        return;
    }

    RubberBandProcessor rbProcessor(context_.codec->sample_rate, 2);
    rbProcessor.setSpeed(speed_.load());

    audioDevice_ = std::make_unique<AudioDevice>(context_.codec->sample_rate);

    Frame frame;
    Frame fltFrame;
    Frame filteredFrame;

    int64_t ptsOffset{};
    bool firstFrameAfterSeek{};

    int64_t outputSamplesWritten_ = 0;

    while (!stopRequested_) {
        waitPaused();

        if (flushRequested_) {
            avcodec_flush_buffers(context_.codec);
            rbProcessor.reset(context_.codec->sample_rate, 2);
            rbProcessor.setSpeed(speed_.load());
            ptsOffset = 0;
            firstFrameAfterSeek = true;
            outputSamplesWritten_ = 0;
            flushRequested_ = false;
            continue;
        }

        AVPacket *packet = queue_->pop();
        if (!packet) {
            if (mediaClock_) {
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
            // 3) декодер -> FLTP
            int outSamples = swr_get_out_samples(swrCtxIn, frame->nb_samples);

            av_frame_unref(fltFrame.get());
            fltFrame->format = AV_SAMPLE_FMT_FLTP;
            fltFrame->sample_rate = context_.codec->sample_rate;
            fltFrame->channels = 2;
            fltFrame->channel_layout = AV_CH_LAYOUT_STEREO;
            fltFrame->nb_samples = outSamples;
            av_frame_get_buffer(fltFrame.get(), 0);

            swr_convert(swrCtxIn, fltFrame->data, outSamples,
                        (const uint8_t**)frame->data, frame->nb_samples);
            fltFrame->pts = frame->pts;

            // 4) Смещение PTS после seek
            if (firstFrameAfterSeek) {
                AVRational inTb{1, context_.codec->sample_rate};
                auto targetPts = av_rescale_q((int64_t)(seekTargetSec_ * AV_TIME_BASE),
                                              AV_TIME_BASE_Q, inTb);
                ptsOffset = targetPts - fltFrame->pts;
                firstFrameAfterSeek = false;
            }
            if (fltFrame->pts != AV_NOPTS_VALUE) {
                fltFrame->pts += ptsOffset;
            }


            rbProcessor.setSpeed(speed_.load());
            const float* inputData[2] = {
                reinterpret_cast<const float*>(fltFrame->data[0]),
                reinterpret_cast<const float*>(fltFrame->data[1])
            };
            rbProcessor.process(inputData, fltFrame->nb_samples);

            int avail = rbProcessor.available();
            while (avail > 0) {
                av_frame_unref(filteredFrame.get());
                filteredFrame->format = AV_SAMPLE_FMT_FLTP;
                filteredFrame->sample_rate = context_.codec->sample_rate;
                filteredFrame->channels = 2;
                filteredFrame->channel_layout = AV_CH_LAYOUT_STEREO;
                filteredFrame->nb_samples = avail;
                av_frame_get_buffer(filteredFrame.get(), 0);

                float* outputData[2] = {
                    reinterpret_cast<float*>(filteredFrame->data[0]),
                    reinterpret_cast<float*>(filteredFrame->data[1])
                };
                size_t retrieved = rbProcessor.retrieve(outputData, avail);

                int outSamples2 = swr_get_out_samples(swrCtxOut, retrieved);
                int bufferSize = av_samples_get_buffer_size(nullptr, 2, outSamples2,
                                                             AV_SAMPLE_FMT_S16, 1);
                QByteArray pcm(bufferSize, 0);
                uint8_t *outBuf = reinterpret_cast<uint8_t*>(pcm.data());

                int converted = swr_convert(swrCtxOut, &outBuf, outSamples2,
                                            (const uint8_t**)filteredFrame->data,
                                            retrieved);
                int actual = av_samples_get_buffer_size(nullptr, 2, converted, AV_SAMPLE_FMT_S16, 1);
                audioDevice_->readData(pcm, actual);


                // 9) Накопление и обновление MediaClock
                outputSamplesWritten_ += retrieved;
                if (mediaClock_) {
                    double audioTime = (static_cast<double>(outputSamplesWritten_)
                                        - audioDevice_->bufferedFrames()) / context_.codec->sample_rate;
                    mediaClock_->setAudioTime(seekTargetSec_ + speed_.load() * audioTime);
                }
                avail = rbProcessor.available();
            }

            av_frame_unref(frame.get());
        }
    }

    qDebug() << "[AudioDecoder] final";
    swr_free(&swrCtxIn);
    swr_free(&swrCtxOut);
}
