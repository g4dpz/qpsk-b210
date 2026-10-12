// Tests for the loop gains, the carrier loop and the timing loop.
// Requirements 3.3 (carrier), 3.4 (timing), 7.4 (phase), 7.5 (sample-clock offset).

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/carrier_loop.hpp"
#include "dsp/constellation.hpp"
#include "dsp/fir.hpp"
#include "dsp/interpolator.hpp"
#include "dsp/loop_gains.hpp"
#include "dsp/rrc.hpp"
#include "dsp/timing_loop.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cf = std::complex<float>;

namespace {

constexpr double kPi = 3.14159265358979323846;

/**
 * Noise bandwidth Bn*T of the discrete loop, by integrating |H|^2 over frequency, with
 * H = G / (1 + G) and G(z) = Kd z^-1/(1 - z^-1) (Kp + Ki/(1 - z^-1)). Bn*T is the
 * integral of |H|^2 over f in [0, 1/2] in cycles per symbol.
 */
double noise_bandwidth(const LoopGains& g, double detector_gain = 1.0) {
  const int n = 100000;
  double acc = 0.0;
  for (int i = 0; i < n; ++i) {
    const double w = kPi * (i + 0.5) / n;  // 0 .. pi
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> filter = g.kp + g.ki / (1.0 - z1);
    const std::complex<double> open = detector_gain * z1 / (1.0 - z1) * filter;
    acc += std::norm(open / (1.0 + open));
  }
  return acc / n * 0.5;
}

}  // namespace

// ---- Gains ------------------------------------------------------------------

TEST(the_gains_give_the_requested_noise_bandwidth) {
  // The formulas are approximations from a continuous design. Measured ratio of the
  // true noise bandwidth to the target, at damping 0.7071: 1.001 at 0.001, 1.003 at
  // 0.003, 1.009 at 0.01, 1.027 at 0.03, 1.092 at 0.1. The bounds restate that, and
  // the range of validity: do not trust the formulas beyond about 0.1.
  struct Case { double bn_t; double tolerance; };
  for (const Case c : {Case{0.001, 0.005}, Case{0.003, 0.01}, Case{0.01, 0.02}, Case{0.03, 0.05}, Case{0.1, 0.12}}) {
    for (const double zeta : {0.7071, 1.0}) {
      const LoopGains g = second_order_gains(c.bn_t, zeta);
      CHECK_NEAR(noise_bandwidth(g) / c.bn_t, 1.0, c.tolerance);
    }
  }
}

TEST(a_detector_gain_scales_the_gains_so_the_loop_stays_the_same) {
  for (const double kd : {0.26, 1.0, 2.5}) {
    const LoopGains g = second_order_gains(0.01, 0.7071, kd);
    CHECK_NEAR(noise_bandwidth(g, kd), noise_bandwidth(second_order_gains(0.01, 0.7071, 1.0), 1.0), 1e-9);
  }
  const LoopGains unit = second_order_gains(0.01, 0.7071, 1.0);
  const LoopGains half = second_order_gains(0.01, 0.7071, 0.5);
  CHECK_NEAR(half.kp, 2.0 * unit.kp, 1e-12);
  CHECK_NEAR(half.ki, 2.0 * unit.ki, 1e-12);
}

TEST(the_gains_match_the_design_formulas_directly) {
  // theta = BnT / (zeta + 1/(4 zeta)), d = 1 + 2 zeta theta + theta^2, Kp = 4 zeta theta / d, Ki = 4 theta^2 / d.
  const double bn = 0.01, zeta = 0.7071;
  const double theta = bn / (zeta + 1.0 / (4.0 * zeta));
  const double d = 1.0 + 2.0 * zeta * theta + theta * theta;
  const LoopGains g = second_order_gains(bn, zeta);
  CHECK_NEAR(g.kp, 4.0 * zeta * theta / d, 1e-15);
  CHECK_NEAR(g.ki, 4.0 * theta * theta / d, 1e-15);
  CHECK_NEAR(g.kp, 0.0263, 0.0001);   // the values quoted in loop_gains.hpp's discussion
  CHECK_NEAR(g.ki, 0.000351, 0.000001);
}

TEST(a_zero_bandwidth_switches_a_loop_off) {
  const LoopGains g = second_order_gains(0.0, 0.7071);
  CHECK(g.kp == 0.0);
  CHECK(g.ki == 0.0);
}

TEST(bad_loop_arguments_are_rejected) {
  CHECK_THROWS(second_order_gains(-0.01, 0.7));
  CHECK_THROWS(second_order_gains(0.5, 0.7));
  CHECK_THROWS(second_order_gains(std::nan(""), 0.7));
  CHECK_THROWS(second_order_gains(0.01, 0.0));
  CHECK_THROWS(second_order_gains(0.01, -1.0));
  CHECK_THROWS(second_order_gains(0.01, 0.7, 0.0));
  CHECK_THROWS(second_order_gains(0.01, 0.7, -1.0));
}

// ---- Carrier loop -----------------------------------------------------------

namespace {

/** Runs the loop on QPSK points rotated by a true phase (a constant plus a ramp). */
struct CarrierRun {
  double final_phase_error = 0;     // wrapped
  double final_frequency_error = 0;
  double worst_phase_error_late = 0;
};

CarrierRun run_carrier(const CarrierLoopConfig& cfg, double true_phase, double true_freq,
                       double start_phase, double start_freq, int symbols, double noise_variance,
                       std::uint64_t seed) {
  const Constellation qpsk(4);
  mpsk::sim::NoiseSource rng(seed);
  CarrierLoop loop(cfg, start_phase, start_freq);
  CarrierRun r;
  double phase = true_phase;
  for (int k = 0; k < symbols; ++k) {
    const cf sent = qpsk.map(static_cast<unsigned>(rng.uniform() * 4.0) & 3u);
    cf y = sent * std::polar(1.0f, static_cast<float>(phase));
    if (noise_variance > 0.0) y += rng.complex_noise(noise_variance);
    const cf z = loop.derotate(y);
    loop.update(z, qpsk.slice(z).point);
    phase += true_freq;
    if (k >= symbols - 200) {
      r.worst_phase_error_late = std::max(r.worst_phase_error_late, std::fabs(std::remainder(phase - loop.phase(), 2.0 * kPi)));
    }
  }
  r.final_phase_error = std::remainder(phase - loop.phase(), 2.0 * kPi);
  r.final_frequency_error = true_freq - loop.frequency();
  return r;
}

}  // namespace

TEST(the_carrier_loop_locks_to_a_phase_offset) {
  for (const double offset : {0.3, -0.5, 0.7}) {
    const CarrierRun r = run_carrier(CarrierLoopConfig{}, offset, 0.0, 0.0, 0.0, 800, 0.0, 1);
    CHECK(std::fabs(r.final_phase_error) < 0.002);
  }
}

TEST(the_carrier_loop_locks_to_a_frequency_offset_and_reports_it) {
  // A pure frequency error is a phase ramp. A second-order loop follows a ramp with
  // zero steady-state phase error, and its integrator converges to the frequency.
  //
  // The phase it locks to is only defined up to the constellation's symmetry. With
  // data it cannot know, a decision-directed loop settles on any of the four
  // equivalent QPSK rotations: measured, 0.04 rad/symbol locks at exactly -pi/2 after
  // 400 symbols with a frequency error of 1e-6. The receiver removes the ambiguity by
  // starting from the preamble's absolute phase. So the phase is compared modulo pi/2.
  for (const double freq : {0.01, -0.02, 0.04}) {
    const CarrierRun r = run_carrier(CarrierLoopConfig{}, 0.0, freq, 0.0, 0.0, 1500, 0.0, 2);
    const double folded = std::remainder(r.final_phase_error, kPi / 2.0);
    CHECK(std::fabs(folded) < 0.002);
    CHECK(std::fabs(r.final_frequency_error) < 2e-5);
  }
}

TEST(started_from_the_right_phase_the_loop_stays_on_it) {
  // This is how the receiver uses the loop: it starts at the preamble's absolute phase,
  // so the ambiguity above never comes into play and the absolute phase is tracked.
  const CarrierRun r = run_carrier(CarrierLoopConfig{}, 0.3, 0.02, 0.3, 0.018, 1500, 0.0, 12);
  CHECK(std::fabs(r.final_phase_error) < 0.002);
  CHECK(std::fabs(r.final_frequency_error) < 2e-5);
}

TEST(a_narrower_loop_pulls_in_more_slowly_than_a_wider_one) {
  CarrierLoopConfig narrow;
  narrow.bn_t = 0.002;
  CarrierLoopConfig wide;
  wide.bn_t = 0.02;
  // After only 100 symbols, with a modest frequency error, the wide loop is closer.
  const CarrierRun n = run_carrier(narrow, 0.0, 0.01, 0.0, 0.0, 100, 0.0, 4);
  const CarrierRun w = run_carrier(wide, 0.0, 0.01, 0.0, 0.0, 100, 0.0, 4);
  CHECK(std::fabs(w.final_phase_error) < std::fabs(n.final_phase_error));
  CHECK(std::fabs(w.final_frequency_error) < std::fabs(n.final_frequency_error));
}

TEST(phase_jitter_in_noise_follows_bn_t_over_snr) {
  // A decision-directed phase loop at symbol SNR g has phase jitter sqrt(Bn T / g)
  // radians. Measured at Bn T = 0.01 over 60 000 symbols: 0.0100, 0.0179, 0.0254 and
  // 0.0327 rad at Es/N0 of 20, 15, 12 and 10 dB, against 0.0100, 0.0178, 0.0251 and
  // 0.0316 predicted. At 15 dB (g = 31.6) the prediction is 0.0178 rad.
  const double g = std::pow(10.0, 1.5);
  mpsk::sim::NoiseSource rng(6);
  const Constellation qpsk(4);
  CarrierLoop loop(CarrierLoopConfig{}, 0.0, 0.0);
  double sum_sq = 0;
  int count = 0;
  for (int k = 0; k < 20000; ++k) {
    const cf sent = qpsk.map(static_cast<unsigned>(rng.uniform() * 4.0) & 3u);
    const cf y = sent + rng.complex_noise(1.0 / g);
    const cf z = loop.derotate(y);
    loop.update(z, qpsk.slice(z).point);
    if (k > 500) {
      sum_sq += loop.phase() * loop.phase();
      ++count;
    }
  }
  const double jitter = std::sqrt(sum_sq / count);
  const double predicted = std::sqrt(0.01 / g);
  CHECK_NEAR(jitter / predicted, 1.0, 0.1);
}

TEST(the_detector_ignores_amplitude_and_returns_the_phase_error) {
  const Constellation qpsk(4);
  const cf d = qpsk.map(0);
  for (const double err : {-0.6, -0.1, 0.0, 0.2, 0.7}) {
    for (const float amplitude : {0.1f, 1.0f, 8.0f}) {
      CarrierLoop loop(CarrierLoopConfig{}, 0.0, 0.0);
      const double e = loop.update(amplitude * d * std::polar(1.0f, static_cast<float>(err)), d);
      CHECK_NEAR(e, err, 1e-5);
    }
  }
}

TEST(the_detector_is_zero_for_a_zero_sample_and_never_nan) {
  CarrierLoop loop(CarrierLoopConfig{}, 0.0, 0.0);
  const double e = loop.update(cf(0, 0), cf(1, 0));
  CHECK(e == 0.0);
  CHECK(std::isfinite(loop.phase()) && std::isfinite(loop.frequency()));
}

TEST(a_decision_error_beyond_half_the_point_spacing_pulls_the_loop_the_wrong_way) {
  // The reason the loop must start close. At a true phase error of 50 degrees the
  // nearest QPSK point is the neighbour, so the detector reads -40 degrees: it
  // reports a smaller error of the opposite sign, and the loop settles one point away.
  const Constellation qpsk(4);
  const cf sent = qpsk.map(0);
  const double err = 50.0 * kPi / 180.0;
  CarrierLoop loop(CarrierLoopConfig{}, 0.0, 0.0);
  const cf y = sent * std::polar(1.0f, static_cast<float>(err));
  const double e = loop.update(loop.derotate(y), qpsk.slice(y).point);
  CHECK(e < 0.0);
  CHECK_NEAR(e, err - kPi / 2.0, 1e-5);   // -40 degrees
}

TEST(the_loop_can_be_switched_off) {
  CarrierLoopConfig off;
  off.bn_t = 0.0;
  CarrierLoop loop(off, 0.4, 0.01);
  for (int k = 0; k < 50; ++k) loop.update(cf(1, 0), cf(0.7f, 0.7f));
  // No correction from errors, only the starting frequency carrying the phase along.
  CHECK_NEAR(loop.frequency(), 0.01, 1e-15);
  CHECK_NEAR(loop.phase(), std::remainder(0.4 + 50 * 0.01, 2.0 * kPi), 1e-9);
}

TEST(the_phase_stays_bounded_over_a_long_run) {
  CarrierLoop loop(CarrierLoopConfig{}, 0.0, 0.3);   // 0.3 rad per symbol, with no correction needed
  for (int k = 0; k < 100000; ++k) loop.update(cf(1, 0), cf(1, 0));
  CHECK(std::fabs(loop.phase()) <= kPi + 1e-9);
}

TEST(derotation_extrapolates_with_the_frequency_estimate) {
  CarrierLoop loop(CarrierLoopConfig{}, 1.0, 0.2);
  const cf y(1.0f, 0.0f);
  CHECK(std::abs(loop.derotate(y, 0.0) - std::polar(1.0f, -1.0f)) < 1e-6f);
  CHECK(std::abs(loop.derotate(y, 1.0) - std::polar(1.0f, -0.8f)) < 1e-6f);   // phase - 1 * freq
  CHECK(std::abs(loop.derotate(y, 0.5) - std::polar(1.0f, -0.9f)) < 1e-6f);
}

// ---- Gardner detector and timing loop --------------------------------------

namespace {

/** Matched-filter output of random QPSK, and where its first symbol peaks. */
struct Shaped {
  std::vector<cf> y;
  double first_peak;
};

Shaped shaped_qpsk(int symbols, unsigned seed, double rolloff = 0.35, int sps = 4) {
  mpsk::sim::NoiseSource rng(seed);
  std::vector<cf> sym(static_cast<std::size_t>(symbols));
  for (auto& s : sym) {
    s = cf(rng.uniform() < 0.5 ? 0.70710678f : -0.70710678f, rng.uniform() < 0.5 ? 0.70710678f : -0.70710678f);
  }
  const auto taps = rrc_taps(rolloff, sps, 12);
  auto tx = upsample_and_filter(sym, taps, sps);
  tx.resize(tx.size() + taps.size(), cf(0, 0));
  FirFilter mf(taps);
  return {mf.process(tx), static_cast<double>(12 * sps)};
}

double mean_gardner(const Shaped& s, double offset, int sps = 4) {
  double sum = 0;
  const int symbols = static_cast<int>((s.y.size() - 12 * sps) / sps) - 120;
  for (int k = 60; k < symbols; ++k) {
    const double t = s.first_peak + k * sps + offset;
    sum += gardner_error(sinc_interpolate(s.y.data(), s.y.size(), t), sinc_interpolate(s.y.data(), s.y.size(), t - sps),
                         sinc_interpolate(s.y.data(), s.y.size(), t - 0.5 * sps));
  }
  return sum / (symbols - 60);
}

}  // namespace

TEST(the_gardner_error_is_the_documented_formula) {
  // e = Re{ (current - previous) * conj(midpoint) }
  const cf cur(0.9f, 0.2f), prev(-0.8f, 0.1f), mid(0.3f, -0.4f);
  const cf diff = cur - prev;
  const double want = static_cast<double>(diff.real()) * mid.real() + static_cast<double>(diff.imag()) * mid.imag();
  CHECK_NEAR(gardner_error(cur, prev, mid), want, 1e-6);
}

TEST(the_gardner_error_ignores_a_common_carrier_rotation) {
  const cf cur(0.9f, 0.2f), prev(-0.8f, 0.1f), mid(0.3f, -0.4f);
  const cf r = std::polar(1.0f, 1.3f);
  CHECK_NEAR(gardner_error(cur * r, prev * r, mid * r), gardner_error(cur, prev, mid), 1e-5);
}

TEST(the_gardner_s_curve_is_positive_for_late_sampling_and_negative_for_early) {
  // Measured mean error against sampling offset (samples, late positive), unit-power
  // QPSK at 4 samples per symbol: -0.166 at -1, -0.064 at -0.25, 0 at 0, +0.064 at
  // +0.25, +0.166 at +1. The sign is what the loop's negative feedback relies on.
  const Shaped s = shaped_qpsk(6000, 7);
  CHECK_NEAR(mean_gardner(s, 0.0), 0.0, 0.01);
  CHECK_NEAR(mean_gardner(s, 0.25), 0.064, 0.01);
  CHECK_NEAR(mean_gardner(s, -0.25), -0.064, 0.01);
  CHECK_NEAR(mean_gardner(s, 1.0), 0.166, 0.015);
  CHECK_NEAR(mean_gardner(s, -1.0), -0.166, 0.015);
  CHECK(mean_gardner(s, 0.5) > 0.0 && mean_gardner(s, -0.5) < 0.0);
  // And it is odd: the same size either side.
  CHECK_NEAR(mean_gardner(s, 0.5), -mean_gardner(s, -0.5), 0.01);
}

TEST(the_detector_gain_is_independent_of_the_sample_rate_and_depends_on_the_roll_off) {
  // Converged values, averaged over 400 000 symbols, per symbol of timing offset:
  // roll-off 0.2 gives 0.609, 0.35 gives 1.063, 0.5 gives 1.487, 1.0 gives 2.626
  // (QPSK), the same at 4 and 8 samples per symbol. The library's own measurement
  // uses 20000 symbols and is good to about 1%, so the bounds are 4% either side.
  // (An earlier version of these constants came from short single runs and was low
  // by 4 to 7%, see timing_loop.hpp.) This is why the gain is measured for the pulse
  // in use and not written down once.
  const double at4 = TimingLoop::measure_detector_gain(4, 0.35, 11);
  const double at8 = TimingLoop::measure_detector_gain(8, 0.35, 12);
  CHECK_NEAR(at4, 1.063, 0.045);
  CHECK_NEAR(at8, at4, 0.03);
  CHECK_NEAR(TimingLoop::measure_detector_gain(4, 0.2, 12), 0.609, 0.03);
  CHECK_NEAR(TimingLoop::measure_detector_gain(4, 0.5, 12), 1.487, 0.06);
  CHECK_NEAR(TimingLoop::measure_detector_gain(4, 1.0, 12), 2.626, 0.1);
}

TEST(the_detector_gain_is_repeatable) {
  CHECK(TimingLoop::measure_detector_gain(4, 0.35, 11) == TimingLoop::measure_detector_gain(4, 0.35, 11));
}

namespace {

/** Drives a TimingLoop over a shaped signal that is read with an accurate interpolator. */
struct TimingRun {
  double final_position_error = 0;   // samples, loop position minus the true symbol position
  double ppm = 0;
};

TimingRun run_timing(const Shaped& s, const TimingLoopConfig& cfg, double start_offset, int symbols) {
  // The signal here has no clock drift, so the true symbol positions are exactly
  // first_peak + m * sps, and a loop that has locked sits on them. Drift is tested
  // through the whole demodulator, with apply_clock_offset().
  TimingLoop loop(cfg, s.first_peak + 60.0 * cfg.samples_per_symbol + start_offset);
  TimingRun r;
  for (int k = 0; k < symbols; ++k) {
    const double e = gardner_error(sinc_interpolate(s.y.data(), s.y.size(), loop.next_position()),
                                   sinc_interpolate(s.y.data(), s.y.size(), loop.previous_position()),
                                   sinc_interpolate(s.y.data(), s.y.size(), loop.midpoint_position()));
    loop.update(e);
  }
  const double m = 60.0 + symbols;
  r.final_position_error = loop.next_position() - (s.first_peak + m * cfg.samples_per_symbol);
  r.ppm = loop.clock_offset_ppm();
  return r;
}

}  // namespace

TEST(the_timing_loop_pulls_in_a_starting_offset_and_all_starts_converge_together) {
  // The position does not sit still after it settles: the Gardner detector has
  // self-noise from the random data pattern, so it wanders. Measured on a drift-free
  // signal: rms position error 0.0046 sample at Bn T 0.001, 0.014 at 0.003, 0.045 at
  // 0.01, with a learned offset reading of 0.8, 7.6 and 86 ppm rms. The final position
  // error here is one draw from that wander, so the bound is several times the rms.
  //
  // What distinguishes pull-in from failing to pull in is that different starting
  // offsets end up in the same place. They do: from -0.6 to +0.6 sample the runs agree.
  const Shaped s = shaped_qpsk(3000, 11);
  TimingLoopConfig cfg;
  cfg.bn_t = 0.01;
  double lo = 1e9, hi = -1e9;
  for (const double start : {-0.6, -0.3, 0.3, 0.6}) {
    const TimingRun r = run_timing(s, cfg, start, 1500);
    CHECK(std::fabs(r.final_position_error) < 0.2);
    lo = std::min(lo, r.final_position_error);
    hi = std::max(hi, r.final_position_error);
  }
  CHECK(hi - lo < 0.02);   // the starting offset has been forgotten
}

TEST(the_bare_timing_loop_pulls_in_to_half_a_symbol_and_slips_a_whole_symbol_beyond) {
  // Measured on noiseless drift-free QPSK at 4 samples per symbol, Bn T = 0.01: every
  // starting offset from -1.5 to +2.0 samples converges to the same place, and beyond
  // that (+2.5, -2.0 and further) the loop locks one whole symbol away, a final error
  // of +4.0 or -3.9 samples. That is a symbol slip, the timing equivalent of a carrier
  // cycle slip. It is much wider than the 1.0 sample the whole demodulator tolerates
  // (the carrier loop runs alongside it there, on a noisy signal), and far wider than
  // the preamble's own timing error, which is under 0.3 sample even at 0 dB.
  const Shaped s = shaped_qpsk(3000, 11);
  TimingLoopConfig cfg;
  cfg.bn_t = 0.01;

  for (const double inside : {1.0, 1.5, -1.0}) {
    const TimingRun r = run_timing(s, cfg, inside, 1500);
    CHECK(std::fabs(r.final_position_error) < 0.2);
  }
  for (const double outside : {2.5, 3.0, -2.5, -3.0}) {
    const TimingRun r = run_timing(s, cfg, outside, 1500);
    CHECK(std::fabs(std::fabs(r.final_position_error) - 4.0) < 0.3);   // one symbol away
  }
}

TEST(the_learned_clock_offset_reading_is_noisy_on_a_drift_free_signal) {
  // Recorded so nobody reads more into the number than it holds. On a signal with no
  // drift the reading is zero on average but wanders by about 8 ppm rms at the default
  // bandwidth (0.8 at 0.001, 86 at 0.01).
  const Shaped s = shaped_qpsk(4200, 21);
  TimingLoopConfig cfg;
  cfg.bn_t = 0.003;
  cfg.detector_gain = 1.06;
  TimingLoop loop(cfg, s.first_peak + 50.0 * 4);
  double sum_sq = 0, sum = 0;
  int count = 0;
  for (int k = 0; k < 3500; ++k) {
    const double e = gardner_error(sinc_interpolate(s.y.data(), s.y.size(), loop.next_position()),
                                   sinc_interpolate(s.y.data(), s.y.size(), loop.previous_position()),
                                   sinc_interpolate(s.y.data(), s.y.size(), loop.midpoint_position()));
    loop.update(e);
    if (k >= 1500) {
      sum += loop.clock_offset_ppm();
      sum_sq += loop.clock_offset_ppm() * loop.clock_offset_ppm();
      ++count;
    }
  }
  CHECK(std::sqrt(sum_sq / count) < 40.0);   // generous: one draw of a quantity with rms ~8
  CHECK(std::fabs(sum / count) < 30.0);
}

TEST(the_timing_loop_with_zero_bandwidth_does_not_move) {
  const Shaped s = shaped_qpsk(1000, 12);
  TimingLoopConfig cfg;
  cfg.bn_t = 0.0;
  const TimingRun r = run_timing(s, cfg, 0.4, 500);
  CHECK_NEAR(r.final_position_error, 0.4, 1e-9);   // exactly the starting error, unchanged
  CHECK(r.ppm == 0.0);
}

TEST(the_timing_loop_steps_one_symbol_at_a_time_and_has_consistent_positions) {
  TimingLoopConfig cfg;
  cfg.bn_t = 0.0;
  TimingLoop loop(cfg, 100.0);
  CHECK_NEAR(loop.next_position(), 100.0, 1e-12);
  CHECK_NEAR(loop.previous_position(), 96.0, 1e-12);
  CHECK_NEAR(loop.midpoint_position(), 98.0, 1e-12);
  CHECK_NEAR(loop.symbol_period(), 4.0, 1e-12);
  loop.update(0.0);
  CHECK_NEAR(loop.next_position(), 104.0, 1e-12);
  CHECK_NEAR(loop.previous_position(), 100.0, 1e-12);
}

TEST(a_late_error_moves_the_next_sample_earlier) {
  TimingLoopConfig cfg;
  cfg.bn_t = 0.01;
  TimingLoop loop(cfg, 100.0);
  loop.update(+0.5);   // positive: sampling was late
  CHECK(loop.next_position() < 104.0);
  TimingLoop early(cfg, 100.0);
  early.update(-0.5);
  CHECK(early.next_position() > 104.0);
}

TEST(a_persistent_error_is_learned_as_a_clock_offset_with_the_documented_sign) {
  // A steady late error means the symbols are arriving further apart than sps: the
  // loop should stretch its symbol period, which is a positive ppm.
  TimingLoopConfig cfg;
  cfg.bn_t = 0.01;
  TimingLoop loop(cfg, 100.0);
  for (int k = 0; k < 200; ++k) loop.update(-0.05);   // early error: pulls the position later
  // An early error stretches the period, so the learned offset is positive.
  CHECK(loop.clock_offset_ppm() > 0.0);
  CHECK(loop.symbol_period() > 4.0);

  TimingLoop other(cfg, 100.0);
  for (int k = 0; k < 200; ++k) other.update(+0.05);
  CHECK(other.clock_offset_ppm() < 0.0);
  CHECK(other.symbol_period() < 4.0);
}

TEST(a_supplied_detector_gain_is_used_instead_of_measuring) {
  TimingLoopConfig cfg;
  cfg.detector_gain = 2.0;
  const TimingLoop loop(cfg, 0.0);
  CHECK(loop.detector_gain() == 2.0);
  const LoopGains want = second_order_gains(cfg.bn_t, cfg.damping, 2.0);
  CHECK_NEAR(loop.gains().kp, want.kp, 1e-15);
  CHECK_NEAR(loop.gains().ki, want.ki, 1e-15);
}

TEST(timing_loop_arguments_are_checked) {
  TimingLoopConfig cfg;
  cfg.samples_per_symbol = 1;
  CHECK_THROWS(TimingLoop(cfg, 0.0));
  cfg = TimingLoopConfig{};
  cfg.bn_t = 0.6;
  CHECK_THROWS(TimingLoop(cfg, 0.0));
  cfg = TimingLoopConfig{};
  cfg.damping = 0.0;
  CHECK_THROWS(TimingLoop(cfg, 0.0));
}

MPSK_TEST_MAIN()
