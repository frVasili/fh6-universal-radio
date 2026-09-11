#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace fh6 {
// One background completion task. Reservations are atomic; an idle render
// path never touches the queue mutex or wakes this sleeping worker.
class SingleFlightWorker {
public:
    SingleFlightWorker() : thread_([this] { run(); }) {}
    ~SingleFlightWorker() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        cv_.notify_one();
        thread_.join();
    }
    bool busy() const noexcept { return busy_.load(std::memory_order_acquire); }
    bool try_reserve() noexcept {
        bool expected = false;
        return busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }
    void release() noexcept { busy_.store(false, std::memory_order_release); }
    // Caller holds the reservation. Empty tasks are invalid.
    void dispatch(std::function<void()> task) {
        { std::lock_guard lock(mutex_); task_ = std::move(task); }
        cv_.notify_one();
    }
private:
    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || static_cast<bool>(task_); });
                if (!task_ && stopping_) return;
                task = std::move(task_);
                task_ = {};
            }
            task();
            task = {}; // release captured resources before accepting another upload
            release();
        }
    }
    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::function<void()> task_;
    bool stopping_ = false;
    std::thread thread_;
};
}
