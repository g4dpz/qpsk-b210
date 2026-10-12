// Tests for the DC blocker. The start-up behaviour is the point of this file:
// a zero start leaves most of a DC offset across the first frame, a one-sample
// prime adds noise, and priming from a mean of samples does neither. The figures
// asserted here were measured (see dc_blocker.hpp), not derived.

#include <cmath>
#include <complex>
#include <vector>

#include "dsp/dc_blocker.hpp"
#include "sim/channel_ops.hpp"
#include "test_harness.hpp"

using mpsk::dsp::DcBlocker;
using mpsk::sim::add_noise;
using mpsk::sim::mean_power;
using mpsk::sim::NoiseSource;
using cf = std::complex<float>;

namespace {

/** The blocker run from a ZERO state, as an unprimed implementation would behave. */
std::vector<cf> run_from_zero(const std::vector<cf>& x, double a) {
  std::vector<cf> y(x.size());
  std::complex<double> xp(0, 0), yp(0, 0);
  for (std::size_t i = 0; i < x.size(); ++i) {
    const std::complex<double> xi(x[i].real(), x[i].imag());
    const std::complex<double> yi = xi - xp + a * yp;
    xp = xi;
    yp = yi;
    y[i] = cf(static_cast<float>(yi.real()), static_cast<float>(yi.imag()));
  }
  return y;
}

cf mean_of(const std::vector<cf>& y, std::size_t begin, std::size_t end) {
  std::complex<double> s(0, 0);
  for (std::size_t i = begin; i < end; ++i) s += std::complex<double>(y[i].real(), y[i].imag());
  s /= static_cast<double>(end - begin);
  return {static_cast<float>(s.real()), static_cast<float>(s.imag())};
}

}  // namespace

TEST(a_constant_input_gives_zero_output_from_the_first_sample) {
  const std::vector<cf> x(5000, cf(0.7f, -0.3f));
  std::vector<cf> y(x.size());
  DcBlocker dc;
  dc.process(x.data(), y.data(), x.size());
  for (const cf& s : y) CHECK(std::abs(s) < 1e-6f);
}

TEST(a_reset_followed_by_a_dc_offset_frame_is_clean) {
  // The case that motivated the design: DC settled, then a reset, then a frame
  // carrying a large offset. A zero start would pass 76% of it across 2.7 ms.
  NoiseSource rng(1);
  std::vector<cf> x(4000, cf(0.4f, -0.25f));
  add_noise(x, 1e-4, rng);  // faint noise so the mean is not exact

  DcBlocker dc;
  std::vector<cf> y(x.size());
  dc.process(x.data(), y.data(), x.size());
  dc.reset();
  dc.process(x.data(), y.data(), x.size());

  // Residual DC across the first 2.7 ms of the stretch, as a fraction of the offset.
  const double offset = std::abs(cf(0.4f, -0.25f));
  CHECK(std::abs(mean_of(y, 300, 2700)) / offset < 0.01);

  // And the unprimed alternative really is bad, so the check above means something.
  const auto zero_start = run_from_zero(x, 0.9999);
  CHECK(std::abs(mean_of(zero_start, 300, 2700)) / offset > 0.5);
}

TEST(priming_from_one_sample_adds_noise_that_a_mean_does_not) {
  // Measured: noise power gain in the first 2.7 ms after a start is 1.70 for a
  // one-sample prime and 1.002 for a 256-sample mean. Average over seeds so the
  // test does not depend on one realisation.
  double one_sample = 0.0;
  double mean_256 = 0.0;
  const int runs = 150;
  for (int seed = 1; seed <= runs; ++seed) {
    NoiseSource rng(static_cast<std::uint64_t>(seed));
    std::vector<cf> n(3000, cf(0, 0));
    add_noise(n, 1.0, rng);

    std::vector<cf> a(n.size());
    DcBlocker one(0.9999, 1);
    one.process(n.data(), a.data(), n.size());
    one_sample += mean_power(a, 300, 2700);

    std::vector<cf> b(n.size());
    DcBlocker many(0.9999, 256);
    many.process(n.data(), b.data(), n.size());
    mean_256 += mean_power(b, 300, 2700);
  }
  one_sample /= runs;
  mean_256 /= runs;
  CHECK(one_sample > 1.4);
  CHECK(mean_256 < 1.05);
  CHECK(mean_256 < one_sample - 0.3);
}

TEST(steady_state_noise_gain_is_one) {
  NoiseSource rng(2);
  std::vector<cf> n(200000, cf(0, 0));
  add_noise(n, 1.0, rng);
  std::vector<cf> y(n.size());
  DcBlocker dc;
  dc.process(n.data(), y.data(), n.size());
  CHECK_NEAR(mean_power(y, 100000, 200000), 1.0, 0.02);
}

TEST(a_dc_offset_decays_to_zero_if_it_appears_later) {
  // Signal-free input, then a step. The step is not a start-up effect, so the
  // blocker must remove it by its time constant (10 000 samples).
  std::vector<cf> x(60000, cf(0, 0));
  for (std::size_t i = 20000; i < x.size(); ++i) x[i] = cf(1.0f, 0.0f);
  std::vector<cf> y(x.size());
  DcBlocker dc;
  dc.process(x.data(), y.data(), x.size());
  CHECK(std::abs(y[20000]) > 0.9f);                 // the step passes at first
  CHECK(std::abs(y[20000 + 10000]) < 0.4f);         // e^-1 = 0.37 after one time constant
  CHECK(std::abs(y[59999]) < 0.02f);                // gone after 4 time constants
}

TEST(a_tone_far_from_dc_passes_unchanged) {
  // 50 kHz at 1 MS/s: well above the 16 Hz corner.
  const std::size_t n = 20000;
  std::vector<cf> x(n);
  for (std::size_t i = 0; i < n; ++i) x[i] = std::polar(1.0f, static_cast<float>(2.0 * 3.14159265358979 * 0.05 * i));
  std::vector<cf> y(n);
  DcBlocker dc;
  dc.process(x.data(), y.data(), n);
  CHECK_NEAR(mean_power(y, 5000, n), 1.0, 0.01);
}

TEST(block_size_does_not_change_the_output_after_priming) {
  NoiseSource rng(3);
  std::vector<cf> x(8000, cf(0.2f, 0.1f));
  add_noise(x, 0.01, rng);

  std::vector<cf> whole(x.size());
  DcBlocker a;
  a.process(x.data(), whole.data(), x.size());

  // Same samples in uneven blocks; the first block holds at least prime_samples, so
  // it primes from the same mean.
  std::vector<cf> chunked(x.size());
  DcBlocker b;
  std::size_t pos = 0;
  for (const std::size_t n : {std::size_t{3000}, std::size_t{1}, std::size_t{999}, std::size_t{4000}}) {
    b.process(x.data() + pos, chunked.data() + pos, n);
    pos += n;
  }
  CHECK(pos == x.size());
  // The first block primes from its own first 256 samples, same as the whole run.
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(std::abs(whole[i] - chunked[i]) < 1e-5f);
}

TEST(in_place_processing_matches_out_of_place) {
  NoiseSource rng(4);
  std::vector<cf> x(3000, cf(0.3f, 0.0f));
  add_noise(x, 0.01, rng);
  std::vector<cf> expected(x.size());
  DcBlocker a;
  a.process(x.data(), expected.data(), x.size());
  std::vector<cf> inplace = x;
  DcBlocker b;
  b.process(inplace.data(), inplace.data(), inplace.size());
  for (std::size_t i = 0; i < x.size(); ++i) CHECK(inplace[i] == expected[i]);
}

TEST(reset_makes_the_next_samples_prime_the_filter_again) {
  DcBlocker dc;
  std::vector<cf> a(1000, cf(5.0f, 5.0f));
  std::vector<cf> out(1000);
  dc.process(a.data(), out.data(), a.size());
  dc.reset();
  std::vector<cf> b(1000, cf(-2.0f, 1.0f));  // a different DC level
  dc.process(b.data(), out.data(), b.size());
  for (const cf& s : out) CHECK(std::abs(s) < 1e-5f);
}

TEST(a_block_shorter_than_the_prime_length_primes_from_what_it_has) {
  DcBlocker dc(0.9999, 256);
  const std::vector<cf> x(10, cf(0.6f, 0.0f));
  std::vector<cf> y(10);
  dc.process(x.data(), y.data(), x.size());
  for (const cf& s : y) CHECK(std::abs(s) < 1e-6f);
}

TEST(an_empty_block_does_nothing) {
  DcBlocker dc;
  dc.process(nullptr, nullptr, 0);
  // Still unprimed: the next real block primes normally.
  const std::vector<cf> x(500, cf(0.5f, 0.5f));
  std::vector<cf> y(500);
  dc.process(x.data(), y.data(), x.size());
  for (const cf& s : y) CHECK(std::abs(s) < 1e-6f);
}

TEST(bad_arguments_are_rejected) {
  CHECK_THROWS(DcBlocker(1.0));
  CHECK_THROWS(DcBlocker(-0.1));
  CHECK_THROWS(DcBlocker(std::nan("")));
  CHECK_THROWS(DcBlocker(0.9999, 0));
  CHECK(DcBlocker(0.0).pole() == 0.0);
  CHECK(DcBlocker().prime_samples() == 256);
}

MPSK_TEST_MAIN()
