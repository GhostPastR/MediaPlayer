#ifndef BARRIER_H
#define BARRIER_H

#include <mutex>
#include <condition_variable>

class Barrier {
public:
    explicit Barrier(std::size_t n);

    // --- побочный поток ---
    void arrive_and_wait();
    // --- главный поток: ждём, пока все проинкрементируют ---
    void wait_all_arrived();
    // --- главный поток: отпустить всех ---
    void release();

private:
    std::mutex m_;
    std::condition_variable cv_;

    const std::size_t total_;

    // Состояние текущего раунда main
    std::size_t round_{};     // номер текущего раунда main
    std::size_t arrived_{};     // сколько пришло в этом раунде
    bool active_{}; // main в wait_all_arrived
    bool released_{}; // main дал сигнал
};

#endif // BARRIER_H
