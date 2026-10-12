// Tests for the block receiver: finding and decoding every frame in a record.
// Requirements 3.1 to 3.7 as seen from the outside, 4.3 (a lost frame is reported).

#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dsp/framer.hpp"
#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "dsp/receiver.hpp"
#include "dsp/rx_frontend.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cf = std::complex<float>;

namespace {

constexpr std::uint32_t kSeed = 11;

const Modulator& modulator() {
  static const Modulator m{ModulatorConfig{}};
  return m;
}

const Receiver& receiver() {
  static const Receiver r{ReceiverConfig::for_pulse(4, 0.35, 11)};
  return r;
}

struct Stream {
  std::vector<cf> samples;
  std::vector<std::size_t> burst_start;     ///< index of each burst's first sample
  std::vector<std::size_t> burst_length;
};

/** Frames {counter, payload bytes} one after another, @p gap samples of silence between. */
Stream build_stream(const std::vector<std::pair<std::uint16_t, std::size_t>>& frames, std::size_t gap,
                    std::size_t lead = 300, std::size_t tail = 600) {
  Stream s;
  s.samples.assign(lead, cf(0, 0));
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const auto burst = modulator().modulate(FrameType::Data, frames[i].first,
                                            prbs_bytes(kSeed, frames[i].first, frames[i].second));
    s.burst_start.push_back(s.samples.size());
    s.burst_length.push_back(burst.samples.size());
    s.samples.insert(s.samples.end(), burst.samples.begin(), burst.samples.end());
    s.samples.insert(s.samples.end(), i + 1 < frames.size() ? gap : tail, cf(0, 0));
  }
  return s;
}

double expected_position(const Stream& s, std::size_t k) {
  return static_cast<double>(s.burst_start[k] + modulator().first_symbol_peak_offset()) +
         static_cast<double>(receiver().group_delay());
}

}  // namespace

TEST(every_frame_in_a_record_is_found_in_order_with_its_payload) {
  const std::vector<std::pair<std::uint16_t, std::size_t>> sent = {
      {5, 128}, {6, 0}, {7, 1}, {900, 64}, {65535, 300}};
  const Stream s = build_stream(sent, 400);
  const auto frames = receiver().receive(s.samples);
  REQUIRE(frames.size() == sent.size());
  for (std::size_t k = 0; k < sent.size(); ++k) {
    const DecodedFrame& f = frames[k].demod.frame;
    CHECK(f.header_ok);
    CHECK(f.payload_crc_ok);
    CHECK(f.header.frame_counter == sent[k].first);
    CHECK(f.payload == prbs_bytes(kSeed, sent[k].first, sent[k].second));
    CHECK(!frames[k].demod.truncated);
    CHECK_NEAR(frames[k].estimate.position, expected_position(s, k), 0.3);
  }
}

TEST(frames_with_only_their_own_guard_between_them_are_all_found) {
  // No extra silence: just the 64 samples of guard each burst carries at either end.
  const std::vector<std::pair<std::uint16_t, std::size_t>> sent = {{1, 128}, {2, 128}, {3, 128}, {4, 128}};
  const Stream s = build_stream(sent, 0);
  const auto frames = receiver().receive(s.samples);
  REQUIRE(frames.size() == 4);
  for (std::size_t k = 0; k < 4; ++k) {
    CHECK(frames[k].demod.frame.payload_crc_ok);
    CHECK(frames[k].demod.frame.header.frame_counter == k + 1);
  }
}

TEST(the_frames_are_found_where_they_are_whatever_the_spacing) {
  // Position differences equal the transmitted spacing, so nothing is dropped or reread.
  const Stream s = build_stream({{1, 64}, {2, 64}, {3, 64}}, 1234);
  const auto frames = receiver().receive(s.samples);
  REQUIRE(frames.size() == 3);
  CHECK_NEAR(frames[1].estimate.position - frames[0].estimate.position,
             static_cast<double>(s.burst_length[0] + 1234), 0.3);
  CHECK_NEAR(frames[2].estimate.position - frames[1].estimate.position,
             static_cast<double>(s.burst_length[1] + 1234), 0.3);
}

TEST(an_empty_record_and_a_record_of_silence_have_no_frames) {
  CHECK(receiver().receive({}).empty());
  CHECK(receiver().receive(std::vector<cf>(5000, cf(0, 0))).empty());
}

TEST(noise_alone_has_no_frames_and_every_stage_one_crossing_is_refused) {
  mpsk::sim::NoiseSource rng(77);
  std::vector<cf> x(300000, cf(0, 0));
  mpsk::sim::add_noise(x, 0.1, rng);
  AcquisitionStats stats;
  CHECK(receiver().receive(x, &stats).empty());
  CHECK(stats.positions_scanned > 290000);   // it did look at nearly every position
  // Measured for this seed: 9 stage-1 crossings (P = 2.3e-5 per position predicts 7), all
  // refused by the coherent test, the worst reaching V = 0.184 against the threshold of 0.30.
  CHECK(stats.candidates >= 3);
  CHECK(stats.rejected == stats.candidates);
  CHECK(stats.max_rejected_verify > 0.05);
  CHECK(stats.max_rejected_verify < 0.30);
}

TEST(the_acquisition_counters_are_added_up_over_the_scan) {
  const Stream s = build_stream({{1, 64}, {2, 64}, {3, 64}}, 500);
  AcquisitionStats stats;
  const auto frames = receiver().receive(s.samples, &stats);
  REQUIRE(frames.size() == 3);
  // Measured: 2113 of 7303 positions. Each frame's own samples are skipped once it is
  // decoded, so only the lead-in, the gaps and the preamble itself are scanned. Scanning
  // from inside the frame, or from the start again, would show up here.
  CHECK(stats.positions_scanned > 1500);
  CHECK(stats.positions_scanned < 0.35 * static_cast<double>(s.samples.size()));
  CHECK(stats.candidates >= 3);
  CHECK(stats.rejected + 3 == stats.candidates);   // three accepted, the rest were sidelobes
  CHECK(stats.rejected > 0);
  CHECK(stats.max_rejected_verify > 0.0);
}

TEST(a_frame_with_a_destroyed_header_is_reported_lost_and_the_next_is_still_found) {
  Stream s = build_stream({{1, 64}, {2, 64}, {3, 64}}, 400);
  // Noise of rms 1.0 over the header (symbols 64 to 135) of the middle frame, where the
  // signal has rms 0.5 / 2 = 0.25 per sample: the header cannot survive that.
  mpsk::sim::NoiseSource rng(5);
  const std::size_t first_peak = s.burst_start[1] + modulator().first_symbol_peak_offset();
  for (std::size_t i = first_peak + 64 * 4; i < first_peak + 136 * 4; ++i) s.samples[i] += rng.complex_noise(1.0);
  const auto frames = receiver().receive(s.samples);
  REQUIRE(frames.size() == 3);
  CHECK(frames[0].demod.frame.payload_crc_ok);
  CHECK(frames[1].demod.lost());
  CHECK(frames[2].demod.frame.payload_crc_ok);
  CHECK(frames[2].demod.frame.header.frame_counter == 3);
}

TEST(a_record_that_ends_inside_a_frame_reports_it_truncated_and_stops) {
  Stream s = build_stream({{1, 128}, {2, 128}}, 400);
  const std::size_t first_peak = s.burst_start[1] + modulator().first_symbol_peak_offset();
  s.samples.resize(first_peak + (64 + 72 + 100) * 4);   // inside the second frame's payload
  const auto frames = receiver().receive(s.samples);
  REQUIRE(frames.size() == 2);
  CHECK(frames[0].demod.frame.payload_crc_ok);
  CHECK(frames[1].demod.frame.header_ok);
  CHECK(frames[1].demod.truncated);
  CHECK(!frames[1].demod.frame.payload_crc_ok);
}

TEST(receiving_the_same_record_twice_gives_the_same_frames) {
  const Stream s = build_stream({{1, 64}, {2, 64}}, 400);
  const auto a = receiver().receive(s.samples);
  const auto b = receiver().receive(s.samples);
  REQUIRE(a.size() == b.size());
  for (std::size_t k = 0; k < a.size(); ++k) {
    CHECK(a[k].estimate.position == b[k].estimate.position);
    CHECK(a[k].demod.frame.payload == b[k].demod.frame.payload);
    CHECK(a[k].demod.evm == b[k].demod.evm);
  }
}

TEST(the_group_delay_is_the_front_ends) {
  CHECK(receiver().group_delay() == RxFrontEnd(RxFrontEndConfig{}).group_delay());
  CHECK(receiver().group_delay() > 0);
}

TEST(a_receiver_whose_parts_disagree_on_the_pulse_is_refused) {
  ReceiverConfig a = ReceiverConfig::for_pulse(4, 0.35, 11);
  a.acquisition.samples_per_symbol = 8;
  CHECK_THROWS(Receiver{a});
  ReceiverConfig b = ReceiverConfig::for_pulse(4, 0.35, 11);
  b.demod.rolloff = 0.5;
  CHECK_THROWS(Receiver{b});
  ReceiverConfig c = ReceiverConfig::for_pulse(4, 0.35, 11);
  c.demod.samples_per_symbol = 2;
  CHECK_THROWS(Receiver{c});
  ReceiverConfig d = ReceiverConfig::for_pulse(4, 0.35, 11);
  d.demod.span_symbols = 9;
  CHECK_THROWS(Receiver{d});
  // The message names the fix.
  try {
    Receiver r{a};
  } catch (const std::invalid_argument& e) {
    CHECK(std::string(e.what()).find("for_pulse") != std::string::npos);
  }
}

TEST(for_pulse_sets_every_part_the_same) {
  const ReceiverConfig c = ReceiverConfig::for_pulse(8, 0.25, 9);
  CHECK(c.front_end.samples_per_symbol == 8);
  CHECK(c.acquisition.samples_per_symbol == 8);
  CHECK(c.demod.samples_per_symbol == 8);
  CHECK(c.front_end.rolloff == 0.25);
  CHECK(c.demod.rolloff == 0.25);
  CHECK(c.front_end.span_symbols == 9);
  CHECK(c.demod.span_symbols == 9);
}

MPSK_TEST_MAIN()
