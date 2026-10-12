#pragma once

/**
 * @file preamble.hpp
 * @brief The known synchronisation sequence at the start of every frame.
 *
 * The receiver finds a frame by correlating the incoming samples against this
 * sequence, then uses the correlation peak to estimate timing, carrier
 * frequency, carrier phase and amplitude. It is BPSK, so it is the same for
 * QPSK and 8-PSK payloads and detection does not depend on the payload
 * modulation.
 *
 * Construction: a 63-chip maximal-length sequence from the polynomial
 * x^6 + x^5 + 1, plus one extra chip to make 64 symbols. Sixty-four splits into
 * four segments of 16 symbols, which the receiver's frequency estimator uses.
 *
 * Why an m-sequence: its autocorrelation is sharply peaked. Slid against
 * itself, the sequence matches fully at zero offset (a peak of 64) and only
 * weakly everywhere else. A frame is therefore detected as one clean peak, and
 * the peak position gives the frame timing.
 *
 * The starting point of the sequence (which of its 63 rotations) and the value
 * of the extra chip were chosen by exhaustive search over all 252 variants
 * (63 rotations, extra chip 0 or 1, appended or prepended). It was scored by
 * the sidelobes of the full 64-symbol coherent autocorrelation, which ranged
 * from 6 to 12 against a peak of 64. The chosen sequence has a worst sidelobe of
 * 6 (about -20.6 dB), among the best on that metric.
 *
 * That is not the metric the receiver's detector uses. The detector (see the
 * design) combines four 16-symbol partial correlations non-coherently, so that
 * it tolerates a carrier frequency offset. Scored that way, the worst sidelobe
 * of the chosen sequence is -8.6 dB, worse than most variants: every one of the
 * 252 lands between -8.3 and -10.1 dB, with a median of -9.6 dB. The best
 * alternative with a similar coherent sidelobe is only 1.3 dB better on the
 * detector metric and 1.4 dB worse on the coherent one, so the sequence was kept.
 * The choice is not a strong lever on detection either way. Changing it changes
 * detection performance, so the unit test pins both measured values.
 */

#include <complex>
#include <vector>

#include "dsp/bits.hpp"

namespace mpsk::dsp {

/** Number of preamble symbols. */
constexpr int kPreambleSymbols = 64;

/** Length of one preamble segment, used for frequency estimation. */
constexpr int kPreambleSegmentSymbols = 16;

/** The preamble as bits (bit 0 maps to +1, bit 1 to -1). */
const Bits& preamble_bits();

/** The preamble as BPSK symbols, each +1 or -1 with zero imaginary part. */
const std::vector<std::complex<float>>& preamble_symbols();

}  // namespace mpsk::dsp
