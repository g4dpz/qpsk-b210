#include "ltp_cla/sdnv.h"

#include <stdexcept>
#include <string>

namespace sdnv {

// ---------------------------------------------------------------------------
// encode — stack-allocated, no heap
// ---------------------------------------------------------------------------

Encoded encode(uint64_t value) {
    Encoded result;

    if (value == 0) {
        result.bytes[0] = 0x00;
        result.length = 1;
        return result;
    }

    // Build from LSB to MSB in a temp buffer, then reverse into result.
    uint8_t tmp[MAX_BYTES];
    uint8_t len = 0;
    bool first = true;

    while (value > 0) {
        auto byte = static_cast<uint8_t>(value & 0x7F);
        if (!first) byte |= 0x80;
        first = false;
        tmp[len++] = byte;
        value >>= 7;
    }

    // Reverse into result (big-endian).
    for (uint8_t i = 0; i < len; ++i) {
        result.bytes[i] = tmp[len - 1 - i];
    }
    result.length = len;
    return result;
}

// ---------------------------------------------------------------------------
// encode_into — append directly to output buffer
// ---------------------------------------------------------------------------

void encode_into(uint64_t value, std::vector<uint8_t>& out) {
    auto enc = encode(value);
    out.insert(out.end(), enc.bytes.data(), enc.bytes.data() + enc.length);
}

// ---------------------------------------------------------------------------
// decode — unchanged
// ---------------------------------------------------------------------------

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

        if (bytes_consumed > MAX_BYTES) {
            throw std::runtime_error(
                "SDNV decode error: sequence exceeds maximum of 10 bytes "
                "(value would exceed 2^63 - 1)");
        }

        value = (value << 7) | (byte & 0x7F);

        if ((byte & 0x80) == 0) {
            return value;
        }
    }

    throw std::runtime_error(
        "SDNV decode error: truncated sequence — continuation bit set on "
        "last available byte (consumed " +
        std::to_string(bytes_consumed) + " bytes)");
}

}  // namespace sdnv
