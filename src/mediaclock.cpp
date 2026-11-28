#include "mediaclock.h"

MediaClock::MediaClock(){}

void MediaClock::setAudioTime(double sec)
{
    audioTime_.store(sec, std::memory_order_relaxed);
}

void MediaClock::setAudioEnded(bool ended)
{
    bool wasEnded = audioEnded_.exchange(ended, std::memory_order_relaxed);
    if (ended && !wasEnded) {
        endTime_ = std::chrono::steady_clock::now();
        paused_ = false;
    }
}

bool MediaClock::isAudioEnded() const
{
    return audioEnded_.load(std::memory_order_relaxed);
}

void MediaClock::pause()
{
    if (audioEnded_.load(std::memory_order_relaxed) && !paused_) {
        pausedAt_ = std::chrono::steady_clock::now();
        paused_ = true;
    }
}

void MediaClock::resume()
{
    if (paused_) {
        auto pauseDuration = std::chrono::steady_clock::now() - pausedAt_;
        endTime_ += pauseDuration;
        paused_ = false;
    }
}

double MediaClock::getReferenceTime() const
{
    if (audioEnded_.load(std::memory_order_relaxed)) {
        if (paused_) {
            return std::chrono::duration<double>(pausedAt_ - endTime_).count()
                    + audioTime_.load(std::memory_order_relaxed);
        }
        auto now = std::chrono::steady_clock::now();
        auto delta = std::chrono::duration<double>(now - endTime_).count();
        return delta + audioTime_.load(std::memory_order_relaxed);
    }
    return audioTime_.load(std::memory_order_relaxed);
}
