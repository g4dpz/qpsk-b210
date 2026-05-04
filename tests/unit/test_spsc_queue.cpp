#include "qpsk_b200/spsc_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Basic push / pop
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, PushAndPopSingleItem) {
    SpscQueue<int> q(4);
    EXPECT_TRUE(q.try_push(42));

    int val = 0;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 42);
}

TEST(SpscQueueTest, PushAndPopMultipleItems) {
    SpscQueue<int> q(8);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(q.try_push(i * 10));
    }
    for (int i = 0; i < 5; ++i) {
        int val = -1;
        EXPECT_TRUE(q.try_pop(val));
        EXPECT_EQ(val, i * 10);
    }
}

TEST(SpscQueueTest, FIFOOrdering) {
    SpscQueue<std::string> q(16);
    q.try_push("alpha");
    q.try_push("beta");
    q.try_push("gamma");

    std::string s;
    EXPECT_TRUE(q.try_pop(s));
    EXPECT_EQ(s, "alpha");
    EXPECT_TRUE(q.try_pop(s));
    EXPECT_EQ(s, "beta");
    EXPECT_TRUE(q.try_pop(s));
    EXPECT_EQ(s, "gamma");
}

// ---------------------------------------------------------------------------
// Queue full behaviour
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, TryPushReturnsFalseWhenFull) {
    // Capacity 4 means 3 usable slots (one slot reserved for full detection)
    SpscQueue<int> q(4);
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4));  // queue is full
}

TEST(SpscQueueTest, PushAfterPopOnFullQueue) {
    SpscQueue<int> q(4);
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4));

    int val = 0;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 1);

    // Now there's room for one more
    EXPECT_TRUE(q.try_push(4));
}

// ---------------------------------------------------------------------------
// Queue empty behaviour
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, TryPopReturnsFalseWhenEmpty) {
    SpscQueue<int> q(4);
    int val = -1;
    EXPECT_FALSE(q.try_pop(val));
    EXPECT_EQ(val, -1);  // unchanged
}

TEST(SpscQueueTest, EmptyAfterDrainingAll) {
    SpscQueue<int> q(4);
    q.try_push(10);
    q.try_push(20);

    int val;
    q.try_pop(val);
    q.try_pop(val);

    EXPECT_FALSE(q.try_pop(val));
    EXPECT_TRUE(q.empty());
}

// ---------------------------------------------------------------------------
// pop_wait with timeout
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, PopWaitReturnsImmediatelyWhenDataAvailable) {
    SpscQueue<int> q(4);
    q.try_push(99);

    int val = 0;
    auto start = std::chrono::steady_clock::now();
    bool ok = q.pop_wait(val, std::chrono::milliseconds(1000));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 99);
    // Should return almost immediately (well under 100ms)
    EXPECT_LT(elapsed, std::chrono::milliseconds(100));
}

TEST(SpscQueueTest, PopWaitTimesOutWhenEmpty) {
    SpscQueue<int> q(4);

    int val = -1;
    auto start = std::chrono::steady_clock::now();
    bool ok = q.pop_wait(val, std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(ok);
    EXPECT_EQ(val, -1);
    // Should have waited at least ~50ms
    EXPECT_GE(elapsed, std::chrono::milliseconds(40));
}

TEST(SpscQueueTest, PopWaitWakesUpWhenItemPushed) {
    SpscQueue<int> q(4);

    std::atomic<bool> got_item{false};
    int received = -1;

    std::thread consumer([&]() {
        got_item = q.pop_wait(received, std::chrono::milliseconds(2000));
    });

    // Give consumer time to enter wait
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.try_push(77);

    consumer.join();
    EXPECT_TRUE(got_item.load());
    EXPECT_EQ(received, 77);
}

// ---------------------------------------------------------------------------
// Size / empty / full queries
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, SizeReportsCorrectCount) {
    SpscQueue<int> q(8);
    EXPECT_EQ(q.size(), 0u);

    q.try_push(1);
    EXPECT_EQ(q.size(), 1u);

    q.try_push(2);
    q.try_push(3);
    EXPECT_EQ(q.size(), 3u);

    int val;
    q.try_pop(val);
    EXPECT_EQ(q.size(), 2u);
}

TEST(SpscQueueTest, EmptyReturnsTrueOnNewQueue) {
    SpscQueue<int> q(4);
    EXPECT_TRUE(q.empty());
}

TEST(SpscQueueTest, EmptyReturnsFalseAfterPush) {
    SpscQueue<int> q(4);
    q.try_push(1);
    EXPECT_FALSE(q.empty());
}

TEST(SpscQueueTest, FullReturnsTrueWhenFull) {
    SpscQueue<int> q(4);  // 3 usable slots
    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    EXPECT_TRUE(q.full());
}

TEST(SpscQueueTest, FullReturnsFalseWhenNotFull) {
    SpscQueue<int> q(4);
    q.try_push(1);
    EXPECT_FALSE(q.full());
}

TEST(SpscQueueTest, CapacityReturnsConstructorValue) {
    SpscQueue<int> q(128);
    EXPECT_EQ(q.capacity(), 128u);
}

// ---------------------------------------------------------------------------
// Wraparound behaviour
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, WraparoundProducesCorrectValues) {
    SpscQueue<int> q(4);  // 3 usable slots

    // Fill and drain several times to force head/tail wraparound
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 3; ++i) {
            EXPECT_TRUE(q.try_push(round * 100 + i));
        }
        for (int i = 0; i < 3; ++i) {
            int val = -1;
            EXPECT_TRUE(q.try_pop(val));
            EXPECT_EQ(val, round * 100 + i);
        }
        EXPECT_TRUE(q.empty());
    }
}

// ---------------------------------------------------------------------------
// Move-only types
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, WorksWithVectorPayload) {
    SpscQueue<std::vector<uint8_t>> q(4);

    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    EXPECT_TRUE(q.try_push(std::move(data)));

    std::vector<uint8_t> out;
    EXPECT_TRUE(q.try_pop(out));
    EXPECT_EQ(out, (std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04}));
}

// ---------------------------------------------------------------------------
// Multi-threaded producer / consumer
// ---------------------------------------------------------------------------

TEST(SpscQueueTest, ConcurrentProducerConsumer) {
    constexpr int NUM_ITEMS = 10000;
    SpscQueue<int> q(64);

    std::vector<int> received;
    received.reserve(NUM_ITEMS);

    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            int val;
            while (!q.try_pop(val)) {
                std::this_thread::yield();
            }
            received.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    // Verify all items received in order
    ASSERT_EQ(received.size(), static_cast<size_t>(NUM_ITEMS));
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}

TEST(SpscQueueTest, ConcurrentProducerConsumerWithPopWait) {
    constexpr int NUM_ITEMS = 1000;
    SpscQueue<int> q(32);

    std::vector<int> received;
    received.reserve(NUM_ITEMS);

    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            while (!q.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            int val;
            bool ok = q.pop_wait(val, std::chrono::seconds(5));
            EXPECT_TRUE(ok);
            received.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(NUM_ITEMS));
    for (int i = 0; i < NUM_ITEMS; ++i) {
        EXPECT_EQ(received[i], i);
    }
}
