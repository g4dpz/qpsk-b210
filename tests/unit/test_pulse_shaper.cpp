#include "qpsk_b200/pulse_shaper.h"

#include <cmath>
#include <complex>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Construction and tap generation
// ---------------------------------------------------------------------------

TEST(PulseShaperTest, DefaultConstructionProduces25Taps) {
    PulseShaper ps;
    EXPECT_EQ(ps.get_taps().size(), 25u);
    EXPECT_EQ(ps.get_sps(), 4);
}

TEST(PulseShaperTest, CustomParameters) {
    PulseShaper ps(0.5, 2, 13);
    EXPECT_EQ(ps.get_taps().size(), 13u);
    EXPECT_EQ(ps.get_sps(), 2);
}

TEST(PulseShaperTest, TapsNormalisedToUnitEnergy) {
    PulseShaper ps(0.35, 4, 25);
    const auto& taps = ps.get_taps();
    double energy = 0.0;
    for (float t : taps) {
        energy += static_cast<double>(t) * static_cast<double>(t);
    }
    EXPECT_NEAR(energy, 1.0, 1e-6);
}

TEST(PulseShaperTest, TapsSymmetric) {
    PulseShaper ps(0.35, 4, 25);
    const auto& taps = ps.get_taps();
    size_t n = taps.size();
    for (size_t i = 0; i < n / 2; ++i) {
        EXPECT_NEAR(taps[i], taps[n - 1 - i], 1e-6)
            << "Tap " << i << " != tap " << (n - 1 - i);
    }
}

TEST(PulseShaperTest, InvalidRolloffThrows) {
    EXPECT_THROW(PulseShaper(0.0, 4, 25), std::invalid_argument);
    EXPECT_THROW(PulseShaper(-0.1, 4, 25), std::invalid_argument);
    EXPECT_THROW(PulseShaper(1.1, 4, 25), std::invalid_argument);
}

TEST(PulseShaperTest, InvalidSpsThrows) {
    EXPECT_THROW(PulseShaper(0.35, 0, 25), std::invalid_argument);
    EXPECT_THROW(PulseShaper(0.35, -1, 25), std::invalid_argument);
}

TEST(PulseShaperTest, InvalidNumTapsThrows) {
    EXPECT_THROW(PulseShaper(0.35, 4, 0), std::invalid_argument);
    EXPECT_THROW(PulseShaper(0.35, 4, -1), std::invalid_argument);
}

TEST(PulseShaperTest, RolloffOneIsValid) {
    EXPECT_NO_THROW(PulseShaper(1.0, 4, 25));
    PulseShaper ps(1.0, 4, 25);
    const auto& taps = ps.get_taps();
    double energy = 0.0;
    for (float t : taps) {
        energy += static_cast<double>(t) * static_cast<double>(t);
    }
    EXPECT_NEAR(energy, 1.0, 1e-6);
}

// ---------------------------------------------------------------------------
// shape() output length and basic behaviour
// ---------------------------------------------------------------------------

TEST(PulseShaperTest, ShapeOutputLength) {
    PulseShaper ps(0.35, 4, 25);
    std::vector<std::complex<float>> symbols(10, {1.0f, 0.0f});
    auto output = ps.shape(symbols);
    // Expected: num_symbols * sps + (num_taps - 1) = 10*4 + 24 = 64
    EXPECT_EQ(output.size(), 64u);
}

TEST(PulseShaperTest, ShapeEmptyInput) {
    PulseShaper ps;
    auto output = ps.shape({});
    EXPECT_TRUE(output.empty());
}

TEST(PulseShaperTest, ShapeSingleSymbol) {
    PulseShaper ps(0.35, 4, 25);
    std::vector<std::complex<float>> symbols = {{1.0f, 0.0f}};
    auto output = ps.shape(symbols);
    // Expected: 1*4 + 24 = 28
    EXPECT_EQ(output.size(), 28u);

    // The output should be the RRC taps scaled by the symbol value
    // (since there's only one non-zero sample in the upsampled stream)
    const auto& taps = ps.get_taps();
    for (size_t i = 0; i < taps.size(); ++i) {
        EXPECT_NEAR(output[i].real(), taps[i], 1e-6)
            << "Mismatch at index " << i;
        EXPECT_NEAR(output[i].imag(), 0.0f, 1e-6);
    }
}

// ---------------------------------------------------------------------------
// matched_filter() output length and basic behaviour
// ---------------------------------------------------------------------------

TEST(PulseShaperTest, MatchedFilterOutputLength) {
    PulseShaper ps(0.35, 4, 25);
    std::vector<std::complex<float>> samples(40, {1.0f, 0.0f});
    auto output = ps.matched_filter(samples);
    // Expected: 40 + 24 = 64
    EXPECT_EQ(output.size(), 64u);
}

TEST(PulseShaperTest, MatchedFilterEmptyInput) {
    PulseShaper ps;
    auto output = ps.matched_filter({});
    EXPECT_TRUE(output.empty());
}

// ---------------------------------------------------------------------------
// TX shape → RX matched filter: combined raised-cosine response
// ---------------------------------------------------------------------------

TEST(PulseShaperTest, ShapeThenMatchedFilterRecoverSymbols) {
    // Use a single isolated symbol to verify the combined TX+RX response
    // produces a peak at the correct symbol instant with near-zero ISI.
    const int sps = 4;
    const int num_taps = 25;
    PulseShaper ps(0.35, sps, num_taps);

    // Create a stream with one non-zero symbol surrounded by zeros
    const int num_symbols = 11;
    std::vector<std::complex<float>> symbols(num_symbols, {0.0f, 0.0f});
    symbols[5] = {1.0f, 0.0f};  // single impulse at centre

    auto shaped = ps.shape(symbols);
    auto filtered = ps.matched_filter(shaped);

    // The combined response peak should be at the symbol-5 sampling instant.
    // After shape: the impulse is at sample index 5*sps = 20 in the upsampled stream.
    // After convolution with filter_len taps, the peak shifts by (filter_len-1)/2 = 12.
    // After matched_filter (another convolution with filter_len taps), another shift of 12.
    // Peak at: 5*sps + (num_taps - 1) = 20 + 24 = 44
    // But the matched_filter adds another (num_taps-1) to the total length.
    // The combined peak is at index 5*sps + (num_taps - 1) in the matched_filter output.
    size_t peak_idx = 5 * static_cast<size_t>(sps) + static_cast<size_t>(num_taps - 1);

    // Find the actual peak
    float max_val = 0.0f;
    size_t max_idx = 0;
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (std::abs(filtered[i].real()) > max_val) {
            max_val = std::abs(filtered[i].real());
            max_idx = i;
        }
    }

    EXPECT_EQ(max_idx, peak_idx) << "Peak not at expected symbol instant";

    // Verify ISI-free at other symbol sampling instants
    for (int sym = 0; sym < num_symbols; ++sym) {
        size_t idx = static_cast<size_t>(sym) * static_cast<size_t>(sps) +
                     static_cast<size_t>(num_taps - 1);
        if (idx < filtered.size()) {
            if (sym == 5) {
                // The peak symbol — should have significant energy
                EXPECT_GT(std::abs(filtered[idx].real()), 0.1f);
            } else {
                // Other symbol instants — should be near zero (ISI-free)
                EXPECT_NEAR(filtered[idx].real(), 0.0f, 0.05f)
                    << "ISI at symbol " << sym;
            }
        }
    }
}
