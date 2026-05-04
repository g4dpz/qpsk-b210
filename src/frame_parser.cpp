#include "qpsk_b200/frame.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// Helper: extract N bits from a bit vector starting at `offset`, MSB first.
// ---------------------------------------------------------------------------
namespace {

uint32_t extract_bits(const std::vector<uint8_t>& bits, size_t offset, size_t count) {
    uint32_t value = 0;
    for (size_t i = 0; i < count; ++i) {
        value = (value << 1) | (bits[offset + i] & 1u);
    }
    return value;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FrameParser
// ---------------------------------------------------------------------------

std::optional<Frame> FrameParser::parse(const std::vector<uint8_t>& bits) {
    // Header is 35 bits: 16 + 4 + 1 + 2 + 12
    constexpr size_t HEADER_BITS = 35;
    constexpr size_t CRC_BITS = 32;

    // Minimum frame size (after preamble stripped): header + CRC, no payload
    if (bits.size() < HEADER_BITS + CRC_BITS) {
        return std::nullopt;
    }

    // --- Extract header fields ---
    size_t pos = 0;

    uint16_t frame_length = static_cast<uint16_t>(extract_bits(bits, pos, 16));
    pos += 16;

    uint8_t padding_bits = static_cast<uint8_t>(extract_bits(bits, pos, 4));
    pos += 4;

    bool fec_enabled = (extract_bits(bits, pos, 1) != 0);
    pos += 1;

    uint8_t rate_val = static_cast<uint8_t>(extract_bits(bits, pos, 2));
    pos += 2;

    uint16_t sequence_number = static_cast<uint16_t>(extract_bits(bits, pos, 12));
    pos += 12;

    // --- Determine payload bit count ---
    // Total bits after header = payload bits + CRC bits
    size_t remaining_bits = bits.size() - HEADER_BITS;
    if (remaining_bits < CRC_BITS) {
        return std::nullopt;
    }
    size_t payload_bit_count = remaining_bits - CRC_BITS;

    // Extract payload bytes
    size_t payload_bytes = payload_bit_count / 8;
    std::vector<uint8_t> payload(payload_bytes);
    for (size_t i = 0; i < payload_bytes; ++i) {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; --b) {
            byte |= static_cast<uint8_t>((bits[pos] & 1u) << b);
            ++pos;
        }
        payload[i] = byte;
    }
    // Skip any remaining non-byte-aligned payload bits
    size_t leftover_bits = payload_bit_count % 8;
    pos += leftover_bits;

    // --- Extract CRC-32 (32 bits, MSB first) ---
    uint32_t received_crc = extract_bits(bits, pos, 32);

    // --- Build the Frame ---
    Frame frame;
    frame.header.payload_length = frame_length;
    frame.header.padding_bits = padding_bits;
    frame.header.fec_enabled = fec_enabled;
    frame.header.fec_code_rate = static_cast<CodeRate>(rate_val);
    frame.header.sequence_number = sequence_number;
    frame.payload = std::move(payload);
    frame.crc = received_crc;

    // --- Verify CRC ---
    frame.crc_valid = verify_crc(frame);

    if (!frame.crc_valid) {
        return std::nullopt;
    }

    return frame;
}

std::optional<Frame> FrameParser::parse_unchecked(const std::vector<uint8_t>& bits) {
    constexpr size_t HEADER_BITS = 35;
    constexpr size_t CRC_BITS = 32;

    if (bits.size() < HEADER_BITS + CRC_BITS) {
        return std::nullopt;
    }

    size_t pos = 0;

    uint16_t frame_length = static_cast<uint16_t>(extract_bits(bits, pos, 16));
    pos += 16;

    uint8_t padding_bits = static_cast<uint8_t>(extract_bits(bits, pos, 4));
    pos += 4;

    bool fec_enabled = (extract_bits(bits, pos, 1) != 0);
    pos += 1;

    uint8_t rate_val = static_cast<uint8_t>(extract_bits(bits, pos, 2));
    pos += 2;

    uint16_t sequence_number = static_cast<uint16_t>(extract_bits(bits, pos, 12));
    pos += 12;

    size_t remaining_bits = bits.size() - HEADER_BITS;
    if (remaining_bits < CRC_BITS) {
        return std::nullopt;
    }
    size_t payload_bit_count = remaining_bits - CRC_BITS;

    size_t payload_bytes = payload_bit_count / 8;
    std::vector<uint8_t> payload(payload_bytes);
    for (size_t i = 0; i < payload_bytes; ++i) {
        uint8_t byte = 0;
        for (int b = 7; b >= 0; --b) {
            byte |= static_cast<uint8_t>((bits[pos] & 1u) << b);
            ++pos;
        }
        payload[i] = byte;
    }
    size_t leftover_bits = payload_bit_count % 8;
    pos += leftover_bits;

    uint32_t received_crc = extract_bits(bits, pos, 32);

    Frame frame;
    frame.header.payload_length = frame_length;
    frame.header.padding_bits = padding_bits;
    frame.header.fec_enabled = fec_enabled;
    frame.header.fec_code_rate = static_cast<CodeRate>(rate_val);
    frame.header.sequence_number = sequence_number;
    frame.payload = std::move(payload);
    frame.crc = received_crc;
    frame.crc_valid = false;  // Not verified

    return frame;
}

bool FrameParser::verify_crc(const Frame& frame) {
    // Reconstruct header bytes the same way FrameBuilder packs them for CRC
    uint16_t fl = frame.header.payload_length;
    uint8_t pad = frame.header.padding_bits;
    uint8_t fec_flag = frame.header.fec_enabled ? 1 : 0;
    uint8_t rate_bits = static_cast<uint8_t>(frame.header.fec_code_rate) & 0x03u;
    uint16_t seq = frame.header.sequence_number;

    std::vector<uint8_t> header_bytes(5);
    header_bytes[0] = static_cast<uint8_t>((fl >> 8) & 0xFF);
    header_bytes[1] = static_cast<uint8_t>(fl & 0xFF);
    header_bytes[2] = static_cast<uint8_t>(
        ((pad & 0x0F) << 4) |
        (fec_flag << 3) |
        (rate_bits << 1) |
        ((seq >> 11) & 0x01)
    );
    header_bytes[3] = static_cast<uint8_t>((seq >> 3) & 0xFF);
    header_bytes[4] = static_cast<uint8_t>((seq & 0x07) << 5);

    std::vector<uint8_t> crc_data;
    crc_data.reserve(header_bytes.size() + frame.payload.size());
    crc_data.insert(crc_data.end(), header_bytes.begin(), header_bytes.end());
    crc_data.insert(crc_data.end(), frame.payload.begin(), frame.payload.end());

    uint32_t computed_crc = FrameBuilder::compute_crc32(crc_data);
    return computed_crc == frame.crc;
}

} // namespace qpsk_b200
