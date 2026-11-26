#include "playerthread.h"

#include <QDebug>
#include <chrono>
#include <thread>

#include "mediaclock.h"
#include "barrier.h"

constexpr double audioDelayMs = 300.0;
constexpr double renderDelayMs = 15.0;
constexpr double msecInSec = 1000;

double current_msec(){
    auto d = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

PlayerThread::PlayerThread(std::shared_ptr<MediaQueue> queue, std::shared_ptr<MediaClock> mediaClock,
                           std::shared_ptr<Barrier> barrier, QObject *parent)
    : QThread(parent), queue_(queue), mediaClock_(mediaClock), barrier_(barrier){}

PlayerThread::~PlayerThread()
{
    stop();
    wait();
}

void PlayerThread::pausePlayback()
{
    PauseStopDecoder::pausePlayback();
}

void PlayerThread::stop()
{
    stopPlayback();
    if (queue_) {
        queue_->abort();
    }
}

void PlayerThread::resumePlayback()
{
    {
        std::lock_guard<std::mutex> lock(pauseMutex_);
        paused_ = false;
    }
    cvPause_.notify_all();
}

void PlayerThread::setSpeed(double speed)
{
    speedChanged_ = true;
}

void PlayerThread::seekFlush()
{
    flushRequested_ = true;
}

void PlayerThread::run()
{
    while (!stopRequested_) {
        waitPaused();
        barrier_->arrive_and_wait();
        if (flushRequested_.exchange(false)) {
            pendingVideo_.clear();
        }

        while (!pendingVideo_.empty()) {
            if(speedChanged_.exchange(false)){
                pendingVideo_.clear();
            }
            auto &front = pendingVideo_.front();
            double ref = mediaClock_ ? mediaClock_->getReferenceTime() : 0.0;
            if (front.pts <= ref) {
                emit frameReady(std::make_shared<QImage>(std::move(front.image)));
                pendingVideo_.pop_front();
            } else {
                break;
            }
        }

        MediaItem item;
        if (!queue_->pop(item)) {
            if (stopRequested_){
                break;
            }
            continue;
        }
        // Отправляем в Widget
        if(item.type == MediaItem::Type::Video) {
            double ref = mediaClock_ ? mediaClock_->getReferenceTime() : 0.0;
            if (item.pts > ref) {
                pendingVideo_.push_back(std::move(item));
            } else {
                emit frameReady(std::make_shared<QImage>(std::move(item.image)));
            }
        }
    }
    qDebug() << "[PlayerThread] завершён";
}
