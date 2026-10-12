#pragma once

/**
 * @file theory.hpp
 * @brief Textbook bit error rates in additive white Gaussian noise, to judge the
 *        simulated receiver against (requirement 6.8).
 */

namespace mpsk::sim {

/** The Gaussian tail probability Q(x) = P(N(0,1) > x). */
double q_function(double x);

/** Es/N0 in dB for a given Eb/N0 in dB: Es = Eb * log2(M). */
double ebn0_to_esn0_db(int modulation_order, double ebn0_db);

/**
 * Bit error rate of Gray-coded M-PSK with ideal coherent detection and ideal timing.
 *
 *   M = 2, 4:   Q( sqrt(2 Eb/N0) )                              exact
 *   M >= 8:     (2 / k) Q( sqrt(2 k Eb/N0) sin(pi / M) )        k = log2 M
 *
 * The M >= 8 form is the usual approximation: nearly every symbol error is to a
 * neighbouring point and Gray coding makes that one bit in k. It is accurate once
 * the BER is below about 1e-2 and overestimates a little above that. With M = 4
 * the second form reduces to the first, so QPSK has the same curve as BPSK.
 *
 * Reference values for QPSK: 1.3e-2 at 4 dB, 2.4e-3 at 6 dB, 1.9e-4 at 8 dB,
 * 3.9e-6 at 10 dB.
 *
 * @throws std::invalid_argument unless @p modulation_order is a power of two, 2 or more.
 */
double mpsk_ber_theory(int modulation_order, double ebn0_db);

/**
 * The Eb/N0 in dB at which mpsk_ber_theory() equals @p ber: the inverse of the curve.
 * Used to say how many dB a measured BER is from theory. Searches -20 to 60 dB.
 *
 * @throws std::invalid_argument unless 0 < ber < 0.5 (outside that the curve has no
 *         inverse), or for a bad modulation order.
 */
double mpsk_ebn0_for_ber(int modulation_order, double ber);

}  // namespace mpsk::sim
