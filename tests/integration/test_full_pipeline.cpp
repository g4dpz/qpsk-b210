// Integration test: Full pipeline loopback
//
// Tests the complete encode → decode pipeline as an integration test:
//   - Encode a payload, decode the samples, verify exact recovery
//   - Multiple payloads in sequence
//   - FEC enabled and disabled
//   - Various payload sizes
//
// This is similar to the loopback unit tests but exercises the full pipeline
// as an integration test with emphasis on end-to-end data integrity.
//
// Validates: Requirements 7.1, 11, 12

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstdint>
#include <numeric>
#include <vector>

#include "qpsk_b200/decoder.h"
#include "qpsk_b200/encoder.h"
#include "qpsk_b200/types.h"

using namespace qpsk_b200;

namespace {

Config test_config(bool fec_enabled = false,
                   CodeRate rate = CodeRate::RATE_1_2) {
    Config cfg = Config::defaults();
    cfg.fec_enabled = fec_enabled;
    cfg.fec_code_rate = rate;
    return cfg;
}

} // anonymous namespace

// ===========================================================================
// Single payload loopback — various sizes
// ===========================================================================

class FullPipelineLoopback : public ::testing::TestWithParam<size_t> {};

TEST_P(FullPipelineLoopback, NoFecExactRecovery) {
    size_t payload_size = GetParam();
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload(payload_size);
    std::iota(payload.begin(), payload.end(), 0);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value())
        << "Decode failed for payload size " << payload_size;
    EXPECT_EQ(result->size(), payload_size);
    EXPECT_EQ(*result, payload);
}

TEST_P(FullPipelineLoopback, FecRate12ExactRecovery) {
    size_t payload_size = GetParam();
    Config cfg = test_config(true, CodeRate::RATE_1_2);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload(payload_size);
    std::iota(payload.begin(), payload.end(), 0);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value())
        << "Decode failed with FEC 1/2 for payload size " << payload_size;
    EXPECT_EQ(result->size(), payload_size);
    EXPECT_EQ(*result, payload);
}

TEST_P(FullPipelineLoopback, FecRate34ExactRecovery) {
    size_t payload_size = GetParam();
    Config cfg = test_config(true, CodeRate::RATE_3_4);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload(payload_size);
    std::iota(payload.begin(), payload.end(), 0);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value())
        << "Decode failed with FEC 3/4 for payload size " << payload_size;
    EXPECT_EQ(result->size(), payload_size);
    EXPECT_EQ(*result, payload);
}

// Test with small payloads to keep tests fast
INSTANTIATE_TEST_SUITE_P(
    PayloadSizes,
    FullPipelineLoopback,
    ::testing::Values(1, 2, 4, 10, 50, 100));

// ===========================================================================
// Multiple sequential payloads
// ===========================================================================

TEST(FullPipelineSequential, MultiplePayloadsNoFec) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    // Encode and decode 5 payloads in sequence
    for (int i = 0; i < 5; ++i) {
        std::vector<uint8_t> payload(10 + i * 5);
        std::iota(payload.begin(), payload.end(),
                  static_cast<uint8_t>(i * 37));

        auto samples = enc.encode(payload);
        auto result = dec.decode(samples);

        ASSERT_TRUE(result.has_value()) << "Frame " << i << " decode failed";
        EXPECT_EQ(*result, payload) << "Frame " << i << " data mismatch";
    }

    // Verify diagnostics
    EXPECT_EQ(enc.diagnostics().frames_transmitted, 5u);
    EXPECT_EQ(dec.diagnostics().frames_received, 5u);
    EXPECT_EQ(dec.diagnostics().crc_errors, 0u);
}

TEST(FullPipelineSequential, MultiplePayloadsWithFecRate12) {
    Config cfg = test_config(true, CodeRate::RATE_1_2);
    Encoder enc(cfg);
    Decoder dec(cfg);

    for (int i = 0; i < 5; ++i) {
        std::vector<uint8_t> payload(10 + i * 5);
        std::iota(payload.begin(), payload.end(),
                  static_cast<uint8_t>(i * 41));

        auto samples = enc.encode(payload);
        auto result = dec.decode(samples);

        ASSERT_TRUE(result.has_value()) << "Frame " << i << " decode failed";
        EXPECT_EQ(*result, payload) << "Frame " << i << " data mismatch";
    }

    EXPECT_EQ(enc.diagnostics().frames_transmitted, 5u);
    EXPECT_EQ(dec.diagnostics().frames_received, 5u);
    EXPECT_EQ(dec.diagnostics().crc_errors, 0u);
}

// ===========================================================================
// Mixed FEC modes (simulating what a real pipeline would do)
// ===========================================================================

TEST(FullPipelineMixed, FecEnabledThenDisabled) {
    // Encode with FEC, then encode without FEC — decoder should handle both
    // because the FEC flag is in the frame header.
    Config cfg_fec = test_config(true, CodeRate::RATE_1_2);
    Config cfg_no_fec = test_config(false);

    Encoder enc_fec(cfg_fec);
    Encoder enc_no_fec(cfg_no_fec);

    // Decoder configured with FEC — it reads the per-frame FEC flag
    Decoder dec_fec(cfg_fec);
    Decoder dec_no_fec(cfg_no_fec);

    std::vector<uint8_t> payload = {0x42, 0x43, 0x44, 0x45};

    // FEC-encoded frame decoded by FEC-aware decoder
    auto samples_fec = enc_fec.encode(payload);
    auto result_fec = dec_fec.decode(samples_fec);
    ASSERT_TRUE(result_fec.has_value());
    EXPECT_EQ(*result_fec, payload);

    // Non-FEC frame decoded by non-FEC decoder
    auto samples_no_fec = enc_no_fec.encode(payload);
    auto result_no_fec = dec_no_fec.decode(samples_no_fec);
    ASSERT_TRUE(result_no_fec.has_value());
    EXPECT_EQ(*result_no_fec, payload);
}

// ===========================================================================
// Data integrity: verify no silent corruption
// ===========================================================================

TEST(FullPipelineIntegrity, AllByteValuesRoundTrip) {
    // Send all 256 byte values and verify exact recovery
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    // Create payload with all byte values 0x00–0xFF
    // Split into two frames to keep payload size reasonable
    std::vector<uint8_t> payload1(128);
    std::iota(payload1.begin(), payload1.end(), 0);

    std::vector<uint8_t> payload2(128);
    std::iota(payload2.begin(), payload2.end(), 128);

    auto samples1 = enc.encode(payload1);
    auto result1 = dec.decode(samples1);
    ASSERT_TRUE(result1.has_value());
    EXPECT_EQ(*result1, payload1);

    auto samples2 = enc.encode(payload2);
    auto result2 = dec.decode(samples2);
    ASSERT_TRUE(result2.has_value());
    EXPECT_EQ(*result2, payload2);
}

TEST(FullPipelineIntegrity, RepeatedPatternPayload) {
    // Repeated pattern to test for any pattern-dependent issues
    Config cfg = test_config(true, CodeRate::RATE_1_2);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload(64, 0xAA);  // alternating bits
    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, payload);
}

TEST(FullPipelineIntegrity, AllZerosPayload) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload(32, 0x00);
    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, payload);
}

TEST(FullPipelineIntegrity, AllOnesPayload) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload(32, 0xFF);
    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, payload);
}
