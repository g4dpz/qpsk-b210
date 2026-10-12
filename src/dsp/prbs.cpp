#include "dsp/prbs.hpp"

#include "dsp/lfsr.hpp"

namespace mpsk::dsp {

namespace {

/**
 * SplitMix64 finaliser. A small, well-mixed 64-bit hash, so that adjacent seeds
 * and adjacent frame counters give unrelated starting states instead of states
 * that are one shift apart.
 */
uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

}  // namespace

Bits prbs_bits(uint32_t seed, uint32_t frame_counter, std::size_t count) {
  const uint64_t key = (static_cast<uint64_t>(seed) << 32) | frame_counter;
  uint32_t state = static_cast<uint32_t>(mix64(key)) & 0x7FFFFFFFu;
  if (state == 0) state = 1;  // the all-zero state is a lock-up

  Lfsr lfsr(31, 28, state);
  Bits bits;
  bits.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    bits.push_back(static_cast<uint8_t>(lfsr.next_bit()));
  }
  return bits;
}

std::vector<uint8_t> prbs_bytes(uint32_t seed, uint32_t frame_counter, std::size_t count) {
  return bits_to_bytes(prbs_bits(seed, frame_counter, count * 8));
}

}  // namespace mpsk::dsp
