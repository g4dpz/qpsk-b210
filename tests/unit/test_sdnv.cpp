#include "ltp_cla/sdnv.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Helper: convert Encoded to vector for comparison
static std::vector<uint8_t> to_vec(const sdnv::Encoded& enc) {
    return {enc.bytes.data(), enc.bytes.data() + enc.length};
}

// Helper: decode from a vector
static uint64_t decode_vec(const std::vector<uint8_t>& v, size_t& offset) {
    return sdnv::decode(v.data(), v.size(), offset);
}

// ---------------------------------------------------------------------------
// Encode — boundary values and byte counts
// ---------------------------------------------------------------------------

TEST(SdnvEncodeTest, Zero) {
    auto encoded = sdnv::encode(0);
    EXPECT_EQ(to_vec(encoded), std::vector<uint8_t>({0x00}));
    EXPECT_EQ(encoded.length, 1u);
}

TEST(SdnvEncodeTest, MaxOneByte_127) {
    auto encoded = sdnv::encode(127);
    EXPECT_EQ(to_vec(encoded), std::vector<uint8_t>({0x7F}));
    EXPECT_EQ(encoded.length, 1u);
}

TEST(SdnvEncodeTest, MinTwoBytes_128) {
    auto encoded = sdnv::encode(128);
    // 128 = 0b10000000 → two 7-bit groups: [0000001] [0000000]
    // Big-endian: 0x81 0x00
    EXPECT_EQ(to_vec(encoded), (std::vector<uint8_t>{0x81, 0x00}));
    EXPECT_EQ(encoded.length, 2u);
}

TEST(SdnvEncodeTest, MaxTwoBytes_16383) {
    auto encoded = sdnv::encode(16383);
    // 16383 = 0x3FFF = 0b11111111111111 → two 7-bit groups: [1111111] [1111111]
    // Big-endian: 0xFF 0x7F
    EXPECT_EQ(to_vec(encoded), (std::vector<uint8_t>{0xFF, 0x7F}));
    EXPECT_EQ(encoded.length, 2u);
}

TEST(SdnvEncodeTest, MinThreeBytes_16384) {
    auto encoded = sdnv::encode(16384);
    // 16384 = 0x4000 = 0b100000000000000 → three 7-bit groups: [0000001] [0000000] [0000000]
    // Big-endian: 0x81 0x80 0x00
    EXPECT_EQ(to_vec(encoded), (std::vector<uint8_t>{0x81, 0x80, 0x00}));
    EXPECT_EQ(encoded.length, 3u);
}

TEST(SdnvEncodeTest, MaxValue_2pow63minus1) {
    // 2^63 - 1 = 9223372036854775807
    uint64_t max_val = UINT64_C(9223372036854775807);
    auto encoded = sdnv::encode(max_val);
    // Should require 9 bytes (63 bits / 7 bits per byte = 9 bytes)
    EXPECT_EQ(encoded.length, 9u);
    // Last byte should have continuation bit clear
    EXPECT_EQ(encoded.bytes[encoded.length - 1] & 0x80, 0);
    // All other bytes should have continuation bit set
    for (size_t i = 0; i < encoded.length - 1; ++i) {
        EXPECT_EQ(encoded.bytes[i] & 0x80, 0x80) << "Byte " << i << " should have continuation bit set";
    }
}

TEST(SdnvEncodeTest, SmallValues_ByteCounts) {
    EXPECT_EQ(sdnv::encode(0).length, 1u);
    EXPECT_EQ(sdnv::encode(1).length, 1u);
    EXPECT_EQ(sdnv::encode(127).length, 1u);
    EXPECT_EQ(sdnv::encode(128).length, 2u);
    EXPECT_EQ(sdnv::encode(16383).length, 2u);
    EXPECT_EQ(sdnv::encode(16384).length, 3u);
    EXPECT_EQ(sdnv::encode(2097151).length, 3u);
    EXPECT_EQ(sdnv::encode(2097152).length, 4u);
}

// ---------------------------------------------------------------------------
// Decode — boundary values
// ---------------------------------------------------------------------------

TEST(SdnvDecodeTest, Zero) {
    std::vector<uint8_t> data = {0x00};
    size_t offset = 0;
    EXPECT_EQ(decode_vec(data, offset), 0u);
    EXPECT_EQ(offset, 1u);
}

TEST(SdnvDecodeTest, MaxOneByte_127) {
    std::vector<uint8_t> data = {0x7F};
    size_t offset = 0;
    EXPECT_EQ(decode_vec(data, offset), 127u);
    EXPECT_EQ(offset, 1u);
}

TEST(SdnvDecodeTest, MinTwoBytes_128) {
    std::vector<uint8_t> data = {0x81, 0x00};
    size_t offset = 0;
    EXPECT_EQ(decode_vec(data, offset), 128u);
    EXPECT_EQ(offset, 2u);
}

TEST(SdnvDecodeTest, MaxTwoBytes_16383) {
    std::vector<uint8_t> data = {0xFF, 0x7F};
    size_t offset = 0;
    EXPECT_EQ(decode_vec(data, offset), 16383u);
    EXPECT_EQ(offset, 2u);
}

TEST(SdnvDecodeTest, MinThreeBytes_16384) {
    std::vector<uint8_t> data = {0x81, 0x80, 0x00};
    size_t offset = 0;
    EXPECT_EQ(decode_vec(data, offset), 16384u);
    EXPECT_EQ(offset, 3u);
}

TEST(SdnvDecodeTest, MaxValue_2pow63minus1) {
    uint64_t max_val = UINT64_C(9223372036854775807);
    auto encoded = sdnv::encode(max_val);
    auto vec = to_vec(encoded);
    size_t offset = 0;
    EXPECT_EQ(decode_vec(vec, offset), max_val);
    EXPECT_EQ(offset, encoded.length);
}

// ---------------------------------------------------------------------------
// Decode — offset advancement (multiple SDNVs in a buffer)
// ---------------------------------------------------------------------------

TEST(SdnvDecodeTest, MultipleValuesInBuffer) {
    // Encode 0, 128, 16384 back-to-back
    std::vector<uint8_t> buffer;
    sdnv::encode_into(0, buffer);
    sdnv::encode_into(128, buffer);
    sdnv::encode_into(16384, buffer);

    size_t offset = 0;
    EXPECT_EQ(decode_vec(buffer, offset), 0u);
    EXPECT_EQ(decode_vec(buffer, offset), 128u);
    EXPECT_EQ(decode_vec(buffer, offset), 16384u);
    EXPECT_EQ(offset, buffer.size());
}

// ---------------------------------------------------------------------------
// Decode — error: truncated SDNV
// ---------------------------------------------------------------------------

TEST(SdnvDecodeTest, TruncatedSdnv_SingleContinuationByte) {
    std::vector<uint8_t> data = {0x81};
    size_t offset = 0;
    EXPECT_THROW(decode_vec(data, offset), std::runtime_error);
}

TEST(SdnvDecodeTest, TruncatedSdnv_MultipleContinuationBytes) {
    std::vector<uint8_t> data = {0x81, 0x80};
    size_t offset = 0;
    EXPECT_THROW(decode_vec(data, offset), std::runtime_error);
}

TEST(SdnvDecodeTest, TruncatedSdnv_DescriptiveMessage) {
    std::vector<uint8_t> data = {0x81};
    size_t offset = 0;
    try {
        decode_vec(data, offset);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("truncated"), std::string::npos)
            << "Error message should mention 'truncated': " << msg;
    }
}

// ---------------------------------------------------------------------------
// Decode — error: oversized SDNV (>10 bytes)
// ---------------------------------------------------------------------------

TEST(SdnvDecodeTest, OversizedSdnv_11Bytes) {
    std::vector<uint8_t> data(11, 0x80);
    data.back() = 0x01;
    size_t offset = 0;
    EXPECT_THROW(decode_vec(data, offset), std::runtime_error);
}

TEST(SdnvDecodeTest, OversizedSdnv_DescriptiveMessage) {
    std::vector<uint8_t> data(11, 0x80);
    data.back() = 0x01;
    size_t offset = 0;
    try {
        decode_vec(data, offset);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("10"), std::string::npos)
            << "Error message should mention '10' byte limit: " << msg;
    }
}

// ---------------------------------------------------------------------------
// Decode — error: empty data
// ---------------------------------------------------------------------------

TEST(SdnvDecodeTest, EmptyData) {
    std::vector<uint8_t> data;
    size_t offset = 0;
    EXPECT_THROW(decode_vec(data, offset), std::runtime_error);
}

TEST(SdnvDecodeTest, OffsetPastEnd) {
    std::vector<uint8_t> data = {0x00};
    size_t offset = 1;
    EXPECT_THROW(decode_vec(data, offset), std::runtime_error);
}
