#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <memory>
#include <QImage>
#include <QThread>

#include "pausedecoder.h"
#include "mediastruct.h"

class SwsContext;
class AVFrame;
class AVFormatContext;
class PacketQueue;
class MediaQueue;

class VideoDecoder : public QThread, public PauseStopDecoder
{
    Q_OBJECT

public:
    VideoDecoder(AVFormatContext *formatContext, int videoIndex,
                 std::shared_ptr<PacketQueue> queue, std::shared_ptr<MediaQueue> mediaQueue,
                 QObject *parent = nullptr);
    ~VideoDecoder();

    void stop();
    void seekFlush();

protected:
    void run() override;

private:
    AVFormatContext *formatContext_{};
    int videoIndex_{-1};
    std::shared_ptr<PacketQueue> queue_;
    std::shared_ptr<MediaQueue> mediaQueue_;
    Context context_;
    SwsContext *swsContext_{};
    AVFrame *frame_{};
    std::atomic<bool> flushRequested_{};

    bool createContext(int videoIdx);
    bool createSwsContext();
    void decoder();
};

#endif // VIDEODECODER_H
