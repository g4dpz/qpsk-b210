#include "dsp/fir.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace mpsk::dsp {

FirFilter::FirFilter(std::vector<float> taps) : taps_(std::move(taps)), pos_(0) {
  if (taps_.empty()) throw std::invalid_argument("FirFilter: taps must not be empty");
  history_.assign(2 * taps_.size(), std::complex<float>(0.0f, 0.0f));
}

std::complex<float> FirFilter::push(std::complex<float> x) {
  const std::size_t n = taps_.size();

  // Move the write slot back by one (wrapping) and store the sample in both
  // halves, so the window of the newest N samples starting at pos_ is contiguous.
  pos_ = (pos_ == 0) ? n - 1 : pos_ - 1;
  history_[pos_] = x;
  history_[pos_ + n] = x;

  // window[k] is the sample k steps old (k = 0 is the newest), so the dot
  // product below is  sum_k h[k] x[n - k].
  const std::complex<float>* window = &history_[pos_];
  float re = 0.0f;
  float im = 0.0f;
  for (std::size_t k = 0; k < n; ++k) {
    re += taps_[k] * window[k].real();
    im += taps_[k] * window[k].imag();
  }
  return {re, im};
}

void FirFilter::process(const std::complex<float>* in, std::complex<float>* out, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) out[i] = push(in[i]);
}

std::vector<std::complex<float>> FirFilter::process(const std::vector<std::complex<float>>& in) {
  std::vector<std::complex<float>> out(in.size());
  process(in.data(), out.data(), in.size());
  return out;
}

void FirFilter::reset() {
  history_.assign(history_.size(), std::complex<float>(0.0f, 0.0f));
  pos_ = 0;
}

std::vector<std::complex<float>> upsample_and_filter(
    const std::vector<std::complex<float>>& symbols, const std::vector<float>& taps, int sps) {
  if (taps.empty()) throw std::invalid_argument("upsample_and_filter: taps must not be empty");
  if (sps < 1) {
    throw std::invalid_argument("upsample_and_filter: samples per symbol must be at least 1, got " +
                                std::to_string(sps));
  }
  if (symbols.empty()) return {};

  const std::size_t step = static_cast<std::size_t>(sps);
  std::vector<std::complex<float>> out((symbols.size() - 1) * step + taps.size(),
                                       std::complex<float>(0.0f, 0.0f));

  // Each symbol adds a scaled copy of the pulse starting at its own sample.
  for (std::size_t m = 0; m < symbols.size(); ++m) {
    const float re = symbols[m].real();
    const float im = symbols[m].imag();
    std::complex<float>* dst = &out[m * step];
    for (std::size_t k = 0; k < taps.size(); ++k) {
      dst[k] += std::complex<float>(taps[k] * re, taps[k] * im);
    }
  }
  return out;
}

}  // namespace mpsk::dsp
