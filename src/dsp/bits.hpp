#pragma once

/**
 * @file bits.hpp
 * @brief Bit-vector helpers shared by the framing, modulation and metrics code.
 *
 * Bits are stored one per byte (0 or 1) in a std::vector. That wastes memory
 * but keeps every stage (scrambler, mapper, BER counter) simple and fast to
 * read, and a frame is only a few thousand bits. Byte packing is always
 * most-significant-bit first, the usual network order.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mpsk::dsp {

/** One bit per element, each 0 or 1. */
using Bits = std::vector<uint8_t>;

/**
 * Unpacks bytes into bits, most significant bit first.
 * 0xA5 becomes 1,0,1,0,0,1,0,1.
 */
Bits bytes_to_bits(const std::vector<uint8_t>& bytes);

/**
 * Packs bits into bytes, most significant bit first.
 * If the bit count is not a multiple of 8, the final byte is padded with zero
 * bits at the low end. Any nonzero input element counts as a 1.
 */
std::vector<uint8_t> bits_to_bytes(const Bits& bits);

/**
 * Counts the positions where two equal-length bit vectors differ.
 * @throws std::invalid_argument if the lengths differ, because a BER computed
 *         over mismatched lengths would be meaningless.
 */
std::size_t count_bit_errors(const Bits& a, const Bits& b);

}  // namespace mpsk::dsp
