// Feature: qpsk-b200-codec, Property 2: Dibit map/demap round trip
//
// For any valid dibit value (0, 1, 2, or 3), mapping the dibit to a QPSK
// constellation point via SymbolMapper::dibit_to_symbol() and then demapping
// the constellation point back via SymbolMapper::symbol_to_dibit() SHALL
// produce the original dibit.
//
// **Validates: Requirements 3.4**

// Feature: qpsk-b200-codec, Property 8: QPSK demodulator nearest-point decision
//
// For any complex sample whose distance to the nearest QPSK constellation point
// is less than sqrt(2)/2 (the decision boundary radius), the QPSK_Demodulator
// SHALL output the dibit corresponding to that nearest constellation point.
//
// **Validates: Requirements 5.5**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/symbol_mapper.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a random valid dibit (0–3).
static rc::Gen<uint8_t> genDibit() {
    return rc::gen::inRange<uint8_t>(0, 4);
}

/// Generate a random bit (0 or 1).
static rc::Gen<uint8_t> genBit() {
    return rc::gen::inRange<uint8_t>(0, 2);
}

/// Generate a random bit vector of even length (2–200).
static rc::Gen<std::vector<uint8_t>> genEvenBitVector() {
    return rc::gen::mapcat(rc::gen::inRange(1, 101), [](int half_len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(half_len * 2), genBit());
    });
}

/// Generate a random bit vector of any length (1–200).
static rc::Gen<std::vector<uint8_t>> genAnyBitVector() {
    return rc::gen::mapcat(rc::gen::inRange(1, 201), [](int len) {
        return rc::gen::container<std::vector<uint8_t>>(
            static_cast<size_t>(len), genBit());
    });
}

// ---------------------------------------------------------------------------
// Property 2: Dibit map/demap round trip
// ---------------------------------------------------------------------------

// 2a: Single dibit round trip via dibit_to_symbol / symbol_to_dibit
RC_GTEST_PROP(SymbolMapperProperty2, SingleDibitRoundTrip, ()) {
    const uint8_t dibit = *genDibit();
    auto sym = SymbolMapper::dibit_to_symbol(dibit);
    uint8_t recovered = SymbolMapper::symbol_to_dibit(sym);
    RC_ASSERT(recovered == dibit);
}

// 2b: Even-length bit vector round trip via map / demap
RC_GTEST_PROP(SymbolMapperProperty2, EvenBitVectorRoundTrip, ()) {
    const auto bits = *genEvenBitVector();
    auto [symbols, padding] = SymbolMapper::map(bits);
    RC_ASSERT(padding == 0);

    auto recovered = SymbolMapper::demap(symbols);
    RC_ASSERT(recovered == bits);
}

// 2c: Any-length bit vector round trip — first N bits (original length) match
RC_GTEST_PROP(SymbolMapperProperty2, AnyLengthBitVectorRoundTrip, ()) {
    const auto bits = *genAnyBitVector();
    auto [symbols, padding] = SymbolMapper::map(bits);

    auto recovered = SymbolMapper::demap(symbols);
    // recovered may have one extra padding bit if original length was odd
    RC_ASSERT(recovered.size() >= bits.size());
    for (size_t i = 0; i < bits.size(); ++i) {
        RC_ASSERT(recovered[i] == bits[i]);
    }
}

// ---------------------------------------------------------------------------
// Property 8: QPSK demodulator nearest-point decision
// ---------------------------------------------------------------------------

/// The decision boundary radius for QPSK with unit-energy constellation
/// points at ±1/√2 is √2/2 ≈ 0.7071. Any noise with magnitude strictly
/// less than this should not change the hard-decision output.
static constexpr float DECISION_BOUNDARY = static_cast<float>(M_SQRT1_2);

/// Generate a random noise complex value with magnitude < DECISION_BOUNDARY.
/// We generate a random angle in [0, 2π) and a random radius in [0, limit).
static rc::Gen<std::complex<float>> genSmallNoise() {
    return rc::gen::map(
        rc::gen::pair(
            // angle: integer mapped to [0, 2π)
            rc::gen::inRange(0, 1000000),
            // radius: integer mapped to [0, DECISION_BOUNDARY * 0.99)
            // Use 0.99 factor to stay safely within the boundary
            rc::gen::inRange(0, 1000000)),
        [](const std::pair<int, int>& p) {
            float angle = static_cast<float>(p.first) / 1000000.0f *
                          2.0f * static_cast<float>(M_PI);
            float radius = static_cast<float>(p.second) / 1000000.0f *
                           DECISION_BOUNDARY * 0.99f;
            return std::complex<float>(
                radius * std::cos(angle),
                radius * std::sin(angle));
        });
}

// 8a: Constellation point + small noise → correct dibit
RC_GTEST_PROP(SymbolMapperProperty8, NearestPointDecision, ()) {
    const uint8_t dibit = *genDibit();
    std::complex<float> constellation_point = CONSTELLATION[dibit];
    std::complex<float> noise = *genSmallNoise();

    std::complex<float> noisy_sample = constellation_point + noise;
    uint8_t recovered = SymbolMapper::symbol_to_dibit(noisy_sample);
    RC_ASSERT(recovered == dibit);
}
