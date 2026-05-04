#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ltp {

// ---------------------------------------------------------------------------
// Timer types and events
// ---------------------------------------------------------------------------

enum class TimerType : uint8_t {
    CHECKPOINT,
    REPORT_SEGMENT,
    CANCEL_SEGMENT
};

struct TimerEvent {
    TimerType type;
    uint64_t engine_id = 0;
    uint64_t session_number = 0;
    uint64_t serial_number = 0;  // checkpoint, report, or cancel serial
};

struct TimerEntry {
    uint64_t timer_id = 0;
    std::chrono::steady_clock::time_point expiry;
    TimerEvent event;

    bool operator>(const TimerEntry& other) const {
        return expiry > other.expiry;
    }
};

// ---------------------------------------------------------------------------
// Timer callback
// ---------------------------------------------------------------------------

using TimerCallback = std::function<void(const TimerEvent& event)>;

// ---------------------------------------------------------------------------
// TimerManager — priority-queue based timer with dedicated thread
// ---------------------------------------------------------------------------

class TimerManager {
public:
    /// Construct with a callback that fires when timers expire.
    explicit TimerManager(TimerCallback callback);
    ~TimerManager();

    /// Start the timer thread. Must be called before start_timer.
    void run();

    /// Stop the timer thread. Cancels all pending timers.
    void stop();

    /// Start a new timer that fires after `timeout`.
    /// Returns a unique timer ID that can be used to cancel.
    uint64_t start_timer(std::chrono::milliseconds timeout,
                         TimerEvent event);

    /// Cancel a timer by ID. No-op if already fired or not found.
    void cancel_timer(uint64_t timer_id);

    /// Returns the number of pending (non-cancelled) timers.
    size_t pending_count() const;

private:
    void thread_func();

    TimerCallback callback_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool running_ = false;

    uint64_t next_id_ = 0;

    // Min-heap ordered by expiry time.
    std::priority_queue<TimerEntry,
                        std::vector<TimerEntry>,
                        std::greater<TimerEntry>> heap_;

    // Set of cancelled timer IDs (lazy deletion from heap).
    std::vector<uint64_t> cancelled_;

    std::thread thread_;
};

}  // namespace ltp
