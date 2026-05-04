/// Integration test: Reliable transfer with simulated segment loss.
///
/// A "lossy wire" drops a configurable percentage of data segments.
/// Verifies that the checkpoint/report handshake recovers the lost data.

#include "ltp_cla/ltp_engine.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <numeric>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Test: reliable transfer with 1 dropped segment
// ---------------------------------------------------------------------------

TEST(LtpReliableLossTest, SingleSegmentDrop) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.remote_engine_id = 2;
    cfg_a.max_segment_size = 100;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.remote_engine_id = 1;
    cfg_b.max_segment_size = 100;
    LtpEngine engine_b(cfg_b);

    // Drop the segment at offset 100 on first pass
    std::atomic<bool> first_pass{true};

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;

        if (SegType::is_data(seg->segment_type) && first_pass.load()) {
            auto& ds = std::get<DataSegContent>(seg->content);
            if (ds.offset == 100) {
                return;  // drop
            }
        }
        if (SegType::has_checkpoint(seg->segment_type)) {
            first_pass = false;
        }
        engine_b.receive_segment(*seg);
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

    std::vector<uint8_t> payload(400);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(0));
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);

    auto diag = engine_a.get_diagnostics();
    EXPECT_GE(diag.retransmissions, 1u);
}

// ---------------------------------------------------------------------------
// Test: reliable transfer with multiple dropped segments
// ---------------------------------------------------------------------------

TEST(LtpReliableLossTest, MultipleSegmentDrops) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.remote_engine_id = 2;
    cfg_a.max_segment_size = 100;
    cfg_a.max_retransmissions = 10;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.remote_engine_id = 1;
    cfg_b.max_segment_size = 100;
    LtpEngine engine_b(cfg_b);

    // Drop segments at offsets 100 and 300 on first pass
    std::atomic<bool> first_pass{true};

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;

        if (SegType::is_data(seg->segment_type) && first_pass.load()) {
            auto& ds = std::get<DataSegContent>(seg->content);
            if (ds.offset == 100 || ds.offset == 300) {
                return;  // drop
            }
        }
        if (SegType::has_checkpoint(seg->segment_type)) {
            first_pass = false;
        }
        engine_b.receive_segment(*seg);
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

    std::vector<uint8_t> payload(500);
    std::iota(payload.begin(), payload.end(), static_cast<uint8_t>(42));
    engine_a.start_session(payload, true);

    EXPECT_TRUE(complete);
    EXPECT_EQ(delivered, payload);
}

// ---------------------------------------------------------------------------
// Test: data integrity preserved through loss and recovery
// ---------------------------------------------------------------------------

TEST(LtpReliableLossTest, DataIntegrityPreserved) {
    LtpEngineConfig cfg_a;
    cfg_a.local_engine_id = 1;
    cfg_a.max_segment_size = 50;
    LtpEngine engine_a(cfg_a);

    LtpEngineConfig cfg_b;
    cfg_b.local_engine_id = 2;
    cfg_b.max_segment_size = 50;
    LtpEngine engine_b(cfg_b);

    // Drop every other data segment on first pass
    int seg_count = 0;
    std::atomic<bool> first_pass{true};

    engine_a.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (!seg) return;

        if (SegType::is_data(seg->segment_type) && first_pass.load()) {
            seg_count++;
            if (seg_count % 2 == 0 && !SegType::has_checkpoint(seg->segment_type)) {
                return;  // drop every other non-checkpoint segment
            }
        }
        if (SegType::has_checkpoint(seg->segment_type)) {
            first_pass = false;
        }
        engine_b.receive_segment(*seg);
    });

    engine_b.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) engine_a.receive_segment(*seg);
    });

    std::vector<uint8_t> delivered;
    engine_b.set_data_arrival_callback([&](std::vector<uint8_t> data) {
        delivered = std::move(data);
    });

    // 300 bytes with 50-byte segments = 6 segments, drop 2
    std::vector<uint8_t> payload(300);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }
    engine_a.start_session(payload, true);

    EXPECT_EQ(delivered, payload);
}
