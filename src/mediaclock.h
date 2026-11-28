#ifndef MEDIACLOCK_H
#define MEDIACLOCK_H

#include <atomic>
#include <chrono>

class MediaClock
{
public:
    MediaClock();

    void setAudioTime(double sec);

    void setAudioEnded(bool ended);
    bool isAudioEnded() const;

    void pause();
    void resume();

    double getReferenceTime() const;

private:
    std::atomic<double> audioTime_{};
    std::atomic<bool> audioEnded_{};

    std::chrono::steady_clock::time_point endTime_{};
    std::chrono::steady_clock::time_point pausedAt_{};
    bool paused_ = false;
};

#endif // MEDIACLOCK_H
