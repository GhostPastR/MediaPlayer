#include "playerthread.h"

#include <QDebug>
#include <chrono>
#include <thread>

#include "mediaclock.h"

const double audioDelayMs = 300.0;
const double renderDelayMs = 15.0;

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

        while (!pendingVideo_.empty()) {
            auto &front = pendingVideo_.front();
            if(front.type == MediaItem::Type::Finish) {
                pendingVideo_.pop_front();
                qDebug() << "[PlayerThread] завершён";
                return;
            }
            double ref = mediaClock_ ? mediaClock_->getReferenceTime() : 0.0;
            if (front.pts <= ref) {
//                emit positionChanged((int)front.pts);
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
        if(item.type == MediaItem::Type::Finish) {
            pendingVideo_.push_back(std::move(item));
        }

        auto pts = item.pts * 1000;
        if (flushRequested_) {
            pendingVideo_.clear();
            if(fastForward_ == 1){
                if(pts >= seekTarget_ * 1000){
                    fastForward_ = 0;
                    qDebug() << "Flush 1" << seekTarget_ << pts;
                    flushRequested_ = false;
                }
                else{
                    continue;
                }
            }
            else if(fastForward_ == -1){
                if((lastSeekTarget_ == seekTarget_) ||
                        (pts <= lastSeekTarget_ * 1000) && (pts >= seekTarget_ * 1000)){
                    fastForward_ = 0;
                    qDebug() << "Flush 2" << QString::number(seekTarget_ * 1000, 'f', 0)
                             << QString::number(pts, 'f', 0);
                    flushRequested_ = false;
                }
                else{
                    continue;
                }
            }
            else{
                continue;
            }
        }

        // Отправляем в Widget
        if(item.type == MediaItem::Type::Video) {
            double ref = mediaClock_ ? mediaClock_->getReferenceTime() : 0.0;
            if (item.pts > ref) {
                pendingVideo_.push_back(std::move(item));
            } else {
//                emit positionChanged((int)item.pts);
                emit frameReady(std::make_shared<QImage>(std::move(item.image)));
            }
        }
    }
}
