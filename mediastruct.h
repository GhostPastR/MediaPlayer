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
        Finish,
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

#endif // MEDIASTRUCT_H
