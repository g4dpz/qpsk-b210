// Tests for the preamble sequence. Requirement 3.5 (preamble correlation) depends on
// its autocorrelation: one sharp peak and small sidelobes.

#include <cmath>
#include <vector>

#include "dsp/preamble.hpp"
#include "test_harness.hpp"

using mpsk::dsp::kPreambleSymbols;
using mpsk::dsp::preamble_bits;
using mpsk::dsp::preamble_symbols;

namespace {

/** Aperiodic autocorrelation at lag k >= 0 of a real +-1 sequence. */
int aperiodic_autocorrelation(const std::vector<int>& s, int k) {
  int r = 0;
  for (int i = 0; i + k < static_cast<int>(s.size()); ++i) r += s[i] * s[i + k];
  return r;
}

std::vector<int> as_signs() {
  std::vector<int> s;
  for (const auto& c : preamble_symbols()) s.push_back(c.real() > 0.0f ? 1 : -1);
  return s;
}

}  // namespace

TEST(preamble_has_64_symbols) {
  CHECK(kPreambleSymbols == 64);
  CHECK(preamble_bits().size() == 64);
  CHECK(preamble_symbols().size() == 64);
  CHECK(mpsk::dsp::kPreambleSegmentSymbols * 4 == kPreambleSymbols);
}

TEST(preamble_symbols_are_bpsk) {
  const auto& bits = preamble_bits();
  const auto& symbols = preamble_symbols();
  for (std::size_t i = 0; i < symbols.size(); ++i) {
    CHECK_NEAR(symbols[i].imag(), 0.0, 1e-6);
    CHECK_NEAR(symbols[i].real(), bits[i] ? -1.0 : 1.0, 1e-6);  // bit 0 -> +1, bit 1 -> -1
  }
}

TEST(preamble_is_repeatable) {
  CHECK(&preamble_bits() == &preamble_bits());  // built once
  CHECK(preamble_bits() == preamble_bits());
}

TEST(first_63_chips_are_a_maximal_length_sequence) {
  // A degree-6 m-sequence has 32 ones and 31 zeros per period, and its periodic
  // autocorrelation is exactly -1 at every nonzero lag.
  const auto& bits = preamble_bits();
  int ones = 0;
  for (int i = 0; i < 63; ++i) ones += bits[static_cast<std::size_t>(i)];
  CHECK(ones == 32);

  std::vector<int> s;
  for (int i = 0; i < 63; ++i) s.push_back(bits[static_cast<std::size_t>(i)] ? -1 : 1);
  for (int k = 1; k < 63; ++k) {
    int r = 0;
    for (int i = 0; i < 63; ++i) r += s[static_cast<std::size_t>(i)] * s[static_cast<std::size_t>((i + k) % 63)];
    CHECK(r == -1);
  }
}

TEST(autocorrelation_has_a_single_sharp_peak) {
  const std::vector<int> s = as_signs();
  CHECK(aperiodic_autocorrelation(s, 0) == 64);

  int worst = 0;
  for (int k = 1; k < 64; ++k) worst = std::max(worst, std::abs(aperiodic_autocorrelation(s, k)));

  // Pinned to the value found by the search that chose this sequence. If this
  // changes, detection performance has changed and the choice should be revisited.
  CHECK(worst == 6);
  CHECK(20.0 * std::log10(64.0 / worst) > 20.0);  // peak at least 20 dB above any sidelobe
}

TEST(detector_statistic_sidelobes_are_pinned) {
  // The metric the receiver's detector will actually use: four 16-symbol partial
  // correlations combined non-coherently, D(lag) = sum_k |c_k(lag)|^2. Symbols
  // outside the preamble count as zero (silence). The peak is D(0) = 4 * 16^2 = 1024.
  // Measured for this sequence: worst sidelobe 0.137 of the peak (-8.6 dB). Across
  // all 252 variants considered when choosing it the range is 0.099 to 0.148, so
  // this figure is a property of any 64-symbol m-sequence preamble split this way,
  // not a flaw of this choice. The detector threshold must be set with it in mind.
  const std::vector<int> s = as_signs();
  const auto detector = [&](int lag) {
    double d = 0.0;
    for (int k = 0; k < 4; ++k) {
      int c = 0;
      for (int i = 16 * k; i < 16 * k + 16; ++i) {
        const int j = i + lag;
        if (j >= 0 && j < 64) c += s[static_cast<std::size_t>(i)] * s[static_cast<std::size_t>(j)];
      }
      d += static_cast<double>(c) * c;
    }
    return d;
  };

  CHECK_NEAR(detector(0), 1024.0, 1e-9);
  double worst = 0.0;
  for (int lag = -63; lag <= 63; ++lag) {
    if (lag != 0) worst = std::max(worst, detector(lag));
  }
  CHECK_NEAR(worst / 1024.0, 0.137, 0.002);
  CHECK(worst / 1024.0 < 0.15);  // no worse than the weakest of the 252 variants
}

TEST(sidelobes_are_below_a_typical_choice) {
  // The chosen rotation should beat the median candidate (worst sidelobe 9 in the
  // search). This guards against someone swapping in an arbitrary rotation.
  const std::vector<int> s = as_signs();
  int worst = 0;
  for (int k = 1; k < 64; ++k) worst = std::max(worst, std::abs(aperiodic_autocorrelation(s, k)));
  CHECK(worst < 9);
}

MPSK_TEST_MAIN()
