#pragma once

/**
 * @file lfsr.hpp
 * @brief Two-tap Fibonacci linear-feedback shift register.
 *
 * The PRBS generator and the scrambler are both LFSRs for polynomials of the
 * form  x^n + x^k + 1.  One class serves both.
 *
 * Model: the register holds n bits s[n-1..0]. Each step computes the feedback
 *
 *     fb = s[n-1] XOR s[k-1]
 *
 * outputs fb, then shifts the register left by one and puts fb into bit 0.
 * When x^n + x^k + 1 is a primitive polynomial, the register visits every
 * nonzero state before repeating, so the output is a maximal-length sequence
 * (an "m-sequence") of period 2^n - 1. Such a sequence has 2^(n-1) ones and
 * 2^(n-1) - 1 zeros per period, and its runs of identical bits never exceed n.
 * Those properties are what make it a good whitener and a good known test
 * pattern.
 *
 * The all-zero state is a lock-up: fb stays 0 forever, so it is rejected.
 */

#include <cstdint>

namespace mpsk::dsp {

class Lfsr {
 public:
  /**
   * @param degree  n, the register length, 2..31.
   * @param tap     k, the second tap, 1..n-1. The first tap is always n.
   * @param seed    Initial register contents. Only the low n bits are used.
   * @throws std::invalid_argument for a bad degree or tap, or a seed whose low
   *         n bits are all zero.
   */
  Lfsr(int degree, int tap, uint32_t seed);

  /** Advances one step. @return The output bit, 0 or 1. */
  int next_bit();

  /** @return The current register contents, low n bits. */
  uint32_t state() const { return state_; }

 private:
  int degree_;
  int tap_;
  uint32_t mask_;
  uint32_t state_;
};

}  // namespace mpsk::dsp
