// Feature: bp-ltp-dtn, Property 11: LTP Session Number Monotonicity
// Feature: bp-ltp-dtn, Property 7: LTP Segmentation Size Invariant
// Feature: bp-ltp-dtn, Property 6: LTP Segmentation/Reassembly Round-Trip

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "ltp_cla/ltp_engine.h"

#include <cstdint>
#include <numeric>
#include <vector>

using namespace ltp;

// ---------------------------------------------------------------------------
// Property 11: Session Number Monotonicity
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpEngineProperty11, SessionNumberMonotonicity, ()) {
    auto count = *rc::gen::inRange(2, 50);

    LtpEngineConfig cfg;
    cfg.local_engine_id = 1;
    cfg.max_concurrent_sessions = static_cast<uint32_t>(count + 10);
    cfg.max_segment_size = 1400;
    LtpEngine engine(cfg);

    std::vector<uint64_t> session_numbers;
    for (int i = 0; i < count; ++i) {
        std::vector<uint8_t> data(10, static_cast<uint8_t>(i));
        uint64_t sn = engine.start_session(data, true);
        RC_ASSERT(sn != 0);
        session_numbers.push_back(sn);
    }

    // Verify strictly monotonically increasing
    for (size_t i = 1; i < session_numbers.size(); ++i) {
        RC_ASSERT(session_numbers[i] > session_numbers[i - 1]);
    }
}

// ---------------------------------------------------------------------------
// Property 7: Segmentation Size Invariant
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpEngineProperty7, SegmentationSizeInvariant, ()) {
    auto max_seg = *rc::gen::inRange<uint32_t>(1, 2000);
    auto data_size = *rc::gen::inRange<size_t>(0, 10000);

    LtpEngineConfig cfg;
    cfg.local_engine_id = 1;
    cfg.max_segment_size = max_seg;
    cfg.max_concurrent_sessions = 10;
    LtpEngine engine(cfg);

    std::vector<LtpSegment> sent;
    engine.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg && SegType::is_data(seg->segment_type)) sent.push_back(*seg);
    });

    std::vector<uint8_t> data(data_size, 0xAB);
    uint64_t sn = engine.start_session(data, true);  // use red to keep session
    RC_ASSERT(sn != 0);

    for (const auto& seg : sent) {
        auto& ds = std::get<DataSegContent>(seg.content);
        RC_ASSERT(ds.data.size() <= max_seg);
        RC_ASSERT(ds.length == ds.data.size());
    }
}

// ---------------------------------------------------------------------------
// Property 6: Segmentation/Reassembly Round-Trip
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpEngineProperty6, SegmentationReassemblyRoundTrip, ()) {
    auto max_seg = *rc::gen::inRange<uint32_t>(1, 2000);
    auto data = *rc::gen::container<std::vector<uint8_t>>(
        rc::gen::arbitrary<uint8_t>());
    if (data.size() > 5000) data.resize(5000);

    LtpEngineConfig cfg;
    cfg.local_engine_id = 1;
    cfg.max_segment_size = max_seg;
    cfg.max_concurrent_sessions = 10;
    LtpEngine engine(cfg);

    std::vector<LtpSegment> sent;
    engine.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg && SegType::is_data(seg->segment_type)) sent.push_back(*seg);
    });

    uint64_t sn = engine.start_session(data, true);  // red to keep session
    RC_ASSERT(sn != 0);

    // Reassemble
    std::vector<uint8_t> reassembled;
    for (const auto& seg : sent) {
        auto& ds = std::get<DataSegContent>(seg.content);
        RC_ASSERT(ds.offset == reassembled.size());
        reassembled.insert(reassembled.end(), ds.data.begin(), ds.data.end());
    }

    RC_ASSERT(reassembled == data);
}

// ---------------------------------------------------------------------------
// Property 12: Red Final Segment Has EORP+Checkpoint
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpEngineProperty12, RedFinalSegmentHasCheckpoint, ()) {
    auto max_seg = *rc::gen::inRange<uint32_t>(1, 2000);
    auto data_size = *rc::gen::inRange<size_t>(0, 5000);

    LtpEngineConfig cfg;
    cfg.local_engine_id = 1;
    cfg.max_segment_size = max_seg;
    cfg.max_concurrent_sessions = 10;
    LtpEngine engine(cfg);

    std::vector<uint8_t> data(data_size, 0xAB);
    uint64_t sn = engine.start_session(data, true);  // red
    RC_ASSERT(sn != 0);

    auto* session = engine.get_session(cfg.local_engine_id, sn);
    RC_ASSERT(session != nullptr);

    auto segments = engine.segment_data_readonly(*session);
    RC_ASSERT(!segments.empty());

    // Final segment must have EORP + checkpoint
    auto& last = segments.back();
    RC_ASSERT(SegType::has_checkpoint(last.segment_type));
    RC_ASSERT(SegType::has_eorp(last.segment_type));
    RC_ASSERT(SegType::has_eob(last.segment_type));

    // Non-final segments must NOT have EORP
    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        RC_ASSERT(!SegType::has_eorp(segments[i].segment_type));
        RC_ASSERT(!SegType::has_checkpoint(segments[i].segment_type));
    }
}

// ---------------------------------------------------------------------------
// Property 13: Green Data Segments Have No Checkpoint
// ---------------------------------------------------------------------------

RC_GTEST_PROP(LtpEngineProperty13, GreenSegmentsNoCheckpoint, ()) {
    auto max_seg = *rc::gen::inRange<uint32_t>(1, 2000);
    auto data_size = *rc::gen::inRange<size_t>(0, 5000);

    LtpEngineConfig cfg;
    cfg.local_engine_id = 1;
    cfg.max_segment_size = max_seg;
    cfg.max_concurrent_sessions = 10;
    LtpEngine engine(cfg);

    std::vector<LtpSegment> sent;
    engine.set_send_segment_callback([&](std::vector<uint8_t> enc) {
        auto seg = LtpSegment::decode(enc.data(), enc.size());
        if (seg) sent.push_back(*seg);
    });

    std::vector<uint8_t> data(data_size, 0xCD);
    uint64_t sn = engine.start_session(data, false);  // green
    RC_ASSERT(sn != 0);
    RC_ASSERT(!sent.empty());

    // No green segment should have a checkpoint flag
    for (const auto& seg : sent) {
        RC_ASSERT(!SegType::has_checkpoint(seg.segment_type));
        auto& ds = std::get<DataSegContent>(seg.content);
        RC_ASSERT(!ds.checkpoint_serial.has_value());
    }

    // Final segment must have EOB
    RC_ASSERT(SegType::has_eob(sent.back().segment_type));

    // Non-final segments must NOT have EOB
    for (size_t i = 0; i + 1 < sent.size(); ++i) {
        RC_ASSERT(!SegType::has_eob(sent[i].segment_type));
    }
}
