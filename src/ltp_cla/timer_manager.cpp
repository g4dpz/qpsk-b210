#include "ltp_cla/timer_manager.h"

#include <algorithm>
#include <thread>

namespace ltp {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

TimerManager::TimerManager(TimerCallback callback)
    : callback_(std::move(callback)) {}

TimerManager::~TimerManager() {
    stop();
}

// ---------------------------------------------------------------------------
// run — start the timer thread
// ---------------------------------------------------------------------------

void TimerManager::run() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (running_) return;
    running_ = true;
    thread_ = std::thread(&TimerManager::thread_func, this);
}

// ---------------------------------------------------------------------------
// stop — stop the timer thread and cancel all pending timers
// ---------------------------------------------------------------------------

void TimerManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

// ---------------------------------------------------------------------------
// start_timer — schedule a new timer
// ---------------------------------------------------------------------------

uint64_t TimerManager::start_timer(std::chrono::milliseconds timeout,
                                    TimerEvent event) {
    std::lock_guard<std::mutex> lock(mtx_);
    uint64_t id = ++next_id_;

    TimerEntry entry;
    entry.timer_id = id;
    entry.expiry = std::chrono::steady_clock::now() + timeout;
    entry.event = std::move(event);

    heap_.push(std::move(entry));
    cv_.notify_one();
    return id;
}

// ---------------------------------------------------------------------------
// cancel_timer — mark a timer as cancelled (lazy deletion)
// ---------------------------------------------------------------------------

void TimerManager::cancel_timer(uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    cancelled_.push_back(timer_id);
    cv_.notify_one();
}

// ---------------------------------------------------------------------------
// pending_count — approximate count of non-cancelled timers
// ---------------------------------------------------------------------------

size_t TimerManager::pending_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    // This is approximate since we use lazy deletion.
    if (heap_.size() <= cancelled_.size()) return 0;
    return heap_.size() - cancelled_.size();
}

// ---------------------------------------------------------------------------
// thread_func — timer thread main loop
// ---------------------------------------------------------------------------

void TimerManager::thread_func() {
    std::unique_lock<std::mutex> lock(mtx_);

    while (running_) {
        if (heap_.empty()) {
            // No timers — wait until one is added or we're stopped.
            cv_.wait(lock, [this] { return !running_ || !heap_.empty(); });
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        const auto& top = heap_.top();

        if (top.expiry <= now) {
            // Timer expired — check if cancelled.
            TimerEntry entry = heap_.top();
            heap_.pop();

            auto cit = std::find(cancelled_.begin(), cancelled_.end(),
                                 entry.timer_id);
            if (cit != cancelled_.end()) {
                // Cancelled — discard and continue.
                cancelled_.erase(cit);
                continue;
            }

            // Fire the callback outside the lock.
            lock.unlock();
            if (callback_) {
                callback_(entry.event);
            }
            lock.lock();
        } else {
            // Wait until the next timer expires or we're woken.
            cv_.wait_until(lock, top.expiry);
        }
    }
}

}  // namespace ltp
