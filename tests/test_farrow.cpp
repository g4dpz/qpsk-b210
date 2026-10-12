// Tests for the cubic Farrow interpolator.

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/farrow.hpp"
#include "dsp/fir.hpp"
#include "dsp/interpolator.hpp"
#include "dsp/rrc.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cf = std::complex<float>;

namespace {

/** Matched-filter output of random QPSK at 4 samples per symbol: what the loops read. */
std::vector<cf> shaped_signal(unsigned seed) {
  mpsk::sim::NoiseSource rng(seed);
  std::vector<cf> sym(2000);
  for (auto& s : sym) {
    s = cf(rng.uniform() < 0.5 ? 0.70710678f : -0.70710678f, rng.uniform() < 0.5 ? 0.70710678f : -0.70710678f);
  }
  const auto taps = rrc_taps(0.35, 4, 11);
  auto tx = upsample_and_filter(sym, taps, 4);
  tx.resize(tx.size() + taps.size(), cf(0, 0));
  FirFilter mf(taps);
  return mf.process(tx);
}

}  // namespace

TEST(on_the_grid_the_interpolator_returns_the_sample_exactly) {
  const std::vector<cf> y{{1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}};
  for (std::size_t i = 1; i + 2 < y.size(); ++i) {
    const cf got = farrow_cubic(y.data(), y.size(), static_cast<double>(i));
    CHECK(got == y[i]);
  }
}

TEST(a_cubic_is_reproduced_exactly) {
  // Lagrange cubic interpolation is exact for any polynomial of degree 3 or less.
  const auto f = [](double t) { return 0.3 - 1.1 * t + 0.7 * t * t - 0.05 * t * t * t; };
  std::vector<cf> y(20);
  for (std::size_t n = 0; n < y.size(); ++n) {
    y[n] = cf(static_cast<float>(f(static_cast<double>(n))), static_cast<float>(2.0 * f(static_cast<double>(n))));
  }
  for (const double pos : {1.0, 1.1, 5.37, 9.5, 12.99, 17.2}) {
    const cf got = farrow_cubic(y.data(), y.size(), pos);
    CHECK_NEAR(got.real(), f(pos), 1e-4);
    CHECK_NEAR(got.imag(), 2.0 * f(pos), 2e-4);
  }
}

TEST(the_interpolator_is_linear_and_handles_complex_values) {
  mpsk::sim::NoiseSource rng(1);
  std::vector<cf> a(50), b(50), sum(50);
  for (std::size_t i = 0; i < 50; ++i) {
    a[i] = rng.complex_noise(1.0);
    b[i] = rng.complex_noise(1.0);
    sum[i] = 2.0f * a[i] + cf(0.0f, 1.0f) * b[i];
  }
  for (const double pos : {3.3, 10.5, 24.9, 40.1}) {
    const cf want = 2.0f * farrow_cubic(a.data(), a.size(), pos) + cf(0.0f, 1.0f) * farrow_cubic(b.data(), b.size(), pos);
    CHECK(std::abs(farrow_cubic(sum.data(), sum.size(), pos) - want) < 1e-5f);
  }
}

TEST(error_against_the_sinc_interpolator_on_a_shaped_signal_is_small) {
  // Measured on the matched-filter output of random QPSK: relative error -61 dB at a
  // fractional position of 0.1, -54 dB at 0.25, -51 dB at 0.5 (0.27%). That caps the
  // EVM contribution at a fraction of a percent, far below what noise does. Bounds
  // here are about 3 dB looser than measured.
  const auto y = shaped_signal(1);
  struct Case { double mu; double max_error; };
  for (const Case c : {Case{0.1, 0.0016}, Case{0.25, 0.0030}, Case{0.5, 0.0040}, Case{0.75, 0.0030}, Case{0.9, 0.0016}}) {
    double e2 = 0, s2 = 0;
    for (std::size_t i = 200; i + 200 < y.size(); i += 3) {
      const double pos = static_cast<double>(i) + c.mu;
      const cf a = farrow_cubic(y.data(), y.size(), pos);
      const cf b = sinc_interpolate(y.data(), y.size(), pos, 8);
      e2 += std::norm(a - b);
      s2 += std::norm(b);
    }
    CHECK(std::sqrt(e2 / s2) < c.max_error);
  }
}

TEST(the_error_is_worst_at_half_a_sample_and_vanishes_on_the_grid) {
  const auto y = shaped_signal(2);
  const auto rel_error = [&](double mu) {
    double e2 = 0, s2 = 0;
    for (std::size_t i = 200; i + 200 < y.size(); i += 3) {
      const double pos = static_cast<double>(i) + mu;
      e2 += std::norm(farrow_cubic(y.data(), y.size(), pos) - sinc_interpolate(y.data(), y.size(), pos, 8));
      s2 += std::norm(sinc_interpolate(y.data(), y.size(), pos, 8));
    }
    return std::sqrt(e2 / s2);
  };
  CHECK(rel_error(0.0) < 1e-6);
  CHECK(rel_error(0.5) > rel_error(0.1));
  CHECK(rel_error(0.5) > rel_error(0.9));
}

TEST(range_checking_follows_the_samples_it_reads) {
  const std::vector<cf> y(10, cf(1, 0));
  // Reads samples floor(pos) - 1 .. floor(pos) + 2, so pos must satisfy 1 <= pos and floor(pos) + 2 <= 9.
  CHECK(farrow_in_range(10, 1.0));
  CHECK(farrow_in_range(10, 7.99));
  CHECK(!farrow_in_range(10, 8.0));
  CHECK(!farrow_in_range(10, 0.99));
  CHECK(!farrow_in_range(10, -3.0));
  CHECK(!farrow_in_range(10, std::nan("")));
  CHECK(!farrow_in_range(3, 1.0));
  CHECK(!farrow_in_range(0, 1.0));
  CHECK_THROWS(farrow_cubic(y.data(), y.size(), 8.0));
  CHECK_THROWS(farrow_cubic(y.data(), y.size(), 0.5));
  CHECK(std::abs(farrow_cubic(y.data(), y.size(), 7.5) - cf(1, 0)) < 1e-6f);
  CHECK(kFarrowBefore == 1 && kFarrowAfter == 2);
}

TEST(a_constant_signal_is_interpolated_without_error) {
  const std::vector<cf> y(30, cf(0.4f, -0.7f));
  for (const double pos : {2.0, 5.123, 14.5, 27.99}) {
    CHECK(std::abs(farrow_cubic(y.data(), y.size(), pos) - cf(0.4f, -0.7f)) < 1e-6f);
  }
}

MPSK_TEST_MAIN()
