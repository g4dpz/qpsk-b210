#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdnv {

/// Encode an unsigned integer as an SDNV (Self-Delimiting Numeric Value).
///
/// Uses 7 data bits per byte with the high bit as a continuation flag
/// (1 = more bytes follow, 0 = final byte). Big-endian byte order.
/// Minimum number of bytes required to represent the value.
///
/// @param value  Unsigned integer in [0, 2^63 - 1].
/// @return       SDNV-encoded byte sequence.
std::vector<uint8_t> encode(uint64_t value);

/// Decode an SDNV from a byte stream starting at the given offset.
///
/// Consumes only the bytes belonging to the encoded value and advances
/// @p offset past the last consumed byte.
///
/// @param data    Pointer to the byte buffer containing the SDNV.
/// @param length  Total length of the byte buffer.
/// @param offset  On entry, the position of the first SDNV byte.
///                On exit, the position immediately after the last SDNV byte.
/// @return        The decoded unsigned integer.
/// @throws std::runtime_error  If the SDNV is truncated (continuation bit set
///                             on the last available byte) or oversized (>10 bytes).
uint64_t decode(const uint8_t* data, size_t length, size_t& offset);

}  // namespace sdnv
