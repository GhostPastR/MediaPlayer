#include "videodecoder.h"

#include "packetqueue.h"
#include "mediaqueue.h"
#include "barrier.h"

#include <QDebug>

VideoDecoder::VideoDecoder(AVFormatContext *formatContext, int videoIndex,
                           std::shared_ptr<PacketQueue> queue, std::shared_ptr<MediaQueue> mediaQueue,
                           std::shared_ptr<Barrier> barrier, QObject *parent) : QThread(parent)
{
    formatContext_ = formatContext;
    queue_ = queue;
    videoIndex_ = videoIndex;
    mediaQueue_ = mediaQueue;
    barrier_ = barrier;
}

VideoDecoder::~VideoDecoder()
{
    if(context_.codec){
        avcodec_free_context(&context_.codec);
    }
}

void VideoDecoder::stop()
{
    stopPlayback();
    queue_->abort();
}

void VideoDecoder::seekFlush(double targetSec)
{
    flushRequested_ = true;
    seekTargetSec_ = targetSec;
}

void VideoDecoder::run()
{
    decoder();
}

void VideoDecoder::decoder()
{
    if(!createContext(videoIndex_)){
        qDebug() << "Error 1";
        return;
    }

    AVRational timeBase = context_.stream->time_base;

    Frame frame;
    SwsContextPtr swsContext;

    while (!stopRequested_) {
        waitPaused();
        barrier_->arrive_and_wait();
        if (flushRequested_.exchange(false)) {
            // Обработка перемотки — сброс буферов декодера
            avcodec_flush_buffers(context_.codec);
            continue;
        }

        AVPacket *packet = queue_->pop();
        if (!packet){
            continue;
        }

        avcodec_send_packet(context_.codec, packet);
        av_packet_free(&packet);

        while ((avcodec_receive_frame(context_.codec, frame.get()) == 0) && !stopRequested_) {
            createSwsContext(swsContext, frame);

            if (frame.get()->flags & AV_FRAME_FLAG_CORRUPT) {
                av_frame_unref(frame.get());
                continue;
            }

            if (frame->pts == AV_NOPTS_VALUE) {
                av_frame_unref(frame.get());
                continue; // пропускаем кадр без pts
            }
            auto time = frame->pts * av_q2d(timeBase);
            if (time < seekTargetSec_) {
                continue;
            }

            auto image = QImage(frame->width, frame->height, QImage::Format_RGB32);
            uint8_t *destData[4] = { image.bits(), nullptr, nullptr, nullptr };
            int destLinesize[4] = { (int)image.bytesPerLine(), 0, 0, 0 };

            sws_scale(swsContext.get(), frame->data, frame->linesize, 0,
                      frame->height, destData, destLinesize);

            MediaItem item;
            item.type = MediaItem::Type::Video;
            item.pts = time;
            item.image = std::move(image);

            if (!mediaQueue_->push(std::move(item))) {
                if (stopRequested_){
                    break;
                }
            }

            av_frame_unref(frame.get());
        }
    }
    qDebug() << "[VideoDecoder] завершён";
}

bool VideoDecoder::createContext(int videoIdx)
{
    if(formatContext_){
        AVStream *stream = formatContext_->streams[videoIdx];
        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            qDebug() << "Video decoder not found";
            return false;
        }
        AVCodecContext *codec = avcodec_alloc_context3(decoder);

        codec->thread_count = 0;  // 0 = авто (по числу ядер CPU)
        codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

        avcodec_parameters_to_context(codec, stream->codecpar);
        codec->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
        if (avcodec_open2(codec, decoder, nullptr) < 0) {
            avcodec_free_context(&codec);
            return false;
        }
        context_ = {codec, stream};
        return true;
    }
    return false;
}

bool VideoDecoder::createSwsContext(SwsContextPtr &contextPtr, const Frame& frame)
{
    static int lastW = -1, lastH = -1, lastFmt = -1;

    if (!contextPtr || lastW != frame->width || lastH != frame->height || lastFmt != frame->format) {
        auto ptr = sws_getContext(frame->width, frame->height,
            (AVPixelFormat)frame->format, frame->width, frame->height,
            AV_PIX_FMT_RGB32, SWS_POINT | SWS_ACCURATE_RND, nullptr, nullptr, nullptr);
        if (!ptr) {
            av_frame_unref(frame.get());
            return false;
        }
        contextPtr.reset(ptr);

        lastW = frame->width;
        lastH = frame->height;
        lastFmt = frame->format;
    }
    return true;
}
