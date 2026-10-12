#include "dsp/bits.hpp"

#include <stdexcept>
#include <string>

namespace mpsk::dsp {

Bits bytes_to_bits(const std::vector<uint8_t>& bytes) {
  Bits bits;
  bits.reserve(bytes.size() * 8);
  for (const uint8_t byte : bytes) {
    for (int shift = 7; shift >= 0; --shift) {
      bits.push_back(static_cast<uint8_t>((byte >> shift) & 1u));
    }
  }
  return bits;
}

std::vector<uint8_t> bits_to_bytes(const Bits& bits) {
  std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
  for (std::size_t i = 0; i < bits.size(); ++i) {
    if (bits[i] != 0) {
      bytes[i / 8] = static_cast<uint8_t>(bytes[i / 8] | (0x80u >> (i % 8)));
    }
  }
  return bytes;
}

std::size_t count_bit_errors(const Bits& a, const Bits& b) {
  if (a.size() != b.size()) {
    throw std::invalid_argument("count_bit_errors: bit vectors have different lengths (" +
                                std::to_string(a.size()) + " vs " + std::to_string(b.size()) +
                                ")");
  }
  std::size_t errors = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if ((a[i] != 0) != (b[i] != 0)) ++errors;
  }
  return errors;
}

}  // namespace mpsk::dsp
