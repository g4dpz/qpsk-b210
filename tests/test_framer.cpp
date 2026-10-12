// Tests for frame building and decoding. Requirements 2.2, 2.3, 4.3, 8.7.

#include <complex>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "dsp/bits.hpp"
#include "dsp/constellation.hpp"
#include "dsp/crc.hpp"
#include "dsp/framer.hpp"
#include "dsp/preamble.hpp"
#include "dsp/scrambler.hpp"
#include "test_harness.hpp"

using namespace mpsk::dsp;
using cf = std::complex<float>;

namespace {

constexpr std::size_t kHeaderStart = kPreambleSymbols;
constexpr std::size_t kPayloadStart = kPreambleSymbols + kHeaderSymbols;

std::vector<uint8_t> random_bytes(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  std::vector<uint8_t> v(n);
  for (auto& b : v) b = static_cast<uint8_t>(rng() & 0xFF);
  return v;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

bool order_is_enabled(int order) {
  for (const int o : Constellation::supported_orders()) {
    if (o == order) return true;
  }
  return false;
}

/** Rewrites the header CRC, so a hand-edited header is valid except for the edit. */
HeaderResult parse_edited_header(std::vector<uint8_t> bytes) {
  const std::vector<uint8_t> covered(bytes.begin(), bytes.begin() + 7);
  const uint16_t crc = crc16_ccitt(covered);
  bytes[7] = static_cast<uint8_t>(crc >> 8);
  bytes[8] = static_cast<uint8_t>(crc & 0xFF);
  return parse_header_bits(scramble(bytes_to_bits(bytes)));
}

std::vector<uint8_t> good_header_bytes() {
  FrameHeader h;
  h.type = FrameType::Data;
  h.modulation_order = 4;
  h.frame_counter = 0x1234;
  h.payload_length = 128;
  return serialize_header(h);
}

/** Rotates a QPSK-or-denser symbol by one constellation step: a one-bit error. */
cf to_next_neighbour(cf s, int order) {
  const double angle = 2.0 * 3.14159265358979323846 / order;
  return s * cf(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
}

}  // namespace

// ---- Wire format ------------------------------------------------------------

TEST(header_is_nine_bytes_big_endian_with_a_crc) {
  const std::vector<uint8_t> h = good_header_bytes();
  CHECK(h.size() == kHeaderBytes);
  CHECK(h[0] == 0xB1);          // magic and version
  CHECK(h[1] == 0x00);          // data frame
  CHECK(h[2] == 0x02);          // QPSK: 2 bits per symbol
  CHECK(h[3] == 0x12 && h[4] == 0x34);  // frame counter, most significant byte first
  CHECK(h[5] == 0x00 && h[6] == 0x80);  // payload length 128

  const std::vector<uint8_t> covered(h.begin(), h.begin() + 7);
  const uint16_t crc = crc16_ccitt(covered);
  CHECK(h[7] == static_cast<uint8_t>(crc >> 8));
  CHECK(h[8] == static_cast<uint8_t>(crc & 0xFF));
}

TEST(trailer_is_twelve_bytes_big_endian_with_signed_fields) {
  EchoTrailer t;
  t.echoed_counter = 0x0102;
  t.evm_tenths_db = -2;          // 0xFFFE
  t.cfo_hz = -7800;              // 0xE188
  t.level_tenths_dbfs = 0x0304;
  t.bit_errors = 0xFFFF;
  t.flags = kEchoFlagPayloadCrcFailed;

  const std::vector<uint8_t> b = serialize_trailer(t);
  CHECK(b.size() == kEchoTrailerBytes);
  CHECK(b[0] == 0x01 && b[1] == 0x02);
  CHECK(b[2] == 0xFF && b[3] == 0xFE);
  CHECK(b[4] == 0xE1 && b[5] == 0x88);
  CHECK(b[6] == 0x03 && b[7] == 0x04);
  CHECK(b[8] == 0xFF && b[9] == 0xFF);
  CHECK(b[10] == 0x00 && b[11] == 0x01);

  const EchoTrailer back = parse_trailer(b);
  CHECK(back.echoed_counter == 0x0102);
  CHECK(back.evm_tenths_db == -2);
  CHECK(back.cfo_hz == -7800);
  CHECK(back.level_tenths_dbfs == 0x0304);
  CHECK(back.bit_errors == 0xFFFF);
  CHECK(back.flags == kEchoFlagPayloadCrcFailed);
}

TEST(parse_trailer_rejects_a_wrong_size) {
  CHECK_THROWS(parse_trailer(std::vector<uint8_t>(11)));
  CHECK_THROWS(parse_trailer(std::vector<uint8_t>(13)));
}

// ---- Sizes ------------------------------------------------------------------

TEST(default_frame_sizes_match_the_design) {
  // 128-byte payload, QPSK: 64 + 72 + (8 * (128 + 4) / 2) = 664 symbols.
  CHECK(frame_symbol_count(FrameType::Data, 4, 128) == 664);
  // An echo frame adds the 12-byte trailer: 8 * 12 / 2 = 48 more symbols.
  CHECK(frame_symbol_count(FrameType::Echo, 4, 128) == 712);
  // 8-PSK needs no padding here: 1056 bits / 3 = 352 symbols.
  CHECK(frame_symbol_count(FrameType::Data, 8, 128) == 64 + 72 + 352);
}

TEST(payload_section_symbols_round_up_to_whole_symbols) {
  CHECK(payload_section_bits(FrameType::Data, 127) == 8 * (127 + 4));
  CHECK(payload_section_symbols(FrameType::Data, 3, 127) == 350);  // 1048 bits -> 349.33 -> 350
  CHECK(payload_section_symbols(FrameType::Data, 3, 128) == 352);  // 1056 bits, exact
  CHECK(payload_section_symbols(FrameType::Data, 2, 127) == 524);
  CHECK_THROWS(payload_section_symbols(FrameType::Data, 0, 10));
}

TEST(built_frames_have_the_computed_length) {
  for (const int order : Constellation::supported_orders()) {
    for (const std::size_t n : {0u, 1u, 7u, 128u, 1000u}) {
      const auto frame = build_frame_symbols(FrameType::Data, order, 1, random_bytes(n, 1));
      CHECK(frame.size() == frame_symbol_count(FrameType::Data, order, n));
    }
  }
}

// ---- Round trips -----------------------------------------------------------

TEST(data_frames_round_trip_for_many_sizes) {
  for (const int order : Constellation::supported_orders()) {
    for (const std::size_t n : {0u, 1u, 2u, 3u, 7u, 64u, 128u, 1000u, 4096u}) {
      const std::vector<uint8_t> payload = random_bytes(n, static_cast<unsigned>(n) + 5);
      const auto frame = build_frame_symbols(FrameType::Data, order, 777, payload);
      const DecodedFrame d = decode_frame_symbols(frame);

      CHECK(d.header_ok);
      CHECK(d.payload_crc_ok);
      CHECK(d.header.type == FrameType::Data);
      CHECK(d.header.modulation_order == order);
      CHECK(d.header.frame_counter == 777);
      CHECK(d.header.payload_length == n);
      CHECK(d.payload == payload);
      CHECK(!d.trailer.has_value());
    }
  }
}

TEST(echo_frames_round_trip_with_their_trailer) {
  EchoTrailer t;
  t.echoed_counter = 4242;
  t.evm_tenths_db = -231;
  t.cfo_hz = -1234;
  t.level_tenths_dbfs = -456;
  t.bit_errors = 17;
  t.flags = kEchoFlagPayloadCrcFailed;

  for (const int order : Constellation::supported_orders()) {
    const std::vector<uint8_t> payload = random_bytes(128, 9);
    const auto frame = build_frame_symbols(FrameType::Echo, order, 4243, payload, &t);
    CHECK(frame.size() == frame_symbol_count(FrameType::Echo, order, 128));

    const DecodedFrame d = decode_frame_symbols(frame);
    CHECK(d.header_ok);
    CHECK(d.payload_crc_ok);
    CHECK(d.header.type == FrameType::Echo);
    CHECK(d.payload == payload);
    REQUIRE(d.trailer.has_value());
    CHECK(d.trailer->echoed_counter == 4242);
    CHECK(d.trailer->evm_tenths_db == -231);
    CHECK(d.trailer->cfo_hz == -1234);
    CHECK(d.trailer->level_tenths_dbfs == -456);
    CHECK(d.trailer->bit_errors == 17);
    CHECK(d.trailer->flags == kEchoFlagPayloadCrcFailed);
  }
}

TEST(frame_counter_covers_the_full_16_bit_range) {
  for (const unsigned counter : {0u, 1u, 255u, 256u, 32768u, 65535u}) {
    const auto frame = build_frame_symbols(FrameType::Data, 4, static_cast<uint16_t>(counter), {1, 2, 3});
    CHECK(decode_frame_symbols(frame).header.frame_counter == counter);
  }
}

TEST(frames_start_with_the_preamble) {
  const auto frame = build_frame_symbols(FrameType::Data, 4, 1, random_bytes(16, 2));
  const auto& preamble = preamble_symbols();
  for (std::size_t i = 0; i < preamble.size(); ++i) CHECK(frame[i] == preamble[i]);
}

TEST(building_is_deterministic) {
  const auto a = build_frame_symbols(FrameType::Data, 4, 5, random_bytes(40, 3));
  const auto b = build_frame_symbols(FrameType::Data, 4, 5, random_bytes(40, 3));
  CHECK(a == b);
}

TEST(trailing_symbols_beyond_the_frame_are_ignored) {
  const std::vector<uint8_t> payload = random_bytes(50, 4);
  auto frame = build_frame_symbols(FrameType::Data, 4, 9, payload);
  frame.insert(frame.end(), 200, cf(0.0f, 0.0f));  // the guard interval
  const DecodedFrame d = decode_frame_symbols(frame);
  CHECK(d.header_ok);
  CHECK(d.payload_crc_ok);
  CHECK(d.payload == payload);
}

// ---- Scrambling -------------------------------------------------------------

TEST(header_is_scrambled_on_the_wire) {
  FrameHeader h;
  h.frame_counter = 0x1234;
  h.payload_length = 128;
  const auto frame = build_frame_symbols(FrameType::Data, 4, 0x1234, random_bytes(128, 6));
  const auto unscrambled = map_bits(Constellation(2), bytes_to_bits(serialize_header(h)));

  int differing = 0;
  for (std::size_t i = 0; i < unscrambled.size(); ++i) {
    if (frame[kHeaderStart + i] != unscrambled[i]) ++differing;
  }
  CHECK(differing > 20);  // roughly half the 72 symbols differ
}

TEST(long_constant_payloads_do_not_produce_long_constant_symbol_runs) {
  // The timing loop needs symbol transitions. Without scrambling, 1024 zero bytes
  // would be 4096 identical symbols.
  for (const uint8_t fill : {uint8_t{0x00}, uint8_t{0xFF}}) {
    const auto frame = build_frame_symbols(FrameType::Data, 4, 1, std::vector<uint8_t>(1024, fill));
    std::size_t run = 1;
    std::size_t longest = 1;
    for (std::size_t i = kPayloadStart + 1; i < frame.size(); ++i) {
      run = (std::abs(frame[i] - frame[i - 1]) < 1e-4f) ? run + 1 : 1;
      longest = std::max(longest, run);
    }
    CHECK(longest <= 8);
  }
}

// ---- Padding ----------------------------------------------------------------

TEST(payload_bits_are_padded_to_whole_symbols_and_round_trip) {
  // QPSK never needs padding, so these exercise the padding path directly with
  // other bits-per-symbol values, including the 3 used by 8-PSK.
  for (const int bps : {1, 2, 3, 4, 5}) {
    for (const std::size_t n : {0u, 1u, 126u, 127u, 128u, 129u}) {
      const std::vector<uint8_t> payload = random_bytes(n, 11);
      const Bits bits = pack_payload_section(FrameType::Data, bps, payload, nullptr);

      CHECK(bits.size() % static_cast<std::size_t>(bps) == 0);
      CHECK(bits.size() == payload_section_symbols(FrameType::Data, bps, n) * static_cast<std::size_t>(bps));

      const PayloadSection s = unpack_payload_section(FrameType::Data, n, bits);
      CHECK(s.crc_ok);
      CHECK(s.payload == payload);
    }
  }
}

TEST(padding_bits_are_zero_and_do_not_affect_decoding) {
  const std::vector<uint8_t> payload = random_bytes(127, 12);  // 1048 bits, pads to 1050 for bps 3
  Bits bits = pack_payload_section(FrameType::Data, 3, payload, nullptr);
  CHECK(bits.size() == 1050);
  CHECK(bits[1048] == 0 && bits[1049] == 0);

  bits[1048] = 1;  // garbage in the padding must not matter
  bits[1049] = 1;
  const PayloadSection s = unpack_payload_section(FrameType::Data, 127, bits);
  CHECK(s.crc_ok);
  CHECK(s.payload == payload);
}

TEST(a_section_shorter_than_expected_is_reported_as_bad) {
  const std::vector<uint8_t> payload = random_bytes(40, 13);
  Bits bits = pack_payload_section(FrameType::Data, 2, payload, nullptr);
  bits.pop_back();
  const PayloadSection s = unpack_payload_section(FrameType::Data, 40, bits);
  CHECK(!s.crc_ok);
  CHECK(s.payload.empty());
}

// ---- Corruption -------------------------------------------------------------

TEST(every_single_header_bit_error_is_detected) {
  const auto frame = build_frame_symbols(FrameType::Data, 4, 100, random_bytes(64, 14));
  for (std::size_t i = 0; i < static_cast<std::size_t>(kHeaderSymbols); ++i) {
    auto damaged = frame;
    damaged[kHeaderStart + i] = -damaged[kHeaderStart + i];  // flips one BPSK bit
    const DecodedFrame d = decode_frame_symbols(damaged);
    CHECK(!d.header_ok);
    CHECK(contains(d.error, "CRC"));
  }
}

TEST(every_double_header_bit_error_is_detected) {
  // CRC-16 detects every two-bit error in a message this short.
  const auto frame = build_frame_symbols(FrameType::Data, 4, 100, random_bytes(64, 15));
  for (std::size_t i = 0; i < static_cast<std::size_t>(kHeaderSymbols); ++i) {
    for (std::size_t j = i + 1; j < static_cast<std::size_t>(kHeaderSymbols); ++j) {
      auto damaged = frame;
      damaged[kHeaderStart + i] = -damaged[kHeaderStart + i];
      damaged[kHeaderStart + j] = -damaged[kHeaderStart + j];
      CHECK(!decode_frame_symbols(damaged).header_ok);
    }
  }
}

TEST(a_payload_error_is_reported_without_losing_the_frame) {
  // One bit error in the payload: the header is fine and the frame is still
  // decoded, but the CRC flags it. The payload is returned so BER can be measured.
  const std::vector<uint8_t> payload = random_bytes(128, 16);
  auto frame = build_frame_symbols(FrameType::Data, 4, 31, payload);
  frame[kPayloadStart + 10] = to_next_neighbour(frame[kPayloadStart + 10], 4);

  const DecodedFrame d = decode_frame_symbols(frame);
  CHECK(d.header_ok);
  CHECK(!d.payload_crc_ok);
  CHECK(d.payload.size() == payload.size());
  CHECK(count_bit_errors(bytes_to_bits(d.payload), bytes_to_bits(payload)) == 1);
}

TEST(a_corrupted_crc_field_is_detected) {
  const std::vector<uint8_t> payload = random_bytes(32, 17);
  auto frame = build_frame_symbols(FrameType::Data, 4, 3, payload);
  frame[frame.size() - 1] = to_next_neighbour(frame[frame.size() - 1], 4);  // in the CRC-32 itself
  const DecodedFrame d = decode_frame_symbols(frame);
  CHECK(d.header_ok);
  CHECK(!d.payload_crc_ok);
  CHECK(d.payload == payload);  // the payload bits themselves were fine
}

TEST(a_noise_only_input_does_not_decode) {
  std::mt19937 rng(18);
  std::normal_distribution<float> n(0.0f, 1.0f);
  int accepted = 0;
  for (int trial = 0; trial < 2000; ++trial) {
    std::vector<cf> noise(300);
    for (auto& s : noise) s = cf(n(rng), n(rng));
    if (decode_frame_symbols(noise).header_ok) ++accepted;
  }
  // A random 72-bit header passes the CRC-16 with probability 2^-16, then must
  // also match the magic byte, type, modulation and length rules, so essentially
  // none are accepted.
  CHECK(accepted == 0);
}

TEST(too_few_symbols_are_reported) {
  const auto frame = build_frame_symbols(FrameType::Data, 4, 1, random_bytes(10, 19));
  const std::vector<cf> shortened(frame.begin(), frame.begin() + 100);
  const DecodedFrame d = decode_frame_symbols(shortened);
  CHECK(!d.header_ok);
  CHECK(contains(d.error, "too few"));
  CHECK(!decode_frame_symbols({}).header_ok);
}

TEST(a_truncated_payload_is_reported) {
  const auto frame = build_frame_symbols(FrameType::Data, 4, 1, random_bytes(128, 20));
  const std::vector<cf> cut(frame.begin(), frame.end() - 10);
  const DecodedFrame d = decode_frame_symbols(cut);
  CHECK(d.header_ok);  // the header arrived intact
  CHECK(!d.payload_crc_ok);
  CHECK(contains(d.error, "truncated"));
}

// ---- Header validation ------------------------------------------------------

TEST(a_good_header_parses) {
  const HeaderResult r = parse_edited_header(good_header_bytes());
  CHECK(r.ok);
  CHECK(r.header.frame_counter == 0x1234);
  CHECK(r.header.payload_length == 128);
  CHECK(r.header.modulation_order == 4);
  CHECK(r.header.type == FrameType::Data);
}

TEST(a_wrong_bit_count_is_rejected) {
  CHECK(!parse_header_bits(Bits(71, 0)).ok);
  CHECK(!parse_header_bits(Bits(73, 0)).ok);
}

TEST(a_bad_magic_byte_is_rejected_even_with_a_valid_crc) {
  auto bytes = good_header_bytes();
  bytes[0] = 0xB2;
  const HeaderResult r = parse_edited_header(bytes);
  CHECK(!r.ok);
  CHECK(contains(r.error, "magic"));
  CHECK(contains(r.error, "B2"));
}

TEST(an_unknown_frame_type_is_rejected) {
  auto bytes = good_header_bytes();
  bytes[1] = 2;
  const HeaderResult r = parse_edited_header(bytes);
  CHECK(!r.ok);
  CHECK(contains(r.error, "frame type"));
}

TEST(invalid_modulation_codes_are_rejected) {
  for (const uint8_t code : {uint8_t{0}, uint8_t{1}, uint8_t{9}, uint8_t{255}}) {
    auto bytes = good_header_bytes();
    bytes[2] = code;  // 1 would be BPSK, which is never a payload modulation
    CHECK(!parse_edited_header(bytes).ok);
  }
}

TEST(a_modulation_this_build_cannot_decode_is_rejected_with_a_clear_message) {
  auto bytes = good_header_bytes();
  bytes[2] = 3;  // 8-PSK
  const HeaderResult r = parse_edited_header(bytes);
  if (order_is_enabled(8)) {
    CHECK(r.ok);
    CHECK(r.header.modulation_order == 8);
  } else {
    CHECK(!r.ok);
    CHECK(contains(r.error, "8-PSK"));
  }
}

TEST(an_oversized_payload_length_is_rejected_even_with_a_valid_crc) {
  for (const uint16_t length : {uint16_t{4097}, uint16_t{5000}, uint16_t{65535}}) {
    auto bytes = good_header_bytes();
    bytes[5] = static_cast<uint8_t>(length >> 8);
    bytes[6] = static_cast<uint8_t>(length & 0xFF);
    const HeaderResult r = parse_edited_header(bytes);
    CHECK(!r.ok);
    CHECK(contains(r.error, "limit"));
  }
  auto bytes = good_header_bytes();
  bytes[5] = 0x10;  // 4096 exactly: the largest accepted
  bytes[6] = 0x00;
  CHECK(parse_edited_header(bytes).ok);
}

// ---- Builder argument checks -----------------------------------------------

TEST(builders_reject_invalid_arguments) {
  EchoTrailer t;
  CHECK_THROWS(build_frame_symbols(FrameType::Echo, 4, 1, {1, 2}, nullptr));  // echo needs a trailer
  CHECK_THROWS(build_frame_symbols(FrameType::Data, 4, 1, {1, 2}, &t));       // data cannot have one
  CHECK_THROWS(build_frame_symbols(FrameType::Data, 4, 1, std::vector<uint8_t>(4097)));
  CHECK_THROWS(build_frame_symbols(FrameType::Data, 2, 1, {1}));              // BPSK is not a payload modulation
  CHECK_THROWS(build_frame_symbols(FrameType::Data, 3, 1, {1}));              // not a power of two
  CHECK_THROWS(pack_payload_section(FrameType::Data, 0, {1}, nullptr));
  CHECK(build_frame_symbols(FrameType::Data, 4, 1, std::vector<uint8_t>(4096)).size() > 0);
}

TEST(a_modulation_not_enabled_in_this_build_is_refused_by_the_builder) {
  if (order_is_enabled(8)) return;  // stage 2: 8-PSK is enabled and this does not apply
  std::string message;
  try {
    build_frame_symbols(FrameType::Data, 8, 1, {1, 2, 3});
  } catch (const std::exception& e) {
    message = e.what();
  }
  CHECK(contains(message, "8-PSK"));
}

MPSK_TEST_MAIN()
