#ifndef MEDIASTRUCT_H
#define MEDIASTRUCT_H

#include <QImage>
#include <QByteArray>

class AVCodecContext;
class AVStream;

struct Context
{
    AVCodecContext *codec{};
    AVStream *stream{};
};

struct CodecFormat
{
    int rate{};
    int channels{};
    int sample_size{};
};

struct MediaItem
{
    enum class Type {
        Video,
    };

    Type type = Type::Video;
    double pts = 0.0;
    QImage image;
};



struct MediaItemCompare
{
    bool operator()(const MediaItem &a, const MediaItem &b) const {
        if (a.pts != b.pts){
            return a.pts < b.pts;
        }
        return a.type < b.type;
    }
};



#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class Frame {
public:
    Frame() : frame_(av_frame_alloc()) {}

    AVFrame* get() const noexcept { return frame_.get(); }
    AVFrame* operator->() const noexcept { return frame_.get(); }
    explicit operator bool() const noexcept { return static_cast<bool>(frame_); }

private:
    struct Deleter {
        void operator()(AVFrame* f) const noexcept {
            if (f) av_frame_free(&f);
        }
    };
    std::unique_ptr<AVFrame, Deleter> frame_;
};



struct SwsDeleter {
    void operator()(SwsContext* ctx) const noexcept {
        if (ctx) {
            sws_freeContext(ctx);
        }
    }
};

using SwsContextPtr = std::unique_ptr<SwsContext, SwsDeleter>;

#endif // MEDIASTRUCT_H
