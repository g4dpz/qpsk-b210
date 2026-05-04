// Feature: qpsk-b200-codec, Property 12: Codec round-trip integrity
//
// For any valid binary payload (1–200 bytes), encoding with the Encoder and
// then decoding with the Decoder in a loopback configuration (no channel noise,
// no frequency/timing offsets) SHALL recover the original binary payload
// exactly, with the output length equal to the input length and zero CRC
// errors reported.
//
// **Validates: Requirements 7.1, 7.2, 7.3**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/decoder.h"
#include "qpsk_b200/encoder.h"
#include "qpsk_b200/types.h"

#include <cstdint>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a random byte payload of length 1–200 (small for speed).
static rc::Gen<std::vector<uint8_t>> genSmallPayload(int max_len = 200) {
    return rc::gen::mapcat(rc::gen::inRange(1, max_len + 1), [](int len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(len),
            rc::gen::arbitrary<uint8_t>());
    });
}

// ---------------------------------------------------------------------------
// Property 12a: No-FEC round trip — exact payload recovery
// ---------------------------------------------------------------------------

RC_GTEST_PROP(CodecRoundTripProperty12, NoFecExactRecovery, ()) {
    // **Validates: Requirements 7.1, 7.2, 7.3**
    const auto payload = *genSmallPayload(200);

    Config cfg = Config::defaults();
    cfg.fec_enabled = false;

    Encoder enc(cfg);
    Decoder dec(cfg);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    RC_ASSERT(result.has_value());
    RC_ASSERT(*result == payload);
}

// ---------------------------------------------------------------------------
// Property 12b: FEC rate 1/2 round trip — exact payload recovery
// ---------------------------------------------------------------------------

RC_GTEST_PROP(CodecRoundTripProperty12, FecRate12ExactRecovery, ()) {
    // **Validates: Requirements 7.1, 7.2, 7.3**
    const auto payload = *genSmallPayload(100);

    Config cfg = Config::defaults();
    cfg.fec_enabled = true;
    cfg.fec_code_rate = CodeRate::RATE_1_2;

    Encoder enc(cfg);
    Decoder dec(cfg);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    RC_ASSERT(result.has_value());
    RC_ASSERT(*result == payload);
}

// ---------------------------------------------------------------------------
// Property 12c: FEC rate 3/4 round trip — exact payload recovery
// ---------------------------------------------------------------------------

RC_GTEST_PROP(CodecRoundTripProperty12, FecRate34ExactRecovery, ()) {
    // **Validates: Requirements 7.1, 7.2, 7.3**
    const auto payload = *genSmallPayload(100);

    Config cfg = Config::defaults();
    cfg.fec_enabled = true;
    cfg.fec_code_rate = CodeRate::RATE_3_4;

    Encoder enc(cfg);
    Decoder dec(cfg);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    RC_ASSERT(result.has_value());
    RC_ASSERT(*result == payload);
}

// ---------------------------------------------------------------------------
// Property 12d: Output length equals input length
// ---------------------------------------------------------------------------

RC_GTEST_PROP(CodecRoundTripProperty12, OutputLengthEqualsInputLength, ()) {
    // **Validates: Requirements 7.2**
    const auto payload = *genSmallPayload(200);

    Config cfg = Config::defaults();
    cfg.fec_enabled = false;

    Encoder enc(cfg);
    Decoder dec(cfg);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    RC_ASSERT(result.has_value());
    RC_ASSERT(result->size() == payload.size());
}

// ---------------------------------------------------------------------------
// Property 12e: Zero CRC errors in diagnostics after loopback decode
// ---------------------------------------------------------------------------

RC_GTEST_PROP(CodecRoundTripProperty12, ZeroCrcErrorsInLoopback, ()) {
    // **Validates: Requirements 7.3**
    const auto payload = *genSmallPayload(200);

    Config cfg = Config::defaults();
    cfg.fec_enabled = false;

    Encoder enc(cfg);
    Decoder dec(cfg);

    auto samples = enc.encode(payload);
    auto result = dec.decode(samples);

    RC_ASSERT(result.has_value());
    RC_ASSERT(dec.diagnostics().crc_errors == 0u);
    RC_ASSERT(dec.diagnostics().frames_received == 1u);
}
