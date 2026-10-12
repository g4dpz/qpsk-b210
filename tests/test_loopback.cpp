// Integration tests of the one-way loopback with QPSK: modulator, simulated channel,
// receiver, bit counting. Requirements 6.1 to 6.6, 4.1.
//
// "High SNR" below is Es/N0 = 25 dB. At that SNR the ideal BER is far below 1e-100, so
// any bit error is the receiver's or the channel model's doing, not the noise's.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "dsp/modulator.hpp"
#include "dsp/rx_frontend.hpp"
#include "sim/channel.hpp"
#include "sim/loopback.hpp"
#include "test_harness.hpp"

using namespace mpsk::sim;

namespace {

constexpr double kSymbolRate = 250000.0;

const LoopbackLink& link() {
  static const LoopbackLink l{LoopbackConfig{}};
  return l;
}

ChannelConfig high_snr() {
  ChannelConfig c;
  c.esn0_db = 25.0;
  return c;
}

double mean_of(const std::vector<double>& v) {
  double s = 0;
  for (double x : v) s += x;
  return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}

/** Position the first frame's first preamble symbol should have in the front end's output. */
double expected_first_position(const LoopbackConfig& lc, double delay) {
  const mpsk::dsp::Modulator m(lc.modulator);
  return static_cast<double>(lc.lead_samples) + delay + static_cast<double>(m.first_symbol_peak_offset()) +
         static_cast<double>(mpsk::dsp::RxFrontEnd(mpsk::dsp::RxFrontEndConfig{}).group_delay());
}

void expect_clean(const LoopbackResult& r, const char* what) {
  if (r.frames_delivered != r.frames_sent || r.bit_errors != 0 || r.frames_payload_ok != r.frames_sent) {
    std::fprintf(stderr, "    case: %s: delivered %zu of %zu, payload ok %zu, errors %zu of %zu bits\n", what,
                 r.frames_delivered, r.frames_sent, r.frames_payload_ok, r.bit_errors, r.bits_compared);
  }
  CHECK(r.frames_delivered == r.frames_sent);
  CHECK(r.frames_payload_ok == r.frames_sent);
  CHECK(r.bit_errors == 0);
  CHECK(r.bits_compared == r.bits_sent);
}

}  // namespace

// ---- 6.3: no impairments ----------------------------------------------------

TEST(noiseless_loopback_has_exactly_zero_bit_errors_over_many_bits) {
  const LoopbackResult r = link().run(ChannelConfig{}, 20);
  CHECK(r.bits_compared >= 10000);
  CHECK(r.bits_compared == 20u * 128 * 8);
  CHECK(r.bit_errors == 0);
  CHECK(r.frames_delivered == 20);
  CHECK(r.frames_payload_ok == 20);
  CHECK(r.frames_lost == 0);
  CHECK(r.frames_detected == 20);
  CHECK(r.ber() == 0.0);
}

TEST(noiseless_loopback_is_error_free_for_other_payload_sizes_and_seeds) {
  for (const std::size_t bytes : {std::size_t{1}, std::size_t{16}, std::size_t{300}}) {
    LoopbackConfig lc;
    lc.payload_bytes = bytes;
    lc.prbs_seed = 12345u + static_cast<std::uint32_t>(bytes);
    const LoopbackLink l(lc);
    const LoopbackResult r = l.run(ChannelConfig{}, 40);
    expect_clean(r, "payload size");
  }
}

// ---- 6.4: carrier offset, random phase, fractional timing ------------------

TEST(carrier_offsets_of_1e3_and_1e2_of_the_symbol_rate_are_tracked) {
  const double phases[] = {0.37, 2.9, -1.7, 4.4};
  const double delays[] = {0.13, 0.5, 0.77, 17.31};
  int k = 0;
  for (const double fraction : {1e-3, 1e-2}) {
    for (const double sign : {1.0, -1.0}) {
      ChannelConfig c = high_snr();
      c.cfo_hz = sign * fraction * kSymbolRate;
      c.phase_rad = phases[k % 4];
      c.delay_samples = delays[k % 4];
      c.seed = 100 + static_cast<std::uint64_t>(k);
      ++k;
      const LoopbackResult r = link().run(c, 25);
      expect_clean(r, "carrier offset");
      // And the receiver's reading of the offset agrees with what was applied.
      std::vector<double> cfo;
      for (const FrameReport& f : r.frames) cfo.push_back(f.cfo_hz);
      CHECK_NEAR(mean_of(cfo), c.cfo_hz, 3.0);
    }
  }
}

TEST(every_phase_and_fractional_timing_decodes_at_both_offsets) {
  // A sweep over the phase circle and over a whole sample of delay, so no lucky pair hides a fault.
  for (int i = 0; i < 12; ++i) {
    ChannelConfig c = high_snr();
    c.cfo_hz = (i % 2 == 0 ? 1.0 : -1.0) * (i % 4 < 2 ? 250.0 : 2500.0);
    c.phase_rad = 2.0 * 3.14159265358979 * i / 12.0;
    c.delay_samples = i / 12.0;
    c.seed = 300 + static_cast<std::uint64_t>(i);
    expect_clean(link().run(c, 4), "phase/timing sweep");
  }
}

// ---- 6.5: small offset, sample-clock offset, slow drift ---------------------

TEST(a_small_offset_a_clock_offset_and_slow_drift_hold_lock_for_the_whole_capture) {
  // 100 frames, 0.3 s. The offset starts at 20 Hz (8e-5 of the symbol rate, under 1e-4), the clock
  // is 5 ppm off, the frequency drifts by 200 Hz/s (60 Hz over the capture) and the phase also
  // random-walks.
  ChannelConfig c = high_snr();
  c.cfo_hz = 20.0;
  c.clock_offset_ppm = 5.0;
  c.freq_drift_hz_per_s = 200.0;
  c.phase_walk_rad_per_sqrt_s = 2.0;
  c.phase_rad = 1.0;
  c.seed = 11;
  const LoopbackResult r = link().run(c, 100, 1, true);
  expect_clean(r, "small offset, ppm, drift");
  REQUIRE(r.frames.size() == 100);

  // Lock held through to the end: the last frames are as clean as the first.
  std::vector<double> first, last, cfo_first, cfo_last;
  for (int i = 0; i < 10; ++i) {
    first.push_back(r.frames[static_cast<std::size_t>(i)].evm_snr_db);
    last.push_back(r.frames[static_cast<std::size_t>(90 + i)].evm_snr_db);
    cfo_first.push_back(r.frames[static_cast<std::size_t>(i)].cfo_hz);
    cfo_last.push_back(r.frames[static_cast<std::size_t>(90 + i)].cfo_hz);
  }
  CHECK_NEAR(mean_of(last), mean_of(first), 2.0);
  CHECK(mean_of(last) > 20.0);
  // And the receiver saw the drift: the offset grew by 200 Hz/s times the time between
  // the middles of the first and last groups of ten frames.
  const mpsk::dsp::Modulator m(link().config().modulator);
  const double frame_period = static_cast<double>(
      m.burst_samples(mpsk::dsp::FrameType::Data, link().config().payload_bytes) + link().config().gap_samples);
  const double seconds_between = 90.0 * frame_period * 1e-6;   // group means are 90 frames apart
  CHECK_NEAR(mean_of(cfo_last) - mean_of(cfo_first), 200.0 * seconds_between, 6.0);
}

TEST(a_clock_offset_of_hundreds_of_ppm_over_a_capture_is_followed_frame_by_frame) {
  for (const double ppm : {-200.0, 50.0, 200.0}) {
    ChannelConfig c = high_snr();
    c.cfo_hz = 100.0;
    c.clock_offset_ppm = ppm;
    c.seed = 400;
    const LoopbackResult r = link().run(c, 60);
    expect_clean(r, "clock offset");
    // The receiver's reading has the right sign. (It is a poor measure of small offsets: see
    // DemodResult::clock_offset_ppm.)
    std::vector<double> reading;
    for (const FrameReport& f : r.frames) reading.push_back(f.clock_offset_ppm);
    if (ppm > 0) CHECK(mean_of(reading) > 0.2 * ppm);
    else CHECK(mean_of(reading) < 0.2 * ppm);
  }
}

// ---- 6.6: delay and amplitude ----------------------------------------------

TEST(frames_are_found_for_delays_from_none_to_over_a_frame_across_60_db_of_level) {
  // A frame is 2784 samples with its guard. 1234.5 is under half of one, 5000.3 is nearly two.
  for (const double delay : {0.0, 1234.5, 5000.3}) {
    for (const double gain : {20.0, 0.0, -20.0, -40.0}) {
      ChannelConfig c = high_snr();
      c.delay_samples = delay;
      c.gain_db = gain;
      c.seed = 500;
      const LoopbackResult r = link().run(c, 3);
      expect_clean(r, "delay/gain");
      REQUIRE(r.frames.size() == 3);
      CHECK_NEAR(r.frames[0].position, expected_first_position(link().config(), delay), 0.3);
    }
  }
}

TEST(a_delay_of_many_frames_is_still_found_at_the_right_place) {
  ChannelConfig c = high_snr();
  c.delay_samples = 30000.6;
  const LoopbackResult r = link().run(c, 2);
  expect_clean(r, "very long delay");
  REQUIRE(r.frames.size() == 2);
  CHECK_NEAR(r.frames[0].position, expected_first_position(link().config(), 30000.6), 0.3);
}

TEST(the_symbol_snr_the_receiver_measures_does_not_depend_on_the_level) {
  // Noise is set against the signal as it arrives, so 40 dB of attenuation takes the noise
  // with it. EVM-based SNR is 20 dB for Es/N0 = 20 dB whatever the gain.
  std::vector<double> snr;
  for (const double gain : {20.0, 0.0, -40.0}) {
    ChannelConfig c;
    c.esn0_db = 20.0;
    c.gain_db = gain;
    c.seed = 600;
    const LoopbackResult r = link().run(c, 30);
    std::vector<double> v;
    for (const FrameReport& f : r.frames) if (f.delivered) v.push_back(f.evm_snr_db);
    REQUIRE(v.size() >= 28);
    CHECK_NEAR(mean_of(v), 20.0, 0.5);
    snr.push_back(mean_of(v));
  }
  CHECK_NEAR(snr[0], snr[2], 0.3);
}

// ---- Accounting -------------------------------------------------------------

TEST(frames_are_reported_in_order_with_the_counters_that_were_sent) {
  const LoopbackResult r = link().run(ChannelConfig{}, 6, 1000);
  REQUIRE(r.frames.size() == 6);
  for (std::size_t i = 0; i < 6; ++i) {
    CHECK(r.frames[i].delivered);
    CHECK(r.frames[i].header_ok);
    CHECK(r.frames[i].payload_crc_ok);
    CHECK(!r.frames[i].truncated);
    CHECK(r.frames[i].counter == 1000 + i);
    CHECK(r.frames[i].bits_compared == 1024);
    if (i > 0) CHECK(r.frames[i].position > r.frames[i - 1].position);
  }
}

TEST(payload_crc_and_regenerated_prbs_agree_on_which_frames_have_errors) {
  // At Es/N0 = 10 dB the BER is 8e-4, so a 1024-bit payload has 0.8 errors on average and
  // about half of them are clean. The CRC-32 and the regenerated PRBS must agree on which,
  // with one allowed exception: an error in the 32 CRC bits themselves fails the CRC and
  // leaves the payload intact (32 of the 1056 bits, so about 3% of errors).
  ChannelConfig c;
  c.esn0_db = 10.0;
  c.seed = 700;
  const LoopbackResult r = link().run(c, 100);
  int with_errors = 0, delivered = 0, crc_only = 0;
  std::size_t errors = 0, bits = 0;
  for (const FrameReport& f : r.frames) {
    if (!f.delivered) continue;
    ++delivered;
    errors += f.bit_errors;
    bits += f.bits_compared;
    if (f.payload_crc_ok) CHECK(f.bit_errors == 0);        // a good CRC means a clean payload
    if (f.bit_errors > 0) CHECK(!f.payload_crc_ok);        // errors in the payload fail the CRC
    if (!f.payload_crc_ok && f.bit_errors == 0) ++crc_only;
    if (f.bit_errors > 0) ++with_errors;
  }
  CHECK(delivered >= 90);
  CHECK(with_errors >= 10);              // the test is only meaningful if both kinds occur
  CHECK(with_errors < delivered);
  CHECK(crc_only <= 3);
  CHECK(errors == r.bit_errors);
  CHECK(bits == r.bits_compared);
  CHECK(r.bits_compared == r.frames_delivered * 1024);
  CHECK(r.frames_lost == r.frames_sent - r.frames_delivered);
}

TEST(a_detected_frame_whose_header_fails_is_reported_but_not_delivered) {
  // Es/N0 = 3 dB (Eb/N0 0 dB): the preamble is found, the 72 unprotected header bits
  // rarely all survive.
  ChannelConfig c;
  c.esn0_db = 3.0;
  c.seed = 900;
  const LoopbackResult r = link().run(c, 60);
  int failed_headers = 0;
  for (const FrameReport& f : r.frames) {
    if (f.header_ok) continue;
    ++failed_headers;
    CHECK(!f.delivered);
    CHECK(f.bits_compared == 0);
    CHECK(f.bit_errors == 0);
    CHECK(!f.payload_crc_ok);
  }
  CHECK(failed_headers >= 10);
  CHECK(r.frames_detected == r.frames.size());
  CHECK(r.frames_detected >= r.frames_delivered + static_cast<std::size_t>(failed_headers));
  CHECK(r.frames_delivered >= 5);
}

TEST(frames_lost_in_noise_are_counted_and_contribute_no_bits) {
  ChannelConfig c;
  c.esn0_db = -12.0;
  c.seed = 800;
  const LoopbackResult r = link().run(c, 20);
  CHECK(r.frames_sent == 20);
  CHECK(r.frames_lost == 20 - r.frames_delivered);
  CHECK(r.bits_compared == r.frames_delivered * 1024);
  CHECK(r.frames_delivered <= 2);
  CHECK(r.bits_sent == 20u * 1024);
}

TEST(the_transmitted_and_received_records_are_kept_only_on_request) {
  ChannelConfig c;
  c.delay_samples = 700.0;
  const LoopbackResult plain = link().run(c, 3);
  CHECK(plain.transmitted.empty());
  CHECK(plain.received.empty());

  const LoopbackResult kept = link().run(c, 3, 1, true);
  const LoopbackConfig& lc = link().config();
  const mpsk::dsp::Modulator m(lc.modulator);
  const std::size_t burst = m.burst_samples(mpsk::dsp::FrameType::Data, lc.payload_bytes);
  CHECK(kept.transmitted.size() == lc.lead_samples + 3 * burst + 2 * lc.gap_samples + lc.tail_samples);
  CHECK(kept.received.size() >= kept.transmitted.size() + 700);
  CHECK(kept.frames_delivered == 3);
}

TEST(the_same_channel_seed_gives_the_same_run_and_another_gives_another) {
  ChannelConfig c;
  c.esn0_db = 5.0;
  c.seed = 42;
  const LoopbackResult a = link().run(c, 20);
  const LoopbackResult b = link().run(c, 20);
  c.seed = 43;
  const LoopbackResult d = link().run(c, 20);
  CHECK(a.bit_errors == b.bit_errors);
  CHECK(a.frames_delivered == b.frames_delivered);
  REQUIRE(a.frames.size() == b.frames.size());
  for (std::size_t i = 0; i < a.frames.size(); ++i) CHECK(a.frames[i].evm == b.frames[i].evm);
  CHECK(a.bit_errors != d.bit_errors || a.frames_delivered != d.frames_delivered);
}

TEST(the_channel_is_told_the_modulators_samples_per_symbol) {
  // A channel config that says 8 samples per symbol must not set the noise for 8: the link
  // overrides it. The same seed with either setting must give the same run.
  ChannelConfig a;
  a.esn0_db = 6.0;
  a.seed = 9;
  ChannelConfig b = a;
  b.samples_per_symbol = 8;
  const LoopbackResult ra = link().run(a, 10);
  const LoopbackResult rb = link().run(b, 10);
  CHECK(ra.bit_errors == rb.bit_errors);
  CHECK(ra.frames_delivered == rb.frames_delivered);
}

// ---- Bad requests -----------------------------------------------------------

TEST(bad_requests_are_refused_with_a_reason) {
  CHECK_THROWS(link().run(ChannelConfig{}, 0));
  CHECK_THROWS(link().run(ChannelConfig{}, 10, 65530));   // would pass 65535
  CHECK_THROWS(link().run(ChannelConfig{}, 6, 65531));    // ends at 65536: one too many
  CHECK(link().run(ChannelConfig{}, 6, 65530).frames_delivered == 6);   // exactly to 65535 is allowed

  LoopbackConfig zero;
  zero.payload_bytes = 0;
  CHECK_THROWS(LoopbackLink{zero});
  LoopbackConfig huge;
  huge.payload_bytes = 4097;
  CHECK_THROWS(LoopbackLink{huge});
  LoopbackConfig biggest;
  biggest.payload_bytes = 4096;   // the framer's limit is allowed
  CHECK(LoopbackLink{biggest}.config().payload_bytes == 4096);

  LoopbackConfig mismatch;
  mismatch.receiver = mpsk::dsp::ReceiverConfig::for_pulse(4, 0.5, 11);   // modulator has 0.35
  try {
    LoopbackLink l{mismatch};
    CHECK(false);
  } catch (const std::invalid_argument& e) {
    CHECK(std::string(e.what()).find("differs") != std::string::npos);
  }

  LoopbackConfig same;
  same.receiver = mpsk::dsp::ReceiverConfig::for_pulse(4, 0.35, 11);   // spelled out: fine
  CHECK(LoopbackLink{same}.run(ChannelConfig{}, 2).frames_delivered == 2);
}

MPSK_TEST_MAIN()
