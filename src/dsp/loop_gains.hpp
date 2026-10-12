#pragma once

/**
 * @file loop_gains.hpp
 * @brief Gains for a second-order digital phase- or timing-locked loop.
 *
 * Both tracking loops here (carrier and symbol timing) have the same structure,
 * one update per symbol:
 *
 *     e_k      = detector output (the error)
 *     I_k      = I_{k-1} + Ki * e_k                    integrator
 *     v_k      = Kp * e_k + I_k                        loop filter output
 *     x_{k+1}  = x_k + v_k                             the oscillator integrates v
 *
 * where x is the loop's estimate (carrier phase in radians, or sampling position
 * in samples), and the detector, for an estimate error d = x_true - x_k, produces
 * e_k = Kd * d, with Kd the detector gain. Linearised, with H the closed-loop
 * response from the true value to the estimate:
 *
 *     G(z) = Kd * z^-1 / (1 - z^-1) * (Kp + Ki / (1 - z^-1)),     H = G / (1 + G)
 *
 * The loop is designed from two numbers: the damping factor zeta (0.7071 is the
 * usual choice, a fast response with little overshoot) and the noise bandwidth
 * Bn * T, as a fraction of the symbol rate. Bn is the bandwidth a flat noise floor
 * would have to have to pass the same noise power as the loop, so it sets both how
 * fast the loop pulls in and how much detector noise reaches the estimate: for a
 * decision-directed phase loop at symbol SNR g the phase jitter is sqrt(Bn * T / g)
 * rad. Measured for QPSK at Bn*T = 0.01: 0.0100, 0.0179, 0.0254 and 0.0327 rad at
 * Es/N0 of 20, 15, 12 and 10 dB, against 0.0100, 0.0178, 0.0251 and 0.0316 from the
 * formula. (An earlier version of this comment gave sqrt(2 * Bn * T / g), which is
 * wrong by a factor of sqrt(2): only the quadrature half of the complex noise moves
 * the phase, which cancels the usual factor of 2.)
 *
 * Derivation (after Rice, "Digital Communications: A Discrete-Time Approach",
 * appendix C). For the continuous second-order loop with natural frequency wn,
 * damping zeta, the noise bandwidth is Bn = (wn / 2) * (zeta + 1 / (4 zeta)). Mapping
 * to the discrete loop above with unit detector gain, and matching the response,
 * gives
 *
 *     theta = (Bn T) / (zeta + 1 / (4 zeta))           ( = wn T / 2 )
 *     d     = 1 + 2 zeta theta + theta^2
 *     Kp    = 4 zeta theta / d
 *     Ki    = 4 theta^2 / d
 *
 * and for a detector of gain Kd the same gains divided by Kd, so that Kd * Kp and
 * Kd * Ki are what the formulas give.
 *
 * These are approximations: they come from matching a continuous design, and the
 * match degrades as the loop gets wide. The noise bandwidth of the discrete loop
 * was computed numerically from G(z) and compared with the target:
 *
 *     Bn T target    0.001   0.003   0.01    0.03    0.1      (zeta = 0.7071)
 *     measured ratio 1.001   1.003   1.009   1.027   1.092
 *
 * So below about 0.03 the bandwidth is right to under 3%, which covers every loop
 * in this receiver (carrier 0.01, timing a few thousandths). test_loops repeats the
 * calculation. Do not use these formulas for a loop wider than about 0.1.
 */

namespace mpsk::dsp {

struct LoopGains {
  double kp = 0.0;  ///< proportional gain
  double ki = 0.0;  ///< integral gain
};

/**
 * Gains for a loop with noise bandwidth @p bn_t (Bn * T, fraction of the symbol
 * rate), damping @p zeta, and detector gain @p detector_gain.
 *
 * A @p bn_t of 0 gives zero gains: a loop that never moves, used to switch a loop
 * off.
 *
 * @throws std::invalid_argument for bn_t outside [0, 0.5), zeta <= 0, or
 *         detector_gain <= 0.
 */
LoopGains second_order_gains(double bn_t, double zeta, double detector_gain = 1.0);

}  // namespace mpsk::dsp
