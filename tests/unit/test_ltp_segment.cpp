#include "ltp_cla/ltp_segment.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <optional>
#include <vector>

using namespace ltp;

// Helper: encode then decode, return decoded segment
static std::optional<LtpSegment> round_trip(const LtpSegment& seg) {
    auto bytes = seg.encode();
    return LtpSegment::decode(bytes.data(), bytes.size());
}

// ---------------------------------------------------------------------------
// Segment type helper tests
// ---------------------------------------------------------------------------

TEST(SegTypeTest, ValidTypes) {
    for (uint8_t t : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13}) {
        EXPECT_TRUE(SegType::is_valid(t)) << "type " << (int)t;
    }
    for (uint8_t t : {10, 11, 14, 15}) {
        EXPECT_FALSE(SegType::is_valid(t)) << "type " << (int)t;
    }
}

TEST(SegTypeTest, DataClassification) {
    EXPECT_TRUE(SegType::is_data(0));
    EXPECT_TRUE(SegType::is_data(3));
    EXPECT_TRUE(SegType::is_data(5));
    EXPECT_FALSE(SegType::is_data(6));
    EXPECT_FALSE(SegType::is_data(8));
}

TEST(SegTypeTest, RedGreenClassification) {
    EXPECT_TRUE(SegType::is_red(0));
    EXPECT_TRUE(SegType::is_red(3));
    EXPECT_FALSE(SegType::is_red(4));
    EXPECT_TRUE(SegType::is_green(4));
    EXPECT_TRUE(SegType::is_green(5));
    EXPECT_FALSE(SegType::is_green(3));
}

TEST(SegTypeTest, CheckpointFlags) {
    EXPECT_FALSE(SegType::has_checkpoint(0));
    EXPECT_TRUE(SegType::has_checkpoint(1));
    EXPECT_TRUE(SegType::has_checkpoint(2));
    EXPECT_TRUE(SegType::has_checkpoint(3));
    EXPECT_FALSE(SegType::has_checkpoint(4));
}

TEST(SegTypeTest, EorpEobFlags) {
    EXPECT_FALSE(SegType::has_eorp(0));
    EXPECT_TRUE(SegType::has_eorp(2));
    EXPECT_TRUE(SegType::has_eorp(3));
    EXPECT_FALSE(SegType::has_eob(0));
    EXPECT_TRUE(SegType::has_eob(3));
    EXPECT_TRUE(SegType::has_eob(5));
}

// ---------------------------------------------------------------------------
// Red data segment (type 0 — no checkpoint)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, RedDataNoCheckpoint) {
    LtpSegment seg;
    seg.segment_type = SegType::RED_DATA;
    seg.engine_id = 1;
    seg.session_number = 42;
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 0;
    ds.data = {0xDE, 0xAD, 0xBE, 0xEF};
    ds.length = ds.data.size();
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
    auto& dds = std::get<DataSegContent>(decoded->content);
    EXPECT_FALSE(dds.checkpoint_serial.has_value());
}

// ---------------------------------------------------------------------------
// Red data segment with checkpoint (type 1)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, RedDataWithCheckpoint) {
    LtpSegment seg;
    seg.segment_type = SegType::RED_DATA_CP;
    seg.engine_id = 10;
    seg.session_number = 100;
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 1400;
    ds.data = {0x01, 0x02, 0x03};
    ds.length = ds.data.size();
    ds.checkpoint_serial = 5;
    ds.report_serial = 0;
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Red data EORP+EOB (type 3) — final red segment
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, RedDataEorpEob) {
    LtpSegment seg;
    seg.segment_type = SegType::RED_DATA_CP_EORP_EOB;
    seg.engine_id = 1;
    seg.session_number = 1;
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 0;
    ds.data = {0xFF};
    ds.length = 1;
    ds.checkpoint_serial = 1;
    ds.report_serial = 0;
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Green data (type 4 — no EOB)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, GreenDataNoEob) {
    LtpSegment seg;
    seg.segment_type = SegType::GREEN_DATA;
    seg.engine_id = 2;
    seg.session_number = 7;
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 0;
    ds.data = {0xAA, 0xBB};
    ds.length = ds.data.size();
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Green data EOB (type 5)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, GreenDataEob) {
    LtpSegment seg;
    seg.segment_type = SegType::GREEN_DATA_EOB;
    seg.engine_id = 2;
    seg.session_number = 7;
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 100;
    ds.data = {0xCC};
    ds.length = 1;
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Report segment (type 6)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, ReportSegment) {
    LtpSegment seg;
    seg.segment_type = SegType::REPORT_SEGMENT;
    seg.engine_id = 1;
    seg.session_number = 42;
    ReportSegContent rs;
    rs.report_serial = 1;
    rs.checkpoint_serial = 1;
    rs.upper_bound = 2800;
    rs.lower_bound = 0;
    rs.claims = {{0, 1400}, {1500, 1300}};
    seg.content = rs;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
    auto& drs = std::get<ReportSegContent>(decoded->content);
    EXPECT_EQ(drs.claims.size(), 2u);
}

// ---------------------------------------------------------------------------
// Report segment with no claims (empty report)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, ReportSegmentNoClaims) {
    LtpSegment seg;
    seg.segment_type = SegType::REPORT_SEGMENT;
    seg.engine_id = 5;
    seg.session_number = 10;
    ReportSegContent rs;
    rs.report_serial = 1;
    rs.checkpoint_serial = 1;
    rs.upper_bound = 1400;
    rs.lower_bound = 0;
    seg.content = rs;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Report acknowledgment (type 7)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, ReportAck) {
    LtpSegment seg;
    seg.segment_type = SegType::REPORT_ACK;
    seg.engine_id = 1;
    seg.session_number = 42;
    seg.content = RptAckContent{3};

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Cancel by sender (type 8)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, CancelBySender) {
    LtpSegment seg;
    seg.segment_type = SegType::CANCEL_BY_SENDER;
    seg.engine_id = 1;
    seg.session_number = 99;
    seg.content = CancelContent{0x01};

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Cancel ack to sender (type 9) — no content body
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, CancelAckToSender) {
    LtpSegment seg;
    seg.segment_type = SegType::CANCEL_ACK_TO_SENDER;
    seg.engine_id = 1;
    seg.session_number = 99;
    seg.content = CancelContent{0};

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Cancel by receiver (type 12)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, CancelByReceiver) {
    LtpSegment seg;
    seg.segment_type = SegType::CANCEL_BY_RECEIVER;
    seg.engine_id = 2;
    seg.session_number = 50;
    seg.content = CancelContent{0x02};

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Cancel ack to receiver (type 13) — no content body
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, CancelAckToReceiver) {
    LtpSegment seg;
    seg.segment_type = SegType::CANCEL_ACK_TO_RECEIVER;
    seg.engine_id = 2;
    seg.session_number = 50;
    seg.content = CancelContent{0};

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Segment with header and trailer extensions
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, WithExtensions) {
    LtpSegment seg;
    seg.segment_type = SegType::RED_DATA_CP_EORP_EOB;
    seg.engine_id = 1;
    seg.session_number = 1;
    seg.header_ext_count = 2;
    seg.header_extensions = {
        {0x01, {0xAA, 0xBB}},
        {0x02, {0xCC}},
    };
    seg.trailer_ext_count = 1;
    seg.trailer_extensions = {
        {0x03, {0xDD, 0xEE, 0xFF}},
    };
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 0;
    ds.data = {0x42};
    ds.length = 1;
    ds.checkpoint_serial = 1;
    ds.report_serial = 0;
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
    EXPECT_EQ(decoded->header_extensions.size(), 2u);
    EXPECT_EQ(decoded->trailer_extensions.size(), 1u);
}

// ---------------------------------------------------------------------------
// Invalid version
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, InvalidVersion) {
    LtpSegment seg;
    seg.version = 1;  // invalid — must be 0
    seg.segment_type = SegType::RED_DATA;
    seg.engine_id = 1;
    seg.session_number = 1;
    seg.content = DataSegContent{1, 0, 1, {}, {}, {0x00}};

    auto bytes = seg.encode();
    auto decoded = LtpSegment::decode(bytes.data(), bytes.size());
    EXPECT_FALSE(decoded.has_value());
}

// ---------------------------------------------------------------------------
// Invalid segment type code
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, InvalidSegmentType) {
    // Manually craft a segment with type 10 (invalid)
    std::vector<uint8_t> bytes = {
        0x0A,  // version 0, type 10
        0x01,  // engine_id = 1
        0x01,  // session_number = 1
        0x00,  // header ext count
        0x00,  // trailer ext count
    };
    auto decoded = LtpSegment::decode(bytes.data(), bytes.size());
    EXPECT_FALSE(decoded.has_value());
}

// ---------------------------------------------------------------------------
// Truncated segment (too short)
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, TruncatedSegment) {
    std::vector<uint8_t> bytes = {0x00};  // just control byte, no session ID
    auto decoded = LtpSegment::decode(bytes.data(), bytes.size());
    EXPECT_FALSE(decoded.has_value());
}

// ---------------------------------------------------------------------------
// Empty data
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, EmptyData) {
    auto decoded = LtpSegment::decode(nullptr, 0);
    EXPECT_FALSE(decoded.has_value());
}

// ---------------------------------------------------------------------------
// Data segment with empty payload
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, DataSegmentEmptyPayload) {
    LtpSegment seg;
    seg.segment_type = SegType::GREEN_DATA_EOB;
    seg.engine_id = 1;
    seg.session_number = 1;
    DataSegContent ds;
    ds.client_service_id = 1;
    ds.offset = 0;
    ds.length = 0;
    seg.content = ds;

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}

// ---------------------------------------------------------------------------
// Large engine ID and session number
// ---------------------------------------------------------------------------

TEST(LtpSegmentTest, LargeSessionId) {
    LtpSegment seg;
    seg.segment_type = SegType::REPORT_ACK;
    seg.engine_id = 1000000;
    seg.session_number = 9999999;
    seg.content = RptAckContent{42};

    auto decoded = round_trip(seg);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, seg);
}
