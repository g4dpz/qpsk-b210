// Tests for the channel building blocks and the interpolator they rest on.

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/fir.hpp"
#include "dsp/interpolator.hpp"
#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "dsp/rrc.hpp"
#include "dsp/rx_frontend.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::sim;
using mpsk::dsp::sinc_interpolation_weights;
using cf = std::complex<float>;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

// ---- Noise source -----------------------------------------------------------

TEST(noise_is_reproducible_for_a_seed_and_differs_between_seeds) {
  NoiseSource a(42), b(42), c(43);
  bool differs = false;
  for (int i = 0; i < 100; ++i) {
    const double x = a.gaussian();
    CHECK(x == b.gaussian());
    if (x != c.gaussian()) differs = true;
  }
  CHECK(differs);
}

TEST(gaussian_draws_have_the_right_moments) {
  // Measured over 2e6 draws: mean +0.0004, variance 0.9984, kurtosis 3.0025.
  NoiseSource rng(5);
  const int n = 1000000;
  double s1 = 0, s2 = 0, s4 = 0;
  for (int i = 0; i < n; ++i) {
    const double x = rng.gaussian();
    s1 += x;
    s2 += x * x;
    s4 += x * x * x * x;
  }
  const double mean = s1 / n, var = s2 / n - mean * mean;
  CHECK_NEAR(mean, 0.0, 0.005);
  CHECK_NEAR(var, 1.0, 0.01);
  CHECK_NEAR((s4 / n) / (var * var), 3.0, 0.05);
}

TEST(successive_draws_are_uncorrelated) {
  NoiseSource rng(6);
  const int n = 500000;
  std::vector<double> x(n);
  for (auto& v : x) v = rng.gaussian();
  for (const int lag : {1, 2, 3, 7}) {
    double c = 0;
    for (int i = 0; i + lag < n; ++i) c += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i + lag)];
    CHECK_NEAR(c / (n - lag), 0.0, 0.006);  // five standard errors (1 / sqrt(n) = 0.0014)
  }
}

TEST(uniform_stays_in_the_half_open_unit_interval) {
  NoiseSource rng(7);
  double lo = 1, hi = 0;
  for (int i = 0; i < 200000; ++i) {
    const double u = rng.uniform();
    CHECK(u >= 0.0 && u < 1.0);
    lo = std::min(lo, u);
    hi = std::max(hi, u);
  }
  CHECK(lo < 0.001 && hi > 0.999);
}

TEST(complex_noise_splits_its_variance_equally_between_i_and_q) {
  NoiseSource rng(8);
  const int n = 500000;
  double rr = 0, ii = 0, ri = 0;
  for (int i = 0; i < n; ++i) {
    const cf z = rng.complex_noise(2.0);
    rr += static_cast<double>(z.real()) * z.real();
    ii += static_cast<double>(z.imag()) * z.imag();
    ri += static_cast<double>(z.real()) * z.imag();
  }
  CHECK_NEAR(rr / n, 1.0, 0.02);
  CHECK_NEAR(ii / n, 1.0, 0.02);
  CHECK_NEAR(ri / n, 0.0, 0.01);
}

TEST(add_noise_produces_the_requested_power) {
  NoiseSource rng(9);
  for (const double var : {0.0025, 0.1, 4.0}) {
    std::vector<cf> x(200000, cf(0, 0));
    add_noise(x, var, rng);
    CHECK_NEAR(mean_power(x, 0, x.size()) / var, 1.0, 0.02);
  }
}

// ---- Es/N0 ------------------------------------------------------------------

TEST(noise_variance_follows_the_derived_formula) {
  // variance = Ps * sps / (Es/N0)
  CHECK_NEAR(noise_variance_for_esn0(0.0625, 4, 10.0), 0.0625 * 4 / 10.0, 1e-12);
  CHECK_NEAR(noise_variance_for_esn0(0.0625, 4, 0.0), 0.25, 1e-12);
  CHECK_NEAR(noise_variance_for_esn0(0.0625, 4, 20.0), 0.0025, 1e-12);
  // 3 dB more SNR means half the noise power.
  CHECK_NEAR(noise_variance_for_esn0(0.1, 4, 3.0103) * 2.0, noise_variance_for_esn0(0.1, 4, 0.0), 1e-4);
}

TEST(the_symbol_snr_at_the_receiver_matches_the_requested_esn0) {
  // End to end through the real transmitter and receiver front end: the noise
  // actually present at the decision instants, against the symbol amplitude there.
  // This is the test that exposed a start-up fault in the DC blocker (noise 8x too
  // high on a short record); with it fixed, the delivered SNR is within 0.15 dB.
  mpsk::dsp::Modulator mod{mpsk::dsp::ModulatorConfig{}};
  const int sps = 4;
  const auto burst = mod.modulate(mpsk::dsp::FrameType::Data, 1, mpsk::dsp::prbs_bytes(3, 1, 16));
  std::vector<cf> x(300, cf(0, 0));
  x.insert(x.end(), burst.samples.begin(), burst.samples.end());
  x.insert(x.end(), 400, cf(0, 0));

  mpsk::dsp::RxFrontEnd clean_fe{mpsk::dsp::RxFrontEndConfig{}};
  const auto clean = clean_fe.process(x);
  const std::size_t tp = 300 + burst.first_symbol_peak_index + static_cast<std::size_t>(clean_fe.group_delay());
  const double symbol_power = 0.25;  // (output scale 0.5)^2, unit-power symbols

  for (const double esn0 : {15.0, 8.0, 3.0}) {
    double noise = 0.0;
    const int runs = 60;
    for (int seed = 1; seed <= runs; ++seed) {
      NoiseSource rng(static_cast<std::uint64_t>(seed));
      auto r = x;
      add_noise(r, noise_variance_for_esn0(mean_power(burst.samples, 184, burst.samples.size() - 184), sps, esn0), rng);
      mpsk::dsp::RxFrontEnd fe{mpsk::dsp::RxFrontEndConfig{}};
      const auto y = fe.process(r);
      double nv = 0;
      for (int i = 0; i < 64; ++i) {
        nv += std::norm(y[tp + static_cast<std::size_t>(i * sps)] - clean[tp + static_cast<std::size_t>(i * sps)]);
      }
      noise += nv / 64.0;
    }
    noise /= runs;
    const double delivered_db = 10.0 * std::log10(symbol_power / noise);
    CHECK_NEAR(delivered_db, esn0, 0.3);
  }
}

// ---- Interpolation ----------------------------------------------------------

TEST(interpolation_weights_sum_to_one_and_hit_the_sample_on_the_grid) {
  for (const double frac : {0.0, 0.1, 0.5, 0.9, 0.999}) {
    const auto w = sinc_interpolation_weights(frac, 8);
    CHECK(w.size() == 16);
    double sum = 0;
    for (const double v : w) sum += v;
    CHECK_NEAR(sum, 1.0, 1e-12);
  }
  const auto on_grid = sinc_interpolation_weights(0.0, 8);
  CHECK(on_grid[7] == 1.0);  // the sample itself
  double others = 0;
  for (std::size_t i = 0; i < on_grid.size(); ++i) {
    if (i != 7) others += std::fabs(on_grid[i]);
  }
  CHECK(others == 0.0);
}

TEST(interpolating_a_band_limited_signal_is_accurate) {
  // A tone at 17% of the sampling rate, the width of the pulse-shaped signals here.
  std::vector<cf> x(400);
  const double f = 0.17;
  for (std::size_t n = 0; n < x.size(); ++n) x[n] = std::polar(1.0f, static_cast<float>(2 * kPi * f * static_cast<double>(n)));
  double worst = 0;
  for (const double pos : {100.1, 150.25, 200.5, 250.75, 300.9}) {
    const cf got = mpsk::dsp::sinc_interpolate(x.data(), x.size(), pos);
    const cf want = std::polar(1.0f, static_cast<float>(2 * kPi * f * pos));
    worst = std::max(worst, static_cast<double>(std::abs(got - want)));
  }
  CHECK(worst < 1e-3);
}

TEST(interpolation_treats_samples_beyond_the_ends_as_zero) {
  const std::vector<cf> x(10, cf(1, 0));
  const cf at_edge = mpsk::dsp::sinc_interpolate(x.data(), x.size(), -0.5);
  CHECK(std::abs(at_edge) < 0.9f);  // fewer than 16 real samples contribute
  CHECK(std::abs(mpsk::dsp::sinc_interpolate(x.data(), x.size(), 4.5)) > 0.99f);
}

TEST(interpolation_arguments_are_checked) {
  CHECK_THROWS(sinc_interpolation_weights(1.0, 8));
  CHECK_THROWS(sinc_interpolation_weights(-0.1, 8));
  CHECK_THROWS(sinc_interpolation_weights(0.5, 0));
}

// ---- Delay ------------------------------------------------------------------

TEST(a_whole_sample_delay_shifts_the_signal_exactly) {
  const std::vector<cf> x{{1, 2}, {3, 4}, {5, 6}, {7, 8}};
  const auto y = delay_signal(x, 3.0);
  CHECK(y.size() == x.size() + 3 + 8);
  for (std::size_t i = 0; i < 3; ++i) CHECK(y[i] == cf(0, 0));
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(std::abs(y[i + 3] - x[i]) < 1e-6f);
}

TEST(zero_delay_leaves_the_signal_unchanged) {
  const std::vector<cf> x{{1, 2}, {3, 4}, {5, 6}};
  const auto y = delay_signal(x, 0.0);
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(std::abs(y[i] - x[i]) < 1e-6f);
}

TEST(a_fractional_delay_moves_a_pulse_shaped_signal_by_that_amount) {
  // Delay a shaped signal by 0.37 samples, then delay it a further 2.63, and
  // compare with delaying by 3 in one step: delays add.
  const auto taps = mpsk::dsp::rrc_taps(0.35, 4, 11);
  std::vector<cf> syms;
  NoiseSource rng(11);
  for (int i = 0; i < 80; ++i) syms.push_back(cf(rng.uniform() < 0.5 ? 0.7071f : -0.7071f, rng.uniform() < 0.5 ? 0.7071f : -0.7071f));
  const auto x = mpsk::dsp::upsample_and_filter(syms, taps, 4);

  const auto two_steps = delay_signal(delay_signal(x, 0.37), 2.63);
  const auto one_step = delay_signal(x, 3.0);
  double worst = 0;
  for (std::size_t i = 20; i < x.size() + 3 - 20; ++i) {
    worst = std::max(worst, static_cast<double>(std::abs(two_steps[i] - one_step[i])));
  }
  CHECK(worst < 0.01);
}

TEST(the_delay_is_in_the_direction_it_claims) {
  // A pulse at index 40 appears at 40 + d after a delay of d.
  std::vector<cf> x(100, cf(0, 0));
  const auto taps = mpsk::dsp::rrc_taps(0.35, 4, 11);
  const auto pulse = mpsk::dsp::upsample_and_filter({cf(1, 0)}, taps, 4);
  for (std::size_t k = 0; k < pulse.size(); ++k) x[k + 10] = pulse[k];  // peak at 10 + 22 = 32
  const auto y = delay_signal(x, 5.0);
  std::size_t peak_x = 0, peak_y = 0;
  for (std::size_t i = 0; i < x.size(); ++i) if (std::abs(x[i]) > std::abs(x[peak_x])) peak_x = i;
  for (std::size_t i = 0; i < y.size(); ++i) if (std::abs(y[i]) > std::abs(y[peak_y])) peak_y = i;
  CHECK(peak_x == 32);
  CHECK(peak_y == 37);
}

TEST(a_bad_delay_is_rejected) {
  const std::vector<cf> x(10, cf(1, 0));
  CHECK_THROWS(delay_signal(x, -0.5));
  CHECK_THROWS(delay_signal(x, std::nan("")));
  CHECK_THROWS(delay_signal(x, INFINITY));
}

// ---- Carrier rotation -------------------------------------------------------

TEST(rotation_applies_the_phase_and_the_frequency) {
  std::vector<cf> x(5, cf(1, 0));
  rotate_carrier(x, 0.1, 0.5);  // 0.1 cycles per sample, 0.5 rad
  for (std::size_t n = 0; n < x.size(); ++n) {
    const cf want = std::polar(1.0f, static_cast<float>(2 * kPi * 0.1 * static_cast<double>(n) + 0.5));
    CHECK(std::abs(x[n] - want) < 1e-6f);
  }
}

TEST(rotation_keeps_the_magnitude) {
  NoiseSource rng(12);
  std::vector<cf> x(1000, cf(0, 0));
  add_noise(x, 1.0, rng);
  auto y = x;
  rotate_carrier(y, 0.0123, 1.0);
  for (std::size_t i = 0; i < x.size(); ++i) CHECK_NEAR(std::abs(y[i]), std::abs(x[i]), 1e-5);
}

TEST(a_block_cut_from_a_longer_signal_keeps_its_phase_track_with_start_index) {
  std::vector<cf> whole(100, cf(1, 0));
  rotate_carrier(whole, 0.03, 0.2);
  std::vector<cf> part(40, cf(1, 0));
  rotate_carrier(part, 0.03, 0.2, 60.0);
  for (std::size_t i = 0; i < part.size(); ++i) CHECK(std::abs(part[i] - whole[60 + i]) < 1e-5f);
}

MPSK_TEST_MAIN()
