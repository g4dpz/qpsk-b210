// Feature: qpsk-b200-codec, Property 9: Frame structure invariants
//
// For any valid payload byte vector, the frame produced by FrameBuilder::build_frame()
// SHALL: (a) begin with the configured preamble sequence, (b) contain a header with
// the correct payload length, padding bit count, sequence number, FEC enabled flag,
// and FEC code rate, (c) end with a CRC-32 that matches the CRC-32 computed over
// the header and payload, and (d) have a total length consistent with the sum of
// preamble, header, payload (or FEC-coded payload), and CRC fields.
//
// **Validates: Requirements 6.1, 6.2, 6.3, 13.5**

// Feature: qpsk-b200-codec, Property 10: Preamble detection
//
// For any symbol stream containing an embedded preamble at a random position
// surrounded by random non-preamble symbols, the FrameSynchronizer::detect()
// method SHALL return the correct start index of the preamble within the stream.
//
// **Validates: Requirements 6.4**

// Feature: qpsk-b200-codec, Property 11: CRC error detection
//
// For any valid frame and any single or multi-bit corruption of the payload or
// header (at least one bit flipped), the CRC-32 verification SHALL detect the
// corruption and report a CRC mismatch.
//
// **Validates: Requirements 6.6**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/frame.h"
#include "qpsk_b200/frame_sync.h"
#include "qpsk_b200/types.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// 13-bit Barker sequence repeated twice (26 bits)
static const std::vector<uint8_t> PREAMBLE = {
    1,1,1,1,1,0,0,1,1,0,1,0,1,
    1,1,1,1,1,0,0,1,1,0,1,0,1
};

static constexpr size_t PREAMBLE_BITS  = 26;
static constexpr size_t HEADER_BITS    = 35;
static constexpr size_t CRC_BITS       = 32;

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a random byte payload of length 0–1000.
static rc::Gen<std::vector<uint8_t>> genPayload() {
    return rc::gen::mapcat(rc::gen::inRange(0, 1001), [](int len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(len),
            rc::gen::arbitrary<uint8_t>());
    });
}

/// Generate a random byte payload of length 1–500 (non-empty).
static rc::Gen<std::vector<uint8_t>> genNonEmptyPayload() {
    return rc::gen::mapcat(rc::gen::inRange(1, 501), [](int len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(len),
            rc::gen::arbitrary<uint8_t>());
    });
}

/// Generate a random padding bit count (0–7).
static rc::Gen<uint8_t> genPadding() {
    return rc::gen::inRange<uint8_t>(0, 8);
}

/// Generate a random FEC enabled flag.
static rc::Gen<bool> genFecEnabled() {
    return rc::gen::arbitrary<bool>();
}

/// Generate a random code rate (RATE_1_2 or RATE_3_4).
static rc::Gen<CodeRate> genCodeRate() {
    return rc::gen::element(CodeRate::RATE_1_2, CodeRate::RATE_3_4);
}

// ---------------------------------------------------------------------------
// Property 9: Frame structure invariants
// ---------------------------------------------------------------------------

// 9a: Frame starts with the configured preamble sequence
RC_GTEST_PROP(FrameProperty9, FrameStartsWithPreamble, ()) {
    const auto payload = *genPayload();
    const auto padding = *genPadding();
    const auto fec_en  = *genFecEnabled();
    const auto rate    = *genCodeRate();

    FrameBuilder builder(PREAMBLE);
    auto bits = builder.build_frame(payload, padding, fec_en, rate);

    RC_ASSERT(bits.size() >= PREAMBLE_BITS);
    for (size_t i = 0; i < PREAMBLE_BITS; ++i) {
        RC_ASSERT(bits[i] == PREAMBLE[i]);
    }
}

// 9b: Total frame length = preamble(26) + header(35) + payload*8 + CRC(32)
RC_GTEST_PROP(FrameProperty9, FrameTotalLengthCorrect, ()) {
    const auto payload = *genPayload();
    const auto padding = *genPadding();
    const auto fec_en  = *genFecEnabled();
    const auto rate    = *genCodeRate();

    FrameBuilder builder(PREAMBLE);
    auto bits = builder.build_frame(payload, padding, fec_en, rate);

    size_t expected = PREAMBLE_BITS + HEADER_BITS + payload.size() * 8 + CRC_BITS;
    RC_ASSERT(bits.size() == expected);
}

// 9c: Strip preamble, parse with FrameParser, verify all header fields match
RC_GTEST_PROP(FrameProperty9, BuildThenParseRecoverAllFields, ()) {
    const auto payload = *genPayload();
    const auto padding = *genPadding();
    const auto fec_en  = *genFecEnabled();
    const auto rate    = *genCodeRate();

    FrameBuilder builder(PREAMBLE);
    auto bits = builder.build_frame(payload, padding, fec_en, rate);

    // Strip preamble for parser
    std::vector<uint8_t> after_preamble(bits.begin() + PREAMBLE_BITS, bits.end());

    auto result = FrameParser::parse(after_preamble);
    RC_ASSERT(result.has_value());

    RC_ASSERT(result->header.payload_length == static_cast<uint16_t>(payload.size()));
    RC_ASSERT(result->header.padding_bits == padding);
    RC_ASSERT(result->header.fec_enabled == fec_en);
    RC_ASSERT(result->header.fec_code_rate == rate);
    RC_ASSERT(result->header.sequence_number == 0u);
    RC_ASSERT(result->payload == payload);
    RC_ASSERT(result->crc_valid);
}

// 9d: CRC in the frame matches CRC computed over header + payload bytes
RC_GTEST_PROP(FrameProperty9, CRCMatchesComputed, ()) {
    const auto payload = *genPayload();
    const auto padding = *genPadding();
    const auto fec_en  = *genFecEnabled();
    const auto rate    = *genCodeRate();

    FrameBuilder builder(PREAMBLE);
    auto bits = builder.build_frame(payload, padding, fec_en, rate);

    // Extract the CRC from the last 32 bits of the frame
    RC_ASSERT(bits.size() >= CRC_BITS);
    uint32_t frame_crc = 0;
    size_t crc_start = bits.size() - CRC_BITS;
    for (size_t i = 0; i < CRC_BITS; ++i) {
        frame_crc = (frame_crc << 1) | (bits[crc_start + i] & 1u);
    }

    // Reconstruct header bytes the same way FrameBuilder does for CRC
    uint16_t fl = static_cast<uint16_t>(payload.size());
    uint16_t seq = 0;  // first frame
    uint8_t fec_flag = fec_en ? 1 : 0;
    uint8_t rate_bits = static_cast<uint8_t>(rate) & 0x03u;

    std::vector<uint8_t> header_bytes(5);
    header_bytes[0] = static_cast<uint8_t>((fl >> 8) & 0xFF);
    header_bytes[1] = static_cast<uint8_t>(fl & 0xFF);
    header_bytes[2] = static_cast<uint8_t>(
        ((padding & 0x0F) << 4) |
        (fec_flag << 3) |
        (rate_bits << 1) |
        ((seq >> 11) & 0x01)
    );
    header_bytes[3] = static_cast<uint8_t>((seq >> 3) & 0xFF);
    header_bytes[4] = static_cast<uint8_t>((seq & 0x07) << 5);

    std::vector<uint8_t> crc_data;
    crc_data.insert(crc_data.end(), header_bytes.begin(), header_bytes.end());
    crc_data.insert(crc_data.end(), payload.begin(), payload.end());

    uint32_t computed_crc = FrameBuilder::compute_crc32(crc_data);
    RC_ASSERT(frame_crc == computed_crc);
}

// ---------------------------------------------------------------------------
// Property 10: Preamble detection
// ---------------------------------------------------------------------------

// 10a: Preamble embedded at a random offset is detected at the correct position
RC_GTEST_PROP(FrameProperty10, PreambleDetectedAtCorrectOffset, ()) {
    const int offset = *rc::gen::inRange(0, 51);

    // BPSK-map the preamble: 0 → +1.0, 1 → −1.0
    std::vector<std::complex<float>> preamble_syms;
    preamble_syms.reserve(PREAMBLE.size());
    for (uint8_t bit : PREAMBLE) {
        float val = (bit == 0) ? 1.0f : -1.0f;
        preamble_syms.emplace_back(val, 0.0f);
    }

    // Build symbol stream: low-energy noise + preamble + trailing noise
    std::vector<std::complex<float>> symbols;
    symbols.reserve(offset + PREAMBLE.size() + 30);

    // Leading noise: small random values that won't trigger correlation
    for (int i = 0; i < offset; ++i) {
        // Use very small amplitude to avoid false correlation
        float re = 0.05f * (static_cast<float>((i * 7 + 3) % 11) / 10.0f - 0.5f);
        float im = 0.05f * (static_cast<float>((i * 13 + 5) % 11) / 10.0f - 0.5f);
        symbols.emplace_back(re, im);
    }

    // Insert preamble symbols
    symbols.insert(symbols.end(), preamble_syms.begin(), preamble_syms.end());

    // Trailing noise
    for (int i = 0; i < 30; ++i) {
        float re = 0.05f * (static_cast<float>((i * 11 + 7) % 11) / 10.0f - 0.5f);
        float im = 0.05f * (static_cast<float>((i * 3 + 1) % 11) / 10.0f - 0.5f);
        symbols.emplace_back(re, im);
    }

    FrameSynchronizer sync(PREAMBLE);
    auto result = sync.detect(symbols);

    RC_ASSERT(result.has_value());
    // detect() returns the index of the first symbol AFTER the preamble
    RC_ASSERT(*result == static_cast<size_t>(offset) + PREAMBLE.size());
}

// 10b: Preamble detection works with scaled preamble amplitude
RC_GTEST_PROP(FrameProperty10, PreambleDetectedWithScaling, ()) {
    const int offset = *rc::gen::inRange(0, 31);
    // Scale factor between 0.5 and 5.0
    const float scale = 0.5f + 4.5f * (*rc::gen::inRange(0, 101)) / 100.0f;

    std::vector<std::complex<float>> preamble_syms;
    for (uint8_t bit : PREAMBLE) {
        float val = (bit == 0) ? 1.0f : -1.0f;
        preamble_syms.emplace_back(val * scale, 0.0f);
    }

    std::vector<std::complex<float>> symbols;

    // Leading low-energy noise
    for (int i = 0; i < offset; ++i) {
        float re = 0.02f * (static_cast<float>((i * 7 + 3) % 11) / 10.0f - 0.5f);
        float im = 0.02f * (static_cast<float>((i * 13 + 5) % 11) / 10.0f - 0.5f);
        symbols.emplace_back(re, im);
    }

    symbols.insert(symbols.end(), preamble_syms.begin(), preamble_syms.end());

    // Trailing noise
    for (int i = 0; i < 20; ++i) {
        float re = 0.02f * (static_cast<float>((i * 11 + 7) % 11) / 10.0f - 0.5f);
        float im = 0.02f * (static_cast<float>((i * 3 + 1) % 11) / 10.0f - 0.5f);
        symbols.emplace_back(re, im);
    }

    FrameSynchronizer sync(PREAMBLE);
    auto result = sync.detect(symbols);

    RC_ASSERT(result.has_value());
    RC_ASSERT(*result == static_cast<size_t>(offset) + PREAMBLE.size());
}

// ---------------------------------------------------------------------------
// Property 11: CRC error detection
// ---------------------------------------------------------------------------

// 11a: Flipping 1–5 random bits in the header/payload area causes CRC mismatch
RC_GTEST_PROP(FrameProperty11, BitFlipsCausesCRCMismatch, ()) {
    const auto payload = *genNonEmptyPayload();
    const auto padding = *genPadding();
    const auto fec_en  = *genFecEnabled();
    const auto rate    = *genCodeRate();

    FrameBuilder builder(PREAMBLE);
    auto bits = builder.build_frame(payload, padding, fec_en, rate);

    // Strip preamble for parser
    std::vector<uint8_t> after_preamble(bits.begin() + PREAMBLE_BITS, bits.end());

    // The region we can corrupt: header + payload bits (exclude CRC at the end)
    size_t corruptible_len = after_preamble.size() - CRC_BITS;
    RC_PRE(corruptible_len > 0);

    // Number of bits to flip: 1–5
    int num_flips = *rc::gen::inRange(1, 6);
    // Ensure we don't try to flip more bits than available
    num_flips = std::min(num_flips, static_cast<int>(corruptible_len));

    // Generate unique random bit positions to flip
    auto flip_positions = *rc::gen::unique<std::vector<int>>(
        static_cast<size_t>(num_flips),
        rc::gen::inRange(0, static_cast<int>(corruptible_len)));

    // Flip the selected bits
    for (int pos : flip_positions) {
        after_preamble[pos] ^= 1;
    }

    // Parse should return nullopt due to CRC mismatch
    auto result = FrameParser::parse(after_preamble);
    RC_ASSERT(!result.has_value());
}

// 11b: Single-bit flip always detected
RC_GTEST_PROP(FrameProperty11, SingleBitFlipAlwaysDetected, ()) {
    const auto payload = *genNonEmptyPayload();

    FrameBuilder builder(PREAMBLE);
    auto bits = builder.build_frame(payload, 0, false, CodeRate::RATE_1_2);

    // Strip preamble
    std::vector<uint8_t> after_preamble(bits.begin() + PREAMBLE_BITS, bits.end());

    // Flip exactly one bit in the header+payload area
    size_t corruptible_len = after_preamble.size() - CRC_BITS;
    RC_PRE(corruptible_len > 0);

    int flip_pos = *rc::gen::inRange(0, static_cast<int>(corruptible_len));
    after_preamble[flip_pos] ^= 1;

    auto result = FrameParser::parse(after_preamble);
    RC_ASSERT(!result.has_value());
}
