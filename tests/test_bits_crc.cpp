// Tests for the bit helpers and the CRC functions.

#include <cstdint>
#include <string>
#include <vector>

#include "dsp/bits.hpp"
#include "dsp/crc.hpp"
#include "test_harness.hpp"

using mpsk::dsp::Bits;

namespace {

std::vector<uint8_t> ascii(const std::string& s) { return {s.begin(), s.end()}; }

}  // namespace

TEST(bytes_unpack_most_significant_bit_first) {
  const Bits bits = mpsk::dsp::bytes_to_bits({0xA5});
  CHECK((bits == Bits{1, 0, 1, 0, 0, 1, 0, 1}));
}

TEST(bytes_round_trip) {
  std::vector<uint8_t> bytes;
  for (int i = 0; i < 256; ++i) bytes.push_back(static_cast<uint8_t>(i));
  CHECK(mpsk::dsp::bits_to_bytes(mpsk::dsp::bytes_to_bits(bytes)) == bytes);
}

TEST(partial_final_byte_is_padded_with_zeros_at_the_low_end) {
  // 1,0,1 -> 1010 0000
  const auto bytes = mpsk::dsp::bits_to_bytes(Bits{1, 0, 1});
  CHECK(bytes.size() == 1);
  CHECK(bytes[0] == 0xA0);
  CHECK(mpsk::dsp::bits_to_bytes(Bits{}).empty());
}

TEST(any_nonzero_element_counts_as_one) {
  CHECK(mpsk::dsp::bits_to_bytes(Bits{7, 0, 0, 0, 0, 0, 0, 0})[0] == 0x80);
}

TEST(bit_errors_are_counted) {
  CHECK(mpsk::dsp::count_bit_errors(Bits{}, Bits{}) == 0);
  CHECK(mpsk::dsp::count_bit_errors(Bits{1, 0, 1, 1}, Bits{1, 0, 1, 1}) == 0);
  CHECK(mpsk::dsp::count_bit_errors(Bits{1, 0, 1, 1}, Bits{0, 0, 1, 0}) == 2);
  CHECK(mpsk::dsp::count_bit_errors(Bits{0, 0, 0, 0}, Bits{1, 1, 1, 1}) == 4);
}

TEST(bit_errors_reject_mismatched_lengths) {
  CHECK_THROWS(mpsk::dsp::count_bit_errors(Bits{1, 0}, Bits{1, 0, 1}));
}

TEST(crc16_matches_the_published_check_value) {
  CHECK(mpsk::dsp::crc16_ccitt(ascii("123456789")) == 0x29B1);
}

TEST(crc16_of_empty_input_is_the_initial_value) {
  CHECK(mpsk::dsp::crc16_ccitt(nullptr, 0) == 0xFFFF);
}

TEST(crc32_matches_the_published_check_value) {
  CHECK(mpsk::dsp::crc32_ieee(ascii("123456789")) == 0xCBF43926u);
}

TEST(crc32_of_empty_input_is_zero) {
  CHECK(mpsk::dsp::crc32_ieee(nullptr, 0) == 0u);
}

TEST(crc32_matches_a_second_known_vector) {
  // CRC-32 of "The quick brown fox jumps over the lazy dog" is 0x414FA339.
  CHECK(mpsk::dsp::crc32_ieee(ascii("The quick brown fox jumps over the lazy dog")) == 0x414FA339u);
}

TEST(every_single_bit_flip_is_detected) {
  // A CRC with a degree-n polynomial detects all single-bit errors.
  const std::vector<uint8_t> original = ascii("header-sized and payload-sized data 0123456789");
  const uint16_t c16 = mpsk::dsp::crc16_ccitt(original);
  const uint32_t c32 = mpsk::dsp::crc32_ieee(original);

  for (std::size_t byte = 0; byte < original.size(); ++byte) {
    for (int bit = 0; bit < 8; ++bit) {
      std::vector<uint8_t> corrupted = original;
      corrupted[byte] = static_cast<uint8_t>(corrupted[byte] ^ (1u << bit));
      CHECK(mpsk::dsp::crc16_ccitt(corrupted) != c16);
      CHECK(mpsk::dsp::crc32_ieee(corrupted) != c32);
    }
  }
}

TEST(crc_depends_on_byte_order) {
  CHECK(mpsk::dsp::crc16_ccitt(std::vector<uint8_t>{1, 2, 3}) !=
        mpsk::dsp::crc16_ccitt(std::vector<uint8_t>{3, 2, 1}));
  CHECK(mpsk::dsp::crc32_ieee(std::vector<uint8_t>{1, 2, 3}) !=
        mpsk::dsp::crc32_ieee(std::vector<uint8_t>{3, 2, 1}));
}

MPSK_TEST_MAIN()
