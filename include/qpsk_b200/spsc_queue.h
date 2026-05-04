#ifndef QPSK_B200_SPSC_QUEUE_H
#define QPSK_B200_SPSC_QUEUE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

namespace qpsk_b200 {

/// Single-Producer / Single-Consumer lock-free ring buffer.
///
/// Uses std::atomic with memory_order_acquire/release for the head and
/// tail pointers.  The buffer is a fixed-size vector allocated at
/// construction time.
///
/// The queue supports:
///   - Non-blocking try_push / try_pop
///   - Blocking pop_wait with configurable timeout
///   - Overflow detection (try_push returns false when full)
///
/// Maps to Requirements 11.4, 12.4.
template <typename T>
class SpscQueue {
public:
    /// Construct a ring buffer with the given capacity.
    /// @param capacity  Maximum number of elements (default 64).
    explicit SpscQueue(size_t capacity = 64)
        : capacity_(capacity)
        , buffer_(capacity)
        , head_(0)
        , tail_(0) {}

    /// Try to push an item into the queue (producer side).
    /// @return true if the item was enqueued, false if the queue is full.
    bool try_push(T item) {
        const size_t cur_head = head_.load(std::memory_order_relaxed);
        const size_t next_head = (cur_head + 1) % capacity_;

        if (next_head == tail_.load(std::memory_order_acquire)) {
            // Queue is full
            return false;
        }

        buffer_[cur_head] = std::move(item);
        head_.store(next_head, std::memory_order_release);

        // Notify any waiting consumer.
        // Lock + notify ensures the consumer sees the updated head
        // before the condition variable check.
        {
            std::lock_guard<std::mutex> lock(mtx_);
        }
        cv_.notify_one();
        return true;
    }

    /// Try to pop an item from the queue (consumer side).
    /// @param[out] item  Receives the dequeued element on success.
    /// @return true if an item was dequeued, false if the queue is empty.
    bool try_pop(T& item) {
        const size_t cur_tail = tail_.load(std::memory_order_relaxed);

        if (cur_tail == head_.load(std::memory_order_acquire)) {
            // Queue is empty
            return false;
        }

        item = std::move(buffer_[cur_tail]);
        tail_.store((cur_tail + 1) % capacity_, std::memory_order_release);
        return true;
    }

    /// Block until an item is available or the timeout expires.
    /// @param[out] item     Receives the dequeued element on success.
    /// @param      timeout  Maximum time to wait.
    /// @return true if an item was dequeued, false on timeout.
    template <typename Rep, typename Period>
    bool pop_wait(T& item, std::chrono::duration<Rep, Period> timeout) {
        // Fast path: try non-blocking pop first
        if (try_pop(item)) {
            return true;
        }

        // Slow path: wait on condition variable
        std::unique_lock<std::mutex> lock(mtx_);
        bool signaled = cv_.wait_for(lock, timeout, [this]() {
            return head_.load(std::memory_order_acquire) !=
                   tail_.load(std::memory_order_acquire);
        });
        if (!signaled) {
            return false;
        }
        return try_pop(item);
    }

    /// Number of elements currently in the queue.
    /// Note: this is an approximation in a concurrent context.
    size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        if (h >= t) {
            return h - t;
        }
        return capacity_ - t + h;
    }

    /// Check whether the queue is empty.
    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /// Check whether the queue is full.
    bool full() const {
        const size_t next_head =
            (head_.load(std::memory_order_acquire) + 1) % capacity_;
        return next_head == tail_.load(std::memory_order_acquire);
    }

    /// Return the fixed capacity of the queue.
    size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    std::vector<T> buffer_;

    // Aligned to avoid false sharing between producer and consumer
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;

    // Condition variable for blocking pop_wait
    std::mutex mtx_;
    std::condition_variable cv_;
};

} // namespace qpsk_b200

#endif // QPSK_B200_SPSC_QUEUE_H
