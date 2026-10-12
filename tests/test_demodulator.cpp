// Tests for the demodulator. Requirements 3.3, 3.4, 3.7 (carrier, timing, demapping),
// 4.1, 4.2, 4.3 (statistics, BER, a failed header is a lost frame), 7.4, 7.5.
//
// Tolerances come from measurement on this code (see the comments), run through the
// whole chain: modulator, channel operations, front end, acquisition, demodulator.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

#include "dsp/demodulator.hpp"
#include "dsp/framer.hpp"
#include "dsp/metrics.hpp"
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

constexpr std::uint32_t kSeed = 3;   // the PRBS seed rx_scenario.hpp uses

struct Decoded {
  bool found = false;
  PreambleEstimate est;
  DemodResult result;
  BerResult ber;
};

/** Runs acquisition then the demodulator on a scenario, and scores the payload against the PRBS. */
Decoded decode(const Scenario& s, std::uint16_t counter = 1, std::size_t payload_bytes = 128,
               const DemodConfig& dc = DemodConfig{}) {
  Decoded d;
  const Received r = make_received(s, counter, payload_bytes);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  if (!e) return d;
  d.found = true;
  d.est = *e;
  Demodulator demod(dc);
  d.result = demod.demodulate(r.y.data(), r.y.size(), *e);
  d.ber = payload_ber(d.result.frame.payload, prbs_bytes(kSeed, counter, payload_bytes));
  return d;
}

}  // namespace

// ---- A clean frame ----------------------------------------------------------

TEST(a_clean_frame_decodes_with_zero_errors) {
  Scenario s;
  s.add_noise = false;
  const Decoded d = decode(s);
  REQUIRE(d.found);
  CHECK(d.result.frame.header_ok);
  CHECK(!d.result.lost());
  CHECK(d.result.frame.payload_crc_ok);
  CHECK(d.ber.bit_errors == 0);
  CHECK(d.ber.bits_compared == 1024);
  CHECK(!d.ber.length_mismatch);
  CHECK(d.result.frame.header.type == FrameType::Data);
  CHECK(d.result.frame.header.modulation_order == 4);
  CHECK(d.result.frame.header.frame_counter == 1);
  CHECK(d.result.frame.header.payload_length == 128);
  CHECK(!d.result.truncated);
  CHECK(d.result.frame.error.empty());
}

TEST(the_evm_of_a_clean_frame_is_small) {
  // Measured: 1.0% (40 dB) with no carrier offset, 1.2% at 7.5 kHz. That floor is the
  // finite RRC span's inter-symbol interference (0.5%) plus the cubic interpolator
  // (0.3%), not noise.
  Scenario s;
  s.add_noise = false;
  const Decoded d = decode(s);
  REQUIRE(d.found && d.result.frame.header_ok);
  CHECK(d.result.evm > 0.003);       // not exactly zero: the floor is real
  CHECK(d.result.evm < 0.02);
  CHECK(d.result.evm_snr_db > 34.0);
  CHECK(d.result.evm_symbols == 528);
}

TEST(the_corrected_symbols_sit_on_the_constellation) {
  Scenario s;
  s.add_noise = false;
  const Decoded d = decode(s);
  REQUIRE(d.found && d.result.frame.header_ok);
  REQUIRE(d.result.payload_symbols.size() == 528);
  REQUIRE(d.result.header_symbols.size() == 72);
  const float a = 0.70710678f;
  for (const cf& y : d.result.payload_symbols) {
    CHECK_NEAR(std::fabs(y.real()), a, 0.04);
    CHECK_NEAR(std::fabs(y.imag()), a, 0.04);
  }
  for (const cf& y : d.result.header_symbols) {
    CHECK_NEAR(std::fabs(y.real()), 1.0, 0.05);   // BPSK
    CHECK_NEAR(y.imag(), 0.0, 0.05);
  }
  CHECK(d.result.symbols_decoded == 72 + 528);
}

TEST(frames_with_other_counters_and_sizes_decode) {
  for (const std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{64}, std::size_t{300}}) {
    Scenario s;
    s.add_noise = false;
    s.delay = 0.3;
    const Decoded d = decode(s, 40000 + static_cast<std::uint16_t>(n), n);
    REQUIRE(d.found);
    CHECK(d.result.frame.header_ok);
    CHECK(d.result.frame.payload_crc_ok);
    CHECK(d.result.frame.header.payload_length == n);
    CHECK(d.ber.bit_errors == 0);
    CHECK(d.result.frame.payload.size() == n);
  }
}

// ---- Impairments ------------------------------------------------------------

TEST(carrier_offsets_across_the_acquisition_range_decode_cleanly) {
  // Measured: zero errors in 40 960 bits at 0, 3000, +7500 and -7500 Hz, noiseless.
  for (const double f : {0.0, 1000.0, 3000.0, 5000.0, 7500.0, -7500.0}) {
    Scenario s;
    s.add_noise = false;
    s.cfo_hz = f;
    s.phase_rad = 1.9;
    const Decoded d = decode(s);
    REQUIRE(d.found);
    CHECK(d.result.frame.payload_crc_ok);
    CHECK(d.ber.bit_errors == 0);
    // The demodulator's frequency, averaged over the payload, is far better than the
    // preamble's alone (about 60 Hz at 10 dB): measured under 5 Hz here.
    CHECK(std::fabs(d.result.cfo_hz(kSymbolRate) - f) < 15.0);
  }
}

TEST(the_loop_refines_the_preambles_frequency_estimate) {
  // At 10 dB the preamble's frequency is good to about 60 Hz rms. After the carrier
  // loop has run over a frame, the averaged estimate is good to a few hertz.
  double preamble_err = 0, loop_err = 0;
  const int trials = 16;
  for (int i = 0; i < trials; ++i) {
    Scenario s;
    s.esn0_db = 10.0;
    s.cfo_hz = 2000.0;
    s.seed = 700 + static_cast<std::uint64_t>(i);
    s.delay = (i % 10) / 10.0;
    s.phase_rad = 0.3 * i;
    const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
    REQUIRE(d.found && d.result.frame.header_ok);
    const double pe = d.est.cfo_hz(kSymbolRate) - 2000.0;
    const double le = d.result.cfo_hz(kSymbolRate) - 2000.0;
    preamble_err += pe * pe;
    loop_err += le * le;
  }
  CHECK(std::sqrt(loop_err / trials) < 10.0);
  CHECK(std::sqrt(loop_err / trials) < 0.3 * std::sqrt(preamble_err / trials));
}

TEST(a_level_range_and_a_dc_offset_do_not_matter) {
  for (const double db : {20.0, 0.0, -30.0, -60.0}) {
    Scenario s;
    s.add_noise = false;
    s.gain = std::pow(10.0, db / 20.0);
    s.dc = cf(0.2f, -0.1f);
    s.cfo_hz = 1200.0;
    const Decoded d = decode(s);
    REQUIRE(d.found);
    CHECK(d.result.frame.payload_crc_ok);
    CHECK(d.ber.bit_errors == 0);
  }
}

TEST(fractional_timing_offsets_all_decode) {
  for (int k = 0; k < 10; ++k) {
    Scenario s;
    s.add_noise = false;
    s.delay = k / 10.0;
    const Decoded d = decode(s);
    REQUIRE(d.found);
    CHECK(d.result.frame.payload_crc_ok);
    CHECK(d.ber.bit_errors == 0);
  }
}

// ---- Sample-clock offset (requirement 7.5) ---------------------------------

namespace {

/** A long frame through a receiver whose sample clock is off by @p ppm. */
Decoded decode_with_clock_offset(double ppm, double timing_bn_t, std::uint64_t seed) {
  const std::size_t kBytes = 4096;
  const auto burst = modulator().modulate(FrameType::Data, 7, prbs_bytes(kSeed, 7, kBytes));
  std::vector<cf> x(400, cf(0, 0));
  x.insert(x.end(), burst.samples.begin(), burst.samples.end());
  x.insert(x.end(), 400, cf(0, 0));
  auto r = mpsk::sim::delay_signal(x, (seed % 6) / 6.0);
  mpsk::sim::rotate_carrier(r, 1500.0 / kSampleRate, 0.7);
  if (ppm != 0.0) r = mpsk::sim::apply_clock_offset(r, ppm);
  mpsk::sim::NoiseSource rng(seed);
  mpsk::sim::add_noise(r, mpsk::sim::noise_variance_for_esn0(0.0625, kSps, 20.0), rng);

  RxFrontEnd fe{RxFrontEndConfig{}};
  const auto y = fe.process(r);
  Decoded d;
  const auto e = acquire_preamble(y.data(), y.size(), 0, AcquisitionConfig{});
  if (!e) return d;
  d.found = true;
  d.est = *e;
  DemodConfig dc;
  dc.timing_bn_t = timing_bn_t;
  d.result = Demodulator(dc).demodulate(y.data(), y.size(), *e);
  d.ber = payload_ber(d.result.frame.payload, prbs_bytes(kSeed, 7, kBytes));
  return d;
}

}  // namespace

TEST(the_timing_loop_follows_a_sample_clock_offset_that_a_frozen_loop_cannot) {
  // A 4096-byte frame is 16 536 symbols, 66 ms. Measured at Es/N0 = 20 dB, six frames
  // per row. With the timing loop frozen: 20 ppm (1.3 samples of drift over the frame)
  // gives 264 bit errors and no good CRC, and 50 ppm and above tens of thousands of
  // errors. With the loop on, every frame decodes with zero bit errors up to 400 ppm
  // (26 samples of drift, more than six symbols). Requirement 7.5.
  for (const double ppm : {20.0, 100.0, 400.0}) {
    int frozen_ok = 0, tracking_ok = 0;
    for (int i = 0; i < 2; ++i) {
      const Decoded frozen = decode_with_clock_offset(ppm, 0.0, 10 + static_cast<std::uint64_t>(i));
      const Decoded tracking = decode_with_clock_offset(ppm, 0.003, 10 + static_cast<std::uint64_t>(i));
      REQUIRE(frozen.found && tracking.found);
      frozen_ok += frozen.result.frame.payload_crc_ok ? 1 : 0;
      tracking_ok += (tracking.result.frame.payload_crc_ok && tracking.ber.bit_errors == 0) ? 1 : 0;
    }
    CHECK(frozen_ok == 0);
    CHECK(tracking_ok == 2);
  }
}

TEST(with_no_clock_offset_the_loop_makes_no_difference_to_a_long_frame) {
  const Decoded frozen = decode_with_clock_offset(0.0, 0.0, 10);
  const Decoded tracking = decode_with_clock_offset(0.0, 0.003, 10);
  REQUIRE(frozen.found && tracking.found);
  CHECK(frozen.result.frame.payload_crc_ok);
  CHECK(tracking.result.frame.payload_crc_ok);
  CHECK_NEAR(tracking.result.evm, frozen.result.evm, 0.01);
}

TEST(the_learned_clock_offset_has_the_right_sign_and_size_for_large_offsets) {
  // Measured, averaged over the payload of a 4096-byte frame: reads 16, 52, 97, 197
  // and 400 ppm for true offsets of 20, 50, 100, 200 and 400. Within about 20% above
  // 50 ppm. Below that it is lost in the loop's own noise (about 8 ppm rms), which is
  // why the offset is documented as a diagnostic for large offsets only.
  for (const double ppm : {100.0, 400.0, -200.0}) {
    const Decoded d = decode_with_clock_offset(ppm, 0.003, 11);
    REQUIRE(d.found && d.result.frame.header_ok);
    CHECK_NEAR(d.result.clock_offset_ppm / ppm, 1.0, 0.25);
  }
  const Decoded none = decode_with_clock_offset(0.0, 0.003, 11);
  REQUIRE(none.found && none.result.frame.header_ok);
  CHECK(std::fabs(none.result.clock_offset_ppm) < 25.0);
}

// ---- Noise: the receiver performs as theory says ---------------------------

TEST(ber_at_eight_db_is_close_to_theory) {
  // Gray-coded QPSK at symbol SNR g = Es/N0 has BER = Q(sqrt(g)). At 10 dB that is
  // 7.8e-4; at 8 dB 6.0e-3. 8 dB is used because it yields about 370 errors from 60
  // frames, where 10 dB would need 160 frames (15 s in a Debug build) for 130. Measured
  // at the default loop bandwidths: 6.5e-3 at 8 dB (1.08 times the ideal) and 8.6e-4 at
  // 10 dB from 400 frames (1.10 times). This is a loose check: the sweep against theory
  // is task 8.
  const double g = std::pow(10.0, 0.8);
  const double ideal = 0.5 * std::erfc(std::sqrt(g) / std::sqrt(2.0));
  CHECK_NEAR(ideal, 6.0e-3, 0.1e-3);

  std::size_t errors = 0, bits = 0;
  int lost = 0;
  const int frames = 60;
  for (int i = 0; i < frames; ++i) {
    Scenario s;
    s.esn0_db = 8.0;
    s.cfo_hz = 2000.0;
    s.seed = 4000 + static_cast<std::uint64_t>(i);
    s.delay = (i % 10) / 10.0;
    s.phase_rad = 0.3 * i;
    const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
    if (!d.found || !d.result.frame.header_ok) {
      ++lost;
      continue;
    }
    errors += d.ber.bit_errors;
    bits += d.ber.bits_compared;
  }
  CHECK(lost <= 2);
  CHECK(bits > 55000);
  const double ber = static_cast<double>(errors) / static_cast<double>(bits);
  CHECK(ber > 0.6 * ideal);    // no better than theory, within statistics
  CHECK(ber < 2.0 * ideal);    // and within a factor of two of it
}

TEST(the_evm_snr_tracks_the_true_snr_through_the_whole_receiver) {
  // Measured: 19.9, 14.9, 9.9 and 7.9 dB for true Es/N0 of 20, 15, 10 and 8.
  for (const double esn0 : {20.0, 15.0, 10.0}) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < 8; ++i) {
      Scenario s;
      s.esn0_db = esn0;
      s.seed = 900 + static_cast<std::uint64_t>(i);
      s.delay = (i % 10) / 10.0;
      s.cfo_hz = 1500.0;
      const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
      if (!d.found || !d.result.frame.header_ok) continue;
      sum += d.result.evm_snr_db;
      ++n;
    }
    REQUIRE(n >= 7);
    CHECK_NEAR(sum / n, esn0, 0.4);
  }
}

TEST(a_frame_with_a_few_errors_still_reports_its_header_and_the_crc_failure) {
  // At 8 dB there are a handful of bit errors per frame: the header (BPSK, with
  // 3 dB of extra margin) decodes, and the payload CRC says the payload is not clean.
  int crc_bad = 0, header_ok = 0;
  for (int i = 0; i < 30; ++i) {
    Scenario s;
    s.esn0_db = 8.0;
    s.seed = 2000 + static_cast<std::uint64_t>(i);
    const Decoded d = decode(s, static_cast<std::uint16_t>(i + 1));
    if (!d.found || !d.result.frame.header_ok) continue;
    ++header_ok;
    if (!d.result.frame.payload_crc_ok) {
      ++crc_bad;
      CHECK(d.ber.bit_errors > 0);   // the CRC and the known payload agree that it is wrong
    } else {
      CHECK(d.ber.bit_errors == 0);
    }
  }
  CHECK(header_ok >= 28);
  CHECK(crc_bad >= 20);     // at 8 dB almost every 1056-bit frame has an error
}

// ---- Failures are reported, not thrown --------------------------------------

TEST(a_destroyed_header_is_a_lost_frame_with_no_payload) {
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const std::size_t start = static_cast<std::size_t>(std::llround(e->position));

  // Measured: a noise burst of rms 0.2 on the header still decodes, and rms 1.0 and
  // above fails its CRC. The frame is then lost and no payload is returned.
  auto y = r.y;
  mpsk::sim::NoiseSource rng(5);
  for (std::size_t i = start + 64 * 4 - 6; i < start + 136 * 4 + 6; ++i) y[i] += rng.complex_noise(1.0);

  const DemodResult d = Demodulator(DemodConfig{}).demodulate(y.data(), y.size(), *e);
  CHECK(d.lost());
  CHECK(!d.frame.header_ok);
  CHECK(!d.frame.payload_crc_ok);
  CHECK(d.frame.payload.empty());
  CHECK(!d.frame.trailer.has_value());
  CHECK(d.frame.error.find("CRC") != std::string::npos);
  CHECK(d.payload_symbols.empty());
  CHECK(d.evm_symbols == 0);
  CHECK(!d.truncated);
}

TEST(a_moderate_noise_burst_on_the_header_does_not_lose_the_frame) {
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const std::size_t start = static_cast<std::size_t>(std::llround(e->position));
  auto y = r.y;
  mpsk::sim::NoiseSource rng(5);
  for (std::size_t i = start + 64 * 4 - 6; i < start + 136 * 4 + 6; ++i) y[i] += rng.complex_noise(0.04);   // rms 0.2
  const DemodResult d = Demodulator(DemodConfig{}).demodulate(y.data(), y.size(), *e);
  CHECK(d.frame.header_ok);
  CHECK(d.frame.payload_crc_ok);
}

TEST(a_buffer_that_ends_inside_the_header_is_truncated_not_lost_to_a_crc) {
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const std::size_t start = static_cast<std::size_t>(std::llround(e->position));
  const DemodResult d = Demodulator(DemodConfig{}).demodulate(r.y.data(), start + 100 * 4, *e);
  CHECK(d.truncated);
  CHECK(d.lost());
  CHECK(d.frame.error.find("header") != std::string::npos);
  CHECK(d.symbols_decoded < 72);
}

TEST(a_buffer_that_ends_inside_the_payload_keeps_the_header_and_says_truncated) {
  // Measured: the frame completes once the buffer reaches its last payload symbol (symbol
  // 663 of 664 from the preamble start), and a buffer short of that reports exactly how
  // many payload symbols were read.
  // A 128-byte payload: 528 payload symbols, 664 symbols from the preamble start. (The
  // default payload in rx_scenario.hpp is only 16 bytes, and a cut computed for the
  // larger frame would then lie beyond the record and cut nothing.)
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s, 1, 128);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const std::size_t start = static_cast<std::size_t>(std::llround(e->position));
  const std::size_t frame_symbols = frame_symbol_count(FrameType::Data, 4, 128);
  REQUIRE(frame_symbols == 664);
  REQUIRE(start + frame_symbols * 4 < r.y.size());   // the record really holds the whole frame
  const Demodulator demod{DemodConfig{}};

  const DemodResult cut = demod.demodulate(r.y.data(), start + 300 * 4 + 6, *e);
  CHECK(cut.truncated);
  CHECK(cut.frame.header_ok);               // the header was read before the buffer ended
  CHECK(!cut.frame.payload_crc_ok);
  CHECK(cut.frame.payload.empty());
  CHECK(cut.frame.error.find("truncated") != std::string::npos);
  CHECK(cut.payload_symbols.size() > 100 && cut.payload_symbols.size() < 528);
  CHECK(cut.symbols_decoded == 72 + cut.payload_symbols.size());

  // Measured: the last payload symbol is read once the buffer reaches 6 samples past
  // it (the interpolator looks 2 ahead, and the loop has drifted by a fraction).
  const DemodResult whole = demod.demodulate(r.y.data(), start + 663 * 4 + 6, *e);
  CHECK(!whole.truncated);
  CHECK(whole.frame.payload_crc_ok);
  const DemodResult just_short = demod.demodulate(r.y.data(), start + 660 * 4 + 6, *e);
  CHECK(just_short.truncated);
}

TEST(an_unusable_amplitude_estimate_is_reported) {
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s);
  auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const Demodulator demod{DemodConfig{}};
  const double inf = std::numeric_limits<double>::infinity();
  for (const double bad : {0.0, -1.0, std::nan(""), inf}) {
    PreambleEstimate est = *e;
    est.amplitude = bad;
    const DemodResult d = demod.demodulate(r.y.data(), r.y.size(), est);
    CHECK(d.lost());
    CHECK(d.frame.error.find("amplitude") != std::string::npos);
  }
}

// ---- How far the starting estimates may be off -----------------------------

TEST(the_loops_recover_from_a_starting_error_much_larger_than_the_preamble_leaves) {
  // Measured through the whole demodulator, noiseless. Starting frequency error: fine up
  // to 1500 Hz, fails at 2500. Starting phase error: fine up to 69 degrees (the header
  // is BPSK, with 90 degrees of room), fails at 115. Starting timing error: fine up to
  // 1.0 sample, fails at 1.5. The preamble's own errors are under 300 Hz, 0.1 rad and
  // 0.3 sample even at 0 dB, so each has several times of margin.
  Scenario s;
  s.add_noise = false;
  s.cfo_hz = 1000.0;
  const Received r = make_received(s);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const Demodulator demod{DemodConfig{}};
  const auto ok = [&](PreambleEstimate est) {
    const DemodResult d = demod.demodulate(r.y.data(), r.y.size(), est);
    return d.frame.header_ok && d.frame.payload_crc_ok;
  };

  PreambleEstimate f = *e;
  f.omega += 2.0 * kPi * 1000.0 / kSymbolRate;
  CHECK(ok(f));
  f = *e;
  f.omega += 2.0 * kPi * 4000.0 / kSymbolRate;
  CHECK(!ok(f));

  PreambleEstimate p = *e;
  p.phase_centre += 1.0;
  CHECK(ok(p));
  p = *e;
  p.phase_centre += 2.0;
  CHECK(!ok(p));

  PreambleEstimate t = *e;
  t.position += 0.5;
  CHECK(ok(t));
  t = *e;
  t.position += 2.0;
  CHECK(!ok(t));
}

// ---- Configuration ----------------------------------------------------------

TEST(a_bad_configuration_is_rejected_when_the_demodulator_is_built) {
  DemodConfig c;
  c.samples_per_symbol = 1;
  CHECK_THROWS(Demodulator{c});
  c = DemodConfig{};
  c.carrier.bn_t = -0.1;
  CHECK_THROWS(Demodulator{c});
  c = DemodConfig{};
  c.carrier.bn_t = 0.7;
  CHECK_THROWS(Demodulator{c});
  c = DemodConfig{};
  c.timing_bn_t = 0.6;
  CHECK_THROWS(Demodulator{c});
  c = DemodConfig{};
  c.timing_damping = 0.0;
  CHECK_THROWS(Demodulator{c});
  c = DemodConfig{};
  c.carrier.damping = -1.0;
  CHECK_THROWS(Demodulator{c});
}

TEST(a_supplied_detector_gain_avoids_the_measurement_and_gives_the_same_result) {
  Scenario s;
  s.esn0_db = 12.0;
  s.seed = 7;
  const Decoded a = decode(s);
  DemodConfig dc;
  dc.timing_detector_gain = TimingLoop::measure_detector_gain(4, 0.35, 11);
  const Decoded b = decode(s, 1, 128, dc);
  REQUIRE(a.found && b.found);
  CHECK(a.result.frame.payload == b.result.frame.payload);
  CHECK_NEAR(a.result.evm, b.result.evm, 1e-9);
}

TEST(the_detector_gain_is_measured_once_when_the_demodulator_is_built_not_per_frame) {
  // Measuring takes around 15 ms (20 000 symbols). Doing it for every frame would be
  // the single largest cost of decoding one. The result is identical either way, so
  // only timing can show it: decoding a frame takes under a millisecond.
  Scenario s;
  s.add_noise = false;
  const Received r = make_received(s, 1, 128);
  const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());

  const Demodulator demod{DemodConfig{}};   // the measurement happens here
  const auto start = std::chrono::steady_clock::now();
  const int frames = 60;
  for (int i = 0; i < frames; ++i) {
    const DemodResult d = demod.demodulate(r.y.data(), r.y.size(), *e);
    CHECK(d.frame.payload_crc_ok);
  }
  const double per_frame_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
  // Measured about 0.1 ms per frame in Release. A measurement per frame would be 15 ms or
  // more. The bound is generous for a slow CI machine or a Debug build, and still far
  // below the cost of re-measuring.
  CHECK(per_frame_ms < 8.0);
}

TEST(the_demodulator_can_be_reused_for_many_frames) {
  const Demodulator demod{DemodConfig{}};
  for (int i = 0; i < 5; ++i) {
    Scenario s;
    s.add_noise = false;
    s.cfo_hz = 500.0 * i;
    s.delay = i / 5.0;
    const Received r = make_received(s, static_cast<std::uint16_t>(i + 1));
    const auto e = acquire_preamble(r.y.data(), r.y.size(), 0, AcquisitionConfig{});
    REQUIRE(e.has_value());
    const DemodResult d = demod.demodulate(r.y.data(), r.y.size(), *e);
    CHECK(d.frame.payload_crc_ok);
    CHECK(d.frame.header.frame_counter == i + 1);
  }
}

// ---- Echo frames ------------------------------------------------------------

TEST(an_echo_frame_returns_its_trailer) {
  EchoTrailer t;
  t.echoed_counter = 321;
  t.evm_tenths_db = -253;
  t.cfo_hz = -1500;
  t.level_tenths_dbfs = -120;
  t.bit_errors = 3;
  const auto burst = modulator().modulate(FrameType::Echo, 322, prbs_bytes(kSeed, 322, 128), &t);
  std::vector<cf> x(400, cf(0, 0));
  x.insert(x.end(), burst.samples.begin(), burst.samples.end());
  x.insert(x.end(), 400, cf(0, 0));
  auto r = mpsk::sim::delay_signal(x, 0.4);
  mpsk::sim::rotate_carrier(r, 900.0 / kSampleRate, 2.0);
  RxFrontEnd fe{RxFrontEndConfig{}};
  const auto y = fe.process(r);

  const auto e = acquire_preamble(y.data(), y.size(), 0, AcquisitionConfig{});
  REQUIRE(e.has_value());
  const DemodResult d = Demodulator(DemodConfig{}).demodulate(y.data(), y.size(), *e);
  REQUIRE(d.frame.header_ok);
  CHECK(d.frame.header.type == FrameType::Echo);
  CHECK(d.frame.payload_crc_ok);
  REQUIRE(d.frame.trailer.has_value());
  CHECK(d.frame.trailer->echoed_counter == 321);
  CHECK(d.frame.trailer->evm_tenths_db == -253);
  CHECK(d.frame.trailer->cfo_hz == -1500);
  CHECK(d.frame.trailer->bit_errors == 3);
  CHECK(d.evm_symbols == payload_section_symbols(FrameType::Echo, 2, 128));
}

MPSK_TEST_MAIN()
