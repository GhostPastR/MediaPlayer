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
class Barrier;

class VideoDecoder : public QThread, public PauseStopDecoder
{
    Q_OBJECT

public:
    VideoDecoder(AVFormatContext *formatContext, int videoIndex,
                 std::shared_ptr<PacketQueue> queue, std::shared_ptr<MediaQueue> mediaQueue,
                 std::shared_ptr<Barrier> barrier, QObject *parent = nullptr);
    ~VideoDecoder();

    void stop();
    void seekFlush(double targetSec);

protected:
    void run() override;

private:
    AVFormatContext *formatContext_{};
    int videoIndex_{-1};
    std::shared_ptr<PacketQueue> queue_;
    std::shared_ptr<MediaQueue> mediaQueue_;
    Context context_;
    std::atomic<bool> flushRequested_{};
    std::atomic<double> seekTargetSec_{};
    std::shared_ptr<Barrier> barrier_;

    bool createContext(int videoIdx);
    bool createSwsContext(SwsContextPtr &contextPtr, const Frame& frame);
    void decoder();
};

#endif // VIDEODECODER_H
