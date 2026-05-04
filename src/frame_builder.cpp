#include "qpsk_b200/frame.h"

#include <array>
#include <cstdint>
#include <vector>

namespace qpsk_b200 {

// ---------------------------------------------------------------------------
// CRC-32 lookup table (ISO 3309 / ITU-T V.42, reflected polynomial 0xEDB88320)
// ---------------------------------------------------------------------------
namespace {

constexpr std::array<uint32_t, 256> make_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = make_crc32_table();

} // anonymous namespace

// ---------------------------------------------------------------------------
// FrameBuilder
// ---------------------------------------------------------------------------

FrameBuilder::FrameBuilder(const std::vector<uint8_t>& preamble)
    : preamble_(preamble) {}

uint32_t FrameBuilder::compute_crc32(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data) {
        crc = CRC32_TABLE[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> FrameBuilder::build_frame(
    const std::vector<uint8_t>& payload,
    uint8_t padding_bits,
    bool fec_enabled,
    CodeRate fec_code_rate)
{
    // --- Build the header + payload byte vector for CRC computation ---
    // Header fields (packed into bytes for CRC):
    //   Frame Length      16 bits (payload byte count)
    //   Padding Bit Count  4 bits
    //   FEC Enabled        1 bit
    //   FEC Code Rate      2 bits
    //   Sequence Number   12 bits
    //   Total: 35 bits → 5 bytes (with 5 padding bits at the end of byte 5)

    uint16_t frame_length = static_cast<uint16_t>(payload.size());
    uint16_t seq = static_cast<uint16_t>(seq_num_ & 0x0FFFu);
    uint8_t fec_flag = fec_enabled ? 1 : 0;
    uint8_t rate_bits = static_cast<uint8_t>(fec_code_rate) & 0x03u;

    // Pack header into 5 bytes for CRC computation:
    // Byte 0: frame_length[15:8]
    // Byte 1: frame_length[7:0]
    // Byte 2: padding_bits[3:0] | fec_flag | rate_bits[1:0] | seq[11]
    // Byte 3: seq[10:3]
    // Byte 4: seq[2:0] | 00000 (5 zero-padding bits)
    std::vector<uint8_t> header_bytes(5);
    header_bytes[0] = static_cast<uint8_t>((frame_length >> 8) & 0xFF);
    header_bytes[1] = static_cast<uint8_t>(frame_length & 0xFF);
    header_bytes[2] = static_cast<uint8_t>(
        ((padding_bits & 0x0F) << 4) |
        (fec_flag << 3) |
        (rate_bits << 1) |
        ((seq >> 11) & 0x01)
    );
    header_bytes[3] = static_cast<uint8_t>((seq >> 3) & 0xFF);
    header_bytes[4] = static_cast<uint8_t>((seq & 0x07) << 5);

    // CRC is computed over header bytes + payload bytes
    std::vector<uint8_t> crc_data;
    crc_data.reserve(header_bytes.size() + payload.size());
    crc_data.insert(crc_data.end(), header_bytes.begin(), header_bytes.end());
    crc_data.insert(crc_data.end(), payload.begin(), payload.end());
    uint32_t crc = compute_crc32(crc_data);

    // --- Build the frame as a bit vector ---
    std::vector<uint8_t> bits;
    // Reserve approximate size: preamble + header(35) + payload*8 + crc(32)
    bits.reserve(preamble_.size() + 35 + payload.size() * 8 + 32);

    // 1. Preamble
    bits.insert(bits.end(), preamble_.begin(), preamble_.end());

    // 2. Header — 35 bits, MSB first
    // Frame Length (16 bits)
    for (int i = 15; i >= 0; --i) {
        bits.push_back((frame_length >> i) & 1u);
    }
    // Padding Bit Count (4 bits)
    for (int i = 3; i >= 0; --i) {
        bits.push_back((padding_bits >> i) & 1u);
    }
    // FEC Enabled (1 bit)
    bits.push_back(fec_flag);
    // FEC Code Rate (2 bits)
    for (int i = 1; i >= 0; --i) {
        bits.push_back((rate_bits >> i) & 1u);
    }
    // Sequence Number (12 bits)
    for (int i = 11; i >= 0; --i) {
        bits.push_back((seq >> i) & 1u);
    }

    // 3. Payload bits (MSB first per byte)
    for (uint8_t byte : payload) {
        for (int i = 7; i >= 0; --i) {
            bits.push_back((byte >> i) & 1u);
        }
    }

    // 4. CRC-32 (32 bits, MSB first)
    for (int i = 31; i >= 0; --i) {
        bits.push_back((crc >> i) & 1u);
    }

    // Increment sequence number (mod 4096)
    seq_num_ = (seq_num_ + 1) & 0x0FFFu;

    return bits;
}

} // namespace qpsk_b200
