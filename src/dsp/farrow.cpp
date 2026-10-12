#include "dsp/farrow.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace mpsk::dsp {

bool farrow_in_range(std::size_t n, double position) {
  if (!(position >= 1.0)) return false;  // also rejects NaN
  const double base = std::floor(position);
  return base + static_cast<double>(kFarrowAfter) <= static_cast<double>(n) - 1.0;
}

std::complex<float> farrow_cubic(const std::complex<float>* y, std::size_t n, double position) {
  if (!farrow_in_range(n, position)) {
    throw std::out_of_range("farrow_cubic: position " + std::to_string(position) +
                            " needs samples outside [0, " + std::to_string(n) + ")");
  }
  const double base = std::floor(position);
  const double mu = position - base;
  const std::size_t i = static_cast<std::size_t>(base);

  // Work in double: the four terms nearly cancel for small mu, and float would
  // lose the small differences that carry the interpolated value.
  const std::complex<double> xm1(static_cast<double>(y[i - 1].real()), static_cast<double>(y[i - 1].imag()));
  const std::complex<double> x0(static_cast<double>(y[i].real()), static_cast<double>(y[i].imag()));
  const std::complex<double> x1(static_cast<double>(y[i + 1].real()), static_cast<double>(y[i + 1].imag()));
  const std::complex<double> x2(static_cast<double>(y[i + 2].real()), static_cast<double>(y[i + 2].imag()));

  const std::complex<double> v3 = (-1.0 / 6.0) * xm1 + 0.5 * x0 - 0.5 * x1 + (1.0 / 6.0) * x2;
  const std::complex<double> v2 = 0.5 * xm1 - x0 + 0.5 * x1;
  const std::complex<double> v1 = (-1.0 / 3.0) * xm1 - 0.5 * x0 + x1 - (1.0 / 6.0) * x2;
  const std::complex<double> v0 = x0;

  const std::complex<double> out = ((v3 * mu + v2) * mu + v1) * mu + v0;
  return {static_cast<float>(out.real()), static_cast<float>(out.imag())};
}

}  // namespace mpsk::dsp
