#include "videodecoder.h"

#include "packetqueue.h"
#include "mediaqueue.h"

#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
//#include <libavcodec/avcodec.h>
//#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
//#include <libswresample/swresample.h>
}

VideoDecoder::VideoDecoder(AVFormatContext *formatContext, int videoIndex,
                           std::shared_ptr<PacketQueue> queue, std::shared_ptr<MediaQueue> mediaQueue,
                           QObject *parent) : QThread(parent)
{
    formatContext_ = formatContext;
    queue_ = queue;
    videoIndex_ = videoIndex;
    mediaQueue_ = mediaQueue;
    frame_ = av_frame_alloc();
}

VideoDecoder::~VideoDecoder()
{
    if (swsContext_){
        sws_freeContext(swsContext_);
    }
    if(frame_){
        av_frame_free(&frame_);
    }
    if(context_.codec){
        avcodec_free_context(&context_.codec);
    }
}

void VideoDecoder::stop()
{
    stopPlayback();
    queue_->abort();
}

void VideoDecoder::seekFlush()
{
    flushRequested_ = true;
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

    uint64_t i = 0;
    while (!stopRequested_) {
        waitPaused();
//         Обработка перемотки — сброс буферов декодера
        if (flushRequested_) {
            avcodec_flush_buffers(context_.codec);
            flushRequested_ = false;
            continue;
        }

        AVPacket *packet = queue_->pop();
        if (!packet){
            break;
        }

        avcodec_send_packet(context_.codec, packet);
        av_packet_free(&packet);

        while ((avcodec_receive_frame(context_.codec, frame_) == 0) && !stopRequested_) {
            createSwsContext();

            if (frame_->flags & AV_FRAME_FLAG_CORRUPT) {
                av_frame_unref(frame_);
                continue;
            }

            if (frame_->pts == AV_NOPTS_VALUE) {
                av_frame_unref(frame_);
                continue; // пропускаем кадр без pts
            }

            auto image = QImage(frame_->width, frame_->height, QImage::Format_RGB32);
            uint8_t *destData[4] = { image.bits(), nullptr, nullptr, nullptr };
            int destLinesize[4] = { (int)image.bytesPerLine(), 0, 0, 0 };
            sws_scale(swsContext_, frame_->data, frame_->linesize, 0,
                      frame_->height, destData, destLinesize);

            MediaItem item;
            item.type = MediaItem::Type::Video;
            item.pts = frame_->pts * av_q2d(timeBase);
            item.image = std::move(image);

            if (!mediaQueue_->push(std::move(item))) {
                if (stopRequested_){
                    break;
                }
            }

            av_frame_unref(frame_);
        }
    }
    MediaItem item;
    item.type = MediaItem::Type::Finish;
    mediaQueue_->push(std::move(item));
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

bool VideoDecoder::createSwsContext()
{
    if (!swsContext_) {
        swsContext_ = sws_getContext(frame_->width, frame_->height,
            (AVPixelFormat)frame_->format, frame_->width, frame_->height,
            AV_PIX_FMT_RGB32, SWS_POINT, nullptr, nullptr, nullptr);
        if (!swsContext_) {
            av_frame_unref(frame_);
            return false;
        }
    }
    return true;
}
