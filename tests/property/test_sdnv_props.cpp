// Feature: bp-ltp-dtn, Property 1: SDNV Encode/Decode Round-Trip
//
// For any uint64_t value in [0, 2^63 - 1], encoding to SDNV and then
// decoding SHALL produce the original value.
//
// **Validates: Requirements 1.5**

// Feature: bp-ltp-dtn, Property 2: SDNV Minimum Byte Encoding
//
// The SDNV encoding SHALL use the minimum number of bytes required.
// Specifically, the first byte of the encoded sequence must not be a
// "leading zero" continuation byte (0x80) — unless the value is 0.
//
// **Validates: Requirements 1.6**

// Feature: bp-ltp-dtn, Property 3: SDNV Malformed Input Rejection
//
// Truncated SDNVs (continuation bit set on last available byte) and
// oversized SDNVs (>10 bytes) SHALL produce std::runtime_error.
//
// **Validates: Requirements 1.3, 1.4**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "ltp_cla/sdnv.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a uint64_t in [0, 2^63 - 1].
static rc::Gen<uint64_t> genSdnvValue() {
    return rc::gen::inRange(
        static_cast<uint64_t>(0),
        static_cast<uint64_t>(INT64_MAX));
}

/// Generate a truncated SDNV: 1–10 bytes, all with continuation bit set.
static rc::Gen<std::vector<uint8_t>> genTruncatedSdnv() {
    return rc::gen::mapcat(
        rc::gen::inRange(1, 11),
        [](int len) {
            return rc::gen::container<std::vector<uint8_t>>(
                static_cast<size_t>(len),
                rc::gen::map(rc::gen::arbitrary<uint8_t>(), [](uint8_t b) -> uint8_t {
                    return b | 0x80;  // Force continuation bit on every byte
                }));
        });
}

/// Generate an oversized SDNV: 11–15 bytes, continuation on all but last.
static rc::Gen<std::vector<uint8_t>> genOversizedSdnv() {
    return rc::gen::mapcat(
        rc::gen::inRange(11, 16),
        [](int len) {
            return rc::gen::map(
                rc::gen::container<std::vector<uint8_t>>(
                    static_cast<size_t>(len),
                    rc::gen::arbitrary<uint8_t>()),
                [](std::vector<uint8_t> bytes) {
                    // Set continuation bit on all bytes except the last
                    for (size_t i = 0; i + 1 < bytes.size(); ++i) {
                        bytes[i] |= 0x80;
                    }
                    // Clear continuation bit on last byte
                    bytes.back() &= 0x7F;
                    return bytes;
                });
        });
}

// ---------------------------------------------------------------------------
// Property 1: SDNV Encode/Decode Round-Trip
// ---------------------------------------------------------------------------

RC_GTEST_PROP(SdnvProperty1, EncodeDecodeRoundTrip, ()) {
    const auto value = *genSdnvValue();

    auto encoded = sdnv::encode(value);
    size_t offset = 0;
    auto decoded = sdnv::decode(encoded.data(), encoded.length, offset);

    RC_ASSERT(decoded == value);
    RC_ASSERT(offset == encoded.length);
}

// ---------------------------------------------------------------------------
// Property 2: SDNV Minimum Byte Encoding
// ---------------------------------------------------------------------------

RC_GTEST_PROP(SdnvProperty2, MinimumByteEncoding, ()) {
    const auto value = *genSdnvValue();

    auto encoded = sdnv::encode(value);

    // The encoding must not be empty.
    RC_ASSERT(!encoded.length == 0);

    // If the encoding is more than 1 byte, the first byte must not be
    // a "leading zero" continuation byte (0x80), which would mean the
    // top 7 data bits are all zero — wasting a byte.
    if (encoded.length > 1) {
        RC_ASSERT(encoded.bytes[0] != 0x80);
    }
}

// ---------------------------------------------------------------------------
// Property 3: SDNV Malformed Input Rejection
// ---------------------------------------------------------------------------

// 3a: Truncated SDNVs (all continuation bits set) must throw.
RC_GTEST_PROP(SdnvProperty3, TruncatedSdnvThrows, ()) {
    const auto data = *genTruncatedSdnv();

    size_t offset = 0;
    RC_ASSERT_THROWS_AS(
        sdnv::decode(data.data(), data.size(), offset),
        std::runtime_error);
}

// 3b: Oversized SDNVs (>10 bytes) must throw.
RC_GTEST_PROP(SdnvProperty3, OversizedSdnvThrows, ()) {
    const auto data = *genOversizedSdnv();

    size_t offset = 0;
    RC_ASSERT_THROWS_AS(
        sdnv::decode(data.data(), data.size(), offset),
        std::runtime_error);
}
