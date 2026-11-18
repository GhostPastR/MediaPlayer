#ifndef PACKETQUEUE_H
#define PACKETQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

class AVPacket;

class PacketQueue
{
public:
    PacketQueue(size_t maxSize = 200);
    ~PacketQueue();

    void push(AVPacket *packet);

    AVPacket* pop();
    void abort();
    void clear();
    size_t size() const;

private:
    const size_t m_maxSize;
    std::queue<AVPacket*> queue_;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    bool m_abort = false;
};

#endif // PACKETQUEUE_H
