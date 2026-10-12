#include "dsp/dc_blocker.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace mpsk::dsp {

DcBlocker::DcBlocker(double pole, std::size_t prime_samples)
    : pole_(pole), prime_samples_(prime_samples) {
  if (!(pole >= 0.0 && pole < 1.0)) {  // also rejects NaN
    throw std::invalid_argument("DcBlocker: pole must be in [0, 1), got " + std::to_string(pole));
  }
  if (prime_samples == 0) {
    throw std::invalid_argument("DcBlocker: prime_samples must be at least 1");
  }
}

void DcBlocker::prime_from(std::complex<double> dc) {
  x_prev_ = dc;
  y_prev_ = {0.0, 0.0};
  primed_ = true;
}

std::complex<float> DcBlocker::push(std::complex<float> in) {
  const std::complex<double> x(static_cast<double>(in.real()), static_cast<double>(in.imag()));
  if (!primed_) prime_from(x);

  const std::complex<double> y = x - x_prev_ + pole_ * y_prev_;
  x_prev_ = x;
  y_prev_ = y;
  return {static_cast<float>(y.real()), static_cast<float>(y.imag())};
}

void DcBlocker::process(const std::complex<float>* in, std::complex<float>* out, std::size_t n) {
  if (n == 0) return;

  if (!primed_) {
    // The mean of the leading samples is the DC estimate. Done before any output is
    // written, so in-place filtering still reads the original input.
    const std::size_t count = std::min(n, prime_samples_);
    std::complex<double> sum(0.0, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
      sum += std::complex<double>(static_cast<double>(in[i].real()), static_cast<double>(in[i].imag()));
    }
    prime_from(sum / static_cast<double>(count));
  }

  for (std::size_t i = 0; i < n; ++i) out[i] = push(in[i]);
}

void DcBlocker::reset() {
  x_prev_ = {0.0, 0.0};
  y_prev_ = {0.0, 0.0};
  primed_ = false;
}

}  // namespace mpsk::dsp
