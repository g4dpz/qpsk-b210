#pragma once

/**
 * @file scrambler.hpp
 * @brief Additive scrambler, polynomial x^7 + x^4 + 1.
 *
 * Why scramble: data from a file can hold long runs of identical bits, for
 * example a block of zeros. The symbol-timing loop recovers timing from symbol
 * transitions, so a long constant run would starve it. XORing the data with a
 * fixed pseudorandom sequence turns any input into a sequence with plenty of
 * transitions, and the longest constant run is bounded by the m-sequence.
 *
 * "Additive" means the output is the input XORed with a sequence that does not
 * depend on the data. Applying the same operation twice returns the original,
 * so one function does both scrambling and descrambling. The sequence restarts
 * from a fixed state at the start of every call, which the transmitter and the
 * receiver both do at the start of each frame. A bit error therefore stays a
 * single bit error and does not spread, unlike a self-synchronising scrambler.
 */

#include "dsp/bits.hpp"

namespace mpsk::dsp {

/** Register state at the start of every frame. All ones is a valid nonzero seed. */
constexpr uint32_t kScramblerSeed = 0x7F;

/**
 * XORs @p bits with the scrambler sequence, starting from kScramblerSeed.
 * The sequence has period 127. Calling it twice returns the original bits.
 */
Bits scramble(const Bits& bits);

/** Same operation as scramble(); a separate name keeps receiver code readable. */
inline Bits descramble(const Bits& bits) { return scramble(bits); }

}  // namespace mpsk::dsp
