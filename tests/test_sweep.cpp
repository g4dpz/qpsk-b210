// The AWGN sweep (task 8.4, requirement 6.8): bit error rate against Eb/N0 through the
// whole simulated link, judged against QPSK theory.
//
// What a point can prove depends on how many errors it saw. With 50 or more, the measured
// rate is within about 15% (one standard deviation 14%), which is 0.2 dB of Eb/N0 here, so
// the receiver's loss against theory can be read off. With fewer, only an upper bound
// is meaningful: the receiver is not worse than 1 dB, but the point does not measure how
// much better it is.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "sim/sweep.hpp"
#include "sim/theory.hpp"
#include "test_harness.hpp"

using namespace mpsk::sim;

namespace {

/** Upper bound on the errors in n bits if the receiver were 1 dB worse than theory, plus 5 sigma. */
double errors_allowed_at_1db_loss(const SweepPoint& p) {
  const double expected = mpsk_ber_theory(4, p.ebn0_db - 1.0) * static_cast<double>(p.bits_compared);
  return expected + 5.0 * std::sqrt(expected) + 5.0;
}

void print_table(const std::vector<SweepPoint>& points) {
  std::fprintf(stderr, "    Eb/N0   BER measured   errors/bits         theory    loss dB   delivered\n");
  for (const SweepPoint& p : points) {
    const auto loss = p.loss_db(4);
    char l[16] = "    n/a";
    if (loss) std::snprintf(l, sizeof l, "%7.2f", *loss);
    std::fprintf(stderr, "    %5.1f   %12.3e   %6zu/%-9zu %11.3e   %s   %zu/%zu\n", p.ebn0_db, p.ber(),
                 p.bit_errors, p.bits_compared, p.theory_ber, l, p.frames_delivered, p.frames_sent);
  }
}

SweepConfig standard_sweep() {
  SweepConfig c;
  c.ebn0_db = {0, 2, 4, 6, 8, 10, 12};
  c.target_errors = 100;
  c.max_bits = 600000;
  c.frames_per_run = 50;
  c.seed = 1;
  return c;
}

}  // namespace

TEST(the_sweep_from_0_to_12_db_is_monotonic_and_within_1_db_of_qpsk_theory) {
  const SweepConfig cfg = standard_sweep();
  const std::vector<SweepPoint> pts = run_sweep(cfg);
  REQUIRE(pts.size() == 7);
  bool any_failed = false;

  int measured_points = 0;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const SweepPoint& p = pts[i];
    CHECK_NEAR(p.ebn0_db, cfg.ebn0_db[i], 1e-12);
    CHECK_NEAR(p.esn0_db, p.ebn0_db + 3.0103, 1e-3);
    CHECK_NEAR(p.theory_ber, mpsk_ber_theory(4, p.ebn0_db), 1e-15);
    CHECK(p.bits_sent >= 1);

    if (p.bit_errors >= 50) {
      ++measured_points;
      const auto loss = p.loss_db(4);
      REQUIRE(loss.has_value());
      // Not better than theory beyond what the count of errors allows, and not 1 dB worse.
      if (!(*loss > -0.5 && *loss < 1.0)) any_failed = true;
      CHECK(*loss > -0.5);
      CHECK(*loss < 1.0);
    } else {
      if (!(p.bit_errors <= errors_allowed_at_1db_loss(p))) any_failed = true;
      CHECK(static_cast<double>(p.bit_errors) <= errors_allowed_at_1db_loss(p));
    }

    // Monotonic: never rises with SNR, and falls clearly between points that both measured.
    if (i > 0) {
      CHECK(p.ber() <= pts[i - 1].ber());
      if (p.bit_errors >= 50 && pts[i - 1].bit_errors >= 50) CHECK(p.ber() < 0.8 * pts[i - 1].ber());
    }
  }
  CHECK(measured_points >= 4);   // 0, 2, 4, 6 and 8 dB all collect their errors in the bit cap
  if (any_failed) print_table(pts);
}

TEST(a_point_stops_on_the_error_target_or_the_bit_cap_whichever_comes_first) {
  SweepConfig c;
  c.ebn0_db = {0.0, 12.0};
  c.target_errors = 100;
  c.max_bits = 102400;      // exactly 20 runs of 5 frames: a cap reached exactly stops the point
  c.frames_per_run = 5;
  const std::vector<SweepPoint> pts = run_sweep(c);
  REQUIRE(pts.size() == 2);

  // At 0 dB errors come fast: stops on the target, well under the cap, within one run of it.
  CHECK(pts[0].reached_target);
  CHECK(pts[0].bit_errors >= 100);
  CHECK(pts[0].bits_sent < c.max_bits);
  CHECK(pts[0].runs >= 1);
  // The run that crosses the target is the last: one fewer run was short of it.
  CHECK(pts[0].bit_errors < 100 + 5 * 1024);

  // At 12 dB there are none: runs until the cap, in whole runs.
  CHECK(!pts[1].reached_target);
  CHECK(pts[1].bits_sent == c.max_bits);
  CHECK(pts[1].runs == 20);
  // Every frame arrives clean at 12 dB, and not at 0 dB.
  CHECK(pts[1].frames_delivered == pts[1].frames_sent);
  CHECK(pts[1].frames_payload_ok == pts[1].frames_sent);
  CHECK(pts[1].bits_compared == pts[1].bits_sent);
  CHECK_NEAR(pts[1].frame_loss(), 0.0, 1e-12);
  CHECK(pts[0].frames_delivered > 0);
  CHECK(pts[0].frames_delivered < pts[0].frames_sent);
  CHECK(pts[0].frames_payload_ok < pts[0].frames_delivered);
  CHECK(pts[0].frame_loss() > 0.3);
  CHECK(pts[0].bits_compared == pts[0].frames_delivered * 1024);
  CHECK(pts[1].frames_sent == pts[1].runs * 5);
}

TEST(every_run_of_every_point_gets_its_own_noise_and_its_own_payloads) {
  // Noise seeds: all different over a grid of points and runs, and moved by the base seed.
  std::vector<std::uint64_t> seeds;
  for (std::size_t p = 0; p < 20; ++p) {
    for (std::size_t r = 0; r < 500; ++r) seeds.push_back(sweep_run_seed(1, p, r));
  }
  std::sort(seeds.begin(), seeds.end());
  CHECK(std::adjacent_find(seeds.begin(), seeds.end()) == seeds.end());
  CHECK(sweep_run_seed(1, 3, 4) != sweep_run_seed(2, 3, 4));

  // Payloads: consecutive runs use consecutive, non-overlapping counter ranges, inside 16 bits.
  const std::size_t per_run = 50;
  std::size_t sent = 0;
  std::uint16_t last_end = 0;
  for (int run = 0; run < 40; ++run) {
    const std::uint16_t first = sweep_first_counter(sent);
    CHECK(first >= 1);
    CHECK(static_cast<std::size_t>(first) + per_run - 1 <= 65535);
    if (run > 0 && sent % 50000 != 0) CHECK(first > last_end);
    last_end = static_cast<std::uint16_t>(first + per_run - 1);
    sent += per_run;
  }
  // Even at the largest run size the range stays inside the counter, wherever the point is.
  for (std::size_t before = 0; before < 200000; before += 997) {
    CHECK(sweep_first_counter(before) >= 1);
    CHECK(static_cast<std::size_t>(sweep_first_counter(before)) + 5000 - 1 <= 65535);
  }
  CHECK(sweep_first_counter(0) == 1);
  CHECK(sweep_first_counter(50000) == 1);   // and it cycles
}

TEST(a_sweep_is_repeatable_and_the_seed_changes_it) {
  SweepConfig c;
  c.ebn0_db = {2.0, 6.0};
  c.max_bits = 50000;
  const auto a = run_sweep(c);
  const auto b = run_sweep(c);
  c.seed = 2;
  const auto d = run_sweep(c);
  REQUIRE(a.size() == 2 && b.size() == 2 && d.size() == 2);
  for (std::size_t i = 0; i < 2; ++i) {
    CHECK(a[i].bit_errors == b[i].bit_errors);
    CHECK(a[i].bits_compared == b[i].bits_compared);
    CHECK(a[i].frames_delivered == b[i].frames_delivered);
  }
  CHECK(a[0].bit_errors != d[0].bit_errors || a[1].bit_errors != d[1].bit_errors);
}

TEST(the_curve_holds_with_a_carrier_offset_a_delay_and_a_clock_offset_in_the_channel) {
  // The impairments apply at every point of the sweep, so the curve shows what they cost.
  SweepConfig c;
  c.ebn0_db = {4.0, 6.0, 8.0};
  c.max_bits = 400000;
  c.channel.cfo_hz = 1500.0;
  c.channel.delay_samples = 345.678;
  c.channel.clock_offset_ppm = 20.0;
  c.channel.phase_rad = 2.0;
  const std::vector<SweepPoint> pts = run_sweep(c);
  REQUIRE(pts.size() == 3);
  for (const SweepPoint& p : pts) {
    if (p.bit_errors >= 50) {
      const auto loss = p.loss_db(4);
      REQUIRE(loss.has_value());
      CHECK(*loss > -0.5);
      CHECK(*loss < 1.0);
    } else {
      CHECK(static_cast<double>(p.bit_errors) <= errors_allowed_at_1db_loss(p));
    }
  }
  CHECK(pts[0].ber() > pts[1].ber());
  CHECK(pts[1].ber() > pts[2].ber());
}

TEST(a_point_reports_its_loss_against_theory_in_db) {
  SweepPoint p;
  p.ebn0_db = 6.0;
  p.bits_compared = 1000000;
  // A receiver that needs 0.7 dB more than theory sees the BER theory gives at 5.3 dB.
  p.bit_errors = static_cast<std::size_t>(std::llround(mpsk_ber_theory(4, 5.3) * 1e6));
  const auto loss = p.loss_db(4);
  REQUIRE(loss.has_value());
  CHECK_NEAR(*loss, 0.7, 0.01);
  // One that matches theory has no loss, and one that beats it has a negative loss.
  p.bit_errors = static_cast<std::size_t>(std::llround(mpsk_ber_theory(4, 6.0) * 1e6));
  CHECK_NEAR(*p.loss_db(4), 0.0, 0.01);
  p.bit_errors = static_cast<std::size_t>(std::llround(mpsk_ber_theory(4, 6.4) * 1e6));
  CHECK_NEAR(*p.loss_db(4), -0.4, 0.01);
}

TEST(a_point_with_no_errors_or_no_comparable_bits_has_no_loss_figure) {
  SweepPoint none;
  none.ebn0_db = 10.0;
  none.bits_compared = 1000000;
  none.bit_errors = 0;
  CHECK(!none.loss_db(4).has_value());
  CHECK(none.ber() == 0.0);

  SweepPoint empty;
  CHECK(empty.ber() == 0.0);
  CHECK(!empty.loss_db(4).has_value());
  CHECK(empty.frame_loss() == 0.0);

  SweepPoint coin_toss;
  coin_toss.ebn0_db = -5.0;
  coin_toss.bits_compared = 1000;
  coin_toss.bit_errors = 500;   // BER 0.5: no better than guessing, the curve has no inverse there
  CHECK(!coin_toss.loss_db(4).has_value());
}

TEST(the_frame_loss_fraction_counts_undelivered_frames) {
  SweepPoint p;
  p.frames_sent = 50;
  p.frames_delivered = 20;
  CHECK_NEAR(p.frame_loss(), 0.6, 1e-12);
  p.frames_delivered = 50;
  CHECK_NEAR(p.frame_loss(), 0.0, 1e-12);
}

TEST(the_progress_callback_sees_each_point_as_it_finishes_and_in_order) {
  SweepConfig c;
  c.ebn0_db = {1.0, 3.0, 5.0};
  c.max_bits = 20000;
  std::vector<SweepPoint> seen;
  std::size_t returned_when_called = 0;
  c.on_point = [&](const SweepPoint& p) { seen.push_back(p); returned_when_called = seen.size(); };
  const std::vector<SweepPoint> pts = run_sweep(c);
  REQUIRE(seen.size() == 3);
  REQUIRE(pts.size() == 3);
  CHECK(returned_when_called == 3);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK_NEAR(seen[i].ebn0_db, c.ebn0_db[i], 1e-12);
    CHECK(seen[i].bit_errors == pts[i].bit_errors);
    CHECK(seen[i].bits_compared == pts[i].bits_compared);
  }
  // A callback that throws ends the sweep and the exception reaches the caller.
  int calls = 0;
  c.on_point = [&](const SweepPoint&) {
    if (++calls == 2) throw std::runtime_error("stop here");
  };
  CHECK_THROWS(run_sweep(c));
  CHECK(calls == 2);
}

TEST(a_sweep_with_unusable_settings_is_refused) {
  SweepConfig c;
  c.ebn0_db.clear();
  CHECK_THROWS(run_sweep(c));
  c = SweepConfig{};
  c.target_errors = 0;
  CHECK_THROWS(run_sweep(c));
  c = SweepConfig{};
  c.max_bits = 0;
  CHECK_THROWS(run_sweep(c));
  c = SweepConfig{};
  c.frames_per_run = 0;
  CHECK_THROWS(run_sweep(c));
  c = SweepConfig{};
  c.frames_per_run = 5001;
  CHECK_THROWS(run_sweep(c));
  c = SweepConfig{};
  c.link.payload_bytes = 0;
  CHECK_THROWS(run_sweep(c));
  c = SweepConfig{};
  c.channel.delay_samples = -3.0;   // the channel refuses it when the first run starts
  CHECK_THROWS(run_sweep(c));
}

MPSK_TEST_MAIN()
