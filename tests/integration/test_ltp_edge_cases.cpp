/// Edge-case integration tests for the LTP CLA.
///
/// These test conditions that occur on real RF links: duplicate delivery,
/// out-of-order segments, boundary payloads, resource exhaustion, and
/// interleaved sessions with loss.

#include "ltp_cla/ltp_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <numeric>
#include <random>
#include <vector>

using namespace ltp;

// ===========================================================================
// Duplicate segment delivery
// ===========================================================================

// ---------------------------------------------------------------------------
// Duplicate data segments — receiver should not double-count bytes
// ---------------------------------------------------------------------------

TEST(LtpEdgeCaseTest, DuplicateDataSegments) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 100;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 100;
    LtpEngine engine_b(cfg_b);

    // Deliver every segment to B twice
    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;
        engine_b.receive_segment(*seg);
        engine_b.receive_segment(*seg);  // duplicate
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    std::vector<uint8_t> payload(300);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(0));
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);
}

// ---------------------------------------------------------------------------
// Duplicate report segments — originator should not retransmit unnecessarily
// ---------------------------------------------------------------------------

TEST(LtpEdgeCaseTest, DuplicateReportSegments) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 1400;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 1400;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });

    // Deliver every report to A twice
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;
        engine_a.receive_segment(*seg);
        if (seg->segment_type == SegType::REPORT_SEGMENT) {
            engine_a.receive_segment(*seg);  // duplicate report
        }
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    std::vector<uint8_t> payload(500, 0xAA);
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);

    // Should have 0 retransmissions (duplicate report shouldn't trigger retx)
    auto diag = engine_a.get_diagnostics();
    EXPECT_EQ(diag.retransmissions, 0u);
}

// ---------------------------------------------------------------------------
// Duplicate report-ack — receiver should handle gracefully
// ---------------------------------------------------------------------------

TEST(LtpEdgeCaseTest, DuplicateReportAck) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 1400;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 1400;
    LtpEngine engine_b(cfg_b);

    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    // Deliver every report-ack to B twice
    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;
        engine_b.receive_segment(*seg);
        if (seg->segment_type == SegType::REPORT_ACK) {
            engine_b.receive_segment(*seg);  // duplicate ack
        }
    });

    int delivery_count = 0;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t>) {
        delivery_count++;
    });

    std::vector<uint8_t> payload(200, 0xBB);
    engine_a.start_session(payload, true);

    // Data should be delivered exactly once despite duplicate ack
    EXPECT_EQ(delivery_count, 1);
}

// ===========================================================================
// Out-of-order segment delivery
// ===========================================================================

TEST(LtpEdgeCaseTest, OutOfOrderSegments) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 100;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 100;
    LtpEngine engine_b(cfg_b);

    // Phase 1: collect initial data segments from A without delivering
    bool buffering = true;
    std::vector<LtpSegment> buffered_segments;

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;
        if (buffering) {
            buffered_segments.push_back(*seg);
        } else {
            engine_b.receive_segment(*seg);
        }
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    // 500 bytes → 5 segments
    std::vector<uint8_t> payload(500);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(0));
    engine_a.start_session(payload, true);

    ASSERT_FALSE(buffered_segments.empty());
    EXPECT_FALSE(complete);

    // Phase 2: switch to direct delivery for the handshake
    buffering = false;

    // Shuffle non-checkpoint segments, deliver checkpoint last
    std::vector<LtpSegment> non_cp;
    LtpSegment checkpoint_seg{};
    bool has_cp = false;
    for (auto& seg : buffered_segments) {
        if (SegType::has_checkpoint(seg.segment_type)) {
            checkpoint_seg = seg;
            has_cp = true;
        } else {
            non_cp.push_back(seg);
        }
    }

    std::mt19937 rng(42);
    std::shuffle(non_cp.begin(), non_cp.end(), rng);

    for (const auto& seg : non_cp) {
        engine_b.receive_segment(seg);
    }

    // Deliver checkpoint — this triggers: B sends report → A receives report,
    // sends report-ack (now direct) → B receives report-ack, delivers data
    if (has_cp) {
        engine_b.receive_segment(checkpoint_seg);
    }

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);
}

// ===========================================================================
// Boundary payloads
// ===========================================================================

// ---------------------------------------------------------------------------
// Payload exactly equal to max_segment_size
// ---------------------------------------------------------------------------

TEST(LtpEdgeCaseTest, ExactlyOneSegmentSize) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 256;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 256;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    // Exactly 256 bytes = exactly 1 segment
    std::vector<uint8_t> payload(256);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(0));
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);

    // Should be exactly 1 red segment sent
    auto diag = engine_a.get_diagnostics();
    EXPECT_EQ(diag.red_segments_sent, 1u);
    EXPECT_EQ(diag.checkpoints_sent, 1u);
}

// ---------------------------------------------------------------------------
// Single-byte payload
// ---------------------------------------------------------------------------

TEST(LtpEdgeCaseTest, SingleBytePayload) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 1400;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 1400;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    bool complete = false;
    engine_a.set_session_complete_callback([&](uint64_t) { complete = true; });

    std::vector<uint8_t> payload = {0x42};
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);
}

// ===========================================================================
// Max concurrent sessions
// ===========================================================================

TEST(LtpEdgeCaseTest, MaxConcurrentSessionsRecovery) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 1400;
    cfg_a.max_concurrent_sessions = 3;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 1400;
    cfg_b.max_concurrent_sessions = 10;
    LtpEngine engine_b(cfg_b);

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    int delivery_count = 0;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t>) {
        delivery_count++;
    });

    std::vector<uint8_t> data(100, 0xAA);

    // Fill up to max
    uint64_t sn1 = engine_a.start_session(data, true);
    uint64_t sn2 = engine_a.start_session(data, true);
    uint64_t sn3 = engine_a.start_session(data, true);
    EXPECT_NE(sn1, 0u);
    EXPECT_NE(sn2, 0u);
    EXPECT_NE(sn3, 0u);

    // All 3 should have completed (direct wire, no loss)
    // so sessions are freed immediately
    EXPECT_EQ(delivery_count, 3);
    EXPECT_EQ(engine_a.active_session_count(), 0u);

    // Should be able to start more now
    uint64_t sn4 = engine_a.start_session(data, true);
    EXPECT_NE(sn4, 0u);
    EXPECT_EQ(delivery_count, 4);
}

// ===========================================================================
// Interleaved sessions with loss
// ===========================================================================

TEST(LtpEdgeCaseTest, InterleavedSessionsWithLoss) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 100;
    cfg_a.max_concurrent_sessions = 10;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 100;
    cfg_b.max_concurrent_sessions = 10;
    LtpEngine engine_b(cfg_b);

    // Drop segment at offset 100 for session 1, offset 200 for session 2
    // (first pass only)
    std::atomic<int> session1_data_count{0};
    std::atomic<int> session2_data_count{0};
    std::atomic<bool> first_pass_s1{true};
    std::atomic<bool> first_pass_s2{true};

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;

        if (SegType::is_data(seg->segment_type)) {
            auto& ds = std::get<DataSegContent>(seg->content);

            if (seg->session_number == 1 && first_pass_s1.load()) {
                if (ds.offset == 100) return;  // drop for session 1
                if (SegType::has_checkpoint(seg->segment_type))
                    first_pass_s1 = false;
            }
            if (seg->session_number == 2 && first_pass_s2.load()) {
                if (ds.offset == 200) return;  // drop for session 2
                if (SegType::has_checkpoint(seg->segment_type))
                    first_pass_s2 = false;
            }
        }

        engine_b.receive_segment(*seg);
    });
    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    std::mutex mtx;
    std::vector<std::vector<uint8_t>> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        std::lock_guard<std::mutex> lock(mtx);
        delivered.push_back(std::move(data));
    });

    // Session 1: 400 bytes (drop at offset 100)
    std::vector<uint8_t> payload1(400);
    std::iota(payload1.begin(), payload1.end(), static_cast<uint8_t>(0x10));

    // Session 2: 400 bytes (drop at offset 200)
    std::vector<uint8_t> payload2(400);
    std::iota(payload2.begin(), payload2.end(), static_cast<uint8_t>(0x20));

    engine_a.start_session(payload1, true);
    engine_a.start_session(payload2, true);

    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_EQ(delivered.size(), 2u);

    // Both payloads should be delivered correctly (order may vary)
    bool found1 = false, found2 = false;
    for (const auto& d : delivered) {
        if (d == payload1) found1 = true;
        if (d == payload2) found2 = true;
    }
    EXPECT_TRUE(found1) << "Session 1 payload not delivered";
    EXPECT_TRUE(found2) << "Session 2 payload not delivered";

    // Both sessions should have required retransmission
    auto diag = engine_a.get_diagnostics();
    EXPECT_GE(diag.retransmissions, 2u);
    EXPECT_EQ(engine_a.active_session_count(), 0u);
    EXPECT_EQ(engine_b.active_session_count(), 0u);
}
