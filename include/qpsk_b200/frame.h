#ifndef QPSK_B200_FRAME_H
#define QPSK_B200_FRAME_H

#include <cstdint>
#include <optional>
#include <vector>

#include "qpsk_b200/types.h"

namespace qpsk_b200 {

/// Builds transmit frames with the wire format:
///   [Preamble 26b][Header 35b][Payload Mb][CRC-32 32b]
///
/// Header layout (35 bits):
///   Frame Length      16 bits  (payload byte count, pre-FEC)
///   Padding Bit Count  4 bits
///   FEC Enabled        1 bit
///   FEC Code Rate      2 bits  (00 = 1/2, 01 = 3/4)
///   Sequence Number   12 bits
///
/// CRC-32 is computed over header + original (pre-FEC) payload bytes.
/// The preamble is the 13-bit Barker sequence repeated twice (26 bits).
class FrameBuilder {
public:
    /// Construct a frame builder with the given preamble bit pattern.
    /// The default preamble is the 13-bit Barker sequence repeated twice.
    explicit FrameBuilder(const std::vector<uint8_t>& preamble);

    /// Build a complete frame as a bit vector (each element is 0 or 1).
    ///
    /// @param payload       Payload bytes (pre-FEC).
    /// @param padding_bits  Number of padding bits added during symbol mapping (0–15).
    /// @param fec_enabled   Whether FEC is applied to this frame's payload.
    /// @param fec_code_rate FEC code rate (meaningful only when fec_enabled is true).
    /// @return Bit vector representing the complete frame.
    std::vector<uint8_t> build_frame(const std::vector<uint8_t>& payload,
                                      uint8_t padding_bits,
                                      bool fec_enabled,
                                      CodeRate fec_code_rate);

    /// Compute CRC-32 (ISO 3309 / ITU-T V.42) over a byte vector.
    /// Uses polynomial 0xEDB88320 (reflected representation).
    static uint32_t compute_crc32(const std::vector<uint8_t>& data);

    /// Return the current sequence number (next frame will use this value).
    uint32_t current_sequence_number() const { return seq_num_; }

private:
    std::vector<uint8_t> preamble_;  ///< Preamble bit pattern (26 bits for Barker×2)
    uint32_t seq_num_ = 0;          ///< Auto-incrementing sequence number (mod 4096)
};

/// Parses received frames from a demodulated bit stream.
///
/// Expects the bit stream to start AFTER the preamble (i.e., the
/// FrameSynchronizer has already located and stripped the preamble).
///
/// Header layout (35 bits):
///   Frame Length      16 bits
///   Padding Bit Count  4 bits
///   FEC Enabled        1 bit
///   FEC Code Rate      2 bits
///   Sequence Number   12 bits
class FrameParser {
public:
    /// Parse a frame from a bit vector (each element is 0 or 1).
    ///
    /// @param bits  Bit vector starting at the header (preamble already stripped).
    /// @return Parsed Frame with crc_valid set, or std::nullopt if the frame
    ///         is too short or CRC verification fails.
    static std::optional<Frame> parse(const std::vector<uint8_t>& bits);

    /// Parse a frame without CRC verification.
    ///
    /// Extracts header fields, payload, and CRC from the bit stream but does
    /// NOT verify the CRC.  The crc_valid field is set to false.  This is
    /// used by the Decoder when FEC is enabled: the coded payload must be
    /// FEC-decoded before CRC can be verified against the original payload.
    ///
    /// @param bits  Bit vector starting at the header (preamble already stripped).
    /// @return Parsed Frame with crc_valid = false, or std::nullopt if too short.
    static std::optional<Frame> parse_unchecked(const std::vector<uint8_t>& bits);

    /// Verify CRC-32 of a parsed frame.
    /// Recomputes CRC over header + payload bytes and compares to frame.crc.
    static bool verify_crc(const Frame& frame);
};

} // namespace qpsk_b200

#endif // QPSK_B200_FRAME_H
