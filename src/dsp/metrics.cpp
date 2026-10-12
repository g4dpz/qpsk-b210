#include "dsp/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mpsk::dsp {

void EvmAccumulator::add(std::complex<float> corrected, std::complex<float> decision) {
  const double dr = static_cast<double>(corrected.real()) - static_cast<double>(decision.real());
  const double di = static_cast<double>(corrected.imag()) - static_cast<double>(decision.imag());
  sum_squares_ += dr * dr + di * di;
  ++count_;
}

double EvmAccumulator::rms() const {
  return count_ == 0 ? 0.0 : std::sqrt(sum_squares_ / static_cast<double>(count_));
}

double EvmAccumulator::snr_db() const { return evm_to_snr_db(rms()); }

double evm_to_snr_db(double evm) {
  if (evm <= 0.0) return std::numeric_limits<double>::infinity();
  return -20.0 * std::log10(evm);
}

BerResult payload_ber(const std::vector<std::uint8_t>& decoded,
                      const std::vector<std::uint8_t>& expected) {
  BerResult r;
  r.length_mismatch = (decoded.size() != expected.size());
  const std::size_t common = std::min(decoded.size(), expected.size());
  for (std::size_t i = 0; i < common; ++i) {
    // The number of set bits in the XOR is the number of bits that differ.
    unsigned diff = static_cast<unsigned>(decoded[i] ^ expected[i]);
    while (diff != 0) {
      r.bit_errors += diff & 1u;
      diff >>= 1;
    }
  }
  r.bits_compared = common * 8;
  return r;
}

SignalLevel measure_signal_level(const std::complex<float>* raw, std::size_t n, std::size_t begin,
                                 std::size_t end, double clip_threshold) {
  SignalLevel level;
  if (end > n) end = n;
  if (begin >= end) return level;

  double power = 0.0;
  double peak_sq = 0.0;
  const double thr = clip_threshold;
  for (std::size_t i = begin; i < end; ++i) {
    const double re = static_cast<double>(raw[i].real());
    const double im = static_cast<double>(raw[i].imag());
    const double p = re * re + im * im;
    power += p;
    peak_sq = std::max(peak_sq, p);
    if (std::fabs(re) >= thr || std::fabs(im) >= thr) ++level.clipped_samples;
  }
  level.samples = end - begin;
  power /= static_cast<double>(level.samples);
  level.peak_magnitude = std::sqrt(peak_sq);
  level.rms_dbfs = (power > 0.0) ? 10.0 * std::log10(power) : -200.0;
  if (level.rms_dbfs < -200.0) level.rms_dbfs = -200.0;
  return level;
}

}  // namespace mpsk::dsp
