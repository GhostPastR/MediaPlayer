#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include <memory>
#include <QImage>
#include <QThread>

#include "pausedecoder.h"
#include "mediastruct.h"
#include "safequeue.h"

class SwsContext;
class AVFrame;
class AVFormatContext;
class Barrier;
class MediaClock;

using MediaQueue = SafeQueue<VideoItem>;
using PacketQueue = SafeQueue<AVPacket*>;

class VideoDecoder : public QThread, public PauseStopDecoder
{
    Q_OBJECT

public:
    VideoDecoder(AVFormatContext *formatContext, int videoIndex,
                 std::shared_ptr<PacketQueue> queue,
                 std::shared_ptr<MediaClock> mediaClock, std::shared_ptr<Barrier> barrier,
                 QObject *parent = nullptr);
    ~VideoDecoder();

    void stop();
    void seekFlush(double targetSec);


    void pause();
//    void stop();
//    void seekFlush();
    void resume();

    void setSpeed(double speed);

signals:
    void frameReady(std::shared_ptr<QImage> frame);

protected:
    void run() override;

private:
    AVFormatContext *formatContext_{};
    int videoIndex_{-1};
    std::shared_ptr<PacketQueue> queue_;
    Context context_;
    std::atomic<bool> flushRequested_{};
    std::atomic<bool> flushRequestedProcessing_{};
    std::atomic<double> seekTargetSec_{};
    std::shared_ptr<Barrier> barrier_;
    std::shared_ptr<MediaClock> mediaClock_;
    std::atomic<bool> speedChanged_{};
    std::thread thread_;

    bool createContext(int videoIdx);
    bool createSwsContext(SwsContextPtr &contextPtr, const Frame& frame);
    void decoder();
    void frameProcessing(std::shared_ptr<MediaQueue> queue);
};

#endif // VIDEODECODER_H
