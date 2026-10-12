#pragma once

/**
 * @file fir.hpp
 * @brief Streaming complex FIR filter with real taps, and the transmit
 *        upsample-and-filter path.
 *
 * Every filter in this chain (the RRC pulse shaper and the matched filter) has
 * real taps and complex samples, so the I and Q channels are filtered
 * identically. Using real taps halves the multiplies compared with a general
 * complex filter.
 */

#include <complex>
#include <cstddef>
#include <vector>

namespace mpsk::dsp {

/**
 * Direct-form FIR filter,  y[n] = sum_k h[k] x[n - k],  that keeps its history
 * between calls, so a signal can be fed in blocks of any size and the output is
 * identical to filtering it in one piece.
 *
 * Implementation note: the history is stored twice in a buffer of length 2N and
 * the newest sample is written at a slot that moves backwards. The N most recent
 * samples are then always one contiguous run, so the inner loop is a plain dot
 * product with no wrap-around test per tap.
 */
class FirFilter {
 public:
  /**
   * @param taps  Filter coefficients h[0..N-1]; h[0] multiplies the newest sample.
   * @throws std::invalid_argument if @p taps is empty.
   */
  explicit FirFilter(std::vector<float> taps);

  /** Filters one sample. @return y[n]. */
  std::complex<float> push(std::complex<float> x);

  /**
   * Filters @p n samples from @p in into @p out. The two ranges may be the same
   * (in-place filtering), because each input sample is read before its output is
   * written.
   */
  void process(const std::complex<float>* in, std::complex<float>* out, std::size_t n);

  /** Filters a vector and returns a new vector of the same length. */
  std::vector<std::complex<float>> process(const std::vector<std::complex<float>>& in);

  /** Clears the history, as if the filter had only ever seen zeros. */
  void reset();

  /** @return The number of taps N. */
  std::size_t num_taps() const { return taps_.size(); }

  /**
   * Group delay in samples, (N - 1) / 2. This is exact for symmetric taps such
   * as an RRC filter, which have linear phase.
   */
  double group_delay() const { return static_cast<double>(taps_.size() - 1) / 2.0; }

 private:
  std::vector<float> taps_;
  std::vector<std::complex<float>> history_;  ///< length 2N, each sample stored twice
  std::size_t pos_;                           ///< slot of the newest sample
};

/**
 * Upsamples a symbol sequence by @p sps and filters it, which is the transmit
 * pulse shaper:
 *
 *   y[n] = sum_m s[m] h[n - m * sps]
 *
 * This is the full convolution of the symbols (each followed by sps - 1 zeros)
 * with the taps. Only the nonzero input samples are visited, so the cost is
 * (symbols x taps) multiply-adds, not (samples x taps).
 *
 * The output holds the whole burst, including the filter's ramp-up and ramp-down:
 * its length is (symbols - 1) * sps + taps, or zero for no symbols. With a
 * symmetric N-tap filter the peak of symbol m falls at sample
 * m * sps + (N - 1) / 2.
 *
 * @throws std::invalid_argument if @p taps is empty or @p sps < 1.
 */
std::vector<std::complex<float>> upsample_and_filter(
    const std::vector<std::complex<float>>& symbols, const std::vector<float>& taps, int sps);

}  // namespace mpsk::dsp
