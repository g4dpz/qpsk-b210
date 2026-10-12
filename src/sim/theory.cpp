#include "sim/theory.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace mpsk::sim {

namespace {
constexpr double kPi = 3.14159265358979323846;

int bits_per_symbol(int order) {
  if (order < 2 || (order & (order - 1)) != 0) {
    throw std::invalid_argument("modulation order must be a power of two, 2 or more, got " +
                                std::to_string(order));
  }
  int k = 0;
  while ((1 << k) < order) ++k;
  return k;
}
}  // namespace

double q_function(double x) { return 0.5 * std::erfc(x / std::sqrt(2.0)); }

double ebn0_to_esn0_db(int modulation_order, double ebn0_db) {
  return ebn0_db + 10.0 * std::log10(static_cast<double>(bits_per_symbol(modulation_order)));
}

double mpsk_ber_theory(int modulation_order, double ebn0_db) {
  const int k = bits_per_symbol(modulation_order);
  const double ebn0 = std::pow(10.0, ebn0_db / 10.0);
  if (modulation_order <= 4) return q_function(std::sqrt(2.0 * ebn0));
  return (2.0 / k) * q_function(std::sqrt(2.0 * k * ebn0) * std::sin(kPi / modulation_order));
}

double mpsk_ebn0_for_ber(int modulation_order, double ber) {
  if (!(ber > 0.0 && ber < 0.5)) {
    throw std::invalid_argument("mpsk_ebn0_for_ber: ber must be in (0, 0.5), got " + std::to_string(ber));
  }
  (void)bits_per_symbol(modulation_order);   // validate the order
  // The curve falls steadily with Eb/N0, so bisection converges. 80 halvings of an
  // 80 dB interval is far below any rounding that matters.
  double lo = -20.0, hi = 60.0;
  for (int i = 0; i < 80; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (mpsk_ber_theory(modulation_order, mid) > ber) lo = mid; else hi = mid;
  }
  return 0.5 * (lo + hi);
}

}  // namespace mpsk::sim
