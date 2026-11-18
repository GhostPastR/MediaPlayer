#ifndef PAUSEDECODER_H
#define PAUSEDECODER_H

#include <atomic>
#include <condition_variable>

class PauseStopDecoder
{
public:
    void pausePlayback();
    void resumePlayback();

protected:
    std::atomic<bool> paused_{false};
    std::atomic<bool> stopRequested_{false};
    std::mutex pauseMutex_;
    std::condition_variable cvPause_;

    void waitPaused();
    void stopPlayback();
};

#endif // PAUSEDECODER_H
