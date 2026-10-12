#pragma once

/**
 * @file constellation.hpp
 * @brief Gray-coded M-PSK constellation, mapper and slicer.
 *
 * An M-PSK point is  exp(j (2*pi*k / M + phi0))  for position k = 0..M-1.
 * Every point has magnitude 1, so the average symbol power is exactly 1
 * whatever M is. The bit pattern ("label") of position k is the Gray code of k,
 * so neighbouring points differ in one bit (see gray.hpp).
 *
 * The code is generic over M. Which orders a given build accepts is a separate,
 * deliberate policy, see Constellation::supported_orders().
 */

#include <complex>
#include <vector>

#include "dsp/bits.hpp"

namespace mpsk::dsp {

class Constellation {
 public:
  /** A hard decision: the label of the nearest point, and that point itself. */
  struct Decision {
    unsigned label;
    std::complex<float> point;
  };

  /**
   * @param order The modulation order M, a power of two.
   * @throws std::invalid_argument if @p order is not usable in this build. The
   *         message says what is supported, and for 8-PSK that it is not
   *         enabled yet.
   *
   * Accepted orders are supported_orders() (the payload modulations) plus
   * M = 2 (BPSK), which framing uses internally for the preamble and header and
   * which is never offered as a payload choice.
   */
  explicit Constellation(int order);

  /**
   * Payload modulation orders this build enables. Stage 1 enables QPSK only.
   * Stage 2 adds 8, and nothing else in this class needs to change.
   */
  static const std::vector<int>& supported_orders();

  /** @return The modulation order M. */
  int order() const { return order_; }

  /** @return log2(M), the number of bits carried by one symbol. */
  int bits_per_symbol() const { return bits_per_symbol_; }

  /**
   * Maps a bit label to its constellation point.
   * @param label bits_per_symbol() bits, first bit of the symbol in the most
   *        significant position. Higher bits are ignored.
   */
  std::complex<float> map(unsigned label) const;

  /**
   * Hard decision by angle. Finds the position k whose angle is closest to
   * arg(y), which is the nearest-point decision for equal-magnitude points and
   * ignores the amplitude of @p y. That independence from amplitude is what
   * lets the receiver slice before its gain is settled.
   */
  Decision slice(std::complex<float> y) const;

  /** @return The label of the point nearest to @p y. */
  unsigned demap(std::complex<float> y) const { return slice(y).label; }

 private:
  int order_;
  int bits_per_symbol_;
  double phase_offset_;                       ///< phi0, radians
  std::vector<std::complex<float>> points_;   ///< indexed by label
};

/**
 * Maps a bit vector to symbols, bits_per_symbol() bits per symbol, first bit
 * most significant.
 * @throws std::invalid_argument if the bit count is not a multiple of
 *         bits_per_symbol(). Padding is the framer's job, so that a silent
 *         truncation here cannot hide a framing bug.
 */
std::vector<std::complex<float>> map_bits(const Constellation& constellation,
                                          const Bits& bits);

/** Slices each symbol and unpacks the labels into bits, first bit most significant. */
Bits demap_symbols(const Constellation& constellation,
                   const std::vector<std::complex<float>>& symbols);

}  // namespace mpsk::dsp
