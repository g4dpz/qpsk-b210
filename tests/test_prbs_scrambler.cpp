// Tests for the LFSR, the PRBS payload generator and the additive scrambler.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "dsp/bits.hpp"
#include "dsp/lfsr.hpp"
#include "dsp/prbs.hpp"
#include "dsp/scrambler.hpp"
#include "test_harness.hpp"

using mpsk::dsp::Bits;
using mpsk::dsp::Lfsr;

namespace {

struct RunLengths {
  std::size_t longest_ones = 0;
  std::size_t longest_zeros = 0;
};

RunLengths longest_runs(const Bits& bits) {
  RunLengths r;
  std::size_t run = 0;
  for (std::size_t i = 0; i < bits.size(); ++i) {
    run = (i > 0 && bits[i] == bits[i - 1]) ? run + 1 : 1;
    std::size_t& longest = bits[i] ? r.longest_ones : r.longest_zeros;
    longest = std::max(longest, run);
  }
  return r;
}

std::size_t count_ones(const Bits& bits) {
  return static_cast<std::size_t>(std::count(bits.begin(), bits.end(), uint8_t{1}));
}

/** Steps an LFSR until its state returns to the start. @return Steps taken, or 0 if > limit. */
std::size_t period_of(int degree, int tap, std::size_t* ones_in_period = nullptr) {
  Lfsr lfsr(degree, tap, 1);
  const uint32_t start = lfsr.state();
  const std::size_t limit = (std::size_t{1} << degree);
  std::size_t steps = 0;
  std::size_t ones = 0;
  do {
    ones += static_cast<std::size_t>(lfsr.next_bit());
    ++steps;
    if (steps > limit) return 0;
  } while (lfsr.state() != start);
  if (ones_in_period) *ones_in_period = ones;
  return steps;
}

}  // namespace

// ---- LFSR ------------------------------------------------------------------

TEST(lfsr_polynomials_are_maximal_length) {
  // For a primitive polynomial of degree n the period is 2^n - 1 and one period
  // holds 2^(n-1) ones. This checks the tap convention of the Lfsr class.
  struct Poly {
    int degree;
    int tap;
  };
  for (const Poly p : {Poly{7, 6}, Poly{7, 4}, Poly{9, 5}, Poly{15, 14}}) {
    std::size_t ones = 0;
    const std::size_t period = period_of(p.degree, p.tap, &ones);
    CHECK(period == (std::size_t{1} << p.degree) - 1);
    CHECK(ones == (std::size_t{1} << (p.degree - 1)));
  }
}

TEST(lfsr_with_a_non_primitive_polynomial_is_not_maximal) {
  // x^7 + x^2 + 1 is not primitive; this confirms period_of can tell the difference.
  const std::size_t period = period_of(7, 2);
  CHECK(period != 127);
}

TEST(lfsr_rejects_bad_arguments) {
  CHECK_THROWS(Lfsr(1, 1, 1));    // degree too small
  CHECK_THROWS(Lfsr(32, 5, 1));   // degree too large
  CHECK_THROWS(Lfsr(7, 0, 1));    // tap too small
  CHECK_THROWS(Lfsr(7, 7, 1));    // tap must be below the degree
  CHECK_THROWS(Lfsr(7, 4, 0));    // all-zero seed locks up
  CHECK_THROWS(Lfsr(7, 4, 0x80)); // low 7 bits are zero after masking
}

TEST(lfsr_output_is_deterministic) {
  Lfsr a(15, 14, 0x1234);
  Lfsr b(15, 14, 0x1234);
  for (int i = 0; i < 1000; ++i) CHECK(a.next_bit() == b.next_bit());
}

// ---- PRBS payloads ---------------------------------------------------------

TEST(prbs_is_repeatable) {
  CHECK(mpsk::dsp::prbs_bits(7, 3, 4096) == mpsk::dsp::prbs_bits(7, 3, 4096));
}

TEST(prbs_short_request_is_a_prefix_of_a_longer_one) {
  const Bits longer = mpsk::dsp::prbs_bits(11, 5, 1000);
  const Bits shorter = mpsk::dsp::prbs_bits(11, 5, 300);
  CHECK(std::equal(shorter.begin(), shorter.end(), longer.begin()));
}

TEST(prbs_bits_are_zero_or_one) {
  for (const uint8_t b : mpsk::dsp::prbs_bits(1, 1, 5000)) CHECK(b == 0 || b == 1);
}

TEST(prbs_differs_between_frame_counters_and_seeds) {
  const Bits base = mpsk::dsp::prbs_bits(1, 0, 4096);
  const Bits next_frame = mpsk::dsp::prbs_bits(1, 1, 4096);
  const Bits other_seed = mpsk::dsp::prbs_bits(2, 0, 4096);

  // Unrelated sequences disagree in about half the positions (2048 +/- ~64 for
  // one standard deviation 32). The wide bounds below are about 8 sigma.
  const auto disagreements = [](const Bits& a, const Bits& b) {
    return mpsk::dsp::count_bit_errors(a, b);
  };
  CHECK(disagreements(base, next_frame) > 1800 && disagreements(base, next_frame) < 2300);
  CHECK(disagreements(base, other_seed) > 1800 && disagreements(base, other_seed) < 2300);
}

TEST(prbs_adjacent_frame_counters_are_not_shifted_copies) {
  // A weak seeding would make frame n+1 a one-bit shift of frame n. Compare at
  // shifts 0..8 and require every alignment to look unrelated.
  const Bits a = mpsk::dsp::prbs_bits(9, 100, 2048 + 8);
  const Bits b = mpsk::dsp::prbs_bits(9, 101, 2048);
  for (std::size_t shift = 0; shift <= 8; ++shift) {
    const Bits window(a.begin() + static_cast<std::ptrdiff_t>(shift),
                      a.begin() + static_cast<std::ptrdiff_t>(shift) + 2048);
    const std::size_t d = mpsk::dsp::count_bit_errors(window, b);
    CHECK(d > 800 && d < 1250);
  }
}

TEST(prbs_is_balanced_and_has_bounded_runs) {
  const Bits bits = mpsk::dsp::prbs_bits(42, 7, 1000000);
  const double ones_fraction = static_cast<double>(count_ones(bits)) / 1e6;
  CHECK_NEAR(ones_fraction, 0.5, 0.005);  // 1 sigma is 0.0005

  // In a PRBS31 m-sequence no run of identical bits exceeds 31.
  const RunLengths r = longest_runs(bits);
  CHECK(r.longest_ones <= 31);
  CHECK(r.longest_zeros <= 31);
  CHECK(r.longest_ones >= 10);  // 1e6 bits are far more than enough to see long runs
  CHECK(r.longest_zeros >= 10);
}

// ---- Scrambler -------------------------------------------------------------

TEST(scrambler_is_its_own_inverse) {
  Bits data;
  for (int i = 0; i < 1000; ++i) data.push_back(static_cast<uint8_t>((i * 7 + i / 3) & 1));
  CHECK(mpsk::dsp::scramble(mpsk::dsp::scramble(data)) == data);
  CHECK(mpsk::dsp::descramble(mpsk::dsp::scramble(data)) == data);
}

TEST(scrambler_changes_the_data) {
  const Bits zeros(500, 0);
  CHECK(mpsk::dsp::scramble(zeros) != zeros);
}

TEST(scrambler_sequence_has_period_127_and_is_balanced) {
  // Scrambling zeros exposes the raw sequence.
  const Bits seq = mpsk::dsp::scramble(Bits(127 * 3, 0));
  for (std::size_t i = 0; i + 127 < seq.size(); ++i) CHECK(seq[i] == seq[i + 127]);

  const Bits one_period(seq.begin(), seq.begin() + 127);
  CHECK(count_ones(one_period) == 64);  // an m-sequence of degree 7 has 64 ones, 63 zeros
}

TEST(scrambler_breaks_up_long_constant_runs) {
  // The whole point of scrambling: a long run of zeros or ones must come out
  // with no constant run longer than the m-sequence allows (7).
  for (const uint8_t fill : {uint8_t{0}, uint8_t{1}}) {
    const Bits out = mpsk::dsp::scramble(Bits(5000, fill));
    const RunLengths r = longest_runs(out);
    CHECK(r.longest_ones <= 7);
    CHECK(r.longest_zeros <= 7);
    const double ones_fraction = static_cast<double>(count_ones(out)) / 5000.0;
    CHECK(ones_fraction > 0.45 && ones_fraction < 0.55);
  }
}

TEST(scrambler_restarts_for_every_call) {
  const Bits data(300, 1);
  CHECK(mpsk::dsp::scramble(data) == mpsk::dsp::scramble(data));
}

TEST(a_bit_error_in_scrambled_data_stays_a_single_bit_error) {
  Bits data;
  for (int i = 0; i < 400; ++i) data.push_back(static_cast<uint8_t>((i * 5 + 1) & 1));
  Bits scrambled = mpsk::dsp::scramble(data);
  scrambled[123] ^= 1;
  CHECK(mpsk::dsp::count_bit_errors(mpsk::dsp::descramble(scrambled), data) == 1);
}

MPSK_TEST_MAIN()
