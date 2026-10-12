#include "dsp/framer.hpp"

#include <stdexcept>
#include <string>

#include "dsp/constellation.hpp"
#include "dsp/crc.hpp"
#include "dsp/preamble.hpp"
#include "dsp/scrambler.hpp"

namespace mpsk::dsp {

namespace {

using cf = std::complex<float>;

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v & 0xFF));
}

uint16_t get_u16(const std::vector<uint8_t>& in, std::size_t pos) {
  return static_cast<uint16_t>((static_cast<uint16_t>(in[pos]) << 8) | in[pos + 1]);
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
  for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>((v >> shift) & 0xFF));
}

uint32_t get_u32(const std::vector<uint8_t>& in, std::size_t pos) {
  return (static_cast<uint32_t>(in[pos]) << 24) | (static_cast<uint32_t>(in[pos + 1]) << 16) |
         (static_cast<uint32_t>(in[pos + 2]) << 8) | static_cast<uint32_t>(in[pos + 3]);
}

/** log2 of a power of two, or 0 if @p n is not a power of two of at least 2. */
int log2_if_power_of_two(int n) {
  if (n < 2 || (n & (n - 1)) != 0) return 0;
  int bits = 0;
  while ((1 << bits) < n) ++bits;
  return bits;
}

std::size_t round_up(std::size_t n, std::size_t multiple) {
  return ((n + multiple - 1) / multiple) * multiple;
}

std::string hex_byte(uint8_t v) {
  const char* digits = "0123456789ABCDEF";
  return std::string{digits[v >> 4], digits[v & 0xF]};
}

}  // namespace

// ---- Wire format ------------------------------------------------------------

std::vector<uint8_t> serialize_header(const FrameHeader& header) {
  const int code = log2_if_power_of_two(header.modulation_order);
  if (code == 0) {
    throw std::invalid_argument("serialize_header: modulation order " +
                                std::to_string(header.modulation_order) +
                                " is not a power of two");
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(kHeaderBytes);
  bytes.push_back(kFrameMagicVersion);
  bytes.push_back(static_cast<uint8_t>(header.type));
  bytes.push_back(static_cast<uint8_t>(code));
  put_u16(bytes, header.frame_counter);
  put_u16(bytes, header.payload_length);
  put_u16(bytes, crc16_ccitt(bytes));  // over bytes 0-6
  return bytes;
}

std::vector<uint8_t> serialize_trailer(const EchoTrailer& t) {
  std::vector<uint8_t> bytes;
  bytes.reserve(kEchoTrailerBytes);
  put_u16(bytes, t.echoed_counter);
  put_u16(bytes, static_cast<uint16_t>(t.evm_tenths_db));
  put_u16(bytes, static_cast<uint16_t>(t.cfo_hz));
  put_u16(bytes, static_cast<uint16_t>(t.level_tenths_dbfs));
  put_u16(bytes, t.bit_errors);
  put_u16(bytes, t.flags);
  return bytes;
}

EchoTrailer parse_trailer(const std::vector<uint8_t>& bytes) {
  if (bytes.size() != kEchoTrailerBytes) {
    throw std::invalid_argument("parse_trailer: expected " + std::to_string(kEchoTrailerBytes) +
                                " bytes, got " + std::to_string(bytes.size()));
  }
  EchoTrailer t;
  t.echoed_counter = get_u16(bytes, 0);
  t.evm_tenths_db = static_cast<int16_t>(get_u16(bytes, 2));
  t.cfo_hz = static_cast<int16_t>(get_u16(bytes, 4));
  t.level_tenths_dbfs = static_cast<int16_t>(get_u16(bytes, 6));
  t.bit_errors = get_u16(bytes, 8);
  t.flags = get_u16(bytes, 10);
  return t;
}

// ---- Sizes ------------------------------------------------------------------

std::size_t payload_section_bits(FrameType type, std::size_t payload_length) {
  const std::size_t trailer = (type == FrameType::Echo) ? kEchoTrailerBytes : 0;
  return 8 * (payload_length + trailer + kPayloadCrcBytes);
}

std::size_t payload_section_symbols(FrameType type, int bits_per_symbol,
                                    std::size_t payload_length) {
  if (bits_per_symbol < 1) {
    throw std::invalid_argument("payload_section_symbols: bits per symbol must be at least 1");
  }
  const std::size_t bps = static_cast<std::size_t>(bits_per_symbol);
  return round_up(payload_section_bits(type, payload_length), bps) / bps;
}

std::size_t frame_symbol_count(FrameType type, int modulation_order, std::size_t payload_length) {
  const int bps = log2_if_power_of_two(modulation_order);
  if (bps == 0) {
    throw std::invalid_argument("frame_symbol_count: modulation order must be a power of two");
  }
  return static_cast<std::size_t>(kPreambleSymbols) + static_cast<std::size_t>(kHeaderSymbols) +
         payload_section_symbols(type, bps, payload_length);
}

// ---- Building ---------------------------------------------------------------

Bits pack_payload_section(FrameType type, int bits_per_symbol,
                          const std::vector<uint8_t>& payload, const EchoTrailer* trailer) {
  if (bits_per_symbol < 1) {
    throw std::invalid_argument("pack_payload_section: bits per symbol must be at least 1");
  }
  if (payload.size() > kMaxPayloadBytes) {
    throw std::invalid_argument("pack_payload_section: payload of " +
                                std::to_string(payload.size()) + " bytes exceeds the " +
                                std::to_string(kMaxPayloadBytes) + " byte limit");
  }
  if (type == FrameType::Echo && trailer == nullptr) {
    throw std::invalid_argument("pack_payload_section: an echo frame needs an echo trailer");
  }
  if (type == FrameType::Data && trailer != nullptr) {
    throw std::invalid_argument("pack_payload_section: a data frame cannot carry an echo trailer");
  }

  std::vector<uint8_t> bytes = payload;
  if (trailer != nullptr) {
    const std::vector<uint8_t> t = serialize_trailer(*trailer);
    bytes.insert(bytes.end(), t.begin(), t.end());
  }
  put_u32(bytes, crc32_ieee(bytes));  // CRC over payload and trailer

  Bits bits = scramble(bytes_to_bits(bytes));

  // Pad with zeros to a whole number of symbols. The receiver truncates to the
  // true length before descrambling, so the padding value does not matter.
  bits.resize(round_up(bits.size(), static_cast<std::size_t>(bits_per_symbol)), 0);
  return bits;
}

std::vector<cf> build_frame_symbols(FrameType type, int modulation_order, uint16_t frame_counter,
                                    const std::vector<uint8_t>& payload,
                                    const EchoTrailer* trailer) {
  // Constructing the constellation first rejects unsupported orders with its
  // own message (for example, 8-PSK in stage 1).
  const Constellation payload_constellation(modulation_order);
  if (log2_if_power_of_two(modulation_order) == 1) {
    throw std::invalid_argument("build_frame_symbols: BPSK is not a payload modulation");
  }

  if (payload.size() > kMaxPayloadBytes) {
    throw std::invalid_argument("build_frame_symbols: payload of " +
                                std::to_string(payload.size()) + " bytes exceeds the " +
                                std::to_string(kMaxPayloadBytes) + " byte limit");
  }

  FrameHeader header;
  header.type = type;
  header.modulation_order = modulation_order;
  header.frame_counter = frame_counter;
  header.payload_length = static_cast<uint16_t>(payload.size());

  const Bits payload_bits =
      pack_payload_section(type, payload_constellation.bits_per_symbol(), payload, trailer);
  const Bits header_bits = scramble(bytes_to_bits(serialize_header(header)));

  std::vector<cf> frame;
  frame.reserve(frame_symbol_count(type, modulation_order, payload.size()));

  const std::vector<cf>& preamble = preamble_symbols();
  frame.insert(frame.end(), preamble.begin(), preamble.end());

  const std::vector<cf> header_symbols = map_bits(Constellation(2), header_bits);
  frame.insert(frame.end(), header_symbols.begin(), header_symbols.end());

  const std::vector<cf> payload_symbols = map_bits(payload_constellation, payload_bits);
  frame.insert(frame.end(), payload_symbols.begin(), payload_symbols.end());
  return frame;
}

// ---- Decoding ---------------------------------------------------------------

HeaderResult parse_header_bits(const Bits& scrambled_bits) {
  HeaderResult result;
  if (scrambled_bits.size() != static_cast<std::size_t>(kHeaderSymbols)) {
    result.error = "header has " + std::to_string(scrambled_bits.size()) + " bits, expected " +
                   std::to_string(kHeaderSymbols);
    return result;
  }
  const std::vector<uint8_t> bytes = bits_to_bytes(descramble(scrambled_bits));

  // CRC first: on noise this is the check that rejects the frame.
  const uint16_t received_crc = get_u16(bytes, 7);
  const std::vector<uint8_t> covered(bytes.begin(), bytes.begin() + 7);
  if (crc16_ccitt(covered) != received_crc) {
    result.error = "header CRC failed";
    return result;
  }
  if (bytes[0] != kFrameMagicVersion) {
    result.error = "header magic/version byte is 0x" + hex_byte(bytes[0]) + ", expected 0x" +
                   hex_byte(kFrameMagicVersion);
    return result;
  }
  if (bytes[1] > static_cast<uint8_t>(FrameType::Echo)) {
    result.error = "unknown frame type " + std::to_string(bytes[1]);
    return result;
  }

  // Modulation code is bits per symbol. 1 would be BPSK, which only the
  // preamble and header use, so it is not a valid payload modulation.
  const int code = bytes[2];
  if (code < 2 || code > 8) {
    result.error = "invalid payload modulation code " + std::to_string(code);
    return result;
  }
  const int order = 1 << code;
  try {
    const Constellation check(order);  // throws with a clear message if not enabled
    (void)check;
  } catch (const std::exception& e) {
    result.error = std::string("frame uses a modulation this build cannot decode: ") + e.what();
    return result;
  }

  const uint16_t length = get_u16(bytes, 5);
  if (length > kMaxPayloadBytes) {
    result.error = "header payload length " + std::to_string(length) + " exceeds the " +
                   std::to_string(kMaxPayloadBytes) + " byte limit";
    return result;
  }

  result.header.type = static_cast<FrameType>(bytes[1]);
  result.header.modulation_order = order;
  result.header.frame_counter = get_u16(bytes, 3);
  result.header.payload_length = length;
  result.ok = true;
  return result;
}

PayloadSection unpack_payload_section(FrameType type, std::size_t payload_length,
                                      const Bits& bits) {
  PayloadSection section;
  const std::size_t true_bits = payload_section_bits(type, payload_length);
  if (bits.size() < true_bits) return section;  // too short: crc_ok stays false

  // Drop the padding, then undo the scrambler.
  const Bits unpadded(bits.begin(), bits.begin() + static_cast<std::ptrdiff_t>(true_bits));
  const std::vector<uint8_t> bytes = bits_to_bytes(descramble(unpadded));

  const std::size_t trailer_bytes = (type == FrameType::Echo) ? kEchoTrailerBytes : 0;
  const std::size_t covered = payload_length + trailer_bytes;

  section.payload.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(payload_length));
  if (type == FrameType::Echo) {
    section.trailer = parse_trailer(std::vector<uint8_t>(
        bytes.begin() + static_cast<std::ptrdiff_t>(payload_length),
        bytes.begin() + static_cast<std::ptrdiff_t>(covered)));
  }
  const uint32_t received_crc = get_u32(bytes, covered);
  const std::vector<uint8_t> covered_bytes(bytes.begin(),
                                           bytes.begin() + static_cast<std::ptrdiff_t>(covered));
  section.crc_ok = (crc32_ieee(covered_bytes) == received_crc);
  return section;
}

DecodedFrame decode_frame_symbols(const std::vector<cf>& symbols) {
  DecodedFrame frame;
  const std::size_t header_start = static_cast<std::size_t>(kPreambleSymbols);
  const std::size_t payload_start = header_start + static_cast<std::size_t>(kHeaderSymbols);

  if (symbols.size() < payload_start) {
    frame.error = "only " + std::to_string(symbols.size()) +
                  " symbols, too few to hold the preamble and header";
    return frame;
  }

  const std::vector<cf> header_symbols(symbols.begin() + static_cast<std::ptrdiff_t>(header_start),
                                       symbols.begin() + static_cast<std::ptrdiff_t>(payload_start));
  const HeaderResult header = parse_header_bits(demap_symbols(Constellation(2), header_symbols));
  if (!header.ok) {
    frame.error = header.error;
    return frame;
  }
  frame.header_ok = true;
  frame.header = header.header;

  const Constellation payload_constellation(header.header.modulation_order);
  const std::size_t needed = payload_section_symbols(
      header.header.type, payload_constellation.bits_per_symbol(), header.header.payload_length);
  if (symbols.size() < payload_start + needed) {
    frame.error = "frame is truncated: " + std::to_string(symbols.size() - payload_start) +
                  " payload symbols received, " + std::to_string(needed) + " needed";
    return frame;
  }

  const std::vector<cf> payload_symbols(
      symbols.begin() + static_cast<std::ptrdiff_t>(payload_start),
      symbols.begin() + static_cast<std::ptrdiff_t>(payload_start + needed));
  const PayloadSection section =
      unpack_payload_section(header.header.type, header.header.payload_length,
                             demap_symbols(payload_constellation, payload_symbols));
  frame.payload_crc_ok = section.crc_ok;
  frame.payload = section.payload;
  frame.trailer = section.trailer;
  return frame;
}

}  // namespace mpsk::dsp
