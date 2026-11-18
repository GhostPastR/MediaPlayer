#ifndef MEDIAQUEUE_H
#define MEDIAQUEUE_H

#include <set>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "mediastruct.h"

class MediaQueue
{
public:
    explicit MediaQueue(size_t maxSize = 2000);

    bool push(MediaItem item);
    bool pop(MediaItem &out);

    void clear();
    void abort();

    size_t size() const;
    bool isAborted() const;

private:
    std::set<MediaItem, MediaItemCompare> queue_;
    size_t maxSize_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    bool abort_ = false;
};

#endif // MEDIAQUEUE_H
