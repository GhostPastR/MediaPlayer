#include "barrier.h"

#include <QDebug>

Barrier::Barrier(std::size_t n) : total_(n) {

}

// --- побочный поток ---
void Barrier::arrive_and_wait() {
    std::unique_lock<std::mutex> lk(m_);

    if (!active_) return;             // main ещё не взвёл — ничего не делаем

    ++arrived_;
    if (arrived_ == total_) cv_.notify_all();

    const std::size_t my_round = round_;
    cv_.wait(lk, [&]{
        return released_ || round_ != my_round;
    });
}

// --- главный поток: ждём, пока все проинкрементируют ---
void Barrier::wait_all_arrived() {
    std::unique_lock<std::mutex> lk(m_);
    active_ = true;
    cv_.wait(lk, [&]{
        return arrived_ == total_;
    });
}

// --- главный поток: отпустить всех ---
void Barrier::release() {
    std::unique_lock<std::mutex> lk(m_);

    // Отпускаем всех, кто ждёт текущий раунд
    released_ = true;
    cv_.notify_all();

    // Переходим к следующему раунду СРАЗУ — не нужно ждать,
    // пока потоки выйдут: они увидят round_ != my_round и выйдут.
    ++round_;
    arrived_  = 0;
    active_   = false;
    released_ = false;
}
