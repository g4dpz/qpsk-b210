#pragma once

/**
 * @file rx_frontend.hpp
 * @brief Receive front end: DC removal followed by the RRC matched filter.
 *
 * The output has the same length and sample rate as the input and is delayed by
 * the filter's group delay. A symbol sent at input time t has its pulse peak in
 * the output at t + group_delay() + the transmit filter's own delay, which is
 * the position the preamble detector reports. Converting between the two is the
 * caller's job; group_delay() is what it needs.
 *
 * The matched filter is the same unit-energy RRC as the transmitter, so after
 * both, a symbol of magnitude 1 comes back with magnitude 1 times the channel
 * gain, and noise that was white stays white at symbol spacing.
 */

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/dc_blocker.hpp"
#include "dsp/fir.hpp"

namespace mpsk::dsp {

struct RxFrontEndConfig {
  int samples_per_symbol = 4;
  double rolloff = 0.35;
  int span_symbols = 11;
  double dc_pole = 0.9999;
};

class RxFrontEnd {
 public:
  /** @throws std::invalid_argument for an unusable configuration (see rrc_taps, DcBlocker). */
  explicit RxFrontEnd(const RxFrontEndConfig& config);

  /** Filters @p n samples. The ranges may be the same (in-place). */
  void process(const std::complex<float>* in, std::complex<float>* out, std::size_t n);

  /** Filters a vector and returns a new vector of the same length. */
  std::vector<std::complex<float>> process(const std::vector<std::complex<float>>& in);

  /**
   * Prepares for a new stretch of input: the DC blocker will prime itself on the
   * next sample, and the matched filter forgets its history. Use it after the
   * unit's own transmission.
   */
  void reset();

  /** Group delay of the matched filter, in samples. */
  int group_delay() const { return group_delay_; }

  const std::vector<float>& taps() const { return filter_.taps(); }

  /** Samples processed since construction or the last reset. */
  std::uint64_t samples_processed() const { return samples_processed_; }

 private:
  RxFrontEndConfig config_;
  DcBlocker dc_blocker_;
  FirFilter filter_;
  int group_delay_;
  std::uint64_t samples_processed_ = 0;
};

}  // namespace mpsk::dsp
