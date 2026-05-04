#include "qpsk_b200/timing_recovery.h"
#include "qpsk_b200/pulse_shaper.h"
#include "qpsk_b200/symbol_mapper.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

namespace qpsk_b200 {
namespace {

// ---------------------------------------------------------------------------
// Helper: generate a known QPSK symbol sequence
// ---------------------------------------------------------------------------
static std::vector<std::complex<float>> make_qpsk_symbols(size_t count) {
    std::vector<std::complex<float>> syms;
    syms.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        syms.push_back(CONSTELLATION[i % 4]);
    }
    return syms;
}

// ---------------------------------------------------------------------------
// Test: recovers symbols from a pulse-shaped signal
// ---------------------------------------------------------------------------
TEST(TimingRecoveryTest, RecoversFromPulseShapedSignal) {
    const int sps = 4;
    const size_t num_symbols = 200;

    // Generate symbols and pulse-shape them
    auto symbols = make_qpsk_symbols(num_symbols);
    PulseShaper shaper(0.35, sps, 25);
    auto shaped = shaper.shape(symbols);

    // Apply matched filter
    auto filtered = shaper.matched_filter(shaped);

    // The combined RRC response peaks at symbol centres.
    // Skip the filter transient (rrc_span_symbols on each side).
    // The first symbol peak is at index (num_taps - 1) in the shaped signal,
    // and after matched filter it's at index 2*(num_taps-1)/2 = num_taps-1
    // from the start of the filtered signal.

    TimingRecovery recovery(sps, 0.01);
    auto recovered = recovery.recover(filtered);

    // We should get approximately num_symbols outputs (some may be lost to
    // transients at the start/end)
    EXPECT_GT(recovered.size(), num_symbols / 2)
        << "Expected at least half the symbols to be recovered";

    // After the loop converges, recovered symbols should demap correctly.
    // Check the middle portion (skip first and last 20% for transients).
    size_t start = recovered.size() / 5;
    size_t end   = recovered.size() * 4 / 5;
    int correct = 0;
    int total   = 0;

    for (size_t i = start; i < end && i < recovered.size(); ++i) {
        // The recovered sample should be close to a constellation point.
        // We can't directly compare to the original symbol index because
        // timing recovery may shift the alignment. Instead, check that
        // the recovered sample is close to *some* constellation point.
        uint8_t dibit = SymbolMapper::symbol_to_dibit(recovered[i]);
        std::complex<float> nearest = CONSTELLATION[dibit];
        float dist = std::abs(recovered[i] - nearest);
        if (dist < 0.5f) {
            ++correct;
        }
        ++total;
    }

    // Expect at least 70% of recovered symbols to be near a constellation point
    EXPECT_GT(total, 0);
    double ratio = static_cast<double>(correct) / static_cast<double>(total);
    EXPECT_GE(ratio, 0.70)
        << "Expected at least 70% of recovered symbols near constellation points, got "
        << correct << "/" << total;
}

// ---------------------------------------------------------------------------
// Test: output count is approximately num_symbols
// ---------------------------------------------------------------------------
TEST(TimingRecoveryTest, OutputCountApproximatelyNumSymbols) {
    const int sps = 4;
    const size_t num_symbols = 100;

    auto symbols = make_qpsk_symbols(num_symbols);
    PulseShaper shaper(0.35, sps, 25);
    auto shaped = shaper.shape(symbols);
    auto filtered = shaper.matched_filter(shaped);

    TimingRecovery recovery(sps, 0.01);
    auto recovered = recovery.recover(filtered);

    // The output count should be roughly (total_samples / sps).
    // The filtered signal length is:
    //   shaped_len + filter_len - 1 = (num_symbols*sps + filter_len - 1) + filter_len - 1
    // So roughly num_symbols + 2*(filter_len-1)/sps extra symbols.
    // We expect the output to be within a reasonable range of num_symbols.
    double expected = static_cast<double>(filtered.size()) / static_cast<double>(sps);
    double lower = expected * 0.5;
    double upper = expected * 1.5;

    EXPECT_GE(recovered.size(), static_cast<size_t>(lower))
        << "Too few recovered symbols";
    EXPECT_LE(recovered.size(), static_cast<size_t>(upper))
        << "Too many recovered symbols";
}

// ---------------------------------------------------------------------------
// Test: reset clears state
// ---------------------------------------------------------------------------
TEST(TimingRecoveryTest, ResetClearsState) {
    TimingRecovery recovery(4, 0.01);

    // Process some samples
    auto symbols = make_qpsk_symbols(50);
    PulseShaper shaper(0.35, 4, 25);
    auto shaped = shaper.shape(symbols);
    auto filtered = shaper.matched_filter(shaped);
    recovery.recover(filtered);

    // Reset
    recovery.reset();

    EXPECT_DOUBLE_EQ(recovery.get_timing_offset(), 0.0);
}

// ---------------------------------------------------------------------------
// Test: constructor rejects invalid parameters
// ---------------------------------------------------------------------------
TEST(TimingRecoveryTest, RejectsInvalidParameters) {
    EXPECT_THROW(TimingRecovery(1, 0.01), std::invalid_argument);   // sps < 2
    EXPECT_THROW(TimingRecovery(0, 0.01), std::invalid_argument);   // sps < 2
    EXPECT_THROW(TimingRecovery(4, 0.0), std::invalid_argument);    // gain_mu = 0
    EXPECT_THROW(TimingRecovery(4, -0.1), std::invalid_argument);   // gain_mu < 0
    EXPECT_THROW(TimingRecovery(4, 1.0), std::invalid_argument);    // gain_mu = 1
}

// ---------------------------------------------------------------------------
// Test: empty input returns empty output
// ---------------------------------------------------------------------------
TEST(TimingRecoveryTest, EmptyInput) {
    TimingRecovery recovery(4, 0.01);
    auto result = recovery.recover({});
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// Test: get_timing_offset returns a value
// ---------------------------------------------------------------------------
TEST(TimingRecoveryTest, TimingOffsetDiagnostic) {
    TimingRecovery recovery(4, 0.01);

    // Initially zero
    EXPECT_DOUBLE_EQ(recovery.get_timing_offset(), 0.0);

    // After processing, it may change (but we just check it doesn't crash)
    auto symbols = make_qpsk_symbols(50);
    PulseShaper shaper(0.35, 4, 25);
    auto shaped = shaper.shape(symbols);
    auto filtered = shaper.matched_filter(shaped);
    recovery.recover(filtered);

    // Just verify it returns without error
    (void)recovery.get_timing_offset();
}

} // namespace
} // namespace qpsk_b200
