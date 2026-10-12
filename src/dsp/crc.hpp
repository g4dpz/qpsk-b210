#pragma once

/**
 * @file crc.hpp
 * @brief CRC-16 (header) and CRC-32 (payload) checksums.
 *
 * Both are computed bit by bit. A frame is a few hundred bytes, so the cost is
 * negligible and the code stays short enough to check by eye against the
 * definitions.
 *
 * A CRC is used here only to decide whether a frame was received intact. Bit
 * error rate is never computed from it: the BER always compares decoded bits
 * with the known transmitted bits.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mpsk::dsp {

/**
 * CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, bits processed
 * most significant first, no final XOR. The check value for the ASCII bytes
 * "123456789" is 0x29B1.
 */
uint16_t crc16_ccitt(const uint8_t* data, std::size_t size);

/** Convenience overload for a byte vector. */
inline uint16_t crc16_ccitt(const std::vector<uint8_t>& data) {
  return crc16_ccitt(data.data(), data.size());
}

/**
 * CRC-32 as used by Ethernet and zlib: reflected polynomial 0xEDB88320, initial
 * value 0xFFFFFFFF, final XOR 0xFFFFFFFF. The check value for "123456789" is
 * 0xCBF43926.
 */
uint32_t crc32_ieee(const uint8_t* data, std::size_t size);

/** Convenience overload for a byte vector. */
inline uint32_t crc32_ieee(const std::vector<uint8_t>& data) {
  return crc32_ieee(data.data(), data.size());
}

}  // namespace mpsk::dsp
