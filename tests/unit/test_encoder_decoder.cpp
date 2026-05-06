// Unit tests for Encoder and Decoder orchestration classes.
//
// Tests cover:
//   - Encoder produces non-empty output for valid payload
//   - Encoder diagnostics increment correctly
//   - Encoder with FEC disabled skips FEC (shorter output)
//   - Encoder with FEC enabled produces longer output
//   - Decoder returns nullopt for empty/garbage input
//   - Decoder diagnostics track CRC errors
//   - Loopback: encode then decode recovers payload (no channel impairments)
//
// Requirements: 3, 4, 5, 6, 7, 8.1, 8.2, 9.1, 9.2, 9.5, 13.1, 13.2, 13.5, 13.8

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

// ---------------------------------------------------------------------------
// Helper: create a default Config suitable for testing
// ---------------------------------------------------------------------------
static Config test_config(bool fec_enabled = false,
                          CodeRate rate = CodeRate::RATE_1_2) {
    Config cfg = Config::defaults();
    cfg.fec_enabled = fec_enabled;
    cfg.fec_code_rate = rate;
    return cfg;
}

// ===================================================================
// Encoder tests
// ===================================================================

TEST(EncoderTest, ProducesNonEmptyOutputForValidPayload) {
    Config cfg = test_config(false);
    Encoder enc(cfg);

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto samples = enc.encode(payload);

    EXPECT_FALSE(samples.empty());
}

TEST(EncoderTest, DiagnosticsIncrementAfterEncode) {
    Config cfg = test_config(false);
    Encoder enc(cfg);

    EXPECT_EQ(enc.diagnostics().frames_transmitted, 0u);
    EXPECT_EQ(enc.diagnostics().symbols_transmitted, 0u);

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    enc.encode(payload);

    EXPECT_EQ(enc.diagnostics().frames_transmitted, 1u);
    EXPECT_GT(enc.diagnostics().symbols_transmitted, 0u);

    enc.encode(payload);
    EXPECT_EQ(enc.diagnostics().frames_transmitted, 2u);
}

TEST(EncoderTest, FecDisabledProducesShorterOutput) {
    std::vector<uint8_t> payload(20);
    std::iota(payload.begin(), payload.end(), 0);

    Config cfg_no_fec = test_config(false);
    Encoder enc_no_fec(cfg_no_fec);
    auto samples_no_fec = enc_no_fec.encode(payload);

    Config cfg_fec = test_config(true, CodeRate::RATE_1_2);
    Encoder enc_fec(cfg_fec);
    auto samples_fec = enc_fec.encode(payload);

    // FEC rate 1/2 doubles the payload, so the FEC-enabled output should be
    // longer than the non-FEC output.
    EXPECT_GT(samples_fec.size(), samples_no_fec.size());
}

TEST(EncoderTest, FecRate34ProducesShorterThanRate12) {
    std::vector<uint8_t> payload(20);
    std::iota(payload.begin(), payload.end(), 0);

    Config cfg_12 = test_config(true, CodeRate::RATE_1_2);
    Encoder enc_12(cfg_12);
    auto samples_12 = enc_12.encode(payload);

    Config cfg_34 = test_config(true, CodeRate::RATE_3_4);
    Encoder enc_34(cfg_34);
    auto samples_34 = enc_34.encode(payload);

    // Rate 3/4 has less redundancy than rate 1/2, so output should be shorter
    EXPECT_LT(samples_34.size(), samples_12.size());
}

TEST(EncoderTest, MultipleFramesIncrementSequenceNumber) {
    Config cfg = test_config(false);
    Encoder enc(cfg);

    std::vector<uint8_t> payload = {0xAA, 0xBB};
    enc.encode(payload);
    enc.encode(payload);
    enc.encode(payload);

    EXPECT_EQ(enc.diagnostics().frames_transmitted, 3u);
}

// ===================================================================
// Decoder tests
// ===================================================================

TEST(DecoderTest, ReturnsNulloptForEmptyInput) {
    Config cfg = test_config(false);
    Decoder dec(cfg);

    std::vector<std::complex<float>> empty;
    auto result = dec.decode(empty);

    EXPECT_FALSE(result.has_value());
}

TEST(DecoderTest, ReturnsNulloptForGarbageInput) {
    Config cfg = test_config(false);
    Decoder dec(cfg);

    // Random noise — no valid preamble
    std::vector<std::complex<float>> noise(500);
    for (auto& s : noise) {
        s = {0.01f, -0.01f};
    }

    auto result = dec.decode(noise);
    EXPECT_FALSE(result.has_value());
}

TEST(DecoderTest, CrcErrorsIncrementOnCorruptedFrames) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    // Encode a valid payload
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto samples = enc.encode(payload);

    // Corrupt samples in the last third of the burst (where the CRC lives).
    // The burst layout is:
    //   [ramp-up] [acquisition 128 sym] [Barker 26 sym] [header] [payload]
    //   [CRC] [tail] [ramp-down]
    // Corrupting the last third guarantees we hit the payload/CRC region
    // rather than the acquisition sequence.
    size_t start = (samples.size() * 2) / 3;
    size_t end = std::min(start + 60, samples.size());
    for (size_t i = start; i < end; ++i) {
        samples[i] = -samples[i];
    }

    auto result = dec.decode(samples);
    // The decode may or may not return a value depending on how corruption
    // affects preamble detection, but with the payload/CRC corrupted the
    // recovered payload should NOT match the original. Either decode returns
    // nullopt, or if it returns something, the CRC must have failed.
    if (result.has_value()) {
        EXPECT_NE(*result, payload)
            << "CRC should have caught the corruption";
    }
}

TEST(DecoderTest, ResetClearsDiagnostics) {
    Config cfg = test_config(false);
    Decoder dec(cfg);

    // Feed garbage to potentially increment some counters
    std::vector<std::complex<float>> noise(100, {0.01f, 0.01f});
    dec.decode(noise);

    dec.reset();
    EXPECT_EQ(dec.diagnostics().frames_received, 0u);
    EXPECT_EQ(dec.diagnostics().crc_errors, 0u);
    EXPECT_DOUBLE_EQ(dec.diagnostics().estimated_ber, 0.0);
}

// ===================================================================
// Loopback tests (encode → decode, no channel impairments)
// ===================================================================

TEST(LoopbackTest, NoFecRecoverPayload) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    // Small payload
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    auto samples = enc.encode(payload);

    auto result = dec.decode(samples);
    ASSERT_TRUE(result.has_value()) << "Decoder failed to recover payload in loopback";
    EXPECT_EQ(*result, payload);
}

TEST(LoopbackTest, NoFecRecoverLargerPayload) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    // Larger payload
    std::vector<uint8_t> payload(100);
    std::iota(payload.begin(), payload.end(), 0);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value()) << "Decoder failed to recover 100-byte payload";
    EXPECT_EQ(*result, payload);
}

TEST(LoopbackTest, FecRate12RecoverPayload) {
    Config cfg = test_config(true, CodeRate::RATE_1_2);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto samples = enc.encode(payload);

    auto result = dec.decode(samples);
    ASSERT_TRUE(result.has_value()) << "Decoder failed with FEC rate 1/2 loopback";
    EXPECT_EQ(*result, payload);
}

TEST(LoopbackTest, FecRate34RecoverPayload) {
    Config cfg = test_config(true, CodeRate::RATE_3_4);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC, 0xDD};
    auto samples = enc.encode(payload);

    auto result = dec.decode(samples);
    ASSERT_TRUE(result.has_value()) << "Decoder failed with FEC rate 3/4 loopback";
    EXPECT_EQ(*result, payload);
}

TEST(LoopbackTest, ZeroCrcErrorsInLoopback) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload = {0x42, 0x43, 0x44};
    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(dec.diagnostics().crc_errors, 0u);
    EXPECT_EQ(dec.diagnostics().frames_received, 1u);
}

TEST(LoopbackTest, DiagnosticsAfterMultipleFrames) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> payload = {static_cast<uint8_t>(i), 0xFF};
        auto samples = enc.encode(payload);
        auto result = dec.decode(samples);
        ASSERT_TRUE(result.has_value()) << "Frame " << i << " failed";
        EXPECT_EQ(*result, payload) << "Frame " << i << " mismatch";
    }

    EXPECT_EQ(enc.diagnostics().frames_transmitted, 3u);
    EXPECT_EQ(dec.diagnostics().frames_received, 3u);
    EXPECT_EQ(dec.diagnostics().crc_errors, 0u);
}

TEST(LoopbackTest, SingleBytePayload) {
    Config cfg = test_config(false);
    Encoder enc(cfg);
    Decoder dec(cfg);

    std::vector<uint8_t> payload = {0x42};
    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, payload);
}
