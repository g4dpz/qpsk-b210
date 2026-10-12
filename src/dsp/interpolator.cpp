#include "dsp/interpolator.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace mpsk::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

double sinc(double x) {
  if (std::fabs(x) < 1e-12) return 1.0;
  return std::sin(kPi * x) / (kPi * x);
}

/** Blackman window on u in [-1, 1]: 1 at the centre, 0 at the ends. */
double blackman(double u) {
  if (std::fabs(u) >= 1.0) return 0.0;
  return 0.42 + 0.5 * std::cos(kPi * u) + 0.08 * std::cos(2.0 * kPi * u);
}

}  // namespace

std::vector<double> sinc_interpolation_weights(double frac, int half_width) {
  if (half_width < 1) {
    throw std::invalid_argument("sinc_interpolation_weights: half width must be at least 1, got " +
                                std::to_string(half_width));
  }
  if (!(frac >= 0.0 && frac < 1.0)) {  // also rejects NaN
    throw std::invalid_argument("sinc_interpolation_weights: frac must be in [0, 1), got " +
                                std::to_string(frac));
  }

  std::vector<double> w(static_cast<std::size_t>(2 * half_width), 0.0);

  // On the grid: return the sample itself, exactly. (The sinc is zero at the other
  // integers, but only to within rounding, which would leave tiny leakage.)
  if (frac < 1e-12) {
    w[static_cast<std::size_t>(half_width - 1)] = 1.0;
    return w;
  }

  double sum = 0.0;
  for (int k = -half_width + 1; k <= half_width; ++k) {
    // Distance from the interpolation point to sample i0 + k.
    const double t = frac - static_cast<double>(k);
    const double weight = sinc(t) * blackman(t / static_cast<double>(half_width));
    w[static_cast<std::size_t>(k + half_width - 1)] = weight;
    sum += weight;
  }
  for (double& weight : w) weight /= sum;
  return w;
}

std::complex<float> sinc_interpolate(const std::complex<float>* y, std::size_t n, double position,
                                     int half_width) {
  const double base = std::floor(position);
  const std::vector<double> w = sinc_interpolation_weights(position - base, half_width);
  const long i0 = static_cast<long>(base);

  double re = 0.0;
  double im = 0.0;
  for (int k = -half_width + 1; k <= half_width; ++k) {
    const long idx = i0 + k;
    if (idx < 0 || static_cast<std::size_t>(idx) >= n) continue;  // zero outside the signal
    const double weight = w[static_cast<std::size_t>(k + half_width - 1)];
    re += weight * static_cast<double>(y[idx].real());
    im += weight * static_cast<double>(y[idx].imag());
  }
  return {static_cast<float>(re), static_cast<float>(im)};
}

}  // namespace mpsk::dsp
