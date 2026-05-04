#include "ltp_cla/timer_manager.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Timer fires after timeout
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, TimerFiresAfterTimeout) {
    std::atomic<int> fired{0};
    TimerEvent received_event{};

    TimerManager tm([&](const TimerEvent& evt) {
        received_event = evt;
        fired++;
    });
    tm.run();

    TimerEvent evt;
    evt.type = TimerType::CHECKPOINT;
    evt.engine_id = 1;
    evt.session_number = 42;
    evt.serial_number = 7;

    tm.start_timer(std::chrono::milliseconds(100), evt);

    // Wait for the timer to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(fired.load(), 1);
    EXPECT_EQ(received_event.type, TimerType::CHECKPOINT);
    EXPECT_EQ(received_event.engine_id, 1u);
    EXPECT_EQ(received_event.session_number, 42u);
    EXPECT_EQ(received_event.serial_number, 7u);

    tm.stop();
}

// ---------------------------------------------------------------------------
// Cancel before expiry — timer does not fire
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, CancelBeforeExpiry) {
    std::atomic<int> fired{0};

    TimerManager tm([&](const TimerEvent&) {
        fired++;
    });
    tm.run();

    TimerEvent evt;
    evt.type = TimerType::REPORT_SEGMENT;
    auto tid = tm.start_timer(std::chrono::milliseconds(200), evt);

    // Cancel immediately
    tm.cancel_timer(tid);

    // Wait past the original expiry
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    EXPECT_EQ(fired.load(), 0);

    tm.stop();
}

// ---------------------------------------------------------------------------
// Multiple timers fire in expiration order
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, MultipleTimersFireInOrder) {
    std::vector<int> order;
    std::mutex mtx;

    TimerManager tm([&](const TimerEvent& evt) {
        std::lock_guard<std::mutex> lock(mtx);
        order.push_back(static_cast<int>(evt.serial_number));
    });
    tm.run();

    // Timer 3 fires first (100ms), then 1 (200ms), then 2 (300ms)
    TimerEvent e1, e2, e3;
    e1.type = TimerType::CHECKPOINT;
    e1.serial_number = 1;
    e2.type = TimerType::CHECKPOINT;
    e2.serial_number = 2;
    e3.type = TimerType::CHECKPOINT;
    e3.serial_number = 3;

    tm.start_timer(std::chrono::milliseconds(200), e1);
    tm.start_timer(std::chrono::milliseconds(300), e2);
    tm.start_timer(std::chrono::milliseconds(100), e3);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    tm.stop();

    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 3);  // 100ms
    EXPECT_EQ(order[1], 1);  // 200ms
    EXPECT_EQ(order[2], 2);  // 300ms
}

// ---------------------------------------------------------------------------
// Stop cancels all pending timers
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, StopCancelsPending) {
    std::atomic<int> fired{0};

    TimerManager tm([&](const TimerEvent&) {
        fired++;
    });
    tm.run();

    TimerEvent evt;
    evt.type = TimerType::CHECKPOINT;
    tm.start_timer(std::chrono::milliseconds(500), evt);
    tm.start_timer(std::chrono::milliseconds(600), evt);

    // Stop before timers fire
    tm.stop();

    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    EXPECT_EQ(fired.load(), 0);
}

// ---------------------------------------------------------------------------
// Pending count
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, PendingCount) {
    TimerManager tm([](const TimerEvent&) {});
    tm.run();

    TimerEvent evt;
    evt.type = TimerType::CHECKPOINT;

    auto t1 = tm.start_timer(std::chrono::milliseconds(1000), evt);
    tm.start_timer(std::chrono::milliseconds(1000), evt);

    EXPECT_EQ(tm.pending_count(), 2u);

    tm.cancel_timer(t1);
    EXPECT_EQ(tm.pending_count(), 1u);

    tm.stop();
}

// ---------------------------------------------------------------------------
// Timer with very short timeout (minimum granularity ~100ms)
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, ShortTimeout) {
    std::atomic<int> fired{0};

    TimerManager tm([&](const TimerEvent&) {
        fired++;
    });
    tm.run();

    TimerEvent evt;
    evt.type = TimerType::CHECKPOINT;
    tm.start_timer(std::chrono::milliseconds(100), evt);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(fired.load(), 1);

    tm.stop();
}

// ---------------------------------------------------------------------------
// Double stop is safe
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, DoubleStopSafe) {
    TimerManager tm([](const TimerEvent&) {});
    tm.run();
    tm.stop();
    tm.stop();  // should not crash
}

// ---------------------------------------------------------------------------
// Cancel non-existent timer is safe
// ---------------------------------------------------------------------------

TEST(TimerManagerTest, CancelNonExistentSafe) {
    TimerManager tm([](const TimerEvent&) {});
    tm.run();
    tm.cancel_timer(999);  // should not crash
    tm.stop();
}
