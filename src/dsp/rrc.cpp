#include "dsp/rrc.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace mpsk::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

/**
 * How close t must be to a singular point to use the closed-form limit.
 * Sample times are k / sps, with k an integer, so a point that lands on a
 * singularity does so to within floating-point rounding (~1e-16). A tolerance of
 * 1e-9 catches those, and it is far below the distance to any other sample
 * (1/sps), so no ordinary tap is ever mistaken for a singular one.
 */
constexpr double kSingularTolerance = 1e-9;

}  // namespace

double rrc_impulse(double t, double rolloff) {
  const double b = rolloff;

  // Singular point 1: t = 0.
  if (std::fabs(t) < kSingularTolerance) {
    return 1.0 - b + 4.0 * b / kPi;
  }

  // Singular point 2: t = +-1/(4b). Absent when b = 0 (the point is at infinity).
  if (b > 0.0 && std::fabs(std::fabs(t) - 1.0 / (4.0 * b)) < kSingularTolerance) {
    const double angle = kPi / (4.0 * b);
    return (b / std::sqrt(2.0)) *
           ((1.0 + 2.0 / kPi) * std::sin(angle) + (1.0 - 2.0 / kPi) * std::cos(angle));
  }

  const double numerator = std::sin(kPi * t * (1.0 - b)) + 4.0 * b * t * std::cos(kPi * t * (1.0 + b));
  const double four_bt = 4.0 * b * t;
  const double denominator = kPi * t * (1.0 - four_bt * four_bt);
  return numerator / denominator;
}

std::vector<float> rrc_taps(double rolloff, int sps, int span_symbols) {
  if (!(rolloff >= 0.0 && rolloff <= 1.0)) {  // also rejects NaN
    throw std::invalid_argument("rrc_taps: roll-off must be between 0 and 1, got " +
                                std::to_string(rolloff));
  }
  if (sps < 2) {
    throw std::invalid_argument("rrc_taps: samples per symbol must be at least 2, got " +
                                std::to_string(sps));
  }
  if (span_symbols < 1) {
    throw std::invalid_argument("rrc_taps: filter span must be at least 1 symbol, got " +
                                std::to_string(span_symbols));
  }
  if ((span_symbols * sps) % 2 != 0) {
    throw std::invalid_argument(
        "rrc_taps: span (" + std::to_string(span_symbols) + " symbols) times samples per symbol (" +
        std::to_string(sps) + ") must be even, so the filter has an odd number of taps and a "
        "whole-sample group delay");
  }

  const int num_taps = span_symbols * sps + 1;
  const int centre = (num_taps - 1) / 2;

  std::vector<double> h(static_cast<std::size_t>(num_taps));
  double energy = 0.0;
  for (int n = 0; n < num_taps; ++n) {
    const double t = static_cast<double>(n - centre) / sps;  // time in symbol periods
    h[static_cast<std::size_t>(n)] = rrc_impulse(t, rolloff);
    energy += h[static_cast<std::size_t>(n)] * h[static_cast<std::size_t>(n)];
  }

  // Scale to unit energy. See the header for why.
  const double scale = 1.0 / std::sqrt(energy);
  std::vector<float> taps(h.size());
  for (std::size_t n = 0; n < h.size(); ++n) taps[n] = static_cast<float>(h[n] * scale);
  return taps;
}

}  // namespace mpsk::dsp
