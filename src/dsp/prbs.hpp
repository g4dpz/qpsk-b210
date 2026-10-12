#pragma once

/**
 * @file prbs.hpp
 * @brief Pseudorandom test payloads that any unit can regenerate.
 *
 * The transmitter fills a test frame with prbs_bits(seed, frame_counter, n).
 * Anything that knows the seed and reads the frame counter from the header can
 * regenerate exactly the same bits, so a receiver measures its bit error rate
 * without the payload ever being sent separately. The echo responder uses this
 * to measure the forward-link BER.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/bits.hpp"

namespace mpsk::dsp {

/**
 * Generates @p count payload bits for one frame.
 *
 * The bits come from PRBS31 (polynomial x^31 + x^28 + 1, ITU-T O.150), whose
 * period is 2^31 - 1. The (seed, frame_counter) pair is mixed through a 64-bit
 * hash to choose the starting state, so different frames start at unrelated
 * points of the same long sequence. Overlap between two frames would need their
 * start points to fall within one frame length of each other out of 2^31, which
 * is negligible for frames of a few thousand bits.
 *
 * The first @p n bits of a longer request equal a shorter request with the same
 * arguments, so a receiver may regenerate only as many bits as it decoded.
 */
Bits prbs_bits(uint32_t seed, uint32_t frame_counter, std::size_t count);

/**
 * The same sequence packed into bytes, most significant bit first: the payload
 * of a test frame. Equal to bits_to_bytes(prbs_bits(seed, frame_counter, 8 * n)).
 */
std::vector<uint8_t> prbs_bytes(uint32_t seed, uint32_t frame_counter, std::size_t count);

}  // namespace mpsk::dsp
