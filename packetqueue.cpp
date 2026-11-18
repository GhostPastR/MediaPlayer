#include "packetqueue.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

PacketQueue::PacketQueue(size_t maxSize) : m_maxSize(maxSize)
{

}

PacketQueue::~PacketQueue()
{

}

void PacketQueue::push(AVPacket *packet)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // Ждём, пока очередь не освободится
    m_notFull.wait(lock, [this]() {
        return queue_.size() < m_maxSize || m_abort;
    });
    if (m_abort) {
        av_packet_free(&packet);
        return;
    }
    queue_.push(packet);
    m_notEmpty.notify_one();
}

AVPacket* PacketQueue::pop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_notEmpty.wait(lock, [this]() {
        return !queue_.empty() || m_abort;
    });
    if (queue_.empty()) {
        return nullptr;
    }
    AVPacket *packet = queue_.front();
    queue_.pop();
    m_notFull.notify_one(); // ← будим читателя
    return packet;
}

void PacketQueue::abort()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_abort = true;
    }
    m_notEmpty.notify_all();
    m_notFull.notify_all();
}

void PacketQueue::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!queue_.empty()) {
        AVPacket *packet = queue_.front();
        queue_.pop();
        av_packet_free(&packet);
    }
    m_notFull.notify_all(); // ← будим читателя, если он ждал
}

size_t PacketQueue::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return queue_.size();
}
