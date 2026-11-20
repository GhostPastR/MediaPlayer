#include "filtergraph.h"

#include <QDebug>

extern "C" {
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

#include "mediaclock.h"
#include "audiodevice.h"
#include "mediastruct.h"

FilterGraph::FilterGraph()
{

}

FilterGraph::~FilterGraph()
{
    free();
}

bool FilterGraph::init(double speed, int rate)
{
    speed_ = speed;
    filterGraph_ = avfilter_graph_alloc();
    if (!filterGraph_){
        return false;
    }

    // abuffer: вход должен быть FLT (interleaved float)
    const AVFilter *bufferSrc = avfilter_get_by_name("abuffer");
    char args[512];
    snprintf(args, sizeof(args),
             "time_base=1/%d:sample_rate=%d:sample_fmt=flt:channel_layout=stereo",
             rate, rate);

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
    double s = speed_;
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

void FilterGraph::free()
{
    if (filterGraph_) {
        avfilter_graph_free(&filterGraph_);
        filterGraph_ = nullptr;
        bufferSrcCtx_ = nullptr;
        bufferSinkCtx_ = nullptr;
    }
}

void FilterGraph::avBuffersrc(const Frame &fltFrame, const Frame &filteredFrame, double rate,
                              SwrContext *swrContextOut, std::shared_ptr<MediaClock> mediaClock,
                              std::unique_ptr<AudioDevice> &audioDevice)
{
    if (av_buffersrc_add_frame(bufferSrcCtx_, fltFrame.get()) < 0) {
        qDebug() << "EAGAIN";
    }

    // 3) забор из atempo
    while (av_buffersink_get_frame(bufferSinkCtx_, filteredFrame.get()) == 0) {
        AVRational outTb = av_buffersink_get_time_base(bufferSinkCtx_);
        double bufferedSec = audioDevice->bufferedFrames() / rate;
        double outPtsSec = filteredFrame->pts * av_q2d(outTb) - bufferedSec;
        if(mediaClock){
            mediaClock->setAudioTime(outPtsSec * speed_);
        }

        // 4) FLT -> S16
        int outSamples2 = swr_get_out_samples(swrContextOut, filteredFrame->nb_samples);
        int bufferSize = av_samples_get_buffer_size(nullptr, 2, outSamples2,
                                                     AV_SAMPLE_FMT_S16, 1);
        QByteArray pcm(bufferSize, 0);
        uint8_t *outBuf = reinterpret_cast<uint8_t*>(pcm.data());

        int converted = swr_convert(swrContextOut, &outBuf, outSamples2,
                                    (const uint8_t**)filteredFrame->data,
                                    filteredFrame->nb_samples);
        int actual = av_samples_get_buffer_size(nullptr, 2, converted, AV_SAMPLE_FMT_S16, 1);
        audioDevice->readData(pcm, actual);

        av_frame_unref(filteredFrame.get());
    }
}
