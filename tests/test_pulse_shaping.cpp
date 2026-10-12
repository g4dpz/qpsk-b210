// End-to-end pulse-shaping test: transmit RRC then matched receive RRC.
// This is the property the whole chain depends on: after both filters, each
// symbol can be read back at the right instant with (nearly) no ISI.

#include <cmath>
#include <complex>
#include <iostream>
#include <random>
#include <vector>

#include "dsp/bits.hpp"
#include "dsp/constellation.hpp"
#include "dsp/fir.hpp"
#include "dsp/rrc.hpp"
#include "test_harness.hpp"

using cf = std::complex<float>;

namespace {

/**
 * Shapes @p symbols, matched-filters the result, and returns the symbol-spaced
 * samples at the right instants.
 *
 * Delay bookkeeping: the transmit filter delays by (N-1)/2 samples and the
 * matched filter by another (N-1)/2, so symbol m is read at index
 * m * sps + (N - 1) = m * sps + span * sps.
 */
std::vector<cf> shape_and_recover(const std::vector<cf>& symbols, double rolloff, int sps,
                                  int span) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(rolloff, sps, span);
  std::vector<cf> tx = mpsk::dsp::upsample_and_filter(symbols, taps, sps);

  // Flush: feed zeros so the matched filter sees the tail of the last symbol.
  tx.resize(tx.size() + taps.size() - 1, cf(0.0f, 0.0f));

  mpsk::dsp::FirFilter matched(taps);
  const std::vector<cf> rx = matched.process(tx);

  std::vector<cf> recovered;
  const std::size_t delay = taps.size() - 1;
  for (std::size_t m = 0; m < symbols.size(); ++m) {
    recovered.push_back(rx[m * static_cast<std::size_t>(sps) + delay]);
  }
  return recovered;
}

std::vector<cf> random_qpsk(std::size_t count, unsigned seed) {
  std::mt19937 rng(seed);
  mpsk::dsp::Bits bits(count * 2);
  for (auto& b : bits) b = static_cast<uint8_t>(rng() & 1u);
  return mpsk::dsp::map_bits(mpsk::dsp::Constellation(4), bits);
}

}  // namespace

TEST(qpsk_symbols_are_recovered_with_negligible_isi) {
  const std::vector<cf> symbols = random_qpsk(500, 99);

  // Roll-offs of 0.35 (the default) and 0.5. A smaller roll-off gives a pulse
  // that decays more slowly, so a 12-symbol filter truncates more of it; that
  // case is covered by the span test below instead of by a looser limit here.
  for (const double rolloff : {0.35, 0.5}) {
    for (const int sps : {2, 4, 8}) {
      const int span = 12;  // even for every sps tested
      const std::vector<cf> recovered = shape_and_recover(symbols, rolloff, sps, span);

      double worst = 0.0;
      double sum_sq = 0.0;
      for (std::size_t m = 0; m < symbols.size(); ++m) {
        const double err = static_cast<double>(std::abs(recovered[m] - symbols[m]));
        worst = std::max(worst, err);
        sum_sq += err * err;
      }
      const double evm = std::sqrt(sum_sq / static_cast<double>(symbols.size()));
      std::cout << "    beta=" << rolloff << " sps=" << sps << " worst error=" << worst
                << " rms EVM=" << evm << "\n";
      CHECK(worst < 0.02);
    }
  }
}

TEST(isi_comes_from_truncation_and_shrinks_as_the_span_grows) {
  // At a small roll-off the RRC pulse decays slowly, so cutting it at a finite
  // span leaves residual ISI. Measured: with roll-off 0.2 and 4 samples per
  // symbol the worst symbol error is about 0.019 at a span of 12 symbols and
  // about 0.002 at 48. If the error did not fall with span, the cause would be
  // a bug in the taps or the delay bookkeeping rather than truncation.
  const std::vector<cf> symbols = random_qpsk(500, 99);
  const int sps = 4;

  const auto worst_error = [&](int span) {
    const std::vector<cf> recovered = shape_and_recover(symbols, 0.2, sps, span);
    double worst = 0.0;
    for (std::size_t m = 0; m < symbols.size(); ++m) {
      worst = std::max(worst, static_cast<double>(std::abs(recovered[m] - symbols[m])));
    }
    return worst;
  };

  const double at_12 = worst_error(12);
  const double at_24 = worst_error(24);
  const double at_48 = worst_error(48);
  std::cout << "    beta=0.2 sps=4 worst error: span 12 = " << at_12 << ", span 24 = " << at_24
            << ", span 48 = " << at_48 << "\n";

  CHECK(at_12 < 0.03);
  CHECK(at_24 < at_12);
  CHECK(at_48 < at_24);
  CHECK(at_48 < 0.005);
}

TEST(recovered_symbols_demap_to_the_original_bits) {
  std::mt19937 rng(7);
  mpsk::dsp::Bits bits(2000);
  for (auto& b : bits) b = static_cast<uint8_t>(rng() & 1u);

  const mpsk::dsp::Constellation qpsk(4);
  const std::vector<cf> symbols = mpsk::dsp::map_bits(qpsk, bits);
  const std::vector<cf> recovered = shape_and_recover(symbols, 0.35, 4, 11);

  CHECK(mpsk::dsp::demap_symbols(qpsk, recovered) == bits);
}

TEST(a_wrong_sampling_instant_does_produce_isi) {
  // Guards the test above against passing for the wrong reason: half a symbol
  // off the right instant, the symbols must NOT come back cleanly.
  const std::vector<cf> symbols = random_qpsk(500, 99);
  const int sps = 4;
  const int span = 12;
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, sps, span);
  std::vector<cf> tx = mpsk::dsp::upsample_and_filter(symbols, taps, sps);
  tx.resize(tx.size() + taps.size() - 1, cf(0.0f, 0.0f));
  mpsk::dsp::FirFilter matched(taps);
  const std::vector<cf> rx = matched.process(tx);

  double worst = 0.0;
  for (std::size_t m = 10; m + 10 < symbols.size(); ++m) {
    const std::size_t index = m * sps + (taps.size() - 1) + sps / 2;  // half a symbol late
    worst = std::max(worst, static_cast<double>(std::abs(rx[index] - symbols[m])));
  }
  CHECK(worst > 0.3);
}

TEST(shaped_signal_power_is_one_over_sps) {
  // Unit-energy taps and unit-power symbols give an average sample power of
  // 1/sps. The modulator's output scaling and the channel's noise formula rely
  // on this.
  const std::vector<cf> symbols = random_qpsk(4000, 5);
  for (const int sps : {2, 4, 8}) {
    const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, sps, 12);
    const std::vector<cf> y = mpsk::dsp::upsample_and_filter(symbols, taps, sps);

    // Average over the steady-state middle, away from the ramp-up and ramp-down.
    const std::size_t start = taps.size();
    const std::size_t stop = y.size() - taps.size();
    double power = 0.0;
    for (std::size_t i = start; i < stop; ++i) power += static_cast<double>(std::norm(y[i]));
    power /= static_cast<double>(stop - start);
    CHECK_NEAR(power, 1.0 / sps, 0.02 / sps);
  }
}

MPSK_TEST_MAIN()
