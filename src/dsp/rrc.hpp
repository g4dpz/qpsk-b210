#pragma once

/**
 * @file rrc.hpp
 * @brief Root-raised-cosine (RRC) pulse-shaping filter taps.
 *
 * Why RRC: a pulse train of unit-spaced symbols must have no inter-symbol
 * interference (ISI) at the receiver's sampling instants, and it must stay
 * inside a limited bandwidth. The raised-cosine (RC) pulse does both: its
 * impulse response is zero at every nonzero multiple of the symbol period T
 * (the Nyquist criterion), and its bandwidth is (1 + beta) / (2T), where beta is
 * the roll-off factor in [0, 1].
 *
 * The RC response is split evenly between transmitter and receiver. Each side
 * uses a root-raised-cosine filter, whose frequency response is the square root
 * of the RC response. The cascade of the transmit RRC and the receive RRC (the
 * "matched filter") is then exactly the RC pulse. The receive filter being
 * matched to the transmit filter also maximises the signal-to-noise ratio at the
 * sampling instant for white noise.
 *
 * Closed form (time t measured in symbol periods, so T = 1):
 *
 *   general t:        h(t) = [ sin(pi t (1 - b)) + 4 b t cos(pi t (1 + b)) ]
 *                            / [ pi t (1 - (4 b t)^2) ]
 *
 *   t = 0:            h(0) = 1 - b + 4 b / pi
 *
 *   t = +-1 / (4 b):  h = (b / sqrt 2) [ (1 + 2/pi) sin(pi / (4 b))
 *                                      + (1 - 2/pi) cos(pi / (4 b)) ]
 *
 * The general expression is 0/0 at the last two points, because both the
 * numerator and the denominator vanish there (t = 0 from the factor t, and
 * t = 1/(4b) from the factor 1 - (4bt)^2). The two special values above are the
 * limits of the general expression, found with L'Hopital's rule. They must be
 * handled separately, or the filter has NaN taps whenever the sample grid lands
 * on one of those points, for example b = 0.25 with 4 samples per symbol.
 */

#include <vector>

namespace mpsk::dsp {

/**
 * Value of the (unnormalised) RRC impulse response at time @p t.
 *
 * @param t        Time in symbol periods (T = 1).
 * @param rolloff  Roll-off factor b, 0 to 1.
 *
 * The two singular points, t = 0 and t = +-1/(4b), are evaluated from their
 * closed-form limits, so the result is finite for every t. A roll-off of 0 is
 * the sinc pulse and has no second singular point.
 */
double rrc_impulse(double t, double rolloff);

/**
 * Generates RRC filter taps, sampled at @p sps samples per symbol.
 *
 * The filter spans @p span_symbols symbol periods, so it has
 * span_symbols * sps + 1 taps. The response is symmetric about the centre tap,
 * which gives a linear phase response and a group delay of exactly
 * rrc_group_delay() samples.
 *
 * Taps are scaled to unit energy (sum of squares = 1). With that scaling the
 * transmit filter and the matched receive filter in cascade give a centre value
 * of 1, so a symbol of magnitude 1 is recovered with magnitude 1 and the
 * receiver needs no extra gain stage.
 *
 * @param rolloff       Roll-off factor, 0 to 1. Typical value 0.35.
 * @param sps           Samples per symbol, at least 2.
 * @param span_symbols  Filter length in symbols, at least 1. Typical value 11.
 * @throws std::invalid_argument for a roll-off outside [0, 1], sps < 2,
 *         span_symbols < 1, or span_symbols * sps odd. An odd product gives an
 *         even tap count, which puts the filter centre between two samples and
 *         makes the group delay a half sample, breaking the timing assumptions
 *         of the framer and receiver.
 */
std::vector<float> rrc_taps(double rolloff, int sps, int span_symbols);

/**
 * Group delay of an RRC filter from rrc_taps(), in samples: (taps - 1) / 2.
 * A symbol's pulse peaks this many samples after the symbol's first sample.
 */
constexpr int rrc_group_delay(int sps, int span_symbols) { return span_symbols * sps / 2; }

}  // namespace mpsk::dsp
