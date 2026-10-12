#include "dsp/scrambler.hpp"

#include "dsp/lfsr.hpp"

namespace mpsk::dsp {

Bits scramble(const Bits& bits) {
  Lfsr lfsr(7, 4, kScramblerSeed);  // x^7 + x^4 + 1
  Bits out;
  out.reserve(bits.size());
  for (const uint8_t bit : bits) {
    out.push_back(static_cast<uint8_t>((bit != 0 ? 1 : 0) ^ lfsr.next_bit()));
  }
  return out;
}

}  // namespace mpsk::dsp
