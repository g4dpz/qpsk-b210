#include "qpsk_b200/carrier_sync.h"
#include "qpsk_b200/symbol_mapper.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

namespace qpsk_b200 {
namespace {

// ---------------------------------------------------------------------------
// Helper: generate a pseudo-random QPSK symbol sequence using a simple LCG
// (avoids the false-lock issue that occurs with perfectly uniform cycling)
// ---------------------------------------------------------------------------
static std::vector<std::complex<float>> make_qpsk_symbols(size_t count,
                                                           uint32_t seed = 42) {
    std::vector<std::complex<float>> syms;
    syms.reserve(count);
    uint32_t state = seed;
    for (size_t i = 0; i < count; ++i) {
        // Simple LCG
        state = state * 1103515245u + 12345u;
        uint8_t dibit = static_cast<uint8_t>((state >> 16) & 0x03);
        syms.push_back(CONSTELLATION[dibit]);
    }
    return syms;
}

// ---------------------------------------------------------------------------
// Helper: apply a frequency and phase offset to a symbol stream
// ---------------------------------------------------------------------------
static std::vector<std::complex<float>> apply_offset(
    const std::vector<std::complex<float>>& symbols,
    double freq_rad_per_sample,
    double phase_rad) {
    std::vector<std::complex<float>> out;
    out.reserve(symbols.size());
    for (size_t i = 0; i < symbols.size(); ++i) {
        double angle = phase_rad + freq_rad_per_sample * static_cast<double>(i);
        auto rotator = std::complex<float>(
            static_cast<float>(std::cos(angle)),
            static_cast<float>(std::sin(angle)));
        out.push_back(symbols[i] * rotator);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Helper: check that corrected symbols land near *some* constellation point
// (accounts for QPSK Costas loop 90° phase ambiguity — the loop can lock
// to any of the 4 quadrant rotations that are multiples of π/2)
// ---------------------------------------------------------------------------
static double fraction_near_constellation(
    const std::vector<std::complex<float>>& corrected,
    size_t start, size_t end, float threshold = 0.3f) {
    int near_count = 0;
    int total = 0;
    for (size_t i = start; i < end; ++i) {
        float min_dist = 1e9f;
        for (int j = 0; j < 4; ++j) {
            float d = std::abs(corrected[i] - CONSTELLATION[j]);
            if (d < min_dist) min_dist = d;
        }
        if (min_dist < threshold) {
            ++near_count;
        }
        ++total;
    }
    return (total > 0) ? static_cast<double>(near_count) / static_cast<double>(total) : 0.0;
}

// ---------------------------------------------------------------------------
// Test: corrects a known frequency offset
//
// The Costas loop for QPSK has a π/2 phase ambiguity — it can lock to any
// of the four quadrant rotations.  We verify that after convergence the
// corrected symbols land near *some* constellation point.
// ---------------------------------------------------------------------------
TEST(CarrierSynchronizerTest, CorrectsFrequencyOffset) {
    auto symbols = make_qpsk_symbols(4000);

    // Apply a small frequency offset (0.005 rad/sample)
    double freq_offset = 0.005;
    auto rotated = apply_offset(symbols, freq_offset, 0.0);

    CarrierSynchronizer sync(0.02);
    auto corrected = sync.synchronize(rotated);

    // After convergence, the last 500 symbols should be near constellation points
    size_t check_start = corrected.size() - 500;
    double ratio = fraction_near_constellation(corrected, check_start, corrected.size());

    EXPECT_GE(ratio, 0.90)
        << "Expected at least 90% of symbols near constellation points after "
           "frequency lock, got " << (ratio * 100.0) << "%";
}

// ---------------------------------------------------------------------------
// Test: corrects a known phase offset
// ---------------------------------------------------------------------------
TEST(CarrierSynchronizerTest, CorrectsPhaseOffset) {
    auto symbols = make_qpsk_symbols(1000);

    // Apply a constant phase offset of 0.3 radians (no frequency offset)
    double phase_offset = 0.3;
    auto rotated = apply_offset(symbols, 0.0, phase_offset);

    CarrierSynchronizer sync(0.05);
    auto corrected = sync.synchronize(rotated);

    // After convergence, symbols should be near constellation points
    size_t check_start = corrected.size() - 200;
    double ratio = fraction_near_constellation(corrected, check_start, corrected.size());

    EXPECT_GE(ratio, 0.90)
        << "Expected at least 90% of symbols near constellation points after "
           "phase lock, got " << (ratio * 100.0) << "%";
}

// ---------------------------------------------------------------------------
// Test: zero offset passthrough
// ---------------------------------------------------------------------------
TEST(CarrierSynchronizerTest, ZeroOffsetPassthrough) {
    auto symbols = make_qpsk_symbols(200);

    CarrierSynchronizer sync(0.01);
    auto corrected = sync.synchronize(symbols);

    ASSERT_EQ(corrected.size(), symbols.size());

    // With no offset applied, all symbols should demap correctly
    for (size_t i = 0; i < symbols.size(); ++i) {
        uint8_t orig_dibit = SymbolMapper::symbol_to_dibit(symbols[i]);
        uint8_t recv_dibit = SymbolMapper::symbol_to_dibit(corrected[i]);
        EXPECT_EQ(orig_dibit, recv_dibit) << "Mismatch at symbol " << i;
    }
}

// ---------------------------------------------------------------------------
// Test: reset clears state
// ---------------------------------------------------------------------------
TEST(CarrierSynchronizerTest, ResetClearsState) {
    CarrierSynchronizer sync(0.01);

    // Process some samples to build up state
    auto symbols = make_qpsk_symbols(100);
    auto rotated = apply_offset(symbols, 0.01, 0.5);
    sync.synchronize(rotated);

    // State should be non-zero
    EXPECT_NE(sync.get_freq_offset(), 0.0);

    // Reset
    sync.reset();

    EXPECT_DOUBLE_EQ(sync.get_freq_offset(), 0.0);
    EXPECT_DOUBLE_EQ(sync.get_phase_offset(), 0.0);
}

// ---------------------------------------------------------------------------
// Test: constructor rejects invalid loop bandwidth
// ---------------------------------------------------------------------------
TEST(CarrierSynchronizerTest, RejectsInvalidLoopBandwidth) {
    EXPECT_THROW(CarrierSynchronizer(0.0), std::invalid_argument);
    EXPECT_THROW(CarrierSynchronizer(-0.1), std::invalid_argument);
    EXPECT_THROW(CarrierSynchronizer(1.0), std::invalid_argument);
    EXPECT_THROW(CarrierSynchronizer(2.0), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Test: diagnostics return sensible values after processing
// ---------------------------------------------------------------------------
TEST(CarrierSynchronizerTest, DiagnosticsAfterProcessing) {
    CarrierSynchronizer sync(0.01);

    auto symbols = make_qpsk_symbols(500);
    double freq_offset = 0.003;
    auto rotated = apply_offset(symbols, freq_offset, 0.0);

    sync.synchronize(rotated);

    // The estimated frequency should be in the right ballpark
    // (positive, since we applied a positive offset)
    double est_freq = sync.get_freq_offset();
    EXPECT_GT(est_freq, 0.0)
        << "Expected positive frequency estimate for positive offset";
}

} // namespace
} // namespace qpsk_b200
