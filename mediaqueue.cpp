#include "mediaqueue.h"

#include <QDebug>

MediaQueue::MediaQueue(size_t maxSize) : maxSize_(maxSize) {}

bool MediaQueue::push(MediaItem item)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() >= maxSize_) {
        return false;   // не ждём, сразу возвращаем false
    }
    queue_.insert(std::move(item));
    notEmpty_.notify_one();
    return true;
}

bool MediaQueue::pop(MediaItem &out)
{
    std::unique_lock<std::mutex> lock(mutex_);

    const auto msec = std::chrono::milliseconds(2);

    if (!notEmpty_.wait_for(lock, msec, [this](){
            return !queue_.empty() || abort_;
        })){
        return false;
    }

    if (queue_.empty()) {
        return false;
    }

    auto node = queue_.extract(queue_.begin());
    out = std::move(node.value());

    notFull_.notify_one();
    return true;
}

void MediaQueue::clear()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    notFull_.notify_all();
}

void MediaQueue::abort()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

size_t MediaQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool MediaQueue::isAborted() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return abort_;
}
