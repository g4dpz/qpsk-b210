// Tests for the transmit chain. Requirements 2.2, 2.4, 2.5.
//
// The numbers asserted here come from measurement (see the comments), not from
// the formulas alone: average power matches 1/sps to four digits, and at the
// default settings the peak is about 0.40 of full scale.

#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <vector>

#include "dsp/constellation.hpp"
#include "dsp/fir.hpp"
#include "dsp/framer.hpp"
#include "dsp/modulator.hpp"
#include "dsp/preamble.hpp"
#include "dsp/prbs.hpp"
#include "dsp/rrc.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cf = std::complex<float>;

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

bool order_is_enabled(int order) {
  for (const int o : Constellation::supported_orders()) {
    if (o == order) return true;
  }
  return false;
}

double max_abs_diff(const std::vector<cf>& a, const std::vector<cf>& b) {
  double worst = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
  }
  return worst;
}

}  // namespace

// ---- Length and layout ------------------------------------------------------

TEST(burst_length_is_guard_plus_shaped_frame_plus_guard) {
  const Modulator m(ModulatorConfig{});  // QPSK, sps 4, span 11, guard 64
  const ModulatedBurst b = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 128));

  // 664 symbols: (664 - 1) * 4 + 45 taps = 2697 shaped samples, plus 2 x 64 guard.
  CHECK(b.symbol_count == 664);
  CHECK(b.samples.size() == 2 * 64 + (664 - 1) * 4 + 45);
  CHECK(b.samples.size() == 2825);
  CHECK(b.samples.size() == m.burst_samples(FrameType::Data, 128));
}

TEST(burst_length_matches_the_prediction_for_other_configurations) {
  for (const int sps : {2, 4, 8}) {
    for (const int guard : {0, 1, 64, 200}) {
      ModulatorConfig c;
      c.samples_per_symbol = sps;
      c.span_symbols = 12;
      c.guard_samples = guard;
      const Modulator m(c);
      for (const std::size_t n : {0u, 1u, 128u, 500u}) {
        const auto b = m.modulate(FrameType::Data, 3, prbs_bytes(2, 3, n));
        CHECK(b.samples.size() == m.burst_samples(FrameType::Data, n));
      }
    }
  }
}

TEST(an_echo_burst_is_longer_by_the_trailer) {
  const Modulator m(ModulatorConfig{});
  EchoTrailer t;
  const auto data = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 128));
  const auto echo = m.modulate(FrameType::Echo, 1, prbs_bytes(1, 1, 128), &t);
  // 12 trailer bytes = 96 bits = 48 QPSK symbols = 192 samples at 4 per symbol.
  CHECK(echo.samples.size() == data.samples.size() + 48 * 4);
  CHECK(echo.samples.size() == m.burst_samples(FrameType::Echo, 128));
}

TEST(guard_intervals_are_exact_silence_and_the_body_is_not) {
  const Modulator m(ModulatorConfig{});
  const auto b = m.modulate(FrameType::Data, 7, prbs_bytes(1, 7, 128));
  const std::size_t guard = 64;

  for (std::size_t i = 0; i < guard; ++i) CHECK(b.samples[i] == cf(0.0f, 0.0f));
  for (std::size_t i = b.samples.size() - guard; i < b.samples.size(); ++i) {
    CHECK(b.samples[i] == cf(0.0f, 0.0f));
  }

  // The shaped burst begins right after the guard, with the tail of the first
  // pulse, so the first body sample is small but the middle is at full level.
  double body_energy = 0.0;
  for (std::size_t i = guard; i < b.samples.size() - guard; ++i) {
    body_energy += static_cast<double>(std::norm(b.samples[i]));
  }
  CHECK(body_energy > 100.0);
}

TEST(zero_guard_gives_a_burst_with_no_silent_padding) {
  ModulatorConfig c;
  c.guard_samples = 0;
  const Modulator m(c);
  const auto b = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 16));
  CHECK(b.samples.size() == (b.symbol_count - 1) * 4 + 45);
  CHECK(b.first_symbol_peak_index == 22);
}

TEST(first_symbol_peak_index_is_guard_plus_group_delay) {
  const Modulator m(ModulatorConfig{});
  CHECK(m.first_symbol_peak_offset() == 64 + 22);
  const auto b = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 16));
  CHECK(b.first_symbol_peak_index == 86);
  CHECK(rrc_group_delay(4, 11) == 22);
}

// ---- Level ------------------------------------------------------------------

TEST(average_power_is_scale_squared_over_sps) {
  // Measured: the body of a burst has power 0.2502 at scale 1 and sps 4, against
  // the predicted 1/4. At the default scale of 0.5 that is 0.0625.
  const Modulator m(ModulatorConfig{});
  double power = 0.0;
  std::size_t count = 0;
  for (unsigned f = 0; f < 50; ++f) {
    const auto b = m.modulate(FrameType::Data, static_cast<uint16_t>(f), prbs_bytes(5, f, 128));
    // Skip the pulse ramps at both ends, where the filter is filling or draining.
    for (std::size_t i = 64 + 100; i + 64 + 100 < b.samples.size(); ++i) {
      power += static_cast<double>(std::norm(b.samples[i]));
      ++count;
    }
  }
  power /= static_cast<double>(count);
  CHECK_NEAR(power, 0.5 * 0.5 / 4.0, 0.002);
}

TEST(default_peak_level_leaves_plenty_of_headroom) {
  // Measured over 400 frames at scale 1.0: peak 0.793, so 0.397 at the default
  // scale of 0.5. The bound allows for the frames not seen here.
  const Modulator m(ModulatorConfig{});
  float worst = 0.0f;
  for (unsigned f = 0; f < 200; ++f) {
    const auto b = m.modulate(FrameType::Data, static_cast<uint16_t>(f), prbs_bytes(9, f, 128));
    worst = std::max(worst, b.peak);
  }
  CHECK(worst < 0.45);
  CHECK(worst < 0.8);   // the design's stated limit
  CHECK(worst > 0.3);   // and the signal is really there: a peak near rms 0.25 x crest 1.6
}

TEST(reported_peak_is_the_largest_sample_magnitude) {
  // Checked over many frames and against the full complex magnitude, not I alone.
  // Measured: for the default configuration, 27% of frames have their peak inside
  // the payload (the rest in the BPSK preamble), and the peak sample's quadrature
  // part reaches 0.56 of full scale at scale 1.0, so a peak measured on I alone
  // would under-report.
  const Modulator m(ModulatorConfig{});
  int quadrature_dominated = 0;
  for (unsigned f = 0; f < 100; ++f) {
    const auto b = m.modulate(FrameType::Data, static_cast<uint16_t>(f), prbs_bytes(7, f, 128));
    float largest = 0.0f;
    float largest_i_only = 0.0f;
    for (const cf& s : b.samples) {
      largest = std::max(largest, std::abs(s));
      largest_i_only = std::max(largest_i_only, std::abs(s.real()));
    }
    CHECK(b.peak == largest);
    if (largest_i_only < largest - 1e-4f) ++quadrature_dominated;
  }
  // Guards the test itself: it must include frames where an I-only peak would be wrong.
  CHECK(quadrature_dominated > 5);
}

TEST(output_scale_scales_every_sample_linearly) {
  ModulatorConfig half;
  half.output_scale = 0.5;
  ModulatorConfig quarter;
  quarter.output_scale = 0.25;
  const auto a = Modulator(half).modulate(FrameType::Data, 4, prbs_bytes(1, 4, 64));
  const auto b = Modulator(quarter).modulate(FrameType::Data, 4, prbs_bytes(1, 4, 64));

  CHECK(a.samples.size() == b.samples.size());
  for (std::size_t i = 0; i < a.samples.size(); ++i) {
    CHECK(std::abs(a.samples[i] - 2.0f * b.samples[i]) < 1e-6f);
  }
  CHECK_NEAR(a.peak, 2.0 * b.peak, 1e-6);
}

TEST(a_scale_that_would_clip_is_visible_in_the_reported_peak) {
  // With 2 samples per symbol the pulses overlap more and the crest factor is
  // higher: measured peak 1.12 at scale 1.0. The scale alone cannot promise no
  // clipping, which is why the burst reports its peak for the caller to check.
  ModulatorConfig c;
  c.samples_per_symbol = 2;
  c.span_symbols = 12;
  c.output_scale = 1.0;
  const Modulator m(c);
  float worst = 0.0f;
  for (unsigned f = 0; f < 100; ++f) {
    worst = std::max(worst, m.modulate(FrameType::Data, static_cast<uint16_t>(f), prbs_bytes(7, f, 128)).peak);
  }
  CHECK(worst > 1.0f);
}

// ---- Correctness of the signal ---------------------------------------------

TEST(matched_filtering_recovers_the_frame_symbols_at_the_computed_index) {
  // Receiver-side check of the transmitter's alignment and scale: filter the burst
  // with the matching RRC and read it at guard + (N - 1) + m * sps, undoing the
  // output scale. Each value should be the frame symbol, apart from small ISI.
  const ModulatorConfig cfg;
  const Modulator m(cfg);
  const std::vector<uint8_t> payload = prbs_bytes(3, 11, 128);
  const ModulatedBurst b = m.modulate(FrameType::Data, 11, payload);
  const std::vector<cf> expected = build_frame_symbols(FrameType::Data, 4, 11, payload);

  std::vector<cf> samples = b.samples;
  samples.resize(samples.size() + m.taps().size() - 1, cf(0.0f, 0.0f));  // flush
  FirFilter matched(m.taps());
  const std::vector<cf> y = matched.process(samples);

  const std::size_t first = static_cast<std::size_t>(cfg.guard_samples) + (m.taps().size() - 1);
  CHECK(first == m.first_symbol_peak_offset() + 22);  // peak index + one more group delay

  std::vector<cf> recovered;
  double worst = 0.0;
  for (std::size_t k = 0; k < expected.size(); ++k) {
    const cf r = y[first + k * 4] / static_cast<float>(cfg.output_scale);
    recovered.push_back(r);
    worst = std::max(worst, static_cast<double>(std::abs(r - expected[k])));
  }
  CHECK(worst < 0.02);

  // And the recovered symbols decode to the original payload with a good CRC.
  const DecodedFrame d = decode_frame_symbols(recovered);
  CHECK(d.header_ok);
  CHECK(d.payload_crc_ok);
  CHECK(d.payload == payload);
  CHECK(d.header.frame_counter == 11);
}

TEST(an_echo_burst_round_trips_through_the_matched_filter_too) {
  const ModulatorConfig cfg;
  const Modulator m(cfg);
  EchoTrailer t;
  t.echoed_counter = 99;
  t.cfo_hz = -321;
  const std::vector<uint8_t> payload = prbs_bytes(3, 100, 128);
  const ModulatedBurst b = m.modulate(FrameType::Echo, 100, payload, &t);

  std::vector<cf> samples = b.samples;
  samples.resize(samples.size() + m.taps().size() - 1, cf(0.0f, 0.0f));
  FirFilter matched(m.taps());
  const std::vector<cf> y = matched.process(samples);

  const std::size_t first = static_cast<std::size_t>(cfg.guard_samples) + (m.taps().size() - 1);
  std::vector<cf> recovered;
  for (std::size_t k = 0; k < b.symbol_count; ++k) {
    recovered.push_back(y[first + k * 4] / static_cast<float>(cfg.output_scale));
  }
  const DecodedFrame d = decode_frame_symbols(recovered);
  CHECK(d.header_ok);
  CHECK(d.payload_crc_ok);
  REQUIRE(d.trailer.has_value());
  CHECK(d.trailer->echoed_counter == 99);
  CHECK(d.trailer->cfo_hz == -321);
}

TEST(different_frames_give_different_bursts_and_the_same_frame_the_same_burst) {
  const Modulator m(ModulatorConfig{});
  const auto a = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 64));
  const auto a2 = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 64));
  const auto other_counter = m.modulate(FrameType::Data, 2, prbs_bytes(1, 2, 64));

  CHECK(a.samples == a2.samples);
  CHECK(a.samples.size() == other_counter.samples.size());
  CHECK(max_abs_diff(a.samples, other_counter.samples) > 0.05);
}

TEST(the_preamble_dominates_the_start_of_the_burst) {
  // The first 64 symbols are the BPSK preamble, so the first 64 matched-filter
  // outputs are real (+-1 times the scale) and the quadrature part is near zero.
  const ModulatorConfig cfg;
  const Modulator m(cfg);
  const auto b = m.modulate(FrameType::Data, 1, prbs_bytes(1, 1, 64));

  std::vector<cf> samples = b.samples;
  samples.resize(samples.size() + m.taps().size() - 1, cf(0.0f, 0.0f));
  FirFilter matched(m.taps());
  const std::vector<cf> y = matched.process(samples);

  const std::size_t first = static_cast<std::size_t>(cfg.guard_samples) + (m.taps().size() - 1);
  const auto& preamble = preamble_symbols();
  for (std::size_t k = 0; k < preamble.size(); ++k) {
    const cf r = y[first + k * 4] / static_cast<float>(cfg.output_scale);
    CHECK_NEAR(r.real(), preamble[k].real(), 0.02);
    CHECK_NEAR(r.imag(), 0.0, 0.02);
  }
}

TEST(empty_payloads_are_modulated) {
  const Modulator m(ModulatorConfig{});
  const auto b = m.modulate(FrameType::Data, 5, {});
  CHECK(b.samples.size() == m.burst_samples(FrameType::Data, 0));
  CHECK(b.symbol_count == 64 + 72 + 16);  // only the CRC-32: 32 bits = 16 QPSK symbols
}

// ---- Configuration checks ---------------------------------------------------

TEST(bad_configurations_are_rejected) {
  const auto with = [](auto edit) {
    ModulatorConfig c;
    edit(c);
    return c;
  };
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.output_scale = 0.0; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.output_scale = -0.5; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.output_scale = 1.01; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.output_scale = std::nan(""); })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.guard_samples = -1; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.samples_per_symbol = 1; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.rolloff = 1.5; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.span_symbols = 0; })));
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.samples_per_symbol = 3; })));  // 3 x 11 is odd
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.modulation_order = 2; })));  // BPSK
  CHECK_THROWS(Modulator(with([](ModulatorConfig& c) { c.modulation_order = 5; })));
}

TEST(a_scale_of_exactly_one_is_accepted) {
  ModulatorConfig c;
  c.output_scale = 1.0;
  const Modulator m(c);
  CHECK(m.config().output_scale == 1.0);
}

TEST(a_modulation_not_enabled_in_this_build_is_refused_with_a_clear_message) {
  if (order_is_enabled(8)) return;  // stage 2: this no longer applies
  ModulatorConfig c;
  c.modulation_order = 8;
  std::string message;
  try {
    Modulator m(c);
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(contains(message, "8-PSK"));
}

TEST(modulate_rejects_what_the_framer_rejects) {
  const Modulator m(ModulatorConfig{});
  EchoTrailer t;
  CHECK_THROWS(m.modulate(FrameType::Echo, 1, {1, 2}, nullptr));
  CHECK_THROWS(m.modulate(FrameType::Data, 1, {1, 2}, &t));
  CHECK_THROWS(m.modulate(FrameType::Data, 1, std::vector<uint8_t>(4097)));
}

MPSK_TEST_MAIN()
