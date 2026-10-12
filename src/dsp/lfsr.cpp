#include "dsp/lfsr.hpp"

#include <stdexcept>
#include <string>

namespace mpsk::dsp {

Lfsr::Lfsr(int degree, int tap, uint32_t seed) : degree_(degree), tap_(tap) {
  if (degree < 2 || degree > 31) {
    throw std::invalid_argument("Lfsr: degree must be 2..31, got " + std::to_string(degree));
  }
  if (tap < 1 || tap >= degree) {
    throw std::invalid_argument("Lfsr: tap must be 1..degree-1, got " + std::to_string(tap));
  }
  mask_ = (1u << degree) - 1u;
  state_ = seed & mask_;
  if (state_ == 0) {
    throw std::invalid_argument("Lfsr: seed must have a nonzero low " + std::to_string(degree) +
                                " bits (the all-zero state never changes)");
  }
}

int Lfsr::next_bit() {
  const uint32_t fb = ((state_ >> (degree_ - 1)) ^ (state_ >> (tap_ - 1))) & 1u;
  state_ = ((state_ << 1) | fb) & mask_;
  return static_cast<int>(fb);
}

}  // namespace mpsk::dsp
