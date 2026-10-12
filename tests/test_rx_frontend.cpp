// Tests for the receive front end (DC blocker followed by the matched filter).
// Requirements 3.1 (DC removal) and 3.2 (matched filter).

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/dc_blocker.hpp"
#include "dsp/fir.hpp"
#include "dsp/framer.hpp"
#include "dsp/modulator.hpp"
#include "dsp/prbs.hpp"
#include "dsp/rrc.hpp"
#include "dsp/rx_frontend.hpp"
#include "rx_scenario.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using mpsk::sim::add_noise;
using mpsk::sim::mean_power;
using mpsk::sim::NoiseSource;
using cf = std::complex<float>;

TEST(the_front_end_is_the_dc_blocker_followed_by_the_matched_filter) {
  NoiseSource rng(1);
  std::vector<cf> x(3000, cf(0.2f, -0.1f));
  add_noise(x, 0.05, rng);

  RxFrontEndConfig cfg;
  RxFrontEnd fe(cfg);
  const auto got = fe.process(x);

  DcBlocker dc(cfg.dc_pole);
  std::vector<cf> stage(x.size());
  dc.process(x.data(), stage.data(), x.size());
  FirFilter f(rrc_taps(cfg.rolloff, cfg.samples_per_symbol, cfg.span_symbols));
  const auto want = f.process(stage);

  for (std::size_t i = 0; i < x.size(); ++i) CHECK(std::abs(got[i] - want[i]) < 1e-6f);
}

TEST(the_group_delay_and_taps_follow_the_configuration) {
  RxFrontEndConfig cfg;
  cfg.samples_per_symbol = 8;
  cfg.span_symbols = 10;
  cfg.rolloff = 0.5;
  RxFrontEnd fe(cfg);
  CHECK(fe.group_delay() == 40);
  CHECK(fe.taps().size() == 81);
  CHECK(fe.taps() == rrc_taps(0.5, 8, 10));
}

TEST(a_noiseless_burst_is_recovered_at_the_symbol_instants) {
  // End to end through the real transmitter: symbol k of the frame is read at
  // position + k * sps, scaled by the transmit scale. Measured error: 0.6% at the
  // default settings, from the finite RRC span.
  rx_test::Scenario s;
  s.add_noise = false;
  const rx_test::Received r = rx_test::make_received(s);
  const auto expected = build_frame_symbols(FrameType::Data, 4, 1, prbs_bytes(3, 1, 16));

  const std::size_t first = static_cast<std::size_t>(std::llround(r.true_position));
  double worst = 0.0;
  for (std::size_t k = 0; k < expected.size(); ++k) {
    const cf got = r.y[first + k * rx_test::kSps] / static_cast<float>(rx_test::modulator().config().output_scale);
    worst = std::max(worst, static_cast<double>(std::abs(got - expected[k])));
  }
  CHECK(worst < 0.03);
}

TEST(a_large_dc_offset_does_not_disturb_the_recovered_symbols) {
  rx_test::Scenario clean;
  clean.add_noise = false;
  rx_test::Scenario offset = clean;
  offset.dc = cf(0.8f, -0.6f);   // larger than the signal itself
  const auto a = rx_test::make_received(clean);
  const auto b = rx_test::make_received(offset);

  // Every symbol instant of the frame. The count comes from the frame itself: reading
  // past the end of the record would be undefined behaviour that no check can catch.
  const std::size_t first = static_cast<std::size_t>(std::llround(a.true_position));
  const std::size_t symbols = frame_symbol_count(FrameType::Data, 4, 16);
  REQUIRE(first + (symbols - 1) * rx_test::kSps < a.y.size());
  REQUIRE(a.y.size() == b.y.size());
  double worst = 0.0;
  for (std::size_t k = 0; k < symbols; ++k) {
    const std::size_t i = first + k * rx_test::kSps;
    worst = std::max(worst, static_cast<double>(std::abs(a.y[i] - b.y[i])));
  }
  CHECK(worst < 0.03);
}

TEST(noise_through_the_front_end_has_unit_gain_even_on_a_short_record) {
  // The regression test for a start-up fault. When the DC blocker primed itself from
  // a single noisy sample, a short record came out with 8x too much noise (0.2007
  // against 0.025 injected). With the prime taken from a 256-sample mean the gain is
  // 0.96 here. Averaged over seeds so one realisation cannot decide it.
  const double variance = 0.025;
  double gain = 0.0;
  const int runs = 100;
  for (int seed = 1; seed <= runs; ++seed) {
    NoiseSource rng(static_cast<std::uint64_t>(seed));
    std::vector<cf> n(1733, cf(0, 0));
    add_noise(n, variance, rng);
    RxFrontEnd fe{RxFrontEndConfig{}};
    const auto y = fe.process(n);
    gain += mean_power(y, 100, y.size()) / variance;
  }
  gain /= runs;
  CHECK_NEAR(gain, 1.0, 0.06);
}

TEST(steady_state_noise_gain_is_one_for_a_long_run) {
  NoiseSource rng(2);
  std::vector<cf> n(300000, cf(0, 0));
  add_noise(n, 1.0, rng);
  RxFrontEnd fe{RxFrontEndConfig{}};
  const auto y = fe.process(n);
  CHECK_NEAR(mean_power(y, 50000, y.size()), 1.0, 0.03);
}

TEST(reset_gives_the_same_output_as_a_fresh_front_end) {
  NoiseSource rng(3);
  std::vector<cf> a(2000, cf(0.5f, 0.2f)), b(2000, cf(-0.3f, 0.4f));
  add_noise(a, 0.02, rng);
  add_noise(b, 0.02, rng);

  RxFrontEnd reused{RxFrontEndConfig{}};
  reused.process(a);
  reused.reset();
  const auto after_reset = reused.process(b);

  RxFrontEnd fresh{RxFrontEndConfig{}};
  const auto from_fresh = fresh.process(b);
  for (std::size_t i = 0; i < b.size(); ++i) CHECK(std::abs(after_reset[i] - from_fresh[i]) < 1e-6f);
}

TEST(samples_processed_counts_and_reset_clears_it) {
  RxFrontEnd fe{RxFrontEndConfig{}};
  CHECK(fe.samples_processed() == 0);
  fe.process(std::vector<cf>(1000, cf(1, 0)));
  fe.process(std::vector<cf>(234, cf(1, 0)));
  CHECK(fe.samples_processed() == 1234);
  fe.reset();
  CHECK(fe.samples_processed() == 0);
}

TEST(in_place_processing_matches_out_of_place) {
  NoiseSource rng(4);
  std::vector<cf> x(2500, cf(0.1f, 0.1f));
  add_noise(x, 0.1, rng);
  RxFrontEnd a{RxFrontEndConfig{}};
  const auto expected = a.process(x);
  RxFrontEnd b{RxFrontEndConfig{}};
  std::vector<cf> inplace = x;
  b.process(inplace.data(), inplace.data(), inplace.size());
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(std::abs(inplace[i] - expected[i]) < 1e-6f);
}

TEST(bad_configurations_are_rejected) {
  RxFrontEndConfig c;
  c.samples_per_symbol = 3;   // 3 * 11 is odd
  CHECK_THROWS(RxFrontEnd{c});
  c = RxFrontEndConfig{};
  c.rolloff = 2.0;
  CHECK_THROWS(RxFrontEnd{c});
  c = RxFrontEndConfig{};
  c.dc_pole = 1.0;
  CHECK_THROWS(RxFrontEnd{c});
}

MPSK_TEST_MAIN()
