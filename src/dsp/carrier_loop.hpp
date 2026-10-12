#pragma once

/**
 * @file carrier_loop.hpp
 * @brief Decision-directed second-order phase-locked loop for the carrier.
 *
 * The preamble gives the receiver a good starting estimate of the carrier
 * frequency and phase (see preamble_detector.hpp), but not a perfect one: the
 * frequency estimate is off by tens to a couple of hundred hertz, which over a
 * frame of several hundred symbols would rotate the constellation by a large
 * angle. This loop starts from that estimate and tracks what is left, and any
 * drift after it.
 *
 * Per symbol, in this order:
 *   1. derotate:  z = y * exp(-j * phase)
 *   2. decide:    d = the nearest constellation point to z (done by the caller)
 *   3. detect:    e = arg(z * conj(d))                        in radians
 *   4. update:    the integrator and phase as in loop_gains.hpp
 *
 * The detector. For a point z = d * exp(j * err) the detector returns err exactly
 * (wrapped to (-pi, pi]), so its gain is 1 whatever the constellation, and it does
 * not depend on the amplitude of z, so a wrong amplitude estimate does not change
 * the loop. This is the same for every M, which is why one detector serves BPSK,
 * QPSK and 8-PSK. It is correct as long as the decision d is the point that was
 * sent: with a phase error beyond half the point spacing (45 degrees for QPSK,
 * 22.5 for 8-PSK) the decision is wrong and the detector pushes the loop towards
 * the neighbouring point, which is a cycle slip. For the same reason the loop must
 * start close: the preamble estimate does.
 *
 * The sign: z lags the truth when the loop phase is too small. Then err > 0, e > 0,
 * and the update raises the phase. That is negative feedback.
 */

#include <complex>

#include "dsp/loop_gains.hpp"

namespace mpsk::dsp {

struct CarrierLoopConfig {
  double bn_t = 0.01;      ///< noise bandwidth times the symbol period
  double damping = 0.70710678118654752;
};

class CarrierLoop {
 public:
  /**
   * @param initial_phase      Carrier phase estimate at the first symbol to be
   *                           processed, radians.
   * @param initial_frequency  Carrier frequency estimate, radians per symbol.
   * @throws std::invalid_argument for unusable loop parameters (see second_order_gains).
   */
  CarrierLoop(const CarrierLoopConfig& config, double initial_phase, double initial_frequency);

  /**
   * Removes the carrier from a sample taken @p symbols_earlier symbols before the
   * symbol the loop is currently at: 0 for the symbol itself, 0.5 for the midpoint
   * before it, 1 for the previous symbol. The phase at that earlier time is
   * extrapolated backwards with the loop's frequency estimate.
   */
  std::complex<float> derotate(std::complex<float> y, double symbols_earlier = 0.0) const;

  /**
   * Updates the loop with the symbol just derotated and its decision, and advances
   * to the next symbol.
   * @param corrected  The derotated sample z.
   * @param decision   The constellation point d that was decided for it.
   * @return The detector output e, radians.
   */
  double update(std::complex<float> corrected, std::complex<float> decision);

  double phase() const { return phase_; }
  double frequency() const { return frequency_; }  ///< radians per symbol

  const LoopGains& gains() const { return gains_; }

 private:
  LoopGains gains_;
  double phase_;
  double frequency_;     ///< the integrator state I
};

}  // namespace mpsk::dsp
