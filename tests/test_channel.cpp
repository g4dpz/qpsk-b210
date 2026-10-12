// Tests for the channel model (task 8.1, requirement 6.2, 6.5, 6.6) and the theory
// functions (6.8). Each impairment is checked alone against a closed form, then the
// order they are applied in is checked, because the order is part of the model.

#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "sim/channel.hpp"
#include "sim/theory.hpp"
#include "test_harness.hpp"

using namespace mpsk::sim;
using cf = std::complex<float>;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<cf> constant(std::size_t n, cf v = cf(1.0f, 0.0f)) { return std::vector<cf>(n, v); }

std::vector<cf> tone(std::size_t n, double cycles_per_sample) {
  std::vector<cf> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double a = 2.0 * kPi * cycles_per_sample * static_cast<double>(i);
    x[i] = cf(static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a)));
  }
  return x;
}

/** Mean frequency of a record in cycles per sample, from the phase advance over [begin, end). */
double mean_frequency(const std::vector<cf>& y, std::size_t begin, std::size_t end) {
  std::complex<double> acc(0, 0);
  for (std::size_t i = begin; i + 1 < end; ++i) {
    acc += std::complex<double>(y[i + 1]) * std::conj(std::complex<double>(y[i]));
  }
  return std::arg(acc) / (2.0 * kPi);
}

double power(const std::vector<cf>& y) {
  double s = 0;
  for (const cf& v : y) s += std::norm(v);
  return s / static_cast<double>(y.size());
}

/** Centroid of |y|^2: where the energy of a pulse sits, in samples. */
double centroid(const std::vector<cf>& y) {
  double num = 0, den = 0;
  for (std::size_t i = 0; i < y.size(); ++i) {
    const double p = std::norm(y[i]);
    num += p * static_cast<double>(i);
    den += p;
  }
  return num / den;
}

/** A smooth Gaussian bump centred at @p c, width @p sigma, in a record of @p n samples. */
std::vector<cf> bump(std::size_t n, double c, double sigma) {
  std::vector<cf> x(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double d = (static_cast<double>(i) - c) / sigma;
    x[i] = cf(static_cast<float>(std::exp(-0.5 * d * d)), 0.0f);
  }
  return x;
}

}  // namespace

// ---- Doing nothing ----------------------------------------------------------

TEST(a_default_channel_returns_the_input_unchanged) {
  const auto x = tone(500, 0.013);
  const auto y = Channel(ChannelConfig{}).apply(x);
  REQUIRE(y.size() == x.size());
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(y[i] == x[i]);
}

// ---- Gain -------------------------------------------------------------------

TEST(the_gain_is_an_amplitude_ratio_in_db) {
  const auto x = tone(200, 0.01);
  for (const auto& [db, ratio] : {std::pair{20.0, 10.0}, std::pair{-20.0, 0.1}, std::pair{6.0205999, 2.0},
                                  std::pair{-40.0, 0.01}}) {
    ChannelConfig c;
    c.gain_db = db;
    const auto y = Channel(c).apply(x);
    REQUIRE(y.size() == x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
      CHECK_NEAR(std::abs(y[i]), ratio, 1e-5 * ratio);
    }
  }
}

// ---- Delay ------------------------------------------------------------------

TEST(a_whole_sample_delay_shifts_the_record_and_pads_with_silence) {
  const auto x = tone(300, 0.02);
  ChannelConfig c;
  c.delay_samples = 25.0;
  const auto y = Channel(c).apply(x);
  REQUIRE(y.size() >= x.size() + 25);
  for (std::size_t i = 0; i < 25; ++i) CHECK(y[i] == cf(0, 0));
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(y[i + 25] == x[i]);
}

TEST(a_fractional_delay_moves_a_smooth_signal_by_that_much) {
  // out[n] = x(n - 10.5). A tone is smooth, so the interior must match its closed form.
  const double f = 0.02;
  const auto x = tone(400, f);
  ChannelConfig c;
  c.delay_samples = 10.5;
  const auto y = Channel(c).apply(x);
  REQUIRE(y.size() > 300);
  for (std::size_t n = 40; n < 300; ++n) {
    const double a = 2.0 * kPi * f * (static_cast<double>(n) - 10.5);
    CHECK_NEAR(y[n].real(), std::cos(a), 2e-3);
    CHECK_NEAR(y[n].imag(), std::sin(a), 2e-3);
  }
}

TEST(a_delay_of_many_frames_is_exact_in_position_and_cheap) {
  // 13 000.25 samples is several frames of QPSK at 4 sps. The pulse must land there.
  const auto x = bump(400, 200.0, 12.0);
  ChannelConfig c;
  c.delay_samples = 13000.25;
  const auto y = Channel(c).apply(x);
  REQUIRE(y.size() >= x.size() + 13000);
  CHECK_NEAR(centroid(y), 200.0 + 13000.25, 0.01);
}

// ---- Carrier ----------------------------------------------------------------

TEST(the_carrier_offset_and_phase_rotate_a_constant_exactly) {
  ChannelConfig c;
  c.cfo_hz = 1000.0;     // 1e-3 cycles per sample at 1 MS/s
  c.phase_rad = 0.3;
  const auto y = Channel(c).apply(constant(1000));
  REQUIRE(y.size() == 1000);
  for (std::size_t n = 0; n < y.size(); ++n) {
    const double a = 2.0 * kPi * 1e-3 * static_cast<double>(n) + 0.3;
    CHECK_NEAR(y[n].real(), std::cos(a), 2e-6);
    CHECK_NEAR(y[n].imag(), std::sin(a), 2e-6);
  }
}

TEST(a_phase_offset_alone_does_not_depend_on_the_sample_rate) {
  ChannelConfig c;
  c.phase_rad = 1.1;
  c.sample_rate_hz = 250000.0;
  const auto y = Channel(c).apply(constant(10));
  for (const cf& v : y) {
    CHECK_NEAR(std::arg(std::complex<double>(v)), 1.1, 1e-6);
  }
}

TEST(the_frequency_scale_follows_the_sample_rate) {
  ChannelConfig c;
  c.cfo_hz = 5000.0;
  c.sample_rate_hz = 500000.0;   // 0.01 cycles per sample
  const auto y = Channel(c).apply(constant(2000));
  CHECK_NEAR(mean_frequency(y, 0, y.size()), 0.01, 1e-7);
}

// ---- Slow drift -------------------------------------------------------------

TEST(a_frequency_drift_gives_a_quadratic_phase_and_a_linear_frequency) {
  // 1000 Hz/s for 0.1 s at 1 MS/s: the frequency climbs from 0 to 100 Hz.
  ChannelConfig c;
  c.freq_drift_hz_per_s = 1000.0;
  const std::size_t n = 100000;
  const auto y = Channel(c).apply(constant(n));
  REQUIRE(y.size() == n);
  for (const std::size_t i : {std::size_t{0}, std::size_t{20000}, std::size_t{50000}, std::size_t{99999}}) {
    const double t = static_cast<double>(i) * 1e-6;
    const double phase = 2.0 * kPi * 0.5 * 1000.0 * t * t;
    CHECK_NEAR(y[i].real(), std::cos(phase), 1e-5);
    CHECK_NEAR(y[i].imag(), std::sin(phase), 1e-5);
  }
  // The instantaneous frequency near the start and near the end.
  CHECK_NEAR(mean_frequency(y, 0, 2000) * 1e6, 1000.0 * 1000e-6, 1.0);        // ~1 Hz at t = 1 ms
  CHECK_NEAR(mean_frequency(y, n - 2000, n) * 1e6, 1000.0 * (0.1 - 1e-3), 1.0);  // ~99 Hz
}

TEST(the_drift_starts_from_the_carrier_offset) {
  ChannelConfig c;
  c.cfo_hz = 400.0;
  c.freq_drift_hz_per_s = 1000.0;
  const auto y = Channel(c).apply(constant(100000));
  CHECK_NEAR(mean_frequency(y, 0, 2000) * 1e6, 400.0 + 1.0, 1.0);
  CHECK_NEAR(mean_frequency(y, 98000, 100000) * 1e6, 400.0 + 99.0, 1.0);
}

TEST(the_phase_random_walk_has_the_stated_strength) {
  // Per-sample increments have a standard deviation of w * sqrt(1 / Fs). With
  // w = 100 rad/sqrt(s) at 1 MS/s that is 0.1 rad.
  ChannelConfig c;
  c.phase_walk_rad_per_sqrt_s = 100.0;
  c.seed = 5;
  const std::size_t n = 200000;
  const auto y = Channel(c).apply(constant(n));
  REQUIRE(y.size() == n);
  double sum = 0, sum2 = 0;
  for (std::size_t i = 0; i + 1 < n; ++i) {
    const double d = std::arg(std::complex<double>(y[i + 1]) * std::conj(std::complex<double>(y[i])));
    sum += d;
    sum2 += d * d;
  }
  const double m = static_cast<double>(n - 1);
  CHECK_NEAR(sum / m, 0.0, 0.002);
  CHECK_NEAR(std::sqrt(sum2 / m - (sum / m) * (sum / m)), 0.1, 0.003);
}

TEST(the_walk_is_repeatable_and_depends_on_the_seed) {
  ChannelConfig c;
  c.phase_walk_rad_per_sqrt_s = 50.0;
  c.seed = 7;
  const auto a = Channel(c).apply(constant(1000));
  const auto b = Channel(c).apply(constant(1000));
  c.seed = 8;
  const auto d = Channel(c).apply(constant(1000));
  CHECK(a == b);
  CHECK(!(a == d));
}

TEST(turning_the_walk_on_does_not_change_the_noise) {
  // The two random sources are separate, so adding one impairment leaves the other's
  // numbers alone: a sweep over the walk strength compares like with like. Silence in,
  // so the output is the noise alone, which is added after the rotation.
  ChannelConfig c;
  c.esn0_db = 10.0;
  c.seed = 21;
  const std::vector<cf> silence(5000, cf(0, 0));
  const auto plain = Channel(c).apply(silence, 0.25);
  c.phase_walk_rad_per_sqrt_s = 50.0;
  const auto walking = Channel(c).apply(silence, 0.25);
  CHECK(plain == walking);
  CHECK(std::abs(plain[0]) > 0.0f);   // and it really is noise, not silence
}

// ---- Sample clock -----------------------------------------------------------

TEST(a_fast_receiver_clock_lowers_the_apparent_frequency_and_stretches_the_record) {
  ChannelConfig c;
  c.clock_offset_ppm = 100000.0;   // 10%: large, so the effect is easy to measure
  const double f = 0.02;
  const auto y = Channel(c).apply(tone(5000, f));
  CHECK_NEAR(static_cast<double>(y.size()), 5000 * 1.1, 3.0);
  CHECK_NEAR(mean_frequency(y, 200, y.size() - 200), f / 1.1, 2e-5);
}

TEST(a_slow_receiver_clock_does_the_opposite) {
  ChannelConfig c;
  c.clock_offset_ppm = -50000.0;
  const double f = 0.02;
  const auto y = Channel(c).apply(tone(5000, f));
  CHECK_NEAR(static_cast<double>(y.size()), 5000 * 0.95, 3.0);
  CHECK_NEAR(mean_frequency(y, 200, y.size() - 200), f / 0.95, 2e-5);
}

// ---- Order ------------------------------------------------------------------

TEST(the_delay_comes_before_the_clock_offset) {
  // Delay first, then the clock stretch: a pulse at c, delayed by D, arrives at
  // (c + D)(1 + ppm). With the clock first it would arrive at c(1 + ppm) + D, 50 samples
  // earlier here.
  const auto x = bump(1500, 500.0, 15.0);
  ChannelConfig c;
  c.delay_samples = 1000.0;
  c.clock_offset_ppm = 50000.0;
  const auto y = Channel(c).apply(x);
  CHECK_NEAR(centroid(y), (500.0 + 1000.0) * 1.05, 0.05);
}

TEST(the_carrier_offset_comes_before_the_clock_offset) {
  // The clock offset rescales the carrier too: 10 kHz seen by a clock 10% fast is 10/1.1.
  ChannelConfig c;
  c.cfo_hz = 10000.0;
  c.clock_offset_ppm = 100000.0;
  const auto y = Channel(c).apply(constant(5000));
  CHECK_NEAR(mean_frequency(y, 200, y.size() - 200) * 1e6, 10000.0 / 1.1, 5.0);
}

// ---- Noise ------------------------------------------------------------------

TEST(the_noise_variance_follows_esn0_and_the_signal_power) {
  // variance = Ps * sps / (Es/N0) = 0.25 * 4 / 10 = 0.1 at 10 dB.
  ChannelConfig c;
  c.esn0_db = 10.0;
  c.seed = 3;
  const std::vector<cf> silence(200000, cf(0, 0));
  CHECK_NEAR(power(Channel(c).apply(silence, 0.25)), 0.1, 0.1 * 0.02);

  c.samples_per_symbol = 8;                                   // twice the bandwidth ratio
  CHECK_NEAR(power(Channel(c).apply(silence, 0.25)), 0.2, 0.2 * 0.02);

  c.samples_per_symbol = 4;
  c.esn0_db = 20.0;                                           // 10 dB lower
  CHECK_NEAR(power(Channel(c).apply(silence, 0.25)), 0.01, 0.01 * 0.02);
  CHECK_NEAR(power(Channel(c).apply(silence, 1.0)), 0.04, 0.04 * 0.02);   // 4x the power
}

TEST(the_noise_is_set_against_the_signal_after_the_gain) {
  // 6.0206 dB is an amplitude factor of 2, a power factor of 4: the noise must grow with it.
  ChannelConfig c;
  c.esn0_db = 10.0;
  c.seed = 3;
  const std::vector<cf> silence(200000, cf(0, 0));
  const double base = power(Channel(c).apply(silence, 0.25));
  c.gain_db = 6.0205999;
  CHECK_NEAR(power(Channel(c).apply(silence, 0.25)) / base, 4.0, 0.04);
  c.gain_db = -40.0;
  CHECK_NEAR(power(Channel(c).apply(silence, 0.25)) / base, 1e-4, 1e-6);
}

TEST(no_noise_is_added_unless_asked_for) {
  ChannelConfig c;
  c.cfo_hz = 100.0;
  const auto x = constant(100);
  const auto y = Channel(c).apply(x, 0.25);   // a signal power given but no esn0_db
  for (std::size_t n = 0; n < y.size(); ++n) CHECK_NEAR(std::abs(y[n]), 1.0, 1e-6);
}

TEST(the_noise_covers_the_whole_output_including_the_delay) {
  ChannelConfig c;
  c.esn0_db = 10.0;
  c.delay_samples = 500.0;
  const std::vector<cf> silence(100, cf(0, 0));
  const auto y = Channel(c).apply(silence, 0.25);
  REQUIRE(y.size() >= 600);
  CHECK(std::abs(y[0]) > 0.0f);     // noise in the part that is only delay
  CHECK(std::abs(y[y.size() - 1]) > 0.0f);
}

TEST(the_same_seed_gives_the_same_output_and_another_seed_gives_another) {
  ChannelConfig c;
  c.esn0_db = 5.0;
  c.cfo_hz = 300.0;
  c.seed = 9;
  const auto x = tone(2000, 0.01);
  const auto a = Channel(c).apply(x, 0.25);
  const auto b = Channel(c).apply(x, 0.25);
  c.seed = 10;
  const auto d = Channel(c).apply(x, 0.25);
  CHECK(a == b);
  CHECK(!(a == d));
}

// ---- Bad settings -----------------------------------------------------------

TEST(unusable_settings_are_rejected_with_the_setting_named) {
  const double nan = std::nan("");
  const double inf = std::numeric_limits<double>::infinity();
  auto bad = [](auto&& edit) {
    ChannelConfig c;
    edit(c);
    try {
      Channel ch(c);
    } catch (const std::invalid_argument& e) {
      return std::string(e.what());
    }
    return std::string();
  };
  CHECK(bad([](ChannelConfig& c) { c.sample_rate_hz = 0; }).find("sample_rate_hz") != std::string::npos);
  CHECK(bad([](ChannelConfig& c) { c.samples_per_symbol = 0; }).find("samples_per_symbol") != std::string::npos);
  CHECK(bad([](ChannelConfig& c) { c.delay_samples = -1; }).find("delay_samples") != std::string::npos);
  CHECK(bad([&](ChannelConfig& c) { c.delay_samples = nan; }).find("delay_samples") != std::string::npos);
  CHECK(bad([&](ChannelConfig& c) { c.gain_db = inf; }).find("gain_db") != std::string::npos);
  CHECK(bad([&](ChannelConfig& c) { c.cfo_hz = nan; }).find("cfo_hz") != std::string::npos);
  CHECK(bad([&](ChannelConfig& c) { c.phase_rad = inf; }).find("phase_rad") != std::string::npos);
  CHECK(bad([&](ChannelConfig& c) { c.freq_drift_hz_per_s = nan; }).find("freq_drift") != std::string::npos);
  CHECK(bad([](ChannelConfig& c) { c.phase_walk_rad_per_sqrt_s = -1; }).find("phase_walk") != std::string::npos);
  CHECK(bad([](ChannelConfig& c) { c.clock_offset_ppm = -2e6; }).find("clock_offset_ppm") != std::string::npos);
  CHECK(bad([&](ChannelConfig& c) { c.esn0_db = nan; }).find("esn0_db") != std::string::npos);
  CHECK(bad([](ChannelConfig&) {}).empty());   // and the default is fine
}

TEST(noise_without_a_signal_power_is_an_error) {
  ChannelConfig c;
  c.esn0_db = 10.0;
  const Channel ch(c);
  const auto x = constant(10);
  CHECK_THROWS(ch.apply(x));                           // the default of 0
  CHECK_THROWS(ch.apply(x, -1.0));
  CHECK_THROWS(ch.apply(x, std::nan("")));
  CHECK_THROWS(ch.apply(x, std::numeric_limits<double>::infinity()));
  CHECK_THROWS(ch.apply(x, 0.0));
}

// ---- Theory -----------------------------------------------------------------

TEST(the_q_function_matches_known_values) {
  CHECK_NEAR(q_function(0.0), 0.5, 1e-15);
  CHECK_NEAR(q_function(1.0), 0.158655254, 1e-8);
  CHECK_NEAR(q_function(3.0), 1.34989803e-3, 1e-9);
  CHECK_NEAR(q_function(-1.0), 1.0 - 0.158655254, 1e-8);
}

TEST(qpsk_theory_matches_the_table_in_the_design) {
  CHECK_NEAR(mpsk_ber_theory(4, 4.0), 1.3e-2, 0.1e-2);
  CHECK_NEAR(mpsk_ber_theory(4, 6.0), 2.4e-3, 0.1e-3);
  CHECK_NEAR(mpsk_ber_theory(4, 8.0), 1.9e-4, 0.1e-4);
  CHECK_NEAR(mpsk_ber_theory(4, 10.0), 3.9e-6, 0.1e-6);
  CHECK_NEAR(mpsk_ber_theory(4, 0.0), 0.0786496, 1e-6);       // Q(sqrt 2)
  CHECK_NEAR(mpsk_ber_theory(2, 6.0), mpsk_ber_theory(4, 6.0), 1e-15);   // QPSK = BPSK per bit
}

TEST(eight_psk_theory_matches_the_design) {
  CHECK_NEAR(mpsk_ber_theory(8, 10.0), 1.0e-3, 0.1e-3);
  // "Roughly 3.5 dB more Eb/N0 than QPSK at 1e-5."
  const double gap = mpsk_ebn0_for_ber(8, 1e-5) - mpsk_ebn0_for_ber(4, 1e-5);
  CHECK(gap > 3.2 && gap < 3.8);
}

TEST(theory_falls_with_snr) {
  for (const int order : {2, 4, 8, 16}) {
    double last = 1.0;
    for (double db = 0.0; db <= 14.0; db += 1.0) {
      const double b = mpsk_ber_theory(order, db);
      CHECK(b < last);
      last = b;
    }
  }
}

TEST(the_inverse_of_the_theory_curve_round_trips) {
  for (const int order : {4, 8}) {
    for (const double db : {0.0, 3.0, 7.5, 11.0}) {
      CHECK_NEAR(mpsk_ebn0_for_ber(order, mpsk_ber_theory(order, db)), db, 1e-6);
    }
  }
}

TEST(ebn0_converts_to_esn0_by_the_bits_per_symbol) {
  CHECK_NEAR(ebn0_to_esn0_db(2, 5.0), 5.0, 1e-12);
  CHECK_NEAR(ebn0_to_esn0_db(4, 5.0), 5.0 + 3.0103, 1e-4);
  CHECK_NEAR(ebn0_to_esn0_db(8, 5.0), 5.0 + 4.7712, 1e-4);
}

TEST(theory_rejects_what_it_cannot_describe) {
  for (const int order : {0, 1, 3, 6, -4}) {
    CHECK_THROWS(mpsk_ber_theory(order, 5.0));
    CHECK_THROWS(ebn0_to_esn0_db(order, 5.0));
  }
  CHECK_THROWS(mpsk_ebn0_for_ber(4, 0.0));
  CHECK_THROWS(mpsk_ebn0_for_ber(4, 0.5));
  CHECK_THROWS(mpsk_ebn0_for_ber(4, 0.7));
  CHECK_THROWS(mpsk_ebn0_for_ber(3, 1e-3));
}

MPSK_TEST_MAIN()
