#include "ltp_cla/sdnv.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace sdnv {

// ---------------------------------------------------------------------------
// encode — 7 data bits per byte, high-bit continuation, big-endian, min bytes
// ---------------------------------------------------------------------------

std::vector<uint8_t> encode(uint64_t value) {
    // Special case: zero encodes as a single 0x00 byte.
    if (value == 0) {
        return {0x00};
    }

    // Build the SDNV from least-significant to most-significant 7-bit group,
    // then reverse to get big-endian order.
    std::vector<uint8_t> result;
    bool first = true;
    while (value > 0) {
        auto byte = static_cast<uint8_t>(value & 0x7F);
        if (first) {
            // Last byte in the encoded sequence (no continuation bit).
            first = false;
        } else {
            // Not the last byte — set continuation bit.
            byte |= 0x80;
        }
        result.push_back(byte);
        value >>= 7;
    }

    // Reverse to big-endian order (most-significant group first).
    std::reverse(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// decode — consume correct bytes, reject truncated and oversized sequences
// ---------------------------------------------------------------------------

static constexpr size_t MAX_SDNV_BYTES = 10;

uint64_t decode(const uint8_t* data, size_t length, size_t& offset) {
    if (offset >= length) {
        throw std::runtime_error(
            "SDNV decode error: no data available at offset " +
            std::to_string(offset));
    }

    uint64_t value = 0;
    size_t bytes_consumed = 0;

    while (offset < length) {
        uint8_t byte = data[offset];
        ++offset;
        ++bytes_consumed;

        if (bytes_consumed > MAX_SDNV_BYTES) {
            throw std::runtime_error(
                "SDNV decode error: sequence exceeds maximum of 10 bytes "
                "(value would exceed 2^63 - 1)");
        }

        // Accumulate the 7 data bits.
        value = (value << 7) | (byte & 0x7F);

        // If the high bit is clear, this is the final byte.
        if ((byte & 0x80) == 0) {
            return value;
        }
    }

    // We ran out of data while the continuation bit was still set.
    throw std::runtime_error(
        "SDNV decode error: truncated sequence — continuation bit set on "
        "last available byte (consumed " +
        std::to_string(bytes_consumed) + " bytes)");
}

}  // namespace sdnv
