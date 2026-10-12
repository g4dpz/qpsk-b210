#pragma once

/**
 * @file framer.hpp
 * @brief Frame structure: build a frame into symbols, and decode symbols back.
 *
 * A frame, as a sequence of symbols:
 *
 *   | preamble (64, BPSK) | header (72, BPSK) | payload section (M-PSK) |
 *
 * Guard intervals of silence around the burst are added later, in samples, by
 * the modulator.
 *
 * Header (9 bytes, big-endian, sent as BPSK so it decodes at lower SNR than the
 * payload and before the payload modulation matters):
 *
 *   byte 0     magic and version, kFrameMagicVersion
 *   byte 1     frame type: 0 data, 1 echo
 *   byte 2     payload modulation code = bits per symbol (2 QPSK, 3 8-PSK)
 *   bytes 3-4  frame counter
 *   bytes 5-6  payload length in bytes (not counting the trailer or the CRC)
 *   bytes 7-8  CRC-16/CCITT-FALSE over bytes 0-6
 *
 * Payload section, before scrambling:
 *
 *   payload bytes | echo trailer (12 bytes, echo frames only) | CRC-32
 *
 * The CRC-32 covers the payload and the trailer. The header and the payload
 * section are each scrambled with their own restart of the x^7 + x^4 + 1
 * scrambler, so the receiver can decode the header before it knows anything
 * about the payload. The payload section is then padded with zero bits to a
 * whole number of symbols, which only matters when the bits per symbol does not
 * divide the section length (8-PSK: 3 bits per symbol). The receiver knows the
 * true length from the header and ignores the padding.
 *
 * Corrupted frames are an expected runtime event, not a programming error, so
 * the decoding functions report them in their result and do not throw.
 * Exceptions are reserved for invalid arguments to the builders.
 */

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dsp/bits.hpp"

namespace mpsk::dsp {

/** First header byte: magic nibble 0xB, version nibble 1. */
constexpr uint8_t kFrameMagicVersion = 0xB1;

constexpr std::size_t kHeaderBytes = 9;
constexpr int kHeaderSymbols = 72;  ///< 9 bytes x 8 bits, one bit per BPSK symbol
constexpr std::size_t kEchoTrailerBytes = 12;
constexpr std::size_t kPayloadCrcBytes = 4;

/**
 * Largest payload accepted. The header could carry up to 65535, but a header
 * that passes its CRC with an absurd length would make the receiver wait for
 * that many symbols, so both the builder and the parser enforce this cap.
 * 4096 bytes is about 66 ms of QPSK at 250 ksym/s.
 */
constexpr std::size_t kMaxPayloadBytes = 4096;

/** Echo trailer flag: the responder's payload CRC failed, so its BER is not exact. */
constexpr uint16_t kEchoFlagPayloadCrcFailed = 0x0001;

enum class FrameType : uint8_t { Data = 0, Echo = 1 };

struct FrameHeader {
  FrameType type = FrameType::Data;
  int modulation_order = 4;      ///< payload M, for example 4 for QPSK
  uint16_t frame_counter = 0;
  uint16_t payload_length = 0;   ///< bytes, excluding the trailer and the CRC
};

/**
 * What the responder measured on the forward leg, carried back in an echo frame.
 * Twelve bytes on the wire, big-endian, signed fields in two's complement.
 */
struct EchoTrailer {
  uint16_t echoed_counter = 0;        ///< frame counter of the frame being echoed
  int16_t evm_tenths_db = 0;          ///< EVM, in units of 0.1 dB
  int16_t cfo_hz = 0;                 ///< residual carrier frequency offset, Hz
  int16_t level_tenths_dbfs = 0;      ///< received level, in units of 0.1 dBFS
  uint16_t bit_errors = 0;            ///< bit errors against the expected payload, saturating
  uint16_t flags = 0;                 ///< kEchoFlag* bits
};

// ---- Wire format ------------------------------------------------------------

/** Serialises a header to kHeaderBytes bytes, including its CRC-16. */
std::vector<uint8_t> serialize_header(const FrameHeader& header);

/** Serialises an echo trailer to kEchoTrailerBytes bytes. */
std::vector<uint8_t> serialize_trailer(const EchoTrailer& trailer);

/** Parses kEchoTrailerBytes bytes. @throws std::invalid_argument for any other size. */
EchoTrailer parse_trailer(const std::vector<uint8_t>& bytes);

// ---- Sizes ------------------------------------------------------------------

/** Bits in the payload section before padding: 8 x (payload + trailer + CRC). */
std::size_t payload_section_bits(FrameType type, std::size_t payload_length);

/** Symbols in the payload section: the bit count rounded up to whole symbols. */
std::size_t payload_section_symbols(FrameType type, int bits_per_symbol,
                                    std::size_t payload_length);

/** Symbols in a whole frame: preamble + header + payload section. */
std::size_t frame_symbol_count(FrameType type, int modulation_order, std::size_t payload_length);

// ---- Building ---------------------------------------------------------------

/**
 * Builds the payload section as scrambled, padded bits ready for mapping.
 * Separate from build_frame_symbols() so the bit packing, including padding, can
 * be exercised for any bits-per-symbol value.
 *
 * @param trailer  Required for echo frames and forbidden for data frames.
 * @throws std::invalid_argument for a payload longer than kMaxPayloadBytes, a
 *         trailer on a data frame, a missing trailer on an echo frame, or a
 *         bits_per_symbol below 1.
 */
Bits pack_payload_section(FrameType type, int bits_per_symbol,
                          const std::vector<uint8_t>& payload, const EchoTrailer* trailer);

/**
 * Builds a complete frame as symbols: preamble, header, payload section.
 *
 * @param modulation_order  Payload M. Must be one Constellation accepts as a
 *        payload modulation; others throw with its message.
 * @throws std::invalid_argument as for pack_payload_section(), or for an
 *         unsupported modulation order.
 */
std::vector<std::complex<float>> build_frame_symbols(FrameType type, int modulation_order,
                                                     uint16_t frame_counter,
                                                     const std::vector<uint8_t>& payload,
                                                     const EchoTrailer* trailer = nullptr);

// ---- Decoding ---------------------------------------------------------------

struct HeaderResult {
  bool ok = false;
  FrameHeader header;
  std::string error;  ///< set when !ok
};

/**
 * Parses a header from its kHeaderSymbols hard-decided bits, as received (still
 * scrambled). Checks the CRC first, so noise is rejected as a CRC failure, then
 * the magic, type, modulation and length fields.
 */
HeaderResult parse_header_bits(const Bits& scrambled_bits);

struct PayloadSection {
  bool crc_ok = false;
  std::vector<uint8_t> payload;
  std::optional<EchoTrailer> trailer;  ///< present for echo frames
};

/**
 * Unpacks a received payload section: descrambles, splits payload, trailer and
 * CRC, and checks the CRC-32. Bits beyond the true length (padding) are ignored.
 * The payload and trailer are returned even when the CRC fails, so the caller
 * can still measure the bit error rate. If @p bits is shorter than the section
 * should be, crc_ok is false and the payload is empty.
 */
PayloadSection unpack_payload_section(FrameType type, std::size_t payload_length,
                                      const Bits& bits);

struct DecodedFrame {
  bool header_ok = false;
  FrameHeader header;
  std::string error;  ///< set when the header was rejected or the symbols ran short
  bool payload_crc_ok = false;
  std::vector<uint8_t> payload;
  std::optional<EchoTrailer> trailer;
};

/**
 * Decodes a frame from symbols that start at the first preamble symbol.
 * The preamble is skipped (detection is the receiver's job), the header is
 * decoded, and the payload section is decoded using the modulation the header
 * names. Symbols beyond the end of the frame are ignored.
 */
DecodedFrame decode_frame_symbols(const std::vector<std::complex<float>>& symbols);

}  // namespace mpsk::dsp
