#include "pausedecoder.h"

void PauseStopDecoder::stopPlayback()
{
    stopRequested_ = true;
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        paused_ = false;
    }
    cvPause_.notify_all(); // будим поток, если он спит
}

void PauseStopDecoder::pausePlayback()
{
    paused_ = true;
}

void PauseStopDecoder::resumePlayback()
{
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        paused_ = false;
    }
    cvPause_.notify_all();
}

void PauseStopDecoder::waitPaused()
{
    std::unique_lock<std::mutex> lock(pauseMutex_);
    cvPause_.wait(lock, [this]() {
        return !paused_ || stopRequested_;
    });
}
