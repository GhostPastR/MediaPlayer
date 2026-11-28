#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <memory>
#include <QThread>

#include "pausedecoder.h"
#include "mediastruct.h"
#include "safequeue.h"

class SwrContext;
class AVFormatContext;
class MediaClock;
class AudioDevice;
class Barrier;

using PacketQueue = SafeQueue<AVPacket*>;

class AudioDecoder : public QThread, public PauseStopDecoder
{
    Q_OBJECT
public:
    AudioDecoder(AVFormatContext *formatContext, std::shared_ptr<PacketQueue> queue,
                 std::shared_ptr<MediaClock> mediaClock, std::shared_ptr<Barrier> barrier,
                 QObject *parent = nullptr);
    ~AudioDecoder();

    void pause();
    void resume();

    float volume() const;
    void setVolume(float volume);

    std::optional<CodecFormat> createContext(int audioIndex);

    void stop();
    void seekFlush(double targetSec);

    void setSpeed(double speed);

signals:
    void positionChanged(int seconds);

protected:
    void run() override;

private:
    AVFormatContext *formatContext_{};
    std::shared_ptr<PacketQueue> queue_;
    std::shared_ptr<MediaClock> mediaClock_;
    Context context_;

    std::atomic<bool> flushRequested_{};
    std::atomic<double> speed_{1.0};
    std::atomic<double> seekTargetSec_{};

    std::unique_ptr<AudioDevice> audioDevice_;
    std::shared_ptr<Barrier> barrier_;

    std::optional<SwrContext*> createOutSwrContext();
    void decoder();
};

#endif // AUDIODECODER_H
