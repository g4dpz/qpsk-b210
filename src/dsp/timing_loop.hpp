#pragma once

/**
 * @file timing_loop.hpp
 * @brief Gardner timing-error detector and the second-order loop around it.
 *
 * The preamble gives the receiver a position estimate good to a fraction of a
 * sample, which is enough to start. What it cannot give is the rate: the receiver's
 * sample clock and the transmitter's differ by some parts per million, so the
 * symbols drift across the sampling grid, by 0.01 sample over a 2.7 ms frame at
 * 5 ppm, but 0.3 sample over a 66 ms frame. This loop follows that drift.
 *
 * The detector (Gardner, 1986). With y_k the sample at the decision instant of
 * symbol k, y_{k-1} the one before, and m_k the sample half way between them,
 *
 *     e_k = Re{ (y_k - y_{k-1}) * conj(m_k) }
 *
 * Why it works. When the sampling is on time, a transition between two symbols
 * crosses through the midpoint, so m_k is near zero for a transition and the
 * product averages to zero. When sampling is late, m_k is on the falling side of
 * the pulse and has the sign of the old symbol, correlating with
 * (y_k - y_{k-1}) and giving a positive average. Early sampling gives negative. It
 * needs no decisions, only two samples per symbol, and it ignores carrier phase:
 * a common rotation of y_k, y_{k-1} and m_k cancels in the product. So the timing
 * loop can run on samples that have not yet been derotated. (They are derotated
 * here anyway, which removes the small error from a large frequency offset.)
 *
 * The sign. Measured on a matched-filter output with random QPSK: the average of e
 * is +0.26 per sample of lateness (late sampling positive) at 4 samples per symbol,
 * +0.12 at half a sample, and peaks at about 1 sample before turning over. So the
 * loop SUBTRACTS the filtered error from the sampling position.
 *
 * The gain. The detector's slope at lock, per symbol of timing offset, for
 * unit-power QPSK symbols, averaged over 400 000 random symbols:
 *
 *     roll-off    0.2     0.35    0.5     1.0
 *     gain       0.61    1.06    1.49    2.63        (same at 4 and 8 samples per symbol)
 *
 * (BPSK reads about 10% higher, 1.11 against 1.06 at 0.35.) It does not depend on
 * the samples per symbol, but it depends strongly on the roll-off, by a factor of
 * four across the allowed range. A loop designed with one value would run at the
 * wrong bandwidth with another. So the gain is not a constant here:
 * TimingLoop::measure_detector_gain() finds it for the actual pulse by the same
 * measurement, once, when the receiver is built.
 *
 * How precisely it is known. The average depends on which random symbols were used,
 * and the spread falls as 1/sqrt(N): standard deviation 0.056 for 1000 symbols,
 * 0.030 for 3000, 0.017 for 10000 and 0.006 for 40000 (at roll-off 0.35). The
 * measurement uses 20000, so it is good to about 1%. An earlier version of this
 * table quoted three-figure values from single short runs, which were 4 to 7% low;
 * two significant figures is all that should be read into any of these.
 *
 * Loop. With the detector gain Kd known, the gains come from loop_gains.hpp with
 * the position measured in symbols: the sampling position (in samples) advances by
 * sps + sps * (step), where the step is the loop output in symbols.
 */

#include <complex>
#include <cstddef>

#include "dsp/loop_gains.hpp"

namespace mpsk::dsp {

/** Gardner error for three consecutive samples (current, previous, midpoint). */
double gardner_error(std::complex<float> current, std::complex<float> previous,
                     std::complex<float> midpoint);

struct TimingLoopConfig {
  int samples_per_symbol = 4;
  double rolloff = 0.35;
  int span_symbols = 11;

  /** Noise bandwidth times the symbol period. 0 freezes the loop at its start. */
  double bn_t = 0.003;
  double damping = 0.70710678118654752;

  /**
   * The detector gain per symbol, if already known. 0 means "measure it", which
   * costs a few milliseconds, so a receiver that builds a loop for every frame
   * should call TimingLoop::measure_detector_gain() once and set this.
   */
  double detector_gain = 0.0;
};

class TimingLoop {
 public:
  /**
   * @param start_position  Sampling position, in samples (can be fractional), of the
   *                        first symbol this loop will be asked about. The symbol
   *                        before it, which the Gardner detector also needs, is one
   *                        symbol period earlier.
   * @throws std::invalid_argument for unusable parameters.
   */
  TimingLoop(const TimingLoopConfig& config, double start_position);

  /**
   * Gardner detector gain for unit-power QPSK symbols with this pulse shape, per
   * symbol of timing offset, found by simulation: random symbols are shaped and
   * matched filtered, and the average detector output is measured at timing offsets
   * of plus and minus 5% of a symbol. Deterministic (fixed seed). About 10 ms.
   */
  static double measure_detector_gain(int samples_per_symbol, double rolloff, int span_symbols);

  /** Sampling position of the symbol that update() will be called for next, samples. */
  double next_position() const { return position_; }

  /** Position half a symbol before next_position(), for the midpoint sample. */
  double midpoint_position() const { return position_ - 0.5 * step_; }

  /** Position of the symbol before next_position(). */
  double previous_position() const { return position_ - step_; }

  /**
   * Feeds the Gardner error of the symbol at next_position() and moves on to the
   * next symbol.
   */
  void update(double error);

  /** Current estimate of the symbol period in samples (sps plus the learned drift). */
  double symbol_period() const { return step_; }

  /**
   * The drift the loop has learned, as a sample-clock offset in parts per million.
   * Positive: the symbols arrive further apart than nominal, so the receiver's clock
   * runs fast relative to the transmitter's.
   */
  double clock_offset_ppm() const;

  const LoopGains& gains() const { return gains_; }
  double detector_gain() const { return detector_gain_; }

 private:
  int sps_;
  double detector_gain_;
  LoopGains gains_;
  double position_;
  double integrator_;  ///< I: fractional error of the symbol period (dimensionless)
  double step_;        ///< symbol period in samples, sps * (1 + I)
};

}  // namespace mpsk::dsp
