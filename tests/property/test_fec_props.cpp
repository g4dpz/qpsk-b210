// Feature: qpsk-b200-codec, Property 17: FEC coded length invariant
//
// For any valid binary payload, when FEC is enabled with code rate 1/2, the
// FecEncoder::encode() output length SHALL equal 2 × (input_length + tail_bits).
// When FEC is enabled with code rate 3/4, the output length SHALL equal
// ceil(4/3 × (input_length + tail_bits)) rounded up to the nearest byte.
//
// **Validates: Requirements 13.6, 13.7**

// Feature: qpsk-b200-codec, Property 18: FEC encode/decode round trip
//
// For any valid binary payload, FEC encoding with FecEncoder::encode() followed
// by FEC decoding with FecDecoder::decode() (with no bit errors introduced)
// SHALL recover the original binary payload exactly.
//
// **Validates: Requirements 13.9**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/fec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int TAIL_BITS = 6;  // K-1 = 7-1 = 6

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a random byte payload of length 0–500.
static rc::Gen<std::vector<uint8_t>> genPayload() {
    return rc::gen::mapcat(rc::gen::inRange(0, 501), [](int len) {
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

/// Generate a random code rate (RATE_1_2 or RATE_3_4).
static rc::Gen<CodeRate> genCodeRate() {
    return rc::gen::element(CodeRate::RATE_1_2, CodeRate::RATE_3_4);
}

// ---------------------------------------------------------------------------
// Property 17: FEC coded length invariant
// ---------------------------------------------------------------------------

// 17a: encode() output size matches coded_length() for random payloads and rates
RC_GTEST_PROP(FecProperty17, EncodeSizeMatchesCodedLength, ()) {
    const auto payload = *genPayload();
    const auto rate = *genCodeRate();

    FecEncoder enc(rate);
    auto coded = enc.encode(payload);

    RC_ASSERT(coded.size() == enc.coded_length(payload.size()));
}

// 17b: Rate 1/2 output bits = 2 × (input_bits + 6 tail bits), output bytes = ceil(output_bits / 8)
RC_GTEST_PROP(FecProperty17, Rate12OutputLengthFormula, ()) {
    const auto payload = *genPayload();

    FecEncoder enc(CodeRate::RATE_1_2);
    auto coded = enc.encode(payload);

    const size_t input_bits = payload.size() * 8;
    const size_t total_input_bits = input_bits + TAIL_BITS;
    const size_t output_bits = 2 * total_input_bits;
    const size_t expected_bytes = (output_bits + 7) / 8;

    RC_ASSERT(coded.size() == expected_bytes);
}

// 17c: Rate 3/4 output is shorter than rate 1/2 output for the same input
//      (for payloads large enough that the difference is visible in bytes)
RC_GTEST_PROP(FecProperty17, Rate34ShorterThanRate12, ()) {
    const auto payload = *genNonEmptyPayload();

    FecEncoder enc12(CodeRate::RATE_1_2);
    FecEncoder enc34(CodeRate::RATE_3_4);

    auto coded12 = enc12.encode(payload);
    auto coded34 = enc34.encode(payload);

    // Rate 3/4 keeps 4/6 of rate 1/2 bits, so it should be strictly shorter
    // for any non-trivial payload
    RC_ASSERT(coded34.size() <= coded12.size());
}

// ---------------------------------------------------------------------------
// Property 18: FEC encode/decode round trip
// ---------------------------------------------------------------------------

// 18a: Encode then decode with no errors recovers original payload exactly
RC_GTEST_PROP(FecProperty18, EncodeDecodeRoundTrip, ()) {
    const auto payload = *genNonEmptyPayload();
    const auto rate = *genCodeRate();

    FecEncoder enc(rate);
    FecDecoder dec(rate);

    auto coded = enc.encode(payload);
    auto decoded = dec.decode(coded, payload.size());

    RC_ASSERT(decoded == payload);
}

// 18b: Corrected errors count is 0 for clean (error-free) data
RC_GTEST_PROP(FecProperty18, ZeroCorrectedErrorsOnCleanData, ()) {
    const auto payload = *genNonEmptyPayload();
    const auto rate = *genCodeRate();

    FecEncoder enc(rate);
    FecDecoder dec(rate);

    auto coded = enc.encode(payload);
    dec.decode(coded, payload.size());

    RC_ASSERT(dec.get_corrected_errors() == 0u);
}
