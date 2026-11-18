#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <memory>
#include <QThread>

#include "pausedecoder.h"
#include "mediastruct.h"


class SwrContext;
class AVFrame;
class AVFormatContext;
class PacketQueue;
class MediaClock;

class AVFilterGraph;
class AVFilterContext;

class AudioDevice;

class AudioDecoder : public QThread, public PauseStopDecoder
{
    Q_OBJECT
public:
    AudioDecoder(AVFormatContext *formatContext, std::shared_ptr<PacketQueue> queue,
                 std::shared_ptr<MediaClock> mediaClock, QObject *parent = nullptr);
    ~AudioDecoder();

    void pause();
    void resume();

    float volume() const;
    void setVolume(float volume);

    std::optional<CodecFormat> createContext(int audioIndex);

    void stop();
    void seekFlush(double targetSec);

    // Установка скорости (1.0, 2.0, 3.0, 4.0)
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
    AVFrame *frame_{};

    AVFilterGraph *filterGraph_{};
    AVFilterContext *bufferSrcCtx_{};
    AVFilterContext *bufferSinkCtx_{};

    std::atomic<bool> flushRequested_{false};
    std::atomic<double> speed_{1.0};
    std::atomic<bool> speedChanged_{false};
    std::atomic<double> seekTargetSec_{0.0};

    std::unique_ptr<AudioDevice> audioDevice_;

    std::optional<std::pair<SwrContext*, SwrContext*> > createInOutSwrContext();
    bool initFilterGraph(double speed);   // создание графа atempo
    void freeFilterGraph();
    void decoder();
};

#endif // AUDIODECODER_H
