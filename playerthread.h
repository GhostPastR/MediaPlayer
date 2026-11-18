#ifndef PLAYERTHREAD_H
#define PLAYERTHREAD_H

#include <QThread>
#include <QImage>
#include <QByteArray>
#include <atomic>
#include <chrono>
#include <memory>
#include <deque>

#include "mediaqueue.h"
#include "pausedecoder.h"

class MediaClock;

class PlayerThread : public QThread, public PauseStopDecoder
{
    Q_OBJECT
public:
    explicit PlayerThread(std::shared_ptr<MediaQueue> queue,
                          std::shared_ptr<MediaClock> mediaClock, QObject *parent = nullptr);
    ~PlayerThread() override;

    void pausePlayback();
    void stop();
    void seekFlush(double targetSec);
    void resumePlayback();

signals:
    void frameReady(std::shared_ptr<QImage> frame);
    void positionChanged(int seconds);

protected:
    void run() override;

private:
    std::shared_ptr<MediaQueue> queue_;
    std::shared_ptr<MediaClock> mediaClock_;
    std::atomic<bool> flushRequested_{false};
    std::atomic<int> fastForward_{0};
    std::atomic<double> seekTarget_{};
    std::atomic<double> lastSeekTarget_{};
    std::deque<MediaItem> pendingVideo_;
};

#endif // PLAYERTHREAD_H
