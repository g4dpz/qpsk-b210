#include "dsp/preamble.hpp"

#include "dsp/constellation.hpp"
#include "dsp/lfsr.hpp"

namespace mpsk::dsp {

namespace {

/** Register contents at the start of the sequence, chosen by exhaustive search. */
constexpr uint32_t kPreambleSeed = 9;

/** The 64th chip, appended after the 63-chip m-sequence. */
constexpr uint8_t kPreambleExtraBit = 1;

Bits make_preamble_bits() {
  Lfsr lfsr(6, 5, kPreambleSeed);  // x^6 + x^5 + 1, period 63
  Bits bits;
  bits.reserve(kPreambleSymbols);
  for (int i = 0; i < 63; ++i) bits.push_back(static_cast<uint8_t>(lfsr.next_bit()));
  bits.push_back(kPreambleExtraBit);
  return bits;
}

}  // namespace

const Bits& preamble_bits() {
  static const Bits bits = make_preamble_bits();
  return bits;
}

const std::vector<std::complex<float>>& preamble_symbols() {
  static const std::vector<std::complex<float>> symbols =
      map_bits(Constellation(2), preamble_bits());
  return symbols;
}

}  // namespace mpsk::dsp
