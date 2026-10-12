// Tests for preamble detection and measurement.
// Requirements 3.3 (carrier), 3.4 (timing), 3.5 (frame start), 3.6 (phase), 7.3/7.4/9.4
// (frequency offset, phase, range), 9.3 (unknown level), 9.6 (no false frames).
//
// Tolerances here come from measurement. Where a figure appears in a comment it
// was observed on the code as written; the theory it is compared with is given in
// preamble_detector.hpp.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "dsp/modulator.hpp"
#include "dsp/preamble_detector.hpp"
#include "dsp/prbs.hpp"
#include "dsp/rx_frontend.hpp"
#include "rx_scenario.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using namespace rx_test;

namespace {

/** P(D > t) for noise alone: the Beta(4, 60) tail, sum_{j=0}^{3} C(63, j) t^j (1-t)^(63-j). */
double noise_tail(double t) {
  double p = 0.0;
  double c = 1.0;  // C(63, 0)
  for (int j = 0; j < 4; ++j) {
    if (j > 0) c = c * static_cast<double>(63 - j + 1) / static_cast<double>(j);
    p += c * std::pow(t, j) * std::pow(1.0 - t, 63 - j);
  }
  return p;
}

AcquisitionConfig default_config() { return AcquisitionConfig{}; }

struct Outcome {
  bool found = false;
  PreambleEstimate est;
  double position_error = 0.0;
  double cfo_error_hz = 0.0;
  double phase_error = 0.0;
  double amplitude_error_db = 0.0;
};

Outcome measure(const Scenario& s) {
  const Received r = make_received(s);
  Outcome o;
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, default_config());
  if (!e) return o;
  o.found = true;
  o.est = *e;
  o.position_error = e->position - r.true_position;
  o.cfo_error_hz = e->cfo_hz(kSymbolRate) - r.true_cfo_hz;
  o.phase_error = wrap_phase(e->phase_centre - r.true_phase_centre);
  o.amplitude_error_db = 20.0 * std::log10(e->amplitude / r.true_amplitude);
  return o;
}

}  // namespace

// ---- Constants and small functions -----------------------------------------

TEST(the_geometry_constants_are_what_the_design_states) {
  CHECK(kPreambleSegments == 4);
  CHECK_NEAR(kMaxCfoCyclesPerSymbol, 1.0 / 32.0, 1e-15);
  CHECK_NEAR(kPreambleCentre, 31.5, 1e-15);
  CHECK(preamble_span_samples(4) == 253);               // 63 * 4 + 1
  CHECK(acquisition_margin(4) == 12);                   // 4/2 + 1 + 8 + 1
  // The documented limit: Rs/32 = 7812.5 Hz at 250 ksym/s.
  CHECK_NEAR(kMaxCfoCyclesPerSymbol * kSymbolRate, 7812.5, 1e-9);
}

TEST(phase_extrapolation_and_frequency_conversion) {
  PreambleEstimate e;
  e.omega = 0.02;
  e.phase_centre = 1.0;
  CHECK_NEAR(e.phase_at(31.5), 1.0, 1e-15);
  CHECK_NEAR(e.phase_at(0.0), 1.0 - 0.02 * 31.5, 1e-12);
  CHECK_NEAR(e.phase_at(63.0), 1.0 + 0.02 * 31.5, 1e-12);
  // omega is radians per symbol; one cycle per symbol is the symbol rate.
  e.omega = 2.0 * kPi;
  CHECK_NEAR(e.cfo_hz(250e3), 250e3, 1e-6);
  e.omega = -kPi / 16.0;
  CHECK_NEAR(e.cfo_hz(250e3), -7812.5, 1e-6);
}

// ---- The stage-1 statistic --------------------------------------------------

TEST(the_statistic_is_one_on_a_clean_preamble_and_ignores_level_and_phase) {
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s);
  const std::size_t t = static_cast<std::size_t>(std::llround(r.true_position));
  const double d0 = coarse_metric(r.y.data(), r.y.size(), t, kSps);
  CHECK(d0 > 0.95);   // measured 1.00 at the true position (a fractional delay lowers it slightly)

  // Level and phase must not matter. Measured here to well under 1%.
  for (const double gain : {1.0, 0.01, 7.0}) {
    for (const double phase : {0.0, 1.0, -2.5}) {
      Scenario v = s;
      v.gain = gain;
      v.phase_rad = phase;
      const Received rv = make_received(v);
      const double d = coarse_metric(rv.y.data(), rv.y.size(), t, kSps);
      CHECK_NEAR(d, d0, 0.01);
    }
  }
}

TEST(the_statistic_on_white_noise_follows_the_beta_law) {
  // D = |projection of a random unit vector in C^64 onto a 4-dimensional subspace|^2,
  // so mean 4/64, standard deviation sqrt(4 * 60 / (64^2 * 65)) = 0.0300.
  // Measured: mean 0.0625, sd 0.0300, over 1.5e6 positions.
  mpsk::sim::NoiseSource rng(21);
  const std::size_t n = 600000;
  std::vector<cf> y(n);
  for (auto& v : y) v = rng.complex_noise(1.0);

  double sum = 0, sum2 = 0;
  std::size_t count = 0;
  std::size_t over[3] = {0, 0, 0};
  const double thresholds[3] = {0.15, 0.20, 0.25};
  for (std::size_t t = 0; t + preamble_span_samples(kSps) <= n; ++t) {
    const double d = coarse_metric(y.data(), n, t, kSps);
    sum += d;
    sum2 += d * d;
    ++count;
    for (int k = 0; k < 3; ++k) over[k] += (d > thresholds[k]) ? 1 : 0;
  }
  const double mean = sum / static_cast<double>(count);
  const double sd = std::sqrt(sum2 / static_cast<double>(count) - mean * mean);
  CHECK_NEAR(mean, 0.0625, 0.0015);
  CHECK_NEAR(sd, 0.0300, 0.001);

  // Tail probabilities against the exact law. The counts are Poisson with the
  // expected value below; allow five standard deviations plus one.
  for (int k = 0; k < 3; ++k) {
    const double expected = noise_tail(thresholds[k]) * static_cast<double>(count);
    CHECK(expected > 10.0);  // the comparison needs enough events to mean something
    CHECK_NEAR(static_cast<double>(over[k]), expected, 5.0 * std::sqrt(expected) + 1.0);
  }
}

TEST(the_statistic_is_zero_on_digital_silence_and_never_nan) {
  const std::vector<cf> silence(1000, cf(0, 0));
  CHECK(coarse_metric(silence.data(), silence.size(), 0, kSps) == 0.0);
  CHECK(!acquire_preamble(silence.data(), silence.size(), 0, default_config()).has_value());
}

TEST(coarse_metric_rejects_bad_arguments) {
  const std::vector<cf> y(1000, cf(1, 0));
  CHECK_THROWS(coarse_metric(y.data(), y.size(), 0, 1));
  CHECK_THROWS(coarse_metric(y.data(), y.size(), 800, kSps));  // 800 + 253 > 1000
  CHECK(coarse_metric(y.data(), y.size(), 747, kSps) >= 0.0);  // 747 + 253 = 1000 fits
}

// ---- Measuring a clean preamble --------------------------------------------

TEST(a_clean_preamble_is_measured_almost_exactly) {
  Scenario s;
  s.add_noise = false;
  s.cfo_hz = 1234.0;
  s.phase_rad = 0.7;
  s.delay = 0.43;
  const Outcome o = measure(s);
  REQUIRE(o.found);
  CHECK(std::fabs(o.position_error) < 0.05);       // samples
  CHECK(std::fabs(o.cfo_error_hz) < 15.0);
  CHECK(std::fabs(o.phase_error) < 0.02);
  CHECK(std::fabs(o.amplitude_error_db) < 0.1);
  CHECK(o.est.verify_metric > 0.99);
}

TEST(timing_is_accurate_at_every_fractional_offset) {
  // The parabolic estimate could be biased towards the grid. Sweep the delay across
  // a whole sample, noiseless, and look at the worst error and any trend.
  double worst = 0.0;
  for (int k = 0; k < 20; ++k) {
    Scenario s;
    s.add_noise = false;
    s.delay = static_cast<double>(k) / 20.0;
    s.phase_rad = 0.3;
    const Outcome o = measure(s);
    REQUIRE(o.found);
    worst = std::max(worst, std::fabs(o.position_error));
  }
  CHECK(worst < 0.06);
}

TEST(estimates_are_unbiased_by_a_whole_range_of_phases) {
  for (int k = 0; k < 12; ++k) {
    Scenario s;
    s.add_noise = false;
    s.phase_rad = -kPi + 2.0 * kPi * (k + 0.5) / 12.0;
    s.cfo_hz = 500.0;
    const Outcome o = measure(s);
    REQUIRE(o.found);
    CHECK(std::fabs(o.phase_error) < 0.02);
  }
}

// ---- Accuracy against noise -------------------------------------------------

TEST(estimation_error_at_ten_db_is_small) {
  // Measured at Es/N0 = 10 dB over 150 trials: position rms 0.062 samples, frequency
  // rms 62 Hz, phase rms 0.027 rad. Bounds are about 1.6x those.
  double sp = 0, sc = 0, sph = 0;
  const int trials = 80;
  int found = 0;
  for (int i = 0; i < trials; ++i) {
    Scenario s;
    s.esn0_db = 10.0;
    s.seed = 1000 + static_cast<std::uint64_t>(i);
    s.delay = (i % 10) / 10.0;
    s.phase_rad = -3.0 + 0.077 * i;
    s.cfo_hz = (i % 2 ? 1.0 : -1.0) * 100.0 * (i % 25);
    const Outcome o = measure(s);
    if (!o.found) continue;
    ++found;
    sp += o.position_error * o.position_error;
    sc += o.cfo_error_hz * o.cfo_error_hz;
    sph += o.phase_error * o.phase_error;
  }
  CHECK(found == trials);
  CHECK(std::sqrt(sp / found) < 0.10);
  CHECK(std::sqrt(sc / found) < 100.0);
  CHECK(std::sqrt(sph / found) < 0.045);
}

TEST(the_verification_statistic_matches_its_theory) {
  // For a preamble at symbol SNR g, V is about (64 + 1/g) / (64 (1 + 1/g)). Measured
  // at the exact position, over 80 seeds: 0.913 at 10 dB (theory 0.912), 0.768 at
  // 5 dB (theory 0.764). This ties the code to the derivation and checks the noise
  // level of the whole simulated path at the same time.
  for (const double esn0 : {10.0, 5.0}) {
    const double g = std::pow(10.0, esn0 / 10.0);
    const double theory = (64.0 + 1.0 / g) / (64.0 * (1.0 + 1.0 / g));
    double sum = 0;
    const int trials = 80;
    for (int i = 0; i < trials; ++i) {
      Scenario s;
      s.esn0_db = esn0;
      s.seed = 5000 + static_cast<std::uint64_t>(i);
      s.phase_rad = 0.1 * i;
      const Outcome o = measure(s);
      REQUIRE(o.found);
      sum += o.est.verify_metric;
    }
    CHECK_NEAR(sum / trials, theory, 0.03);
  }
}

TEST(detection_is_reliable_down_to_zero_db) {
  // Measured: 150 of 150 found at Es/N0 = 0 dB, the smallest V being 0.35 against
  // the 0.30 threshold. The 64 symbols give about 18 dB of processing gain.
  int found_3db = 0, found_0db = 0;
  const int trials = 60;
  for (int i = 0; i < trials; ++i) {
    Scenario s;
    s.seed = 9000 + static_cast<std::uint64_t>(i);
    s.delay = (i % 7) / 7.0;
    s.phase_rad = 0.2 * i;
    s.esn0_db = 3.0;
    found_3db += measure(s).found ? 1 : 0;
    s.esn0_db = 0.0;
    found_0db += measure(s).found ? 1 : 0;
  }
  CHECK(found_3db == trials);
  CHECK(found_0db >= trials - 2);
}

// ---- Frequency range --------------------------------------------------------

TEST(frequency_offsets_across_the_acquisition_range_are_measured) {
  // Measured at Es/N0 = 15 dB: all found with frequency error under 100 Hz out to
  // 7500 Hz, which is 96% of the theoretical limit Rs/32 = 7812.5 Hz.
  for (const double f : {0.0, 1000.0, 3000.0, 5000.0, 6500.0, 7500.0}) {
    for (const double sign : {1.0, -1.0}) {
      for (int rep = 0; rep < 8; ++rep) {
        Scenario s;
        s.esn0_db = 15.0;
        s.cfo_hz = sign * f;
        s.seed = 300 + static_cast<std::uint64_t>(rep);
        s.delay = rep / 8.0;
        s.phase_rad = 0.5 * rep;
        const Outcome o = measure(s);
        CHECK(o.found);
        if (!o.found) continue;
        CHECK(std::fabs(o.cfo_error_hz) < 200.0);
        CHECK(std::fabs(o.position_error) < 0.3);
      }
    }
  }
}

TEST(offsets_well_beyond_the_range_are_not_accepted) {
  // Measured: 0 of 200 found at 9000 Hz and above (115% of the limit), and 1 of 200
  // at 8200 Hz. Failing to detect is the right way to fail: nothing wrong is reported.
  int found = 0;
  for (const double f : {10000.0, 12000.0, -10000.0, -15000.0}) {
    for (int rep = 0; rep < 15; ++rep) {
      Scenario s;
      s.esn0_db = 15.0;
      s.cfo_hz = f;
      s.seed = 700 + static_cast<std::uint64_t>(rep);
      s.delay = rep / 15.0;
      found += measure(s).found ? 1 : 0;
    }
  }
  CHECK(found == 0);
}

// ---- Level and DC -----------------------------------------------------------

TEST(detection_works_across_a_wide_level_range_with_a_dc_offset) {
  // Measured: 100 of 100 found at 0, -20 and -40 dB with a DC offset, amplitude
  // within 0.05 dB. The statistics are normalised, so no gain control is needed.
  for (const double db : {20.0, 0.0, -20.0, -40.0, -60.0}) {
    Scenario s;
    s.esn0_db = 15.0;
    s.gain = std::pow(10.0, db / 20.0);
    s.dc = cf(0.1f, -0.1f);
    s.cfo_hz = 1500.0;
    s.seed = 11;
    const Outcome o = measure(s);
    CHECK(o.found);
    if (!o.found) continue;
    CHECK(std::fabs(o.amplitude_error_db) < 0.3);
    CHECK(std::fabs(o.phase_error) < 0.08);
    CHECK(std::fabs(o.position_error) < 0.3);
  }
}

TEST(amplitude_scales_linearly_with_the_received_level) {
  Scenario a;
  a.add_noise = false;
  Scenario b = a;
  b.gain = 0.1;
  const Outcome oa = measure(a);
  const Outcome ob = measure(b);
  REQUIRE(oa.found && ob.found);
  CHECK_NEAR(oa.est.amplitude / ob.est.amplitude, 10.0, 0.05);
  CHECK_NEAR(oa.est.amplitude, 0.5, 0.01);  // output scale 0.5 times gain 1
}

TEST(a_dc_offset_does_not_bias_the_estimates) {
  Scenario clean;
  clean.esn0_db = 20.0;
  clean.cfo_hz = 800.0;
  clean.phase_rad = 1.0;
  Scenario offset = clean;
  offset.dc = cf(0.3f, 0.2f);
  const Outcome a = measure(clean);
  const Outcome b = measure(offset);
  REQUIRE(a.found && b.found);
  CHECK(std::fabs(a.est.phase_centre - b.est.phase_centre) < 0.05);
  CHECK(std::fabs(a.est.position - b.est.position) < 0.1);
  CHECK(std::fabs(a.est.omega - b.est.omega) < 0.005);
}

// ---- False alarms -----------------------------------------------------------

TEST(noise_alone_never_produces_a_detection) {
  // Measured over 1.5e6 samples of noise through the real front end: 33 stage-1
  // crossings (theory 2.3e-5 per position gives 34), all refused by stage 2, with
  // the largest V among them 0.251 against the 0.30 threshold.
  mpsk::sim::NoiseSource rng(31);
  const std::size_t n = 1500000;
  std::vector<cf> r(n);
  for (auto& v : r) v = rng.complex_noise(1.0);
  RxFrontEnd fe{RxFrontEndConfig{}};
  const auto y = fe.process(r);

  AcquisitionStats stats;
  const auto e = acquire_preamble(y.data(), y.size(), 0, default_config(), &stats);
  CHECK(!e.has_value());
  CHECK(stats.positions_scanned > 1000000);
  CHECK(stats.candidates > 5);   // the test means something only if stage 1 did fire
  CHECK(stats.rejected == stats.candidates);
  CHECK(stats.max_rejected_verify < default_config().verify_threshold);
}

TEST(a_false_trigger_before_a_real_preamble_does_not_hide_it) {
  // 200 000 samples of noise ahead of the burst: stage 1 fires several times there,
  // and the scan has to step past each and still find the real one.
  Scenario s;
  s.esn0_db = 8.0;
  s.lead = 200000;
  s.seed = 41;
  const Received r = make_received(s);
  AcquisitionStats stats;
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, default_config(), &stats);
  REQUIRE(e.has_value());
  CHECK(std::fabs(e->position - r.true_position) < 0.4);
  CHECK(stats.candidates >= 1);
  CHECK(stats.rejected == stats.candidates - 1);   // every crossing but the real one
}

TEST(the_first_crossing_is_at_most_about_two_and_a_half_samples_before_the_peak) {
  // The scan looks for the stage-1 peak over the two symbols after the first
  // crossing, and stage 2 then searches +-sps/2 around it. Both rest on how early a
  // crossing can be. Measured against the true position (not a rounded index, which
  // reads half a sample high at a half-sample delay): never more than 2.5 samples,
  // the worst case being a half-sample delay, at offsets out to 7700 Hz where the
  // peak is only 0.40.
  //
  // Stage 2 run from the crossing alone recovers the position in every case measured
  // (100 of 100 at a half-sample delay for Es/N0 of 20, 10 and 5 dB). So the peak
  // search is a safety margin that these scenarios do not need, and shrinking it
  // cannot be detected here. That is recorded as a known survivor of the mutation
  // check rather than hidden behind a contrived test.
  for (const double f : {0.0, 4000.0, 7000.0, 7500.0}) {
    for (int i = 0; i < 20; ++i) {
      Scenario s;
      s.esn0_db = 15.0;
      s.cfo_hz = (i % 2 ? f : -f);
      s.seed = 400 + static_cast<std::uint64_t>(i);
      s.delay = (i % 10) / 10.0;
      const Received r = make_received(s);
      const long start = std::lround(r.true_position) - 12;
      long cross = -1;
      for (long t = start; t <= start + 24 && cross < 0; ++t) {
        if (coarse_metric(r.y.data(), r.y.size(), static_cast<std::size_t>(t), kSps) >= default_config().coarse_threshold) cross = t;
      }
      REQUIRE(cross >= 0);
      CHECK(r.true_position - static_cast<double>(cross) < 3.0);

      const auto e = estimate_preamble(r.y.data(), r.y.size(), static_cast<std::size_t>(cross), default_config());
      REQUIRE(e.has_value());
      CHECK(std::fabs(e->position - r.true_position) < 0.7);
    }
  }
}

TEST(a_noise_bump_is_one_candidate_not_several) {
  // After refusing a candidate the scan must step past the lobe that produced it. A
  // noise bump above the threshold spans about 5 positions; stepping by one sample
  // would re-enter it and count the same bump again and again. Here every candidate
  // is a separate bump, so the number of stage-1 crossings found by an independent
  // scan, grouped into lobes, must equal the scan's own count.
  mpsk::sim::NoiseSource rng(77);
  const std::size_t n = 1500000;
  std::vector<cf> raw(n);
  for (auto& v : raw) v = rng.complex_noise(1.0);
  RxFrontEnd fe{RxFrontEndConfig{}};
  const auto y = fe.process(raw);

  AcquisitionStats stats;
  CHECK(!acquire_preamble(y.data(), y.size(), 0, default_config(), &stats).has_value());

  // Independent count: positions above the threshold, grouped so that crossings less
  // than a symbol apart form one lobe.
  const std::size_t last = y.size() - preamble_span_samples(kSps) - acquisition_margin(kSps);
  std::size_t lobes = 0;
  long previous = -1000;
  for (std::size_t t = acquisition_margin(kSps); t <= last; ++t) {
    if (coarse_metric(y.data(), y.size(), t, kSps) >= default_config().coarse_threshold) {
      if (static_cast<long>(t) - previous > kSps) ++lobes;
      previous = static_cast<long>(t);
    }
  }
  // Measured on this record: 28 lobes, 28 candidates. Stepping one sample instead of
  // past the lobe gives 37, which is the raw number of positions above the threshold.
  // A lobe split by the scan's own peak search could add one, so allow one either way.
  CHECK(lobes > 5);
  CHECK(stats.candidates + 1 >= lobes);
  CHECK(stats.candidates <= lobes + 1);
}

TEST(a_frame_arriving_right_after_a_reset_with_a_dc_offset_is_acquired_cleanly) {
  // The case the DC blocker's start-up design is for, seen from the detector. The
  // record starts with only a few hundred samples of lead, so the frame arrives
  // within a millisecond of the start, while the blocker is still settling. A
  // zero-state start would carry 90% of the offset across the preamble.
  Scenario clean;
  clean.esn0_db = 20.0;
  clean.lead = 300;
  clean.cfo_hz = 600.0;
  clean.phase_rad = 0.9;
  Scenario offset = clean;
  offset.dc = cf(0.25f, -0.2f);   // about as large as the signal amplitude 0.5
  const Outcome a = measure(clean);
  const Outcome b = measure(offset);
  REQUIRE(a.found && b.found);
  CHECK(std::fabs(b.position_error) < 0.15);
  CHECK(std::fabs(b.phase_error) < 0.06);
  CHECK(std::fabs(b.cfo_error_hz) < 80.0);
  CHECK(std::fabs(b.amplitude_error_db) < 0.4);
  CHECK(b.est.verify_metric > 0.95);
}

// ---- Scanning ---------------------------------------------------------------

TEST(scan_start_decides_which_preamble_is_found) {
  Scenario s;
  s.esn0_db = 20.0;
  const Received r = make_received(s);
  const auto from_zero = acquire_preamble(r.y.data(), r.y.size(), 0, default_config());
  REQUIRE(from_zero.has_value());

  // The statistic stays above the 0.25 threshold from 2 samples before the peak to 2
  // after it (measured D: 0.48, 0.88, 1.00, 0.88, 0.49 at offsets -2 to +2), so a scan
  // that starts up to 2 samples late still catches the falling edge and finds the
  // preamble. From 3 samples late (D = 0.13) the preamble is behind it.
  const std::size_t peak = static_cast<std::size_t>(std::llround(from_zero->position));
  CHECK(acquire_preamble(r.y.data(), r.y.size(), peak + 2, default_config()).has_value());
  CHECK(!acquire_preamble(r.y.data(), r.y.size(), peak + 3, default_config()).has_value());
  CHECK(!acquire_preamble(r.y.data(), r.y.size(), peak + 40, default_config()).has_value());
  const std::size_t before = static_cast<std::size_t>(from_zero->position) - 20;
  const auto found = acquire_preamble(r.y.data(), r.y.size(), before, default_config());
  REQUIRE(found.has_value());
  CHECK_NEAR(found->position, from_zero->position, 1e-9);
}

TEST(two_preambles_in_one_record_are_found_one_after_the_other) {
  const std::size_t gap = 3000;
  mpsk::dsp::Modulator& mod = modulator();
  const auto a = mod.modulate(FrameType::Data, 1, prbs_bytes(3, 1, 16));
  const auto b = mod.modulate(FrameType::Data, 2, prbs_bytes(3, 2, 16));

  std::vector<cf> x(500, cf(0, 0));
  x.insert(x.end(), a.samples.begin(), a.samples.end());
  x.insert(x.end(), gap, cf(0, 0));
  const std::size_t second_start = x.size();
  x.insert(x.end(), b.samples.begin(), b.samples.end());
  x.insert(x.end(), 500, cf(0, 0));

  mpsk::sim::NoiseSource rng(51);
  mpsk::sim::add_noise(x, mpsk::sim::noise_variance_for_esn0(0.0625, kSps, 12.0), rng);
  RxFrontEnd fe{RxFrontEndConfig{}};
  const auto y = fe.process(x);

  const auto first = acquire_preamble(y.data(), y.size(), 0, default_config());
  REQUIRE(first.has_value());
  CHECK_NEAR(first->position, 500.0 + 86.0 + fe.group_delay(), 0.4);

  const auto second = acquire_preamble(y.data(), y.size(),
                                       static_cast<std::size_t>(first->position) + 100, default_config());
  REQUIRE(second.has_value());
  CHECK_NEAR(second->position, static_cast<double>(second_start) + 86.0 + fe.group_delay(), 0.4);
  CHECK(second->position > first->position + static_cast<double>(a.samples.size()) - 200.0);
}

// ---- Edges and arguments ----------------------------------------------------

TEST(a_preamble_too_close_to_the_end_of_the_buffer_is_not_reported) {
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s);
  const std::size_t true_pos = static_cast<std::size_t>(std::llround(r.true_position));

  // Cut the record so that less than acquisition_margin() follows the preamble.
  const std::size_t span = preamble_span_samples(kSps);
  const std::size_t margin = acquisition_margin(kSps);
  const std::size_t too_short = true_pos + span + margin - 3;
  CHECK(!acquire_preamble(r.y.data(), too_short, 0, default_config()).has_value());
  CHECK(!estimate_preamble(r.y.data(), too_short, true_pos, default_config()).has_value());

  // With exactly enough, it is found.
  const std::size_t enough = true_pos + span + margin;
  CHECK(estimate_preamble(r.y.data(), enough, true_pos, default_config()).has_value());

  // And too close to the start.
  CHECK(!estimate_preamble(r.y.data(), r.y.size(), margin - 1, default_config()).has_value());
}

TEST(a_buffer_shorter_than_a_preamble_gives_nothing) {
  const std::vector<cf> y(100, cf(1, 1));
  CHECK(!acquire_preamble(y.data(), y.size(), 0, default_config()).has_value());
  CHECK(!acquire_preamble(y.data(), 0, 0, default_config()).has_value());
}

TEST(bad_configurations_are_rejected) {
  const std::vector<cf> y(2000, cf(1, 0));
  AcquisitionConfig c;
  c.samples_per_symbol = 1;
  CHECK_THROWS(acquire_preamble(y.data(), y.size(), 0, c));
  CHECK_THROWS(estimate_preamble(y.data(), y.size(), 100, c));
  c = AcquisitionConfig{};
  c.coarse_threshold = 0.0;
  CHECK_THROWS(acquire_preamble(y.data(), y.size(), 0, c));
  c.coarse_threshold = 1.5;
  CHECK_THROWS(acquire_preamble(y.data(), y.size(), 0, c));
  c = AcquisitionConfig{};
  c.verify_threshold = -0.1;
  CHECK_THROWS(acquire_preamble(y.data(), y.size(), 0, c));
  c.verify_threshold = std::nan("");
  CHECK_THROWS(acquire_preamble(y.data(), y.size(), 0, c));
}

TEST(a_stricter_verify_threshold_refuses_a_weak_preamble) {
  Scenario s;
  s.esn0_db = 1.0;
  s.seed = 61;
  const Received r = make_received(s);
  AcquisitionConfig lax;
  lax.verify_threshold = 0.25;
  AcquisitionConfig strict;
  strict.verify_threshold = 0.95;
  CHECK(acquire_preamble(r.y.data(), r.y.size(), 0, lax).has_value());
  CHECK(!acquire_preamble(r.y.data(), r.y.size(), 0, strict).has_value());
}

MPSK_TEST_MAIN()
