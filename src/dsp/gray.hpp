#pragma once

/**
 * @file gray.hpp
 * @brief Binary-reflected Gray code.
 *
 * Why Gray coding: the most likely symbol error in noise is a slip to a
 * neighbouring constellation point. If neighbours differ in exactly one bit,
 * that slip costs one bit error instead of possibly several, so at moderate SNR
 * the bit error rate is close to the symbol error rate divided by the number of
 * bits per symbol.
 *
 * The binary-reflected Gray code of n is  g = n XOR (n >> 1).
 * Counting n = 0,1,2,3 gives g = 00,01,11,10, and each step flips one bit.
 * The sequence is also cyclic: the last code (10) and the first (00) differ in
 * one bit, which matters for PSK because the points sit on a circle and the
 * last point is a neighbour of the first.
 */

namespace mpsk::dsp {

/** @return The Gray code of @p n. */
constexpr unsigned binary_to_gray(unsigned n) { return n ^ (n >> 1); }

/**
 * Inverse of binary_to_gray().
 *
 * Gray decoding is a running XOR from the top bit down:
 * n = g XOR (g >> 1) XOR (g >> 2) XOR ...
 */
constexpr unsigned gray_to_binary(unsigned g) {
  unsigned n = g;
  for (unsigned shifted = g >> 1; shifted != 0; shifted >>= 1) n ^= shifted;
  return n;
}

}  // namespace mpsk::dsp
