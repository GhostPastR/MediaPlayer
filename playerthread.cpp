#include "playerthread.h"

#include <QDebug>
#include <chrono>
#include <thread>

#include "mediaclock.h"

constexpr double audioDelayMs = 300.0;
constexpr double renderDelayMs = 15.0;
constexpr double msecInSec = 1000;

double current_msec(){
    auto d = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

PlayerThread::PlayerThread(std::shared_ptr<MediaQueue> queue,
                           std::shared_ptr<MediaClock> mediaClock,
                           QObject *parent)
    : QThread(parent), queue_(queue), mediaClock_(mediaClock)
{
}

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
    speedChanged_ = true;;
}

void PlayerThread::seekFlush(double targetSec)
{
    qDebug() << "[seekFlush]" << targetSec;
    lastSeekTarget_ = seekTarget_.load();
    fastForward_ = (seekTarget_.exchange(targetSec) < targetSec) ? 1 : -1;
    flushRequested_ = true;
    queue_->clear();
}

void PlayerThread::run()
{
    while (!stopRequested_) {
        waitPaused();
        if(speedChanged_.exchange(false)){
            pendingVideo_.clear();
        }

        while (!pendingVideo_.empty()) {
            if(speedChanged_.exchange(false)){
                pendingVideo_.clear();
            }
            auto &front = pendingVideo_.front();
            double ref = mediaClock_ ? mediaClock_->getReferenceTime() : 0.0;
            if (front.pts <= ref) {
//                qDebug() << "frameReady2" << front.pts;
                emit frameReady(std::make_shared<QImage>(std::move(front.image)));
                pendingVideo_.pop_front();
            } else {
                break;
            }
        }

        MediaItem item;
        if (!queue_->pop(item)) {
            if (stopRequested_) break;
            continue;
        }

        if(continueFrame(item.pts * msecInSec)){
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
}

bool PlayerThread::continueFrame(double pts)
{
    if (flushRequested_) {
        pendingVideo_.clear();
        if(fastForward_ == 1){
            if(pts >= seekTarget_ * msecInSec){
                fastForward_ = 0;
                flushRequested_ = false;
            }
            else{
                return true;
            }
        }
        else if(fastForward_ == -1){
            if((lastSeekTarget_ == seekTarget_) ||
                    (pts <= lastSeekTarget_ * msecInSec) && (pts >= seekTarget_ * msecInSec)){
                fastForward_ = 0;
                flushRequested_ = false;
            }
            else{
                return true;
            }
        }
        else{
            return true;
        }
    }
    return false;
}
