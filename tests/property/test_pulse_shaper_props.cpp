// Feature: qpsk-b200-codec, Property 3: Pulse shaper output length
//
// For any sequence of QPSK symbols and any valid samples-per-symbol value,
// the PulseShaper::shape() output SHALL have length equal to
// num_symbols × samples_per_symbol + (filter_length − 1), and the
// matched_filter() output SHALL have length input.size() + (filter_length − 1).
//
// **Validates: Requirements 4.1, 4.2**

// Feature: qpsk-b200-codec, Property 4: Spectral bandwidth containment
//
// For any sequence of random QPSK symbols and any valid RRC roll-off factor α,
// the shaped output energy SHALL be proportional to the input energy within a
// reasonable tolerance (verifying the RRC filter preserves energy).
//
// **Validates: Requirements 4.3**

// Feature: qpsk-b200-codec, Property 5: Matched filter combined response
//
// For any sequence of QPSK symbols, applying the RRC pulse shaper followed by
// the RRC matched filter SHALL produce a combined raised-cosine response that
// is inter-symbol-interference-free at the correct symbol sampling instants
// (i.e., the sampled values at symbol centers approximate the original symbols
// within a floating-point tolerance).
//
// **Validates: Requirements 5.1**

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "qpsk_b200/pulse_shaper.h"
#include "qpsk_b200/symbol_mapper.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace qpsk_b200;

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

/// Generate a random valid dibit (0–3) and return the corresponding
/// QPSK constellation symbol.
static rc::Gen<std::complex<float>> genQpskSymbol() {
    return rc::gen::map(rc::gen::inRange<uint8_t>(0, 4),
                        [](uint8_t dibit) { return CONSTELLATION[dibit]; });
}

/// Generate a random vector of QPSK symbols with length in [min_len, max_len].
static rc::Gen<std::vector<std::complex<float>>>
genQpskSymbolVector(int min_len, int max_len) {
    return rc::gen::mapcat(
        rc::gen::inRange(min_len, max_len + 1),
        [](int len) {
            return rc::gen::container<std::vector<std::complex<float>>>(
                static_cast<size_t>(len), genQpskSymbol());
        });
}

/// Generate a random samples-per-symbol value from {2, 4, 8}.
static rc::Gen<int> genSps() {
    return rc::gen::element(2, 4, 8);
}

/// Generate a random roll-off factor in [0.1, 1.0].
/// We generate an integer in [1, 10] and divide by 10.0.
static rc::Gen<double> genRolloff() {
    return rc::gen::map(rc::gen::inRange(1, 11),
                        [](int v) { return static_cast<double>(v) / 10.0; });
}

// ---------------------------------------------------------------------------
// Property 3: Pulse shaper output length
// ---------------------------------------------------------------------------

// 3a: shape() output length = num_symbols × sps + (num_taps − 1)
RC_GTEST_PROP(PulseShaperProperty3, ShapeOutputLength, ()) {
    const auto symbols = *genQpskSymbolVector(1, 100);
    const int sps = *genSps();

    // Use default rolloff and compute num_taps from default span (6 symbols)
    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;
    PulseShaper ps(0.35, sps, num_taps);

    auto output = ps.shape(symbols);

    const size_t expected_len =
        symbols.size() * static_cast<size_t>(sps) +
        static_cast<size_t>(num_taps - 1);

    RC_ASSERT(output.size() == expected_len);
}

// 3b: matched_filter() output length = input.size() + (num_taps − 1)
RC_GTEST_PROP(PulseShaperProperty3, MatchedFilterOutputLength, ()) {
    const auto symbols = *genQpskSymbolVector(1, 100);
    const int sps = *genSps();

    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;
    PulseShaper ps(0.35, sps, num_taps);

    // First shape to get realistic input for matched_filter
    auto shaped = ps.shape(symbols);
    auto output = ps.matched_filter(shaped);

    const size_t expected_len =
        shaped.size() + static_cast<size_t>(num_taps - 1);

    RC_ASSERT(output.size() == expected_len);
}

// ---------------------------------------------------------------------------
// Property 4: Spectral bandwidth containment (energy proportionality)
// ---------------------------------------------------------------------------

// 4a: The shaped output energy is proportional to input energy.
// For N QPSK symbols each with unit energy (|s|² = 1), the shaped output
// energy should be approximately N (since the RRC filter is normalised to
// unit energy). We check within a 20% tolerance.
RC_GTEST_PROP(PulseShaperProperty4, EnergyProportionality, ()) {
    const auto symbols = *genQpskSymbolVector(50, 200);
    const double rolloff = *genRolloff();

    const int sps = 4;
    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;
    PulseShaper ps(rolloff, sps, num_taps);

    auto shaped = ps.shape(symbols);

    // Compute input energy: each QPSK symbol has |s|² = 1
    double input_energy = static_cast<double>(symbols.size());

    // Compute output energy
    double output_energy = 0.0;
    for (const auto& s : shaped) {
        output_energy += static_cast<double>(s.real()) * static_cast<double>(s.real()) +
                         static_cast<double>(s.imag()) * static_cast<double>(s.imag());
    }

    // The output energy should be approximately equal to input energy.
    // The RRC filter is normalised to unit energy, so after upsampling by sps
    // and convolving, the energy should scale by 1 (the filter preserves energy
    // per symbol). Allow 20% tolerance for edge effects.
    double ratio = output_energy / input_energy;
    RC_ASSERT(ratio > 0.8);
    RC_ASSERT(ratio < 1.2);
}

// ---------------------------------------------------------------------------
// Property 5: Matched filter combined response (ISI-free at symbol instants)
// ---------------------------------------------------------------------------

// 5a: TX RRC → RX RRC produces ISI-free response at symbol sampling instants.
// The sampled values at symbol centers should approximate the original symbols.
RC_GTEST_PROP(PulseShaperProperty5, MatchedFilterISIFree, ()) {
    const auto symbols = *genQpskSymbolVector(10, 50);
    const int sps = 4;
    const int rrc_span = 6;
    const int num_taps = rrc_span * sps + 1;
    PulseShaper ps(0.35, sps, num_taps);

    // TX: shape symbols
    auto shaped = ps.shape(symbols);

    // RX: apply matched filter
    auto filtered = ps.matched_filter(shaped);

    // The combined TX+RX filter delay is (num_taps - 1) samples from each
    // convolution. The symbol sampling instants in the filtered output are at:
    //   index = sym_idx * sps + (num_taps - 1)
    // where (num_taps - 1) accounts for the combined filter group delay.
    const size_t filter_delay = static_cast<size_t>(num_taps - 1);

    // Check that sampled values at symbol instants approximate original symbols.
    // Skip the first and last few symbols where edge effects dominate.
    const int margin = 3;  // skip edge symbols
    int checked = 0;

    for (size_t i = static_cast<size_t>(margin);
         i < symbols.size() - static_cast<size_t>(margin); ++i) {
        size_t sample_idx = i * static_cast<size_t>(sps) + filter_delay;
        if (sample_idx >= filtered.size()) break;

        std::complex<float> expected = symbols[i];
        std::complex<float> actual = filtered[sample_idx];

        // The combined RRC response should recover the symbol (within tolerance).
        // Use a generous tolerance since the filter is truncated.
        float err_real = std::abs(actual.real() - expected.real());
        float err_imag = std::abs(actual.imag() - expected.imag());

        RC_ASSERT(err_real < 0.15f);
        RC_ASSERT(err_imag < 0.15f);
        ++checked;
    }

    // Ensure we actually checked some symbols
    RC_ASSERT(checked >= 4);
}
