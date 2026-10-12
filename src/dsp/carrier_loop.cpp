#include "dsp/carrier_loop.hpp"

#include <cmath>

namespace mpsk::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

CarrierLoop::CarrierLoop(const CarrierLoopConfig& config, double initial_phase,
                         double initial_frequency)
    : gains_(second_order_gains(config.bn_t, config.damping, 1.0)),
      phase_(initial_phase),
      frequency_(initial_frequency) {}

std::complex<float> CarrierLoop::derotate(std::complex<float> y, double symbols_earlier) const {
  const std::complex<double> v(static_cast<double>(y.real()), static_cast<double>(y.imag()));
  const std::complex<double> r =
      v * std::polar(1.0, -(phase_ - symbols_earlier * frequency_));
  return {static_cast<float>(r.real()), static_cast<float>(r.imag())};
}

double CarrierLoop::update(std::complex<float> corrected, std::complex<float> decision) {
  const std::complex<double> z(static_cast<double>(corrected.real()), static_cast<double>(corrected.imag()));
  const std::complex<double> d(static_cast<double>(decision.real()), static_cast<double>(decision.imag()));
  // arg() of the product is already in (-pi, pi], so the error wraps correctly.
  const double e = (std::abs(z) > 0.0 && std::abs(d) > 0.0) ? std::arg(z * std::conj(d)) : 0.0;

  frequency_ += gains_.ki * e;                        // I_k = I_{k-1} + Ki e_k
  phase_ += gains_.kp * e + frequency_;               // x_{k+1} = x_k + Kp e_k + I_k

  // Keep the phase bounded; only its value modulo 2 pi matters. remainder() returns
  // a value in [-pi, pi].
  phase_ = std::remainder(phase_, 2.0 * kPi);
  return e;
}

}  // namespace mpsk::dsp
