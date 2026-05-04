#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdnv {

/// Maximum SDNV byte length (encodes up to 2^63 - 1).
static constexpr size_t MAX_BYTES = 10;

/// Stack-allocated SDNV result (no heap allocation).
struct Encoded {
    std::array<uint8_t, MAX_BYTES> bytes{};
    uint8_t length = 0;

    const uint8_t* data() const { return bytes.data(); }
};

/// Encode an unsigned integer as an SDNV on the stack.
Encoded encode(uint64_t value);

/// Encode and append directly to an output buffer (avoids intermediate copy).
void encode_into(uint64_t value, std::vector<uint8_t>& out);

/// Decode an SDNV from a byte stream starting at the given offset.
uint64_t decode(const uint8_t* data, size_t length, size_t& offset);

}  // namespace sdnv
