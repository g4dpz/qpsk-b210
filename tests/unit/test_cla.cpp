#include "ltp_cla/convergence_layer_adapter.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using namespace ltp;

// ===========================================================================
// Length-prefix framing tests (no network required)
// ===========================================================================

// ---------------------------------------------------------------------------
// Frame a single message
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, FrameSingleMessage) {
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto framed = frame_message(data);

    // 4-byte big-endian length (4) + 4 data bytes = 8 bytes
    ASSERT_EQ(framed.size(), 8u);
    EXPECT_EQ(framed[0], 0x00);
    EXPECT_EQ(framed[1], 0x00);
    EXPECT_EQ(framed[2], 0x00);
    EXPECT_EQ(framed[3], 0x04);
    EXPECT_EQ(framed[4], 0xDE);
    EXPECT_EQ(framed[5], 0xAD);
    EXPECT_EQ(framed[6], 0xBE);
    EXPECT_EQ(framed[7], 0xEF);
}

// ---------------------------------------------------------------------------
// Frame empty message
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, FrameEmptyMessage) {
    std::vector<uint8_t> data;
    auto framed = frame_message(data);

    ASSERT_EQ(framed.size(), 4u);
    EXPECT_EQ(framed[0], 0x00);
    EXPECT_EQ(framed[1], 0x00);
    EXPECT_EQ(framed[2], 0x00);
    EXPECT_EQ(framed[3], 0x00);
}

// ---------------------------------------------------------------------------
// Extract a complete framed message
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, ExtractCompleteMessage) {
    std::vector<uint8_t> buffer = {0x00, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> out;

    EXPECT_TRUE(extract_framed_message(buffer, out));
    EXPECT_EQ(out, (std::vector<uint8_t>{0xAA, 0xBB, 0xCC}));
    EXPECT_TRUE(buffer.empty());
}

// ---------------------------------------------------------------------------
// Extract from incomplete buffer (not enough data)
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, ExtractIncompleteData) {
    std::vector<uint8_t> buffer = {0x00, 0x00, 0x00, 0x05, 0xAA, 0xBB};
    std::vector<uint8_t> out;

    EXPECT_FALSE(extract_framed_message(buffer, out));
    EXPECT_EQ(buffer.size(), 6u);  // unchanged
}

// ---------------------------------------------------------------------------
// Extract from buffer too short for length prefix
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, ExtractTooShortForPrefix) {
    std::vector<uint8_t> buffer = {0x00, 0x00};
    std::vector<uint8_t> out;

    EXPECT_FALSE(extract_framed_message(buffer, out));
}

// ---------------------------------------------------------------------------
// Extract multiple messages from a single buffer
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, ExtractMultipleMessages) {
    // Two messages: [0xAA] and [0xBB, 0xCC]
    std::vector<uint8_t> buffer = {
        0x00, 0x00, 0x00, 0x01, 0xAA,
        0x00, 0x00, 0x00, 0x02, 0xBB, 0xCC
    };
    std::vector<uint8_t> out;

    EXPECT_TRUE(extract_framed_message(buffer, out));
    EXPECT_EQ(out, std::vector<uint8_t>({0xAA}));

    EXPECT_TRUE(extract_framed_message(buffer, out));
    EXPECT_EQ(out, (std::vector<uint8_t>{0xBB, 0xCC}));

    EXPECT_TRUE(buffer.empty());
}

// ---------------------------------------------------------------------------
// Frame then extract round-trip
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, FrameExtractRoundTrip) {
    std::vector<uint8_t> original = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto framed = frame_message(original);

    std::vector<uint8_t> buffer(framed.begin(), framed.end());
    std::vector<uint8_t> extracted;

    EXPECT_TRUE(extract_framed_message(buffer, extracted));
    EXPECT_EQ(extracted, original);
}

// ---------------------------------------------------------------------------
// Large message framing
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, LargeMessage) {
    std::vector<uint8_t> data(10000, 0x42);
    auto framed = frame_message(data);

    ASSERT_EQ(framed.size(), 10004u);
    // Check length prefix: 10000 = 0x00002710
    EXPECT_EQ(framed[0], 0x00);
    EXPECT_EQ(framed[1], 0x00);
    EXPECT_EQ(framed[2], 0x27);
    EXPECT_EQ(framed[3], 0x10);

    std::vector<uint8_t> buffer(framed.begin(), framed.end());
    std::vector<uint8_t> extracted;
    EXPECT_TRUE(extract_framed_message(buffer, extracted));
    EXPECT_EQ(extracted, data);
}

// ---------------------------------------------------------------------------
// Extract empty message
// ---------------------------------------------------------------------------

TEST(ClaFramingTest, ExtractEmptyMessage) {
    std::vector<uint8_t> buffer = {0x00, 0x00, 0x00, 0x00};
    std::vector<uint8_t> out;

    EXPECT_TRUE(extract_framed_message(buffer, out));
    EXPECT_TRUE(out.empty());
    EXPECT_TRUE(buffer.empty());
}

// ===========================================================================
// CLA config defaults
// ===========================================================================

TEST(ClaConfigTest, Defaults) {
    ClaConfig cfg;
    EXPECT_EQ(cfg.tx_addr, "127.0.0.1");
    EXPECT_EQ(cfg.tx_port, 5000);
    EXPECT_EQ(cfg.rx_addr, "127.0.0.1");
    EXPECT_EQ(cfg.rx_port, 5001);
    EXPECT_EQ(cfg.initial_reconnect_delay_ms, 1000u);
    EXPECT_EQ(cfg.max_reconnect_delay_ms, 30000u);
}
