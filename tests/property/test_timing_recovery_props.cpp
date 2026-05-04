// Feature: qpsk-b200-codec, Property 7: Timing recovery output
//
// For any fractional timing offset applied to a pulse-shaped QPSK signal,
// the Mueller-Müller timing recovery block SHALL output approximately one
// sample per symbol, and the output samples SHALL correspond to the correct
// decision instants (i.e., recovered symbols are near constellation points).
//
// **Validates: Requirements 5.4**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/pulse_shaper.h"
#include "qpsk_b200/symbol_mapper.h"
#include "qpsk_b200/timing_recovery.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a pseudo-random QPSK symbol sequence using a simple LCG.
static std::vector<std::complex<float>> make_qpsk_symbols(size_t count,
                                                           uint32_t seed) {
    std::vector<std::complex<float>> syms;
    syms.reserve(count);
    uint32_t state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1103515245u + 12345u;
        uint8_t dibit = static_cast<uint8_t>((state >> 16) & 0x03);
        syms.push_back(CONSTELLATION[dibit]);
    }
    return syms;
}

/// Generate a random LCG seed.
static rc::Gen<uint32_t> genSeed() {
    return rc::gen::inRange<uint32_t>(1, 100000);
}

/// Generate a random fractional timing offset in [0.0, 1.0).
/// We generate an integer in [0, 99] and scale by 0.01.
static rc::Gen<double> genTimingOffset() {
    return rc::gen::map(rc::gen::inRange(0, 100),
                        [](int v) { return static_cast<double>(v) * 0.01; });
}

/// Generate a random number of symbols in [120, 300].
static rc::Gen<int> genNumSymbols() {
    return rc::gen::inRange(120, 301);
}

/// Apply a fractional sample delay to a signal via linear interpolation.
/// This simulates a timing offset of `delay` samples (0 <= delay < 1).
static std::vector<std::complex<float>> apply_fractional_delay(
    const std::vector<std::complex<float>>& samples,
    double delay) {
    if (samples.size() < 2 || delay <= 0.0) return samples;

    std::vector<std::complex<float>> out;
    out.reserve(samples.size());

    // Linear interpolation: out[n] = samples[n] * (1 - delay) + samples[n-1] * delay
    // For n=0, use samples[0] directly (no previous sample)
    out.push_back(samples[0]);
    for (size_t n = 1; n < samples.size(); ++n) {
        auto interp = samples[n] * static_cast<float>(1.0 - delay) +
                      samples[n - 1] * static_cast<float>(delay);
        out.push_back(interp);
    }
    return out;
}

/// Compute the fraction of symbols that land near any constellation point.
static double fraction_near_constellation(
    const std::vector<std::complex<float>>& symbols,
    size_t start, size_t end, float threshold = 0.45f) {
    int near_count = 0;
    int total = 0;
    for (size_t i = start; i < end && i < symbols.size(); ++i) {
        float min_dist = 1e9f;
        for (int j = 0; j < 4; ++j) {
            float d = std::abs(symbols[i] - CONSTELLATION[j]);
            if (d < min_dist) min_dist = d;
        }
        if (min_dist < threshold) {
            ++near_count;
        }
        ++total;
    }
    return (total > 0)
               ? static_cast<double>(near_count) / static_cast<double>(total)
               : 0.0;
}

// ---------------------------------------------------------------------------
// Property 7: Timing recovery output
// ---------------------------------------------------------------------------

// 7a: Output count is approximately one sample per symbol.
// The timing recovery should produce roughly num_symbols outputs from a
// pulse-shaped signal with num_symbols * sps input samples.
RC_GTEST_PROP(TimingRecoveryProperty7, OutputCountApproximatelyNumSymbols, ()) {
    const int num_symbols = *genNumSymbols();
    const double timing_offset = *genTimingOffset();
    const uint32_t seed = *genSeed();

    const int sps = 4;
    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;

    auto symbols = make_qpsk_symbols(static_cast<size_t>(num_symbols), seed);

    // Pulse-shape and matched-filter
    PulseShaper shaper(0.35, sps, num_taps);
    auto shaped = shaper.shape(symbols);
    auto filtered = shaper.matched_filter(shaped);

    // Apply fractional timing offset
    auto delayed = apply_fractional_delay(filtered, timing_offset);

    // Run timing recovery
    TimingRecovery recovery(sps, 0.01);
    auto recovered = recovery.recover(delayed);

    // The output count should be within 50% of num_symbols.
    // The filtered signal is longer than num_symbols * sps due to filter
    // transients, so the recovery may produce slightly more outputs.
    double expected = static_cast<double>(delayed.size()) /
                      static_cast<double>(sps);
    double lower = expected * 0.5;
    double upper = expected * 1.5;

    RC_ASSERT(recovered.size() >= static_cast<size_t>(lower));
    RC_ASSERT(recovered.size() <= static_cast<size_t>(upper));
}

// 7b: After convergence, recovered symbols land near constellation points.
// This verifies the timing recovery finds the correct decision instants.
RC_GTEST_PROP(TimingRecoveryProperty7, RecoveredSymbolsNearConstellation, ()) {
    const int num_symbols = *genNumSymbols();
    const double timing_offset = *genTimingOffset();
    const uint32_t seed = *genSeed();

    const int sps = 4;
    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;

    auto symbols = make_qpsk_symbols(static_cast<size_t>(num_symbols), seed);

    // Pulse-shape and matched-filter
    PulseShaper shaper(0.35, sps, num_taps);
    auto shaped = shaper.shape(symbols);
    auto filtered = shaper.matched_filter(shaped);

    // Apply fractional timing offset
    auto delayed = apply_fractional_delay(filtered, timing_offset);

    // Run timing recovery
    TimingRecovery recovery(sps, 0.01);
    auto recovered = recovery.recover(delayed);

    // Need enough symbols to check convergence
    RC_PRE(recovered.size() >= 20);

    // Skip the first 30% for convergence, check the remaining 70%
    size_t check_start = recovered.size() * 3 / 10;
    double ratio = fraction_near_constellation(
        recovered, check_start, recovered.size());

    // At least 60% of converged symbols should be near a constellation point
    RC_ASSERT(ratio >= 0.60);
}

// 7c: With zero timing offset, the recovery still produces valid output.
// This verifies the loop doesn't degrade a perfectly aligned signal.
RC_GTEST_PROP(TimingRecoveryProperty7, ZeroOffsetProducesValidOutput, ()) {
    const int num_symbols = *genNumSymbols();
    const uint32_t seed = *genSeed();

    const int sps = 4;
    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;

    auto symbols = make_qpsk_symbols(static_cast<size_t>(num_symbols), seed);

    // Pulse-shape and matched-filter (no timing offset)
    PulseShaper shaper(0.35, sps, num_taps);
    auto shaped = shaper.shape(symbols);
    auto filtered = shaper.matched_filter(shaped);

    TimingRecovery recovery(sps, 0.01);
    auto recovered = recovery.recover(filtered);

    // Should produce a reasonable number of outputs
    RC_PRE(recovered.size() >= 20);

    // Skip first 30% for convergence
    size_t check_start = recovered.size() * 3 / 10;
    double ratio = fraction_near_constellation(
        recovered, check_start, recovered.size());

    // With no timing offset, expect higher quality — at least 70%
    RC_ASSERT(ratio >= 0.70);
}
