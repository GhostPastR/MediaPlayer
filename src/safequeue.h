#ifndef SAFEQUEUE_H
#define SAFEQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>

extern "C" {
#include <libavcodec/avcodec.h>
}

template<typename Item>
class SafeQueue
{
public:
    SafeQueue(size_t maxSize) : maxSize_(maxSize){}
    ~SafeQueue(){
        clear();
    }

    void push(Item item){
        std::unique_lock<std::mutex> lock(mutex_);
        // Ждём, пока очередь не освободится
        notFull_.wait(lock, [this]() {
            return queue_.size() < maxSize_ || abort_;
        });
        queue_.push(std::move(item));
        notEmpty_.notify_one();
    }

    std::optional<Item> pop(){
        std::unique_lock<std::mutex> lock(mutex_);
        const auto time = std::chrono::milliseconds(2);
        notEmpty_.wait_for(lock, time, [this](){
            return !queue_.empty() || abort_;
        });
        if (queue_.empty()) {
            return std::nullopt;
        }
        auto item = queue_.front();
        queue_.pop();
        notFull_.notify_one();
        return item;
    }

    void abort(){
        {
            std::lock_guard<std::mutex> lock(mutex_);
            abort_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void clear(){
        std::queue<Item> empty;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::swap(queue_, empty);
        }
        notFull_.notify_all();
    }

    size_t size() const{
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool isAborted() const{
        std::lock_guard<std::mutex> lock(mutex_);
        return abort_;
    }

private:
    const size_t maxSize_;
    std::queue<Item> queue_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    bool abort_{};
};

template<>
inline void SafeQueue<AVPacket*>::push(AVPacket* item)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this]() {
            return queue_.size() < maxSize_ || abort_;
        });
        if (abort_) {
            av_packet_free(&item);
            return;
        }
        queue_.push(item);
    }
    notEmpty_.notify_one();
}

template<>
inline void SafeQueue<AVPacket*>::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        auto item = queue_.front();
        queue_.pop();
        av_packet_free(&item);
    }
    notFull_.notify_all();
}

#endif // SAFEQUEUE_H
