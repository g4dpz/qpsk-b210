// Tests for the streaming FIR filter and the upsample-and-filter path.

#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include "dsp/fir.hpp"
#include "dsp/rrc.hpp"
#include "test_harness.hpp"

using mpsk::dsp::FirFilter;
using mpsk::dsp::upsample_and_filter;
using cf = std::complex<float>;

namespace {

std::vector<cf> random_signal(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  std::vector<cf> x(n);
  for (auto& s : x) s = cf(dist(rng), dist(rng));
  return x;
}

double max_abs_diff(const std::vector<cf>& a, const std::vector<cf>& b) {
  double worst = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
  }
  return worst;
}

/** Reference convolution, written the obvious way and independent of FirFilter. */
std::vector<cf> convolve(const std::vector<cf>& x, const std::vector<float>& h) {
  std::vector<cf> y(x.size() + h.size() - 1, cf(0.0f, 0.0f));
  for (std::size_t i = 0; i < x.size(); ++i) {
    for (std::size_t k = 0; k < h.size(); ++k) y[i + k] += x[i] * h[k];
  }
  return y;
}

}  // namespace

TEST(impulse_response_equals_the_taps) {
  const std::vector<float> taps{0.5f, -1.0f, 2.0f, 0.25f, 3.0f};
  FirFilter f(taps);
  std::vector<cf> out;
  out.push_back(f.push(cf(1.0f, 0.0f)));
  for (std::size_t i = 1; i < taps.size() + 3; ++i) out.push_back(f.push(cf(0.0f, 0.0f)));

  for (std::size_t i = 0; i < taps.size(); ++i) {
    CHECK_NEAR(out[i].real(), taps[i], 1e-7);
    CHECK_NEAR(out[i].imag(), 0.0, 1e-7);
  }
  for (std::size_t i = taps.size(); i < out.size(); ++i) CHECK(std::abs(out[i]) == 0.0f);
}

TEST(real_taps_filter_i_and_q_independently) {
  const std::vector<float> taps{1.0f, 2.0f, 3.0f};
  FirFilter f(taps);
  // A pure-imaginary impulse comes out as j times the taps.
  const cf first = f.push(cf(0.0f, 1.0f));
  const cf second = f.push(cf(0.0f, 0.0f));
  CHECK_NEAR(first.real(), 0.0, 1e-7);
  CHECK_NEAR(first.imag(), 1.0, 1e-7);
  CHECK_NEAR(second.imag(), 2.0, 1e-7);
}

TEST(matches_a_reference_convolution) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, 4, 11);
  const std::vector<cf> x = random_signal(500, 1);

  FirFilter f(taps);
  const std::vector<cf> streamed = f.process(x);
  const std::vector<cf> reference = convolve(x, taps);

  CHECK(streamed.size() == x.size());
  // The streaming filter output is the first x.size() samples of the convolution.
  for (std::size_t i = 0; i < x.size(); ++i) {
    CHECK(std::abs(streamed[i] - reference[i]) < 1e-5f);
  }
}

TEST(block_size_does_not_change_the_output) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, 4, 11);
  const std::vector<cf> x = random_signal(1000, 2);

  FirFilter whole(taps);
  const std::vector<cf> expected = whole.process(x);

  // Feed the same signal in blocks of awkward, uneven sizes, including sizes
  // smaller and larger than the filter length.
  FirFilter chunked(taps);
  std::vector<cf> got;
  std::size_t pos = 0;
  const std::vector<std::size_t> sizes{1, 7, 44, 45, 46, 3, 200, 90};
  for (std::size_t i = 0; pos < x.size(); ++i) {
    const std::size_t n = std::min(sizes[i % sizes.size()], x.size() - pos);
    std::vector<cf> block(x.begin() + static_cast<std::ptrdiff_t>(pos),
                          x.begin() + static_cast<std::ptrdiff_t>(pos + n));
    const std::vector<cf> y = chunked.process(block);
    got.insert(got.end(), y.begin(), y.end());
    pos += n;
  }
  CHECK(got.size() == expected.size());
  CHECK(max_abs_diff(got, expected) == 0.0);  // identical, not merely close
}

TEST(in_place_processing_matches_out_of_place) {
  const std::vector<float> taps{0.25f, 0.5f, 0.25f};
  const std::vector<cf> x = random_signal(100, 3);

  FirFilter a(taps);
  const std::vector<cf> expected = a.process(x);

  FirFilter b(taps);
  std::vector<cf> inplace = x;
  b.process(inplace.data(), inplace.data(), inplace.size());
  CHECK(max_abs_diff(inplace, expected) == 0.0);
}

TEST(reset_clears_the_history) {
  const std::vector<float> taps{1.0f, 1.0f, 1.0f, 1.0f};
  FirFilter f(taps);
  for (int i = 0; i < 10; ++i) f.push(cf(5.0f, -3.0f));
  f.reset();
  // After a reset the filter behaves as if it had only seen zeros.
  const cf y = f.push(cf(1.0f, 0.0f));
  CHECK_NEAR(y.real(), 1.0, 1e-7);
  CHECK_NEAR(y.imag(), 0.0, 1e-7);
}

TEST(single_tap_filter_scales_the_signal) {
  FirFilter f({2.0f});
  const cf y = f.push(cf(1.5f, -0.5f));
  CHECK_NEAR(y.real(), 3.0, 1e-7);
  CHECK_NEAR(y.imag(), -1.0, 1e-7);
}

TEST(filter_is_linear) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, 4, 8);
  const std::vector<cf> a = random_signal(200, 4);
  const std::vector<cf> b = random_signal(200, 5);
  std::vector<cf> sum(a.size());
  for (std::size_t i = 0; i < a.size(); ++i) sum[i] = 2.0f * a[i] + b[i];

  FirFilter fa(taps), fb(taps), fs(taps);
  const auto ya = fa.process(a);
  const auto yb = fb.process(b);
  const auto ys = fs.process(sum);
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(std::abs(ys[i] - (2.0f * ya[i] + yb[i])) < 1e-4f);
  }
}

TEST(group_delay_is_half_the_length_minus_one) {
  CHECK_NEAR(FirFilter(mpsk::dsp::rrc_taps(0.35, 4, 11)).group_delay(), 22.0, 1e-12);
  CHECK(FirFilter(mpsk::dsp::rrc_taps(0.35, 4, 11)).num_taps() == 45);
}

TEST(fir_rejects_empty_taps) { CHECK_THROWS(FirFilter(std::vector<float>{})); }

// ---- upsample_and_filter ---------------------------------------------------

TEST(upsample_and_filter_equals_zero_stuffing_then_filtering) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, 4, 11);
  const std::vector<cf> symbols = random_signal(60, 6);
  const int sps = 4;

  // Build the zero-stuffed signal by hand, then convolve with the reference.
  std::vector<cf> stuffed((symbols.size() - 1) * sps + 1, cf(0.0f, 0.0f));
  for (std::size_t m = 0; m < symbols.size(); ++m) stuffed[m * sps] = symbols[m];
  const std::vector<cf> reference = convolve(stuffed, taps);

  const std::vector<cf> got = upsample_and_filter(symbols, taps, sps);
  CHECK(got.size() == reference.size());
  CHECK(max_abs_diff(got, reference) < 1e-5);
}

TEST(upsample_and_filter_output_length) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, 4, 11);  // 45 taps
  const std::vector<cf> symbols(10, cf(1.0f, 0.0f));
  CHECK(upsample_and_filter(symbols, taps, 4).size() == 9 * 4 + 45);
  CHECK(upsample_and_filter(std::vector<cf>(1, cf(1.0f, 0.0f)), taps, 4).size() == 45);
  CHECK(upsample_and_filter({}, taps, 4).empty());
}

TEST(a_single_symbol_produces_the_pulse_scaled_by_the_symbol) {
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, 4, 11);
  const cf symbol(0.6f, -0.8f);
  const std::vector<cf> out = upsample_and_filter({symbol}, taps, 4);
  for (std::size_t k = 0; k < taps.size(); ++k) {
    CHECK(std::abs(out[k] - symbol * taps[k]) < 1e-6f);
  }
}

TEST(symbol_peaks_fall_at_the_group_delay) {
  const int sps = 4;
  const int span = 11;
  const std::vector<float> taps = mpsk::dsp::rrc_taps(0.35, sps, span);
  const std::vector<cf> symbols(5, cf(1.0f, 0.0f));
  const std::vector<cf> out = upsample_and_filter(symbols, taps, sps);

  // For a lone symbol the largest output sample is the centre tap, at the
  // group delay. Here the symbols overlap, but the position still holds the
  // symbol's own peak plus ISI from neighbours that is small by construction.
  const std::vector<cf> lone = upsample_and_filter({cf(1.0f, 0.0f)}, taps, sps);
  std::size_t peak = 0;
  for (std::size_t i = 0; i < lone.size(); ++i) {
    if (std::abs(lone[i]) > std::abs(lone[peak])) peak = i;
  }
  CHECK(static_cast<int>(peak) == mpsk::dsp::rrc_group_delay(sps, span));
}

TEST(upsample_and_filter_rejects_bad_arguments) {
  const std::vector<cf> symbols(3, cf(1.0f, 0.0f));
  CHECK_THROWS(upsample_and_filter(symbols, {}, 4));
  CHECK_THROWS(upsample_and_filter(symbols, {1.0f}, 0));
}

MPSK_TEST_MAIN()
